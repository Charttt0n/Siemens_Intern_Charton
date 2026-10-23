ENTRY(Reset_Handler)
MEMORY {
  CODE (rx) : ORIGIN = 0x00000000, LENGTH = 64K
  RAM (rwx) : ORIGIN = 0x20000000, LENGTH = 3M
}
/* CODE is QEMU SSRAM, not persistent or erasable MCU Flash. */
_stack_top = ORIGIN(RAM) + LENGTH(RAM);
_stack_bottom = _stack_top - 4K;
SECTIONS {
  .isr_vector : { KEEP(*(.isr_vector)) } > CODE
  .text : { *(.text*) *(.rodata*) . = ALIGN(4); } > CODE
  .ARM.exidx : { *(.ARM.exidx*) } > CODE
  .data : {
    . = ALIGN(4); _sdata = .; *(.data*) . = ALIGN(4); _edata = .;
  } > RAM AT> CODE
  _sidata = LOADADDR(.data);
  .bss (NOLOAD) : {
    . = ALIGN(4); _sbss = .; *(.bss*) *(COMMON) . = ALIGN(4); _ebss = .;
  } > RAM
  .stack _stack_bottom (NOLOAD) : { . += 4K; } > RAM
}
ASSERT(ADDR(.isr_vector) == 0, "Reset vector must be at address zero")
ASSERT(SIZEOF(.isr_vector) == 192, "AN500 requires 48 vector entries")
ASSERT((_stack_top & 7) == 0, "MSP must be 8-byte aligned")
ASSERT((_sidata & 3) == 0 && (_sdata & 3) == 0 && (_edata & 3) == 0,
       "Word-copy .data boundaries must be aligned")
ASSERT((_sbss & 3) == 0 && (_ebss & 3) == 0, "Word-clear .bss must be aligned")
ASSERT(_ebss <= _stack_bottom, "Static RAM overlaps reserved stack")

/* 0x20300000..0x203fffff belongs to the host-loaded NOR model.
   Outside all ELF segments: neither startup nor -kernel may initialize it. */
ASSERT(_stack_top == 0x20300000, "Stack must end below simulated storage")
