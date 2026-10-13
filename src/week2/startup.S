.syntax unified
.cpu cortex-m7
.thumb

/* AN500 has 32 external IRQs: 16 core entries + 32 IRQ entries. */
.section .isr_vector,"a",%progbits
.balign 256
.global vectors
vectors:
  .word _stack_top
  .word Reset_Handler
  .word Default_Handler /* NMI */
  .word Default_Handler /* HardFault */
  .word Default_Handler /* MemManage */
  .word Default_Handler /* BusFault */
  .word Default_Handler /* UsageFault */
  .word 0, 0, 0, 0
  .word Default_Handler /* SVCall */
  .word Default_Handler /* DebugMonitor */
  .word 0
  .word Default_Handler /* PendSV */
  .word Default_Handler /* SysTick */
  .rept 32
  .word Default_Handler
  .endr

.section .text.Reset_Handler,"ax",%progbits
.global Reset_Handler
.type Reset_Handler,%function
.thumb_func
Reset_Handler:
  /* Hardware has already loaded MSP from vector[0]. The linker asserts
     word-aligned section boundaries and an 8-byte-aligned stack. */
  ldr r0, =_sidata
  ldr r1, =_sdata
  ldr r2, =_edata
1:
  cmp r1, r2
  bcs 2f
  ldr r3, [r0], #4
  str r3, [r1], #4
  b 1b
2:
  ldr r1, =_sbss
  ldr r2, =_ebss
  movs r3, #0
3:
  cmp r1, r2
  bcs 4f
  str r3, [r1], #4
  b 3b
4:
  bl main
  b Default_Handler
.size Reset_Handler, .-Reset_Handler

.section .text.Default_Handler,"ax",%progbits
.global Default_Handler
.type Default_Handler,%function
.thumb_func
Default_Handler:
  /* Do not depend on UART being initialized on an early exception. */
  mrs r0, ipsr
  b exception_handler
.size Default_Handler, .-Default_Handler
