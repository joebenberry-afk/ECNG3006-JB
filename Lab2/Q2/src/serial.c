/*
 * serial.c, bare-metal UART on SERCOM5 (MKR Zero Serial1 pins)
 *
 *   TX = pin 14 = PB22 = SERCOM5 PAD[2]  (peripheral function D)
 *   RX = pin 13 = PB23 = SERCOM5 PAD[3]  (peripheral function D)
 *   115200 baud, 8N1, polled transmit, GCLK0 = 48 MHz (set by startup)
 *
 * Connect a 3.3 V USB-TTL adapter: pin 14 -> adapter RX, GND -> GND.
 */

#include <sam.h>
#include "serial.h"

#define UART      SERCOM5->USART
#define F_GCLK    48000000UL
#define BAUDRATE  115200UL

/* BAUD = 65536 * (1 - 16 * baud / f)  (arithmetic mode, 16x sampling) */
#define BAUDREG   ((uint16_t)(65536ULL - (65536ULL * 16 * BAUDRATE) / F_GCLK))

void serial_init(void)
{
    PM->APBCMASK.reg |= PM_APBCMASK_SERCOM5;          /* bus clock on      */

    GCLK->CLKCTRL.reg = GCLK_CLKCTRL_ID(SERCOM5_GCLK_ID_CORE) |
                        GCLK_CLKCTRL_GEN_GCLK0 | GCLK_CLKCTRL_CLKEN;
    while (GCLK->STATUS.bit.SYNCBUSY) { }             /* core clock 48 MHz */

    PORT->Group[1].PINCFG[22].reg = PORT_PINCFG_PMUXEN;   /* PB22 TX      */
    PORT->Group[1].PINCFG[23].reg = PORT_PINCFG_PMUXEN | PORT_PINCFG_INEN;
    PORT->Group[1].PMUX[22 >> 1].reg = PORT_PMUX_PMUXE_D | PORT_PMUX_PMUXO_D;

    UART.CTRLA.reg = SERCOM_USART_CTRLA_SWRST;        /* reset SERCOM5     */
    while (UART.CTRLA.bit.SWRST || UART.SYNCBUSY.bit.SWRST) { }

    UART.CTRLA.reg = SERCOM_USART_CTRLA_MODE(1) |     /* internal clock    */
                     SERCOM_USART_CTRLA_DORD    |     /* LSB first         */
                     SERCOM_USART_CTRLA_TXPO(1) |     /* TX on PAD[2]      */
                     SERCOM_USART_CTRLA_RXPO(3);      /* RX on PAD[3]      */
    UART.CTRLB.reg = SERCOM_USART_CTRLB_TXEN | SERCOM_USART_CTRLB_RXEN; /* 8N1 */
    while (UART.SYNCBUSY.bit.CTRLB) { }
    UART.BAUD.reg = BAUDREG;

    UART.CTRLA.reg |= SERCOM_USART_CTRLA_ENABLE;
    while (UART.SYNCBUSY.bit.ENABLE) { }
}

void serial_putc(char c)
{
    while (!UART.INTFLAG.bit.DRE) { }                 /* data reg empty    */
    UART.DATA.reg = (uint8_t)c;
}

void serial_puts(const char *text)
{
    while (*text) serial_putc(*text++);
    serial_putc('\r');
    serial_putc('\n');
}
