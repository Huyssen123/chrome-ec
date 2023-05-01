/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Panic handling, including displaying a message on the panic reporting
 * device, which is currently the UART.
 */

#ifndef __CROS_EC_PANIC_H
#define __CROS_EC_PANIC_H

#include "software_panic.h"

#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

enum cortex_panic_frame_registers {
	CORTEX_PANIC_FRAME_REGISTER_R0 = 0,
	CORTEX_PANIC_FRAME_REGISTER_R1,
	CORTEX_PANIC_FRAME_REGISTER_R2,
	CORTEX_PANIC_FRAME_REGISTER_R3,
	CORTEX_PANIC_FRAME_REGISTER_R12,
	CORTEX_PANIC_FRAME_REGISTER_LR,
	CORTEX_PANIC_FRAME_REGISTER_PC,
	CORTEX_PANIC_FRAME_REGISTER_PSR,
	NUM_CORTEX_PANIC_FRAME_REGISTERS
};

enum cortex_panic_registers {
	CORTEX_PANIC_REGISTER_PSP = 0,
	CORTEX_PANIC_REGISTER_IPSR,
	CORTEX_PANIC_REGISTER_MSP,
	CORTEX_PANIC_REGISTER_R4,
	CORTEX_PANIC_REGISTER_R5,
	CORTEX_PANIC_REGISTER_R6,
	CORTEX_PANIC_REGISTER_R7,
	CORTEX_PANIC_REGISTER_R8,
	CORTEX_PANIC_REGISTER_R9,
	CORTEX_PANIC_REGISTER_R10,
	CORTEX_PANIC_REGISTER_R11,
	CORTEX_PANIC_REGISTER_LR,
	NUM_CORTEX_PANIC_REGISTERS
};

/* ARM Cortex-Mx registers saved on panic */
struct cortex_panic_data {
	/* See cortex_panic_registers enum for information about registers */
	uint32_t regs[NUM_CORTEX_PANIC_REGISTERS];

	/* See cortex_panic_frame_registers enum for more information */
	uint32_t frame[NUM_CORTEX_PANIC_FRAME_REGISTERS];

	uint32_t mmfs;
	uint32_t bfar;
	uint32_t mfar;
	uint32_t shcsr;
	uint32_t hfsr;
	uint32_t dfsr;
};

/* NDS32 N8 registers saved on panic */
struct nds32_n8_panic_data {
	uint32_t itype;
	uint32_t regs[16];        /* r0-r10, r15, fp, gp, lp, sp */
	uint32_t ipc;
	uint32_t ipsw;
};

/* Data saved across reboots */
struct panic_data {
	uint8_t arch;             /* Architecture (PANIC_ARCH_*) */
	uint8_t struct_version;   /* Structure version (currently 2) */
	uint8_t flags;            /* Flags (PANIC_DATA_FLAG_*) */
	uint8_t reserved;         /* Reserved; set 0 */

	/* core specific panic data */
	union {
		struct cortex_panic_data cm;       /* Cortex-Mx registers */
		struct nds32_n8_panic_data nds_n8; /* NDS32 N8 registers */
	};

	/*
	 * These fields go at the END of the struct so we can find it at the
	 * end of memory.
	 */
	uint32_t struct_size;     /* Size of this struct */
	uint32_t magic;           /* PANIC_SAVE_MAGIC if valid */
};

#define PANIC_DATA_MAGIC 0x21636e50  /* "Pnc!" */
enum panic_arch {
	PANIC_ARCH_CORTEX_M = 1,     /* Cortex-M architecture */
	PANIC_ARCH_NDS32_N8 = 2,     /* NDS32 N8 architecture */
};

/* Use PANIC_DATA_PTR to refer to the persistent storage location */
#define PANIC_DATA_PTR ((struct panic_data *)CONFIG_PANIC_DATA_BASE)

/* Flags for panic_data.flags */
/* panic_data.frame is valid */
#define PANIC_DATA_FLAG_FRAME_VALID    (1 << 0)
/* Already printed at console */
#define PANIC_DATA_FLAG_OLD_CONSOLE    (1 << 1)
/* Already returned via host command */
#define PANIC_DATA_FLAG_OLD_HOSTCMD    (1 << 2)
/* Already reported via host event */
#define PANIC_DATA_FLAG_OLD_HOSTEVENT  (1 << 3)
/* The data was truncated to fit panic info host cmd */
#define PANIC_DATA_FLAG_TRUNCATED      (1 << 4)
/* System safe mode was started after a panic */
#define PANIC_DATA_FLAG_SAFE_MODE_STARTED  (1 << 5)
/* System safe mode failed to start */
#define PANIC_DATA_FLAG_SAFE_MODE_FAIL_PRECONDITIONS (1 << 6)

/**
 * Write a string to the panic reporting device
 *
 * This function will not return until the string has left the UART
 * data register. Any previously queued UART traffic is displayed first.
 *
 * @param ch	Character to write
 */
void panic_puts(const char *s);

/**
 * Very basic printf() for use in panic situations
 *
 * See panic_vprintf() for full details
 *
 * @param format	printf-style format string
 * @param ...		Arguments to process
 */
void panic_printf(const char *format, ...);

/*
 * Print saved panic information
 *
 * @param pdata pointer to saved panic data
 */
void panic_data_print(const struct panic_data *pdata);

/**
 * Report an assertion failure and reset
 *
 * @param msg		Assertion expression or other message
 * @param func		Function name where assertion happened
 * @param fname		File name where assertion happened
 * @param linenum	Line number where assertion happened
 */
#ifdef CONFIG_DEBUG_ASSERT_BRIEF
void panic_assert_fail(const char *fname, int linenum)
	__attribute__((noreturn));
#else
void panic_assert_fail(const char *msg, const char *func, const char *fname,
		       int linenum) __attribute__((noreturn));
#endif

/**
 * Display a custom panic message and reset
 *
 * @param msg	Panic message
 */
void panic(const char *msg) __attribute__((noreturn));

/**
 * Display a default message and reset
 */
void panic_reboot(void) __attribute__((noreturn));

#ifdef CONFIG_SOFTWARE_PANIC
/**
 * Store a panic log and halt the system for a software-related reason, such as
 * stack overflow or assertion failure.
 */
void software_panic(uint32_t reason, uint32_t info) __attribute__((noreturn));

/**
 * Log a panic in the panic log, but don't halt the system. Normally
 * called on the subsequent reboot after panic detection.
 */
void panic_set_reason(uint32_t reason, uint32_t info, uint8_t exception);

/**
 * Retrieve the currently stored panic reason + info.
 */
void panic_get_reason(uint32_t *reason, uint32_t *info, uint8_t *exception);
#endif

/**
 * Enable/disable bus fault handler
 *
 * @param ignored	Non-zero if ignoring bus fault
 */
void ignore_bus_fault(int ignored);

/**
 * Return a pointer to the saved data from a previous panic that can be
 * safely interpreted
 *
 * @param pointer to the valid panic data, or NULL if none available (for example,
 * the last reboot was not caused by a panic).
 */
struct panic_data *panic_get_data(void);

/**
 * Return a pointer to the beginning of panic data. This function can be
 * used to obtain pointer which can be used to calculate place of other
 * structures (eg. jump_data). This function should not be used to get access
 * to panic_data structure as it might not be valid
 *
 * @param pointer to the beginning of panic_data, or NULL if there is no
 * panic_data
 */
uintptr_t get_panic_data_start(void);

/*
 * Return a pointer to panic_data structure that can be safely written.
 * Please note that this function can move jump data and jump tags.
 * It can also delete panic data from previous boot, so this function
 * should be used when we are sure that we don't need it.
 *
 * @param pointer to panic_data structure that can be safely written
 */
struct panic_data *get_panic_data_write(void);

/**
 * Return a pointer to the stack of the process that caused the panic.
 * The implementation of this function will depend on the architecture.
 */
uint32_t get_panic_stack_pointer(const struct panic_data *pdata);

/**
 * Chip-specific implementation for backing up panic data to persistent
 * storage. This function is used to ensure that the panic data can survive loss
 * of VCC power rail.
 *
 * There is no generic restore function provided since every chip can decide
 * when it is safe to restore panic data during the system initialization step.
 */
void chip_panic_data_backup(void);

#ifdef __cplusplus
}
#endif

#endif  /* __CROS_EC_PANIC_H */
