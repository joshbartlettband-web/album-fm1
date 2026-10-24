/* SPDX-License-Identifier: MPL-2.0
 * Copyright (c) 2026 Hausway
 * The entry's four steps are felucca-bsp's chip/fm1_cpu1.S (MPL-2.0, Leo Kuroshita, Hugelton Instruments), after the
 * AC79 SDK's cpu1_start: the two stacks, the return address, then rti into the second core's C. */
/* Album's second core: its entry and its stacks (1 KiB user, 4 KiB supervisor; the SDK's are 0x300 and 0x1000). */
	.section .text.album_cpu1,"ax",@progbits
	.globl album_cpu1_entry
album_cpu1_entry:
	usp = album_cpu1_ustack_top
	sp = album_cpu1_sstack_top
	reti = album_cpu1_main
	rti

	.section .bss.album_cpu1_stacks,"aw",@nobits
	.balign 32
	.globl album_cpu1_stacks_lo
album_cpu1_stacks_lo:
	.space 0x400
	.globl album_cpu1_ustack_top
album_cpu1_ustack_top:
	.space 0x1000
	.globl album_cpu1_sstack_top
album_cpu1_sstack_top:
	.globl album_cpu1_stacks_hi
album_cpu1_stacks_hi:
