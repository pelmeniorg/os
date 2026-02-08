OUTPUT_FORMAT(elf64-x86-64)
ENTRY(kmain)

PHDRS {
    text   PT_LOAD FLAGS(5);  /* rx  */
    rodata PT_LOAD FLAGS(4);  /* r   */
    data   PT_LOAD FLAGS(6);  /* rw  */
}

SECTIONS {
    . = 0xffffffff80000000;

    .text : {
        *(.text .text.*)
    } :text

    . = ALIGN(4K);
    .rodata : {
        *(.rodata .rodata.*)
    } :rodata

    . = ALIGN(4K);
    .data : {
        *(.data .data.*)
    } :data

    .bss : {
        *(COMMON)
        *(.bss .bss.*)
    } :data

    /DISCARD/ : {
        *(.eh_frame*)
        *(.note*)
        *(.comment)
    }
}