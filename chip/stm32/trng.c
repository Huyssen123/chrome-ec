/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Hardware Random Number Generator */

#include "common.h"
#include "console.h"
#include "panic.h"
#include "registers.h"
#include "task.h"
#include "trng.h"
#include "util.h"

uint32_t rand(void)
{
	int tries = 300;
	/* Wait for a valid random number */
	while (!(STM32_RNG_SR & STM32_RNG_SR_DRDY) && --tries)
		;
	/* we cannot afford to feed the caller with a dummy number */
	if (!tries)
		software_panic(PANIC_SW_BAD_RNG, task_get_current());
	/* Finally the 32-bit of entropy */
	return STM32_RNG_DR;
}

test_mockable void rand_bytes(void *buffer, size_t len)
{
	while (len) {
		uint32_t number = rand();
		size_t cnt = 4;
		/* deal with the lack of alignment guarantee in the API */
		uintptr_t align = (uintptr_t)buffer & 3;

		if (len < 4 || align) {
			cnt = MIN(4 - align, len);
			memcpy(buffer, &number, cnt);
		} else {
			*(uint32_t *)buffer = number;
		}
		len -= cnt;
		buffer += cnt;
	}
}

test_mockable void init_trng(void)
{
#ifdef CHIP_FAMILY_STM32L4
	/* Enable the 48Mhz internal RC oscillator */
	STM32_RCC_CRRCR |= STM32_RCC_CRRCR_HSI48ON;
	/* no timeout: we watchdog if the oscillator doesn't start */
	while (!(STM32_RCC_CRRCR & STM32_RCC_CRRCR_HSI48RDY))
		;

	/* Clock the TRNG using the HSI48 */
	STM32_RCC_CCIPR = (STM32_RCC_CCIPR & ~STM32_RCC_CCIPR_CLK48SEL_MASK)
			| (0 << STM32_RCC_CCIPR_CLK48SEL_SHIFT);
#elif defined(CHIP_FAMILY_STM32H7)
	/* Enable the 48Mhz internal RC oscillator */
	STM32_RCC_CR |= STM32_RCC_CR_HSI48ON;
	/* no timeout: we watchdog if the oscillator doesn't start */
	while (!(STM32_RCC_CR & STM32_RCC_CR_HSI48RDY))
		;

	/* Clock the TRNG using the HSI48 */
	STM32_RCC_D2CCIP2R =
		(STM32_RCC_D2CCIP2R & ~STM32_RCC_D2CCIP2_RNGSEL_MASK)
			| STM32_RCC_D2CCIP2_RNGSEL_HSI48;
#else
#error "Please add support for CONFIG_RNG on this chip family."
#endif
	/* Enable the RNG logic */
	STM32_RCC_AHB2ENR |= STM32_RCC_AHB2ENR_RNGEN;
	/* Start the random number generation */
	STM32_RNG_CR |= STM32_RNG_CR_RNGEN;
}

test_mockable void exit_trng(void)
{
	STM32_RNG_CR &= ~STM32_RNG_CR_RNGEN;
	STM32_RCC_AHB2ENR &= ~STM32_RCC_AHB2ENR_RNGEN;
#ifdef CHIP_FAMILY_STM32L4
	STM32_RCC_CRRCR &= ~STM32_RCC_CRRCR_HSI48ON;
#elif defined(CHIP_FAMILY_STM32H7)
	STM32_RCC_CR &= ~STM32_RCC_CR_HSI48ON;
#endif
}

#ifdef CONFIG_CMD_RAND
static int command_rand(int argc, char **argv)
{
	uint8_t data[32];

	init_trng();
	rand_bytes(data, sizeof(data));
	exit_trng();

	ccprintf("rand %.*h\n", sizeof(data), data);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(rand, command_rand,
			NULL, "Output random bytes to console.");
#endif
