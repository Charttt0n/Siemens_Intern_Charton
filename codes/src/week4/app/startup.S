.syntax unified
.cpu cortex-m7
.thumb

/* AN500 has 32 external IRQs: 16 core entries + 32 IRQ entries. */
.section .isr_vector,"a",%progbits
.balign 256
.global app_vectors
app_vectors:
  .word _stack_top
  .word App_Reset_Handler
  .word App_Default_Handler /* NMI */
  .word App_Default_Handler /* HardFault */
  .word App_Default_Handler /* MemManage */
  .word App_Default_Handler /* BusFault */
  .word App_Default_Handler /* UsageFault */
  .word 0, 0, 0, 0
  .word App_SVC_Handler /* SVCall */
  .word App_Default_Handler /* DebugMonitor */
  .word 0
  .word App_Default_Handler /* PendSV */
  .word App_Default_Handler /* SysTick */
  .rept 32
  .word App_Default_Handler
  .endr

.section .text.App_Reset_Handler,"ax",%progbits
.global App_Reset_Handler
.type App_Reset_Handler,%function
.thumb_func
App_Reset_Handler:
  cpsid i
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
  bl app_main
  b App_Default_Handler
.size App_Reset_Handler, .-App_Reset_Handler

.section .text.App_Default_Handler,"ax",%progbits
.global App_Default_Handler
.type App_Default_Handler,%function
.thumb_func
App_Default_Handler:
  /* Do not depend on UART being initialized on an early exception. */
  mrs r0, ipsr
  b app_exception_handler
.size App_Default_Handler, .-App_Default_Handler
