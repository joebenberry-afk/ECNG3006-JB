/*
 * serial.h, bare-metal UART (SERCOM5, MKR Zero pins 13 RX / 14 TX)
 */

#ifndef SERIAL_H
#define SERIAL_H

void serial_init(void);               /* SERCOM5 UART, 115200 8N1            */
void serial_putc(char c);             /* send one character (polled)         */
void serial_puts(const char *text);   /* send a line, ends with CR LF        */

#endif /* SERIAL_H */
