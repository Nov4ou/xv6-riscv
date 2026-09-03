        # qemu -kernel loads the kernel at 0x80000000
        # and causes each hart (i.e. CPU) to jump there.
        # kernel.ld causes the following code to
        # be placed at 0x80000000.
.section .text
.global _entry
_entry:
        # set up a stack for C.
        # stack0 is declared in start.c,
        # with a 4096-byte stack per CPU.
        # sp = stack0 + ((hartid + 1) * 4096)

        # QEMU gives us:
        # a0 = hart id
        # a1 = physical address of the DTB

        la sp, stack0
        li t0, 1024*4
        csrr t1, mhartid
        addi t1, t1, 1
        mul t0, t0, t1
        add sp, sp, t0
        # jump to start() in start.c
        # a0 and a1 are still intact here.
        call start
spin:
        j spin
