/*
 * Minimal Cortex-M0+ bare-metal startup for running the integer FFT dogfood
 * harness under Unicorn (emulated Cortex-M0, nRF51-style memory map). Not
 * part of the firmware build — exists only to demonstrate the test passing
 * on target silicon semantics.
 *
 * Provides the full newlib syscall stub set (semihosting only for _write
 * and _exit; the rest are dead stubs the harness never reaches).
 *
 * Build (host toolchain):
 *   arm-none-eabi-gcc -mcpu=cortex-m0plus -mthumb -Os -DPHASE_ENGINE_ENABLED \
 *       --specs=nano.specs -Wl,--gc-sections -Ilib/phase \
 *       -T tests/fft_target.ld \
 *       -o /tmp/int_fft_target.elf \
 *       tests/fft_target_startup.s tests/fft_target_syscalls.c \
 *       tests/test_integer_fft.c \
 *       lib/phase/int_fft.c -lm -lnosys
 * Run: python3 tests/run_fft_target.py /tmp/int_fft_target.elf
 */

    .syntax unified
    .arch armv6-m
    .cpu cortex-m0plus
    .thumb

/* nRF51822 memory map (microbit): flash @ 0x0, 16KB RAM @ 0x20000000. */
.equ RAM_TOP, 0x20004000

    .section .isr_vector, "a"
    .align 2
    .globl vector_table
vector_table:
    .word RAM_TOP                /* Initial SP */
    .word Reset_Handler          /* Reset */
    .word default_handler        /* NMI */
    .word default_handler        /* HardFault */
    .word default_handler        /* Reserved */
    .word default_handler
    .word default_handler
    .word default_handler
    .word default_handler
    .word default_handler
    .word default_handler
    .word default_handler        /* SVCall */
    .word default_handler
    .word default_handler
    .word default_handler
    .word default_handler        /* PendSV */

    .section .text.default_handler, "ax"
    .thumb_func
default_handler:
    b .

    .section .text.reset, "ax"
    .thumb_func
    .globl Reset_Handler
Reset_Handler:
    /* NOTE: .data is already in place — the harness loader maps the ELF's
     * RAM LOAD segment (which carries the initialized .data image) directly
     * at its VMA, so no flash->RAM copy is needed here. Only .bss must be
     * zeroed before main(). (Production firmware uses gossamer's startup.) */
    ldr r0, =__bss_start__
    ldr r1, =__bss_end__
    movs r2, #0
    movs r4, #4
    b 3f
2:
    str r2, [r0]
    adds r0, r0, r4
3:
    cmp r0, r1
    bcc 2b

    /* Jump to C main; on return, hand the exit code to semihost_exit. */
    bl main
    bl semihost_exit
    b .

    .section .text.semihost_write, "ax", %progbits
    .thumb_func
    .globl semihost_write
/* void semihost_write(const void *buf, int n);
 * The emulator intercepts this function entry (symbol address) via its
 * code hook, reads r0/r1 (AAPCS args), emits the bytes, then sets PC = LR
 * and skips the call. The body here is a no-op marker: on real hardware
 * it would issue SYS_WRITE (0x05) via BKPT 0xAB, but Unicorn's Thumb mode
 * raises UC_ERR_EXCEPTION on BKPT before a skip hook can run. */
semihost_write:
    bx lr

    .section .text.semihost_exit, "ax", %progbits
    .thumb_func
    .globl semihost_exit
/* void semihost_exit(uint32_t reason); SYS_EXIT (0x18). Same interception:
 * r0 = reason at entry; the hook stops emulation. */
semihost_exit:
    bx lr

    .end
