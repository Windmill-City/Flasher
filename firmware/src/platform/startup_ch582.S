/*
 * CH582F startup and vector table.
 *
 * The vector table is copied to RAM and mtvec uses the WCH fast-interrupt
 * absolute mode (base | 3).
 */

    .section .vector, "ax", @progbits
    .balign 4
    .global _vector_base
    .type _vector_base, @object
_vector_base:
    .option push
    .option norvc

    .word 0
    .word 0
    .word NMI_Handler
    .word HardFault_Handler
    .word 0xF3F9BDA9
    .word Ecall_M_Mode_Handler
    .word 0
    .word 0
    .word Ecall_U_Mode_Handler
    .word Break_Point_Handler
    .word 0
    .word 0
    .word SysTick_Handler
    .word 0
    .word SW_Handler
    .word 0

    .word TMR0_IRQHandler
    .word GPIOA_IRQHandler
    .word GPIOB_IRQHandler
    .word SPI0_IRQHandler
    .word BB_IRQHandler
    .word LLE_IRQHandler
    .word USB_IRQHandler
    .word USB2_IRQHandler
    .word TMR1_IRQHandler
    .word TMR2_IRQHandler
    .word UART0_IRQHandler
    .word UART1_IRQHandler
    .word RTC_IRQHandler
    .word ADC_IRQHandler
    .word I2C_IRQHandler
    .word PWMX_IRQHandler
    .word TMR3_IRQHandler
    .word UART2_IRQHandler
    .word UART3_IRQHandler
    .word WDOG_BAT_IRQHandler

    .option pop
    .size _vector_base, . - _vector_base

    .section .vector_handler, "ax", @progbits

    .weak NMI_Handler
    .weak HardFault_Handler
    .weak Ecall_M_Mode_Handler
    .weak Ecall_U_Mode_Handler
    .weak Break_Point_Handler
    .weak SysTick_Handler
    .weak SW_Handler
    .weak TMR0_IRQHandler
    .weak GPIOA_IRQHandler
    .weak GPIOB_IRQHandler
    .weak SPI0_IRQHandler
    .weak BB_IRQHandler
    .weak LLE_IRQHandler
    .weak USB_IRQHandler
    .weak USB2_IRQHandler
    .weak TMR1_IRQHandler
    .weak TMR2_IRQHandler
    .weak UART0_IRQHandler
    .weak UART1_IRQHandler
    .weak RTC_IRQHandler
    .weak ADC_IRQHandler
    .weak I2C_IRQHandler
    .weak PWMX_IRQHandler
    .weak TMR3_IRQHandler
    .weak UART2_IRQHandler
    .weak UART3_IRQHandler
    .weak WDOG_BAT_IRQHandler

NMI_Handler:
HardFault_Handler:
Ecall_M_Mode_Handler:
Ecall_U_Mode_Handler:
Break_Point_Handler:
SysTick_Handler:
SW_Handler:
TMR0_IRQHandler:
GPIOA_IRQHandler:
GPIOB_IRQHandler:
SPI0_IRQHandler:
BB_IRQHandler:
LLE_IRQHandler:
USB_IRQHandler:
USB2_IRQHandler:
TMR1_IRQHandler:
TMR2_IRQHandler:
UART0_IRQHandler:
UART1_IRQHandler:
RTC_IRQHandler:
ADC_IRQHandler:
I2C_IRQHandler:
PWMX_IRQHandler:
TMR3_IRQHandler:
UART2_IRQHandler:
UART3_IRQHandler:
WDOG_BAT_IRQHandler:
.Ldefault_handler:
    j .Ldefault_handler

    .section .start, "ax", @progbits
    .balign 4
    .global _start
    .type _start, @function
_start:
    /* Ultra-early boot indicator: PA12 high before any C code or clock setup.
       If this LED never lights, the bootloader did not enter _start. */
    lui a4, 0x40001
    li  a5, 0x1000
    sw  a5, 0xA0(a4)
    sw  a5, 0xA8(a4)

    .option push
    .option norelax
    la gp, __global_pointer$
    la sp, _eusrstack
    .option pop

    /* Copy .highcode (including the vector table) from flash to RAM. */
    la a0, _highcode_lma
    la a1, _highcode_vma_start
    la a2, _highcode_vma_end
    bgeu a1, a2, .Lcopy_highcode_done
.Lcopy_highcode_loop:
    lw t0, 0(a0)
    sw t0, 0(a1)
    addi a0, a0, 4
    addi a1, a1, 4
    bltu a1, a2, .Lcopy_highcode_loop
.Lcopy_highcode_done:


    /* Copy .data from flash to RAM. */
    la a0, _data_lma
    la a1, _data_vma
    la a2, _edata
    bgeu a1, a2, .Lcopy_data_done
.Lcopy_data_loop:
    lw t0, 0(a0)
    sw t0, 0(a1)
    addi a0, a0, 4
    addi a1, a1, 4
    bltu a1, a2, .Lcopy_data_loop
.Lcopy_data_done:

    /* Clear .bss. */
    la a0, _sbss
    la a1, _ebss
    bgeu a0, a1, .Lclear_bss_done
.Lclear_bss_loop:
    sw zero, 0(a0)
    addi a0, a0, 4
    bltu a0, a1, .Lclear_bss_loop
.Lclear_bss_done:

    fence.i

    li t0, 0x1f
    csrw 0xbc0, t0
    li t0, 0x3
    csrw 0x804, t0

    la t0, _vector_base
    ori t0, t0, 3
    csrw mtvec, t0

    j main
    .size _start, . - _start
