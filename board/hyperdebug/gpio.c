/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* HyperDebug GPIO logic and console commands */

#include "atomic.h"
#include "builtin/assert.h"
#include "common.h"
#include "console.h"
#include "cpu.h"
#include "gpio.h"
#include "hooks.h"
#include "registers.h"
#include "task.h"
#include "timer.h"
#include "util.h"

struct dac_t {
	uint32_t enable_mask;
	volatile uint32_t *data_register;
};

/* Sparse array of DAC capabilities for GPIO pins. */
const struct dac_t dac_channels[GPIO_COUNT] = {
	[GPIO_CN7_9] = { STM32_DAC_CR_EN1, &STM32_DAC_DHR12R1 },
	[GPIO_CN7_10] = { STM32_DAC_CR_EN2, &STM32_DAC_DHR12R2 },
};

/*
 * GPIO structure for keeping extra flags such as GPIO_OPEN_DRAIN, to be applied
 * whenever the pin is switched into "alternate" mode.
 */
struct gpio_alt_flags {
	/* Port base address */
	uint32_t port;

	/* Bitmask on that port (multiple bits allowed) */
	uint32_t mask;

	/* Flags (GPIO_*; see above). */
	uint32_t flags;
};

/*
 * Construct the gpio_alt_flags array, this really is just a subset of the
 * columns in the gpio_alt_funcs array in common/gpio.c (which is not accessible
 * from here).  This array is used by extra_alternate_flags().
 */
#define ALTERNATE(pinmask, function, module, flagz) \
	{ GPIO_##pinmask, .flags = (flagz) },

static __const_data const struct gpio_alt_flags gpio_alt_flags[] = {
#include "gpio.wrap"
};
#undef ALTERNATE

/*
 * A cyclic buffer is used to record events (edges) of one or more GPIO
 * signals.  Each event records the time since the previous event, and the
 * signal that changed (the direction of change is not explicitly recorded).
 *
 * So conceptually the buffer entries are pairs of (diff: u64, signal_no: u8).
 * These entries are encoded as bytes in the following way: First the timestamp
 * diff is shifted left by signal_bits, and the signal_no value put into the
 * lower bits freed up this way.  Now we have a single u64, which often will be
 * a small value (or at least, when the edges happen rapidly, and the need to
 * store many of them the highest, then the u64 will be a small value).  This
 * u64 is then stored 7 bits at a time in successive bytes, with the most
 * significant bit indicating whether more bytes belong to the same entry.
 *
 * The chain of relative timestamps are resolved by keeping two absolute
 * timestamps: head_time is the time of the most recently inserted event, and is
 * accessed and updated only by the interrupt handler.  tail_time is the past
 * timestamp on which the diff of the oldest record in the buffer is based (the
 * timestamp of the last record to be removed from the buffer), it is accessed
 * and updated only from the non-interrupt code that removes records from the
 * buffer.
 *
 * In a similar fashion, the signal level is recorded "at both ends" in for each
 * monitored signal by head_level and tail_level, the former only accessed from
 * the interrupt handler, and the latter only accessed from non-interrupt code.
 */
struct cyclic_buffer_header_t {
	/* Number of signals being monitored in this buffer. */
	uint8_t num_signals;
	/* The number of bits required to represent 0..num_signals-1. */
	uint8_t signal_bits;
	/* Sticky bit recording if overflow occurred. */
	volatile uint8_t overflow;
	/* Time of the most recent event, updated from interrupt context. */
	volatile timestamp_t head_time;
	/* Time base that the oldest event is relative to. */
	timestamp_t tail_time;
	/* Index at which new records are placed, updated from interrupt. */
	volatile uint32_t head;
	/* Index af oldest record. */
	uint32_t tail;
	/*
	 * Size of cyclic byte buffer, determined at runtime, not necessarily
	 * power of two.  Head and tail wrap to zero here.
	 */
	uint32_t size;
	/* Data contents */
	uint8_t data[];
};

/*
 * The STM32L5 has 16 edge detection circuits.  Each pin can only be used with
 * one of them.  That is, detector 0 can take its input from one of pins A0,
 * B0, C0, ..., while detector 1 can choose between A1, B1, etc.
 *
 * Information about the current use of each detection circuit is stored in 16
 * "slots" below.
 */
struct monitoring_slot_t {
	/* Link to buffer recording edges of this signal. */
	struct cyclic_buffer_header_t *buffer;
	/* EC enum id of the signal used by this detection slot. */
	int gpio_signal;
	/* The index of the signal as used in the recording buffer. */
	uint8_t signal_no;
	/* Most recently recorded level of the signal. */
	volatile uint8_t head_level;
	/* Level as of the current oldest end (tail) of the recording. */
	uint8_t tail_level;
};
struct monitoring_slot_t monitoring_slots[16];

/*
 * Memory area used for allocation of cyclic buffers.  (Currently the
 * implementation supports only a single allocation.)
 */
uint8_t buffer_area[sizeof(struct cyclic_buffer_header_t) + 8192];
bool buffer_area_in_use = false;

static struct cyclic_buffer_header_t *allocate_cyclic_buffer(size_t size)
{
	struct cyclic_buffer_header_t *res =
		(struct cyclic_buffer_header_t *)buffer_area;
	if (buffer_area_in_use) {
		/* No support for multiple smaller allocations, yet. */
		return 0;
	}
	if (sizeof(struct cyclic_buffer_header_t) + size >
	    sizeof(buffer_area)) {
		/* Requested size exceeds the capacity of the area. */
		return 0;
	}
	buffer_area_in_use = true;
	res->size = size;
	return res;
}

static void free_cyclic_buffer(struct cyclic_buffer_header_t *buf)
{
	buffer_area_in_use = false;
}

/*
 * Counts unacknowledged buffer overflows.  Whenever non-zero, the red LED
 * will flash.
 */
atomic_t num_cur_error_conditions;

/*
 * Counts the number of cyclic buffers currently in existence, the green LED
 * will flash whenever this is non-zero, indicating the monitoring activity.
 */
int num_cur_monitoring = 0;

static __attribute__((noinline)) void overflow(struct monitoring_slot_t *slot)
{
	struct cyclic_buffer_header_t *buffer_header = slot->buffer;
	gpio_disable_interrupt(slot->gpio_signal);
	buffer_header->overflow = 1;
	atomic_add(&num_cur_error_conditions, 1);
}

void gpio_edge(enum gpio_signal signal)
{
	/*
	 * Hardware has detected one or more edges since last time.  We
	 * process by looking at the current level of the signal.  If opposite
	 * the most recent level, we record one edge, if the same as most
	 * recent level, we record two edges, that is, a zero-width pulse.
	 * This is useful for tests trying to verify e.g. that there are no
	 * glitches on a particular signal, and want to know about any pulses,
	 * however narrow.
	 */
	timestamp_t now = get_time();
	int gpio_num = GPIO_MASK_TO_NUM(gpio_list[signal].mask);
	struct monitoring_slot_t *slot = monitoring_slots + gpio_num;
	int current_level = gpio_get_level(signal);
	struct cyclic_buffer_header_t *buffer_header = slot->buffer;
	uint8_t *buffer_data = buffer_header->data;
	uint32_t tail = buffer_header->tail, head = buffer_header->head,
		 size = buffer_header->size;
	uint64_t diff = now.val - buffer_header->head_time.val;

	uint8_t signal_bits = buffer_header->signal_bits;

	/*
	 * Race condition here!  If three or more edges happen in
	 * rapid succession, we may fail to record some of them, but
	 * we should never over-report edges.
	 *
	 * Since the edge interrupts pending bit has been cleared before the
	 * "current_level" was polled, if an edge happened between the two, then
	 * an interrupt is currently pending, and when handled after this method
	 * returns, the logic below would wrongly conclude that the signal must
	 * have seen two transitions, in order to end up at the same level as
	 * before.  In order to avoid such over-reporting, we clear "pending"
	 * interrupt bit below, but only for the direction that goes "towards"
	 * the level measured above.
	 */
	if (current_level)
		STM32_EXTI_RPR = BIT(gpio_num);
	else
		STM32_EXTI_FPR = BIT(gpio_num);

	/*
	 * Insert an entry recording the time since last event, and which
	 * signal changed (the direction of the edge is not explicitly
	 * recorded, as it can be inferred from the initial level).
	 *
	 * The time difference and pin index are encoded in `diff`, which will
	 * be a small integer if the event arrive rapidly.  7 bits of this
	 * integer is then put into one byte at a time, using the high bit of
	 * each byte to indicate if more are to come.  This encoding will use
	 * only one byte per event, in the best case, allowing tens of
	 * thousands of events to be buffered.
	 */
	diff <<= signal_bits;
	diff |= slot->signal_no;
	do {
		buffer_data[head++] = ((diff >= 0x80) ? 0x80 : 0x00) |
				      (diff & 0x7F);
		diff >>= 7;
		if (head == size)
			head = 0;
		if (head == tail) {
			/*
			 * The new head will not be persisted, maintaining the
			 * invatiant that head and tail are equal only when the
			 * buffer is empty.
			 */
			return overflow(slot);
		}
	} while (diff);

	/*
	 * If current level equals the previous level, then record an
	 * additional edge 0ms after the previous.
	 */
	if (!!current_level == !!slot->head_level) {
		/*
		 * Add a record with zero diff, and the same signal no.  (Will
		 * always fit in one byte, as signal_no never uses more than 7
		 * bits.)
		 */
		buffer_data[head++] = slot->signal_no;
		if (head == size)
			head = 0;
		if (head == tail) {
			/*
			 * The new head will not be persisted, maintaining the
			 * invatiant that head and tail are equal only when the
			 * buffer is empty.
			 */
			return overflow(slot);
		}
	} else {
		slot->head_level = current_level;
	}
	buffer_header->head = head;
	buffer_header->head_time = now;
}

static void board_gpio_init(void)
{
	/* Mark every slot as unused. */
	for (int i = 0; i < ARRAY_SIZE(monitoring_slots); i++)
		monitoring_slots[i].gpio_signal = GPIO_COUNT;
}
DECLARE_HOOK(HOOK_INIT, board_gpio_init, HOOK_PRIO_DEFAULT);

static void stop_all_gpio_monitoring(void)
{
	struct monitoring_slot_t *slot;
	struct cyclic_buffer_header_t *buffer_header;
	for (int i = 0; i < ARRAY_SIZE(monitoring_slots); i++) {
		slot = monitoring_slots + i;
		if (!slot->buffer)
			continue;

		/* Disable interrupts for all signals feeding into the same
		 * cyclic buffer. */
		buffer_header = slot->buffer;
		for (int j = i; j < ARRAY_SIZE(monitoring_slots); j++) {
			slot = monitoring_slots + i;
			if (slot->buffer != buffer_header)
				continue;
			gpio_disable_interrupt(slot->gpio_signal);
			slot->gpio_signal = GPIO_COUNT;
		}
		/* Deallocate this one cyclic buffer. */
		num_cur_monitoring--;
		if (buffer_header->overflow)
			atomic_sub(&num_cur_error_conditions, 1);
		free_cyclic_buffer(buffer_header);
	}
}

/*
 * Return GPIO_OPEN_DRAIN or any other special flags to apply when the given
 * signal is in "alternate" mode.
 */
static uint32_t extra_alternate_flags(enum gpio_signal signal)
{
	const struct gpio_info *g = gpio_list + signal;
	const struct gpio_alt_flags *af;

	/* Find the first ALTERNATE() declaration for the given pin. */
	for (af = gpio_alt_flags;
	     af < gpio_alt_flags + ARRAY_SIZE(gpio_alt_flags); af++) {
		if (af->port != g->port)
			continue;

		if (af->mask & g->mask) {
			return af->flags;
		}
	}

	/* No ALTERNATE() declaration mention the given pin. */
	return 0;
}

/**
 * Find a GPIO signal by name.
 *
 * This is copied from gpio.c unfortunately, as it is static over there.
 *
 * @param name		Signal name to find
 *
 * @return the signal index, or GPIO_COUNT if no match.
 */
static enum gpio_signal find_signal_by_name(const char *name)
{
	int i;

	if (!name || !*name)
		return GPIO_COUNT;

	for (i = 0; i < GPIO_COUNT; i++)
		if (gpio_is_implemented(i) &&
		    !strcasecmp(name, gpio_get_name(i)))
			return i;

	return GPIO_COUNT;
}

/*
 * Set the mode of a GPIO pin: input/opendrain/pushpull/alternate.
 */
static int command_gpio_mode(int argc, const char **argv)
{
	int gpio;
	int flags;
	uint32_t dac_enable_value = STM32_DAC_CR;

	if (argc < 3)
		return EC_ERROR_PARAM_COUNT;

	gpio = find_signal_by_name(argv[1]);
	if (gpio == GPIO_COUNT)
		return EC_ERROR_PARAM1;
	flags = gpio_get_flags(gpio);

	flags &= ~(GPIO_INPUT | GPIO_OUTPUT | GPIO_OPEN_DRAIN | GPIO_ANALOG);
	dac_enable_value &= ~dac_channels[gpio].enable_mask;
	if (strcasecmp(argv[2], "input") == 0)
		flags |= GPIO_INPUT;
	else if (strcasecmp(argv[2], "opendrain") == 0)
		flags |= GPIO_OUTPUT | GPIO_OPEN_DRAIN;
	else if (strcasecmp(argv[2], "pushpull") == 0)
		flags |= GPIO_OUTPUT;
	else if (strcasecmp(argv[2], "adc") == 0)
		flags |= GPIO_ANALOG;
	else if (strcasecmp(argv[2], "dac") == 0) {
		if (dac_channels[gpio].enable_mask == 0) {
			ccprintf("Error: Pin does not support dac\n");
			return EC_ERROR_PARAM2;
		}
		dac_enable_value |= dac_channels[gpio].enable_mask;
		/* Disable digital output, when DAC is overriding. */
		flags |= GPIO_INPUT;
	} else if (strcasecmp(argv[2], "alternate") == 0)
		flags |= GPIO_ALTERNATE | extra_alternate_flags(gpio);
	else
		return EC_ERROR_PARAM2;

	/* Update GPIO flags. */
	gpio_set_flags(gpio, flags);
	STM32_DAC_CR = dac_enable_value;
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND_FLAGS(
	gpiomode, command_gpio_mode,
	"name <input | opendrain | pushpull | adc | dac | alternate>",
	"Set a GPIO mode", CMD_FLAG_RESTRICTED);

/*
 * Set the weak pulling of a GPIO pin: up/down/none.
 */
static int command_gpio_pull_mode(int argc, const char **argv)
{
	int gpio;
	int flags;

	if (argc < 3)
		return EC_ERROR_PARAM_COUNT;

	gpio = find_signal_by_name(argv[1]);
	if (gpio == GPIO_COUNT)
		return EC_ERROR_PARAM1;
	flags = gpio_get_flags(gpio);

	flags = flags & ~(GPIO_PULL_UP | GPIO_PULL_DOWN);
	if (strcasecmp(argv[2], "none") == 0)
		;
	else if (strcasecmp(argv[2], "up") == 0)
		flags |= GPIO_PULL_UP;
	else if (strcasecmp(argv[2], "down") == 0)
		flags |= GPIO_PULL_DOWN;
	else
		return EC_ERROR_PARAM2;

	/* Update GPIO flags. */
	gpio_set_flags(gpio, flags);
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND_FLAGS(gpiopullmode, command_gpio_pull_mode,
			      "name <none | up | down>",
			      "Set a GPIO weak pull mode", CMD_FLAG_RESTRICTED);

static int set_dac(int gpio, const char *value)
{
	int milli_volts;
	char *e;
	if (dac_channels[gpio].enable_mask == 0) {
		ccprintf("Error: Pin does not support dac\n");
		return EC_ERROR_PARAM6;
	}

	milli_volts = strtoi(value, &e, 0);
	if (*e)
		return EC_ERROR_PARAM6;

	if (milli_volts <= 0)
		*dac_channels[gpio].data_register = 0;
	else if (milli_volts >= 3300)
		*dac_channels[gpio].data_register = 4095;
	else
		*dac_channels[gpio].data_register = milli_volts * 4096 / 3300;

	return EC_SUCCESS;
}

/*
 * Set the value in millivolts for driving the DAC of a given pin.
 */
static int command_gpio_analog_set(int argc, const char **argv)
{
	int gpio;

	if (argc < 4)
		return EC_ERROR_PARAM_COUNT;

	gpio = find_signal_by_name(argv[2]);
	if (gpio == GPIO_COUNT)
		return EC_ERROR_PARAM2;

	if (set_dac(gpio, argv[3]) != EC_SUCCESS)
		return EC_ERROR_PARAM3;
	return EC_SUCCESS;
}

/*
 * Set multiple aspects of a GPIO pin simultaneously, that is, can switch output
 * level, opendrain/pushpull, and pullup simultaneously, eliminating the risk of
 * glitches.
 */
static int command_gpio_multiset(int argc, const char **argv)
{
	int gpio;
	int flags;
	uint32_t dac_enable_value = STM32_DAC_CR;

	if (argc < 4)
		return EC_ERROR_PARAM_COUNT;

	gpio = find_signal_by_name(argv[2]);
	if (gpio == GPIO_COUNT)
		return EC_ERROR_PARAM2;
	flags = gpio_get_flags(gpio);

	if (argc > 3 && strcasecmp(argv[3], "-") != 0) {
		flags = flags & ~(GPIO_LOW | GPIO_HIGH);
		if (strcasecmp(argv[3], "0") == 0)
			flags |= GPIO_LOW;
		else if (strcasecmp(argv[3], "1") == 0)
			flags |= GPIO_HIGH;
		else
			return EC_ERROR_PARAM3;
	}

	if (argc > 4 && strcasecmp(argv[4], "-") != 0) {
		flags &= ~(GPIO_INPUT | GPIO_OUTPUT | GPIO_OPEN_DRAIN |
			   GPIO_ANALOG);
		dac_enable_value &= ~dac_channels[gpio].enable_mask;
		if (strcasecmp(argv[4], "input") == 0)
			flags |= GPIO_INPUT;
		else if (strcasecmp(argv[4], "opendrain") == 0)
			flags |= GPIO_OUTPUT | GPIO_OPEN_DRAIN;
		else if (strcasecmp(argv[4], "pushpull") == 0)
			flags |= GPIO_OUTPUT;
		else if (strcasecmp(argv[4], "adc") == 0)
			flags |= GPIO_ANALOG;
		else if (strcasecmp(argv[4], "dac") == 0) {
			if (dac_channels[gpio].enable_mask == 0) {
				ccprintf("Error: Pin does not support dac\n");
				return EC_ERROR_PARAM2;
			}
			dac_enable_value |= dac_channels[gpio].enable_mask;
			/* Disable digital output, when DAC is overriding. */
			flags |= GPIO_INPUT;
		} else if (strcasecmp(argv[4], "alternate") == 0)
			flags |= GPIO_ALTERNATE | extra_alternate_flags(gpio);
		else
			return EC_ERROR_PARAM4;
	}

	if (argc > 5 && strcasecmp(argv[5], "-") != 0) {
		flags = flags & ~(GPIO_PULL_UP | GPIO_PULL_DOWN);
		if (strcasecmp(argv[5], "none") == 0)
			;
		else if (strcasecmp(argv[5], "up") == 0)
			flags |= GPIO_PULL_UP;
		else if (strcasecmp(argv[5], "down") == 0)
			flags |= GPIO_PULL_DOWN;
		else
			return EC_ERROR_PARAM5;
	}

	if (argc > 6 && strcasecmp(argv[6], "-") != 0) {
		if (set_dac(gpio, argv[6]) != EC_SUCCESS)
			return EC_ERROR_PARAM6;
	}

	/* Update GPIO flags. */
	gpio_set_flags(gpio, flags);
	STM32_DAC_CR = dac_enable_value;
	return EC_SUCCESS;
}

static int command_gpio_monitoring_start(int argc, const char **argv)
{
	BUILD_ASSERT(STM32_IRQ_EXTI15 < 32);
	int gpios[16];
	int gpio_num = argc - 3;
	int i;
	timestamp_t now;
	int rv;
	uint32_t nvic_mask;
	size_t cyclic_buffer_size = 8192; /* Maybe configurable by parameter */
	struct cyclic_buffer_header_t *buf;
	struct monitoring_slot_t *slot;

	if (gpio_num <= 0 || gpio_num > 16)
		return EC_ERROR_PARAM_COUNT;

	for (i = 0; i < gpio_num; i++) {
		gpios[i] = find_signal_by_name(argv[3 + i]);
		if (gpios[i] == GPIO_COUNT) {
			rv = EC_ERROR_PARAM3 + i;
			goto out_partial_cleanup;
		}
		slot = monitoring_slots +
		       GPIO_MASK_TO_NUM(gpio_list[gpios[i]].mask);
		if (slot->gpio_signal != GPIO_COUNT) {
			ccprintf("Error: Monitoring of %s conflicts with %s\n",
				 argv[3 + i],
				 gpio_list[slot->gpio_signal].name);
			rv = EC_ERROR_PARAM3 + i;
			goto out_partial_cleanup;
		}
		slot->gpio_signal = gpios[i];
	}

	/*
	 * All the requested signals were available for monitoring, and their
	 * slots have been marked as reserved for the respective signal.
	 */
	buf = allocate_cyclic_buffer(cyclic_buffer_size);
	if (!buf) {
		rv = EC_ERROR_BUSY;
		goto out_cleanup;
	}

	buf->head = buf->tail = 0;
	buf->overflow = 0;
	buf->num_signals = gpio_num;
	buf->signal_bits = 0;
	/* Compute how many bits are required to represent 0..gpio_num-1. */
	while ((gpio_num - 1) >> buf->signal_bits)
		buf->signal_bits++;

	for (i = 0; i < gpio_num; i++) {
		slot = monitoring_slots +
		       GPIO_MASK_TO_NUM(gpio_list[gpios[i]].mask);
		slot->buffer = buf;
		slot->signal_no = i;
	}

	/*
	 * The code relies on all EXTIn interrupts belonging to the same 32-bit
	 * NVIC register, so that multiple interrupts can be "unleashed"
	 * simultaneously.
	 */
	nvic_mask = 0;

	/*
	 * Disable interrupts in GPIO/EXTI detection circuits (should be
	 * disabled already, but disabled and clear pending bit to be on the
	 * safe side).
	 */
	for (i = 0; i < gpio_num; i++) {
		int gpio_num = GPIO_MASK_TO_NUM(gpio_list[gpios[i]].mask);
		gpio_disable_interrupt(gpios[i]);
		gpio_clear_pending_interrupt(gpios[i]);
		nvic_mask |= BIT(STM32_IRQ_EXTI0 + gpio_num);
	}
	/* Also disable interrupts at NVIC (interrupt controller) level. */
	CPU_NVIC_UNPEND(0) = nvic_mask;
	CPU_NVIC_DIS(0) = nvic_mask;

	for (i = 0; i < gpio_num; i++) {
		int gpio_num = GPIO_MASK_TO_NUM(gpio_list[gpios[i]].mask);
		slot = monitoring_slots + gpio_num;
		/*
		 * Tell the GPIO block to start detecting rising and falling
		 * edges, and latch them in STM32_EXTI_RPR and STM32_EXTI_FPR
		 * respectively.  Interrupts are still disabled in the NVIC,
		 * meaning that the execution will not be interrupted, yet, even
		 * if the GPIO block requests interrupt.
		 */
		gpio_enable_interrupt(gpios[i]);
		slot->tail_level = slot->head_level = gpio_get_level(gpios[i]);
		/*
		 * Race condition here!  If three or more edges happen in
		 * rapid succession, we may fail to record some of them, but
		 * we should never over-report edges.
		 *
		 * Since edge detection was enabled before the "head_level"
		 * was polled, if an edge happened between the two, then an
		 * interrupt is currently pending, and when handled after this
		 * loop, the logic in the gpio_edge interrupt handler would
		 * wrongly conclude that the signal must have seen two
		 * transitions, in order to end up at the same level as before.
		 * In order to avoid such over-reporting, we clear "pending"
		 * interrupt bit below, but only for the direction that goes
		 * "towards" the level measured above.
		 */
		if (slot->head_level)
			STM32_EXTI_RPR = BIT(gpio_num);
		else
			STM32_EXTI_FPR = BIT(gpio_num);
	}
	/*
	 * Now enable the handling of the set of interrupts.
	 */
	now = get_time();
	buf->head_time = now;
	CPU_NVIC_EN(0) = nvic_mask;

	buf->tail_time = now;
	num_cur_monitoring++;
	ccprintf("  @%lld\n", buf->tail_time.val);

	/*
	 * Dump the initial level of each input, for the convenience of the
	 * caller.  (Allow makes monitoring useful, even if a signal has no
	 * transitions during the monitoring period.
	 */
	for (i = 0; i < gpio_num; i++) {
		slot = monitoring_slots +
		       GPIO_MASK_TO_NUM(gpio_list[gpios[i]].mask);
		ccprintf("  %d %s %d\n", i, gpio_list[gpios[i]].name,
			 slot->tail_level);
	}

	return EC_SUCCESS;

out_cleanup:
	i = gpio_num;
out_partial_cleanup:
	while (i-- > 0) {
		monitoring_slots[GPIO_MASK_TO_NUM(gpio_list[gpios[i]].mask)]
			.gpio_signal = GPIO_COUNT;
	}
	return rv;
}

static int command_gpio_monitoring_read(int argc, const char **argv)
{
	int gpios[16];
	int gpio_num = argc - 3;
	int i;
	struct cyclic_buffer_header_t *buf = NULL;
	struct monitoring_slot_t *slot;
	int gpio_signals_by_no[16];
	uint8_t signal_bits;
	uint32_t tail, head;
	timestamp_t tail_time, now;

	if (gpio_num <= 0 || gpio_num > 16)
		return EC_ERROR_PARAM_COUNT;

	for (i = 0; i < gpio_num; i++) {
		gpios[i] = find_signal_by_name(argv[3 + i]);
		if (gpios[i] == GPIO_COUNT)
			return EC_ERROR_PARAM3 + i; /* May overflow */
		slot = monitoring_slots +
		       GPIO_MASK_TO_NUM(gpio_list[gpios[i]].mask);
		if (slot->gpio_signal != gpios[i]) {
			ccprintf("Error: Not monitoring %s\n",
				 gpio_list[gpios[i]].name);
			return EC_ERROR_PARAM3 + i;
		}
		if (slot->signal_no != i) {
			ccprintf("Error: Inconsistent order at %s\n",
				 gpio_list[gpios[i]].name);
			return EC_ERROR_PARAM3 + i;
		}
		if (buf == NULL) {
			buf = slot->buffer;
		} else if (buf != slot->buffer) {
			ccprintf(
				"Error: Not monitoring %s as part of same groups as %s\n",
				gpio_list[gpios[i]].name,
				gpio_list[gpios[0]].name);
			return EC_ERROR_PARAM3 + i;
		}
		gpio_signals_by_no[slot->signal_no] = gpios[i];
	}
	if (gpio_num != buf->num_signals) {
		ccprintf("Error: Not full set of signals monitored\n");
		return EC_ERROR_INVAL;
	}

	/*
	 * We read current time, before taking a snapshot of the head pointer as
	 * set by the interrupt handler.  This way, we can guarantee that the
	 * transcript will include any edge happening at or before the `now`
	 * timestamp.  If an interrupt happens between the two lines below,
	 * causing our head pointer to include an event that happened after
	 * "now", then it will be skipped in the loop below, and kept for the
	 * next invocation of `gpio monitoring read`.
	 */
	now = get_time();
	head = buf->head;

	ccprintf("  @%lld\n", now.val);
	signal_bits = buf->signal_bits;
	tail = buf->tail;
	tail_time = buf->tail_time;
	while (tail != head) {
		uint8_t *buffer = buf->data;
		timestamp_t diff;
		uint8_t byte;
		uint8_t signal_no;
		int shift = 0;
		uint32_t tentative_tail = tail;
		struct monitoring_slot_t *slot;
		diff.val = 0;
		do {
			byte = buffer[tentative_tail++];
			if (tentative_tail == buf->size)
				tentative_tail = 0;
			diff.val |= (byte & 0x7F) << shift;
			shift += 7;
		} while (byte & 0x80);
		signal_no = diff.val & (0xFF >> (8 - signal_bits));
		diff.val >>= signal_bits;
		if (tail_time.val + diff.val > now.val) {
			/*
			 * Do not consume this or subsequent records, which
			 * apparently happened after our "now" timestamp from
			 * earlier in the execution of this method.
			 */
			break;
		}
		tail = tentative_tail;
		tail_time.val += diff.val;
		slot = monitoring_slots +
		       GPIO_MASK_TO_NUM(
			       gpio_list[gpio_signals_by_no[signal_no]].mask);
		/* To conserve bandwidth, timestamps are relative to `now`. */
		ccprintf("  %d %lld %s\n", signal_no, tail_time.val - now.val,
			 slot->tail_level ? "F" : "R");
		/* Flush console to avoid truncating output */
		cflush();
		slot->tail_level = !slot->tail_level;
	}
	buf->tail = tail;
	buf->tail_time = tail_time;
	if (buf->overflow) {
		ccprintf("Error: Buffer overflow\n");
	}

	return EC_SUCCESS;
}

static int command_gpio_monitoring_stop(int argc, const char **argv)
{
	int gpios[16];
	int gpio_num = argc - 3;
	int i;
	struct cyclic_buffer_header_t *buf = NULL;
	struct monitoring_slot_t *slot;

	if (gpio_num <= 0 || gpio_num > 16)
		return EC_ERROR_PARAM_COUNT;

	for (i = 0; i < gpio_num; i++) {
		gpios[i] = find_signal_by_name(argv[3 + i]);
		if (gpios[i] == GPIO_COUNT)
			return EC_ERROR_PARAM3 + i; /* May overflow */
		slot = monitoring_slots +
		       GPIO_MASK_TO_NUM(gpio_list[gpios[i]].mask);
		if (slot->gpio_signal != gpios[i]) {
			ccprintf("Error: Not monitoring %s\n",
				 gpio_list[gpios[i]].name);
			return EC_ERROR_PARAM3 + i;
		}
		if (buf == NULL) {
			buf = slot->buffer;
		} else if (buf != slot->buffer) {
			ccprintf(
				"Error: Not monitoring %s as part of same groups as %s\n",
				gpio_list[gpios[i]].name,
				gpio_list[gpios[0]].name);
			return EC_ERROR_PARAM3 + i;
		}
	}
	if (gpio_num != buf->num_signals) {
		ccprintf("Error: Not full set of signals monitored\n");
		return EC_ERROR_INVAL;
	}

	for (i = 0; i < gpio_num; i++) {
		gpio_disable_interrupt(gpios[i]);
	}

	/*
	 * With no more interrupts modifying the buffer, it can be deallocated.
	 */
	num_cur_monitoring--;
	for (i = 0; i < gpio_num; i++) {
		slot = monitoring_slots +
		       GPIO_MASK_TO_NUM(gpio_list[gpios[i]].mask);
		slot->gpio_signal = GPIO_COUNT;
		slot->buffer = NULL;
	}

	if (buf->overflow)
		atomic_sub(&num_cur_error_conditions, 1);

	free_cyclic_buffer(buf);
	return EC_SUCCESS;
}

static int command_gpio_monitoring(int argc, const char **argv)
{
	if (argc < 3)
		return EC_ERROR_PARAM_COUNT;
	if (!strcasecmp(argv[2], "start"))
		return command_gpio_monitoring_start(argc, argv);
	if (!strcasecmp(argv[2], "read"))
		return command_gpio_monitoring_read(argc, argv);
	if (!strcasecmp(argv[2], "stop"))
		return command_gpio_monitoring_stop(argc, argv);
	return EC_ERROR_PARAM2;
}

static int command_gpio(int argc, const char **argv)
{
	if (argc < 2)
		return EC_ERROR_PARAM_COUNT;
	if (!strcasecmp(argv[1], "analog-set"))
		return command_gpio_analog_set(argc, argv);
	if (!strcasecmp(argv[1], "monitoring"))
		return command_gpio_monitoring(argc, argv);
	if (!strcasecmp(argv[1], "multiset"))
		return command_gpio_multiset(argc, argv);
	return EC_ERROR_PARAM1;
}
DECLARE_CONSOLE_COMMAND_FLAGS(
	gpio, command_gpio,
	"multiset name [level] [mode] [pullmode] [milli_volts]"
	"\nanalog-set name milli_volts"
	"\nmonitoring start name..."
	"\nmonitoring read name..."
	"\nmonitoring stop name...",
	"GPIO manipulation", CMD_FLAG_RESTRICTED);

static int command_reinit(int argc, const char **argv)
{
	const struct gpio_info *g = gpio_list;
	int i;

	stop_all_gpio_monitoring();

	/* Set all GPIOs to defaults */
	for (i = 0; i < GPIO_COUNT; i++, g++) {
		int flags = g->flags;

		if (flags & GPIO_DEFAULT)
			continue;

		if (flags & GPIO_ALTERNATE)
			flags |= extra_alternate_flags(i);

		/* Set up GPIO based on flags */
		gpio_set_flags_by_mask(g->port, g->mask, flags);
	}

	/* Disable any DAC (which would override GPIO function of pins) */
	STM32_DAC_CR = 0;

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND_FLAGS(reinit, command_reinit, "",
			      "Stop any ongoing operation",
			      CMD_FLAG_RESTRICTED);

static void led_tick(void)
{
	/* Indicate ongoing GPIO monitoring by flashing the green LED. */
	if (num_cur_monitoring)
		gpio_set_level(GPIO_NUCLEO_LED1,
			       !gpio_get_level(GPIO_NUCLEO_LED1));
	else {
		/*
		 * If not flashing, leave the green LED on, to indicate that
		 * HyperDebug firmware is running and ready.
		 */
		gpio_set_level(GPIO_NUCLEO_LED1, 1);
	}
	/* Indicate error conditions by flashing red LED. */
	if (atomic_add(&num_cur_error_conditions, 0))
		gpio_set_level(GPIO_NUCLEO_LED3,
			       !gpio_get_level(GPIO_NUCLEO_LED3));
	else {
		/*
		 * If not flashing, leave the red LED off.
		 */
		gpio_set_level(GPIO_NUCLEO_LED3, 0);
	}
}
DECLARE_HOOK(HOOK_TICK, led_tick, HOOK_PRIO_DEFAULT);
