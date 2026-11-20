/*
 * serial.h
 *
 * C interface to the Arduino SAMD core USB serial library. The core's
 * Serial object is C++ only, so serial.cpp wraps the calls main.c needs.
 */

#ifndef SERIAL_H
#define SERIAL_H

#ifdef __cplusplus
extern "C" {
#endif

void serial_init(void);               /* open the USB serial port            */
void serial_puts(const char *text);   /* print a line to the serial monitor  */

#ifdef __cplusplus
}
#endif

#endif /* SERIAL_H */
