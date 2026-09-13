/* Linker script for running the integer FFT dogfood harness on an emulated
 * Cortex-M0 (Unicorn, nRF51-style memory map):
 * 256KB flash at 0x0, 16KB RAM at 0x20000000. The harness needs ~5KB.
 *
 * Not used by the real firmware build (gossamer provides its own layout).
 */

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x00000000, LENGTH = 256K
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 16K
}

ENTRY(Reset_Handler)

SECTIONS
{
    .isr_vector :
    {
        KEEP(*(.isr_vector))
    } > FLASH

    .text :
    {
        *(.text*)
        *(.rodata*)
    } > FLASH

    .data :
    {
        __data_start__ = .;
        *(.data*)
        __data_end__ = .;
    } > RAM AT > FLASH
    __data_load__ = LOADADDR(.data);

    .bss :
    {
        __bss_start__ = .;
        *(.bss*)
        *(COMMON)
        __bss_end__ = .;
    } > RAM

    . = ALIGN(8);
    __end__ = .;
    end = .;
    __bss_start__ = __bss_start__;
    __bss_end__ = __bss_end__;
}
