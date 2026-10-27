/* mps2-an385 메모리 배치
 * 0x00000000 SSRAM1 4MB: 코드 (QEMU가 -kernel로 여기에 올림)
 * 0x20000000 SSRAM2/3 4MB: 데이터 · 힙 · 스택
 */
MEMORY
{
    FLASH (rx)  : ORIGIN = 0x00000000, LENGTH = 4M
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 4M
}

ENTRY(Reset_Handler)
_estack = ORIGIN(RAM) + LENGTH(RAM);    /* 메인 스택: RAM 끝에서 아래로 */

SECTIONS
{
    .vectors : { KEEP(*(.vectors)) } > FLASH

    .text : {
        *(.text*)
        *(.rodata*)
        KEEP(*(.init)) KEEP(*(.fini))
    } > FLASH

    .ARM.exidx : { *(.ARM.exidx*) } > FLASH

    _sidata = LOADADDR(.data);
    .data : {
        _sdata = .;
        *(.data*)
        _edata = .;
    } > RAM AT > FLASH

    .bss (NOLOAD) : {
        _sbss = .;
        *(.bss*) *(COMMON)
        _ebss = .;
    } > RAM

    end = .;                            /* newlib _sbrk가 쓰는 힙 시작 */
    __end__ = .;
}
