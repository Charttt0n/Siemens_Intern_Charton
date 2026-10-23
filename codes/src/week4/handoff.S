.syntax unified
.cpu cortex-m7
.thumb
.section .text.jump_to_app,"ax",%progbits
.global jump_to_app
.type jump_to_app,%function
.thumb_func
/* r0=MSP, r1=Thumb reset entry; no C stack accesses after changing MSP. */
jump_to_app:
  cpsid i
  movs r2, #0
  msr control, r2
  isb
  msr basepri, r2
  msr faultmask, r2
  msr psp, r2
  ldr r3, =app_return_trap
  mov lr, r3
  msr msp, r0
  dsb
  isb
  bx r1
.size jump_to_app, .-jump_to_app
.thumb_func
app_return_trap:
  cpsid i
1: wfi
  b 1b
