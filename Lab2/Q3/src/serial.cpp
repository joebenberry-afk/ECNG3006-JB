/*
 * serial.cpp
 *
 * Thin C wrapper around the Arduino SAMD core serial library
 * (same approach as serial_rx.cpp in Lab 1 Question 7).
 */

#include <Arduino.h>
#include "serial.h"

extern "C" void __libc_init_array(void);

/*
 * main.c replaces the Arduino main(), so this does the start-up the USB
 * serial library needs that Arduino's main() would normally do.
 */
void serial_init(void)
{
    init();                 /* core clocks and timer used by the USB stack */
    __libc_init_array();    /* construct the C++ Serial/USB objects        */
    USBDevice.init();
    USBDevice.attach();

    /* No while(!Serial) wait: on the native-USB MKR Zero that would
       hang forever if no serial monitor is attached. */
    Serial.begin(115200);
    delay(1000);            /* FreeRTOS_SAMD21 note: let the USB driver
                               start before the scheduler runs            */
}

void serial_puts(const char *text)
{
    Serial.println(text);
}
