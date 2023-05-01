/* Copyright (c) 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "console.h"
#include "cpu.h"
#include "host_command.h"
#include "panic.h"
#include "printf.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "util.h"

/* Whether bus fault is ignored */
static int bus_fault_ignored;

/*
 * This array maps an interrupt vector number to the corresponding
 * exception name. See see "Intel 64 and IA-32 Architectures Software
 * Developer's Manual", Volume 3A, Section 6.15.
 */
const static char *panic_reason[] = {
	"Divide By Zero",
	"Debug Exception",
	"NMI Interrupt",
	"Breakpoint Exception",
	"Overflow Exception",
	"BOUND Range Exceeded Exception",
	"Invalid Opcode Exception",
	"Device Not Available Exception",
	"Double Fault Exception",
	"Coprocessor Segment Overrun",
	"Invalid TSS Exception",
	"Segment Not Present",
	"Stack Fault Exception",
	"General Protection Fault",
	"Page Fault",
	"Reserved",
	"Math Fault",
	"Alignment Check Exception",
	"Machine Check Exception",
	"SIMD Floating Point Exception",
	"Virtualization Exception",
};

/*
 * Print panic data. This may be called either from the report_panic
 * procedure (below) while handling a panic, or from the panicinfo
 * console command.
 */
void panic_data_print(const struct panic_data *pdata)
{
	if (pdata->x86.vector <= 20)
		panic_printf("Reason: %s\n", panic_reason[pdata->x86.vector]);
	else
		panic_printf("Interrupt vector number: 0x%08X (unknown)\n",
			     pdata->x86.vector);
	panic_printf("\n");
	panic_printf("Error Code = 0x%08X\n", pdata->x86.error_code);
	panic_printf("EIP        = 0x%08X\n", pdata->x86.eip);
	panic_printf("CS         = 0x%08X\n", pdata->x86.cs);
	panic_printf("EFLAGS     = 0x%08X\n", pdata->x86.eflags);
	panic_printf("EAX        = 0x%08X\n", pdata->x86.eax);
	panic_printf("EBX        = 0x%08X\n", pdata->x86.ebx);
	panic_printf("ECX        = 0x%08X\n", pdata->x86.ecx);
	panic_printf("EDX        = 0x%08X\n", pdata->x86.edx);
	panic_printf("ESI        = 0x%08X\n", pdata->x86.esi);
	panic_printf("EDI        = 0x%08X\n", pdata->x86.edi);
}

/**
 * Default exception handler, which reports a panic.
 *
 * Declare this as a naked call so we can extract raw LR and IPSR values.
 */
void __keep exception_panic(void);
void exception_panic(void)
{
	/*
	 * If a panic were to occur during the reset procedure, we want
	 * to make sure that this panic will certainly cause a hard
	 * reset, rather than aontaskfw reset. Track if paniced once
	 * already.
	 */
	static int panic_once;

	register uint32_t eax asm("eax");
	register uint32_t ebx asm("ebx");
	register uint32_t ecx asm("ecx");
	register uint32_t edx asm("edx");
	register uint32_t esi asm("esi");
	register uint32_t edi asm("edi");

	/* Save registers to global panic structure */
	PANIC_DATA_PTR->x86.eax = eax;
	PANIC_DATA_PTR->x86.ebx = ebx;
	PANIC_DATA_PTR->x86.ecx = ecx;
	PANIC_DATA_PTR->x86.edx = edx;
	PANIC_DATA_PTR->x86.esi = esi;
	PANIC_DATA_PTR->x86.edi = edi;

	/* Save stack data to global panic structure */
	PANIC_DATA_PTR->x86.vector = vector;
	PANIC_DATA_PTR->x86.error_code = error_code;
	PANIC_DATA_PTR->x86.eip = eip;
	PANIC_DATA_PTR->x86.cs = cs;
	PANIC_DATA_PTR->x86.eflags = eflags;

	/* Initialize panic data */
	PANIC_DATA_PTR->arch = PANIC_ARCH_X86;
	PANIC_DATA_PTR->struct_version = 2;
	PANIC_DATA_PTR->magic = PANIC_DATA_MAGIC;

	/* Display the panic and reset */
	if (panic_once)
		panic_printf("\nWhile resetting from a panic, another panic"
			     " occurred!");

	panic_printf("\n========== PANIC ==========\n");
	panic_data_print(PANIC_DATA_PTR);
	panic_printf("\n");
	panic_printf("Resetting system...\n");
	panic_printf("===========================\n");

	if (panic_once) {
		system_reset(SYSTEM_RESET_HARD);
	} else {
		panic_once = 1;
		system_reset(0);
	}

	__builtin_unreachable();
}

#ifdef CONFIG_SOFTWARE_PANIC
void software_panic(uint32_t reason, uint32_t info)
{
	/* TODO: store panic log */
	while (1)
		;
}

void panic_set_reason(uint32_t reason, uint32_t info, uint8_t exception)
{
}

void panic_get_reason(uint32_t *reason, uint32_t *info, uint8_t *exception)
{
}
#endif

void bus_fault_handler(void)
{
	if (!bus_fault_ignored)
		exception_panic();
}

void ignore_bus_fault(int ignored)
{
	bus_fault_ignored = ignored;
}
