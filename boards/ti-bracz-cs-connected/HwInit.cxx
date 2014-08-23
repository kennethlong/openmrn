/** \copyright
 * Copyright (c) 2012, Stuart W Baker
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are  permitted provided that the following conditions are met:
 *
 *  - Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 *  - Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * \file HwInit.cxx
 * This file represents the hardware initialization for the TI Tiva MCU.
 *
 * @author Stuart W. Baker
 * @date 5 January 2013
 */

#include <cstdint>
#include <new>

#include "inc/hw_types.h"
#include "inc/hw_memmap.h"
#include "inc/hw_ints.h"
#include "driverlib/rom.h"
#include "driverlib/rom_map.h"
#include "driverlib/sysctl.h"
#include "driverlib/gpio.h"
#include "driverlib/timer.h"
#include "driverlib/interrupt.h"
#include "driverlib/pin_map.h"
#include "os/OS.hxx"
#include "TivaDev.hxx"

#include "dcc_control.hxx"

/** override stdin */
const char *STDIN_DEVICE = "/dev/ser0";

/** override stdout */
const char *STDOUT_DEVICE = "/dev/ser0";

/** override stderr */
const char *STDERR_DEVICE = "/dev/ser0";

/** UART 0 serial driver instance */
static TivaUart uart2("/dev/ser0", UART2_BASE, INT_RESOLVE(INT_UART2_, 0));

/** CAN 0 CAN driver instance */
static TivaCan can0("/dev/can0", CAN0_BASE, INT_RESOLVE(INT_CAN0_, 0));

extern "C" {
/** Blink LED */
uint32_t blinker_pattern = 0;
static uint32_t rest_pattern = 0;

void resetblink(uint32_t pattern)
{
    blinker_pattern = pattern;
    /* make a timer event trigger immediately */
}

void setblink(uint32_t pattern)
{
    resetblink(pattern);
}

#define BLINKER_PORT GPIO_PORTN_BASE
#define BLINKER_PIN GPIO_PIN_1

void timer5a_interrupt_handler(void)
{
    //
    // Clear the timer interrupt.
    //
    MAP_TimerIntClear(TIMER5_BASE, TIMER_TIMA_TIMEOUT);
    // Set output LED.
    MAP_GPIOPinWrite(BLINKER_PORT, BLINKER_PIN,
                     (rest_pattern & 1) ? BLINKER_PIN : 0);
    // Shift and maybe reset pattern.
    rest_pattern >>= 1;
    if (!rest_pattern)
        rest_pattern = blinker_pattern;
}

void diewith(uint32_t pattern)
{
    vPortClearInterruptMask(0x20);
    asm("cpsie i\n");

    resetblink(pattern);
    while (1)
        ;
}

/** Configures a GPIO pin to directly drive a LED (with 8mA output drive). */
void set_gpio_led(uint32_t port, uint32_t pin) {
    MAP_GPIOPinWrite(port, pin, 0xff);
    MAP_GPIOPinTypeGPIOOutput(port, pin); 
    MAP_GPIOPadConfigSet(port, pin, GPIO_STRENGTH_8MA_SC, GPIO_PIN_TYPE_STD); 
    MAP_GPIOPinWrite(port, pin, 0xff);
}


/** Configures a GPIO pin to directly drive a LED (with 2mA output drive). */
void set_gpio_drive_low(uint32_t port, uint32_t pin) {
    MAP_GPIOPinWrite(port, pin, 0);
    MAP_GPIOPinTypeGPIOOutput(port, pin); 
    MAP_GPIOPadConfigSet(port, pin, GPIO_STRENGTH_2MA, GPIO_PIN_TYPE_STD); 
    MAP_GPIOPinWrite(port, pin, 0);
}

/** Configures a GPIO pin to directly drive a LED (with 2mA output drive). */
void set_gpio_drive_high(uint32_t port, uint32_t pin) {
    MAP_GPIOPinWrite(port, pin, 0xff);
    MAP_GPIOPinTypeGPIOOutput(port, pin); 
    MAP_GPIOPadConfigSet(port, pin, GPIO_STRENGTH_2MA, GPIO_PIN_TYPE_STD); 
    MAP_GPIOPinWrite(port, pin, 0xff);
}

/** Configures a gpio pin for input with external pullup. */
void set_gpio_extinput(uint32_t port, uint32_t pin) {
    MAP_GPIOPinWrite(port, pin, 0);
    MAP_GPIOPinTypeGPIOInput(port, pin);
    MAP_GPIOPadConfigSet(port, pin, GPIO_STRENGTH_2MA, GPIO_PIN_TYPE_STD);
}

void enable_dcc() {
    auto port = GPIO_PORTA_BASE;
    auto pin = GPIO_PIN_2 | GPIO_PIN_3;
    MAP_GPIOPinTypeTimer(port, pin);
    MAP_GPIOPinConfigure(GPIO_PA2_T1CCP0);
    MAP_GPIOPinConfigure(GPIO_PA3_T1CCP1);
    //MAP_GPIOPadConfigSet(port, pin, GPIO_STRENGTH_2MA, GPIO_PIN_TYPE_STD);
}

void disable_dcc() {
    // Take A2/A3 and set them to drive high. This will turn off the gate
    // driver.
    set_gpio_drive_high(GPIO_PORTA_BASE, GPIO_PIN_2 | GPIO_PIN_3);
}

/** Initialize the processor hardware.
 */
void hw_preinit(void)
{
    /* Globally disables interrupts until the FreeRTOS scheduler is up. */
    asm("cpsid i\n");

    /* First we take care of the important pins that control the power
     * transistors */
    MAP_SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOA);
    MAP_SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOQ);

    // Q2/Q3 are wired together with A2-A3 on the timer output pins. Let's turn
    // them into high-impedance mode and turn off pullups.
    MAP_GPIOPinTypeGPIOInput(GPIO_PORTQ_BASE, GPIO_PIN_2 | GPIO_PIN_3); 
    // Disables any pullup/down.
    MAP_GPIOPadConfigSet(GPIO_PORTQ_BASE, GPIO_PIN_2 | GPIO_PIN_3,
                         GPIO_STRENGTH_2MA, GPIO_PIN_TYPE_STD); 

    disable_dcc();

    // A4 controls the accessory bus, active high.
    set_gpio_drive_low(GPIO_PORTA_BASE, GPIO_PIN_4);

    /* Setup the system clock. */
    MAP_SysCtlClockFreqSet(SYSCTL_XTAL_25MHZ | SYSCTL_USE_PLL |
                           SYSCTL_OSC_MAIN | SYSCTL_CFG_VCO_480,
                           120000000);

    /* Red LED pin initialization */
    MAP_SysCtlPeripheralEnable(SYSCTL_PERIPH_GPION);
    MAP_GPIOPinWrite(GPIO_PORTN_BASE, GPIO_PIN_1, GPIO_PIN_1);
    MAP_GPIOPinTypeGPIOOutput(GPIO_PORTN_BASE, GPIO_PIN_1);
    MAP_GPIOPinWrite(GPIO_PORTN_BASE, GPIO_PIN_1, GPIO_PIN_1);

    /* Blinker timer initialization. */
    MAP_SysCtlPeripheralEnable(SYSCTL_PERIPH_TIMER5);
    MAP_TimerConfigure(TIMER5_BASE, TIMER_CFG_PERIODIC);
    MAP_TimerLoadSet(TIMER5_BASE, TIMER_A, configCPU_CLOCK_HZ / 8);
    MAP_IntEnable(INT_TIMER5A);

    /* This interrupt should hit even during kernel operations. */
    MAP_IntPrioritySet(INT_TIMER5A, 0);
    MAP_TimerIntEnable(TIMER5_BASE, TIMER_TIMA_TIMEOUT);
    MAP_TimerEnable(TIMER5_BASE, TIMER_A);

    /* UART0 pin initialization */
    MAP_SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOD);
    MAP_GPIOPinConfigure(GPIO_PD4_U2RX);
    MAP_GPIOPinConfigure(GPIO_PD5_U2TX);
    MAP_GPIOPinTypeUART(GPIO_PORTD_BASE, GPIO_PIN_4 | GPIO_PIN_5);

    /* CAN pin initialization */
    MAP_SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOA);
    MAP_GPIOPinConfigure(GPIO_PA0_CAN0RX);
    MAP_GPIOPinConfigure(GPIO_PA1_CAN0TX);
    MAP_GPIOPinTypeCAN(GPIO_PORTA_BASE, GPIO_PIN_0 | GPIO_PIN_1);

    /* Boosterpack LEDs */
    MAP_SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOQ);
    MAP_SysCtlPeripheralEnable(SYSCTL_PERIPH_GPION);
    MAP_SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOP);
    MAP_SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOB);
    MAP_SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOF);
    set_gpio_led(GPIO_PORTN_BASE, GPIO_PIN_4);  // red
    set_gpio_led(GPIO_PORTQ_BASE, GPIO_PIN_0);  // yellow
    // hardware bug set_gpio_led(GPIO_PORTP_BASE, GPIO_PIN_4);  // green
    set_gpio_extinput(GPIO_PORTP_BASE, GPIO_PIN_4);  // green
    set_gpio_led(GPIO_PORTB_BASE, GPIO_PIN_4);  // blue

    set_gpio_led(GPIO_PORTN_BASE, GPIO_PIN_1);  // onboard 1
    set_gpio_led(GPIO_PORTN_BASE, GPIO_PIN_0);  // onboard 2
    set_gpio_led(GPIO_PORTF_BASE, GPIO_PIN_4);  // onboard 3
    set_gpio_led(GPIO_PORTF_BASE, GPIO_PIN_0);  // onboard 4

    /* Timer hardware for DCC signal generation */
    MAP_SysCtlPeripheralEnable(SYSCTL_PERIPH_TIMER1);
    MAP_SysCtlPeripheralEnable(SYSCTL_PERIPH_TIMER0);
}



}  // extern C
