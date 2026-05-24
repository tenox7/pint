@ Entry stub for the handoff-test payload (a minimal serial shell - the kernel
@ stand-in; see shell.c and ARCHITECTURE.md).
@
@ The loader copies the payload blob to its link/load base (build/payload.ld,
@ which MUST match PAYLOAD_LOAD_BASE in arcfw/arm/init.c) and jumps here in ARM
@ mode. This sets up a stack, zeroes BSS, and calls shell_main(). Entry is at
@ offset 0 of the .bin via the .text.entry section, placed first by payload.ld.

.section .text.entry, "ax"
.arm
.global _pstart
_pstart:
    ldr     sp, =_pstack_top
    ldr     r0, =_pbss_start
    ldr     r1, =_pbss_end
    mov     r2, #0
1:  cmp     r0, r1
    bhs     2f
    str     r2, [r0], #4
    b       1b
2:  bl      shell_main
3:  wfi                          @ shell_main does not normally return
    b       3b

.section .note.GNU-stack,"",%progbits
