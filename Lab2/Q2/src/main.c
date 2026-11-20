/* main.c, ECNG3006 Lab 2 Q2 - three FreeRTOS tasks and one mutex

     Platform: Arduino MKR Zero (SAMD21), PlatformIO,
               FreeRTOS (briscoetech/FreeRTOS_SAMD21), CMSIS <sam.h>.
               Configuration: include/FreeRTOSConfig.h (project copy).
               Serial: SERCOM5 UART (serial.c), pin 14 TX, 115200 8N1.

   TASKS (the on-board LED pin PB08 is shared, guarded by led_mutex)
     ON     - take mutex, LED on, ACTIVELY wait 0.5 s, give mutex, yield
     OFF    - take mutex, LED off, give mutex, task-delay 1 s
     STATUS - take mutex, read LED pin, give mutex, print status over
              the UART, task-delay 1 s

   PRIORITIES (one const int each, idle task = 0)
     ON is the lowest because it never blocks: it busy-waits and
     taskYIELD() only hands the CPU to tasks of equal or higher priority.
     If ON were higher than OFF/STATUS they would never run (starvation).
     OFF and STATUS sleep in vTaskDelay(), so when their 1 s expires they
     preempt ON. If ON is holding the mutex at that moment, the mutex
     raises ON's priority (priority inheritance) until it gives it back.
*/

#include <sam.h>
#include <stdio.h>
#include <FreeRTOS.h>
#include <task.h>
#include <semphr.h>
#include "serial.h"

const int PRIO_ON = 1;
const int PRIO_STATUS = 2;
const int PRIO_OFF = 3;

#define LEDPORT 1         // on-board LED is PB08: port group B,

#define LEDMASK PORT_PB08 // pin 8

static SemaphoreHandle_t led_mutex;

static volatile unsigned long on_count, off_count;

/* active_wait, burn CPU for ms without blocking (task stays Running)
*/
static void active_wait(unsigned int ms) {

TickType_t start = xTaskGetTickCount();

while ((xTaskGetTickCount() - start) < pdMS_TO_TICKS(ms)) { }
}

/* task_on, LED on, actively wait 0.5 s, then yield
*/
static void task_on(void *params) {

(void)params;

for (;;) {

      xSemaphoreTake(led_mutex, portMAX_DELAY);
      PORT->Group[LEDPORT].OUTSET.reg = LEDMASK;
      on_count++;
      active_wait(500);
      xSemaphoreGive(led_mutex);
      taskYIELD();
}
}

/* task_off, LED off, then task-delay 1 s
*/
static void task_off(void *params) {

(void)params;

for (;;) {

      xSemaphoreTake(led_mutex, portMAX_DELAY);
      PORT->Group[LEDPORT].OUTCLR.reg = LEDMASK;
      off_count++;
      xSemaphoreGive(led_mutex);
      vTaskDelay(pdMS_TO_TICKS(1000));
}
}

/* task_status, read the LED pin under the mutex, print a status line
*/
static void task_status(void *params) {

char line[96];
unsigned int led;

(void)params;

for (;;) {

      xSemaphoreTake(led_mutex, portMAX_DELAY);
      led = (PORT->Group[LEDPORT].OUT.reg & LEDMASK) ? 1 : 0;
      xSemaphoreGive(led_mutex);                 // print outside the lock
      snprintf(line, sizeof line, "t=%lu ms LED=%s on_count=%lu off_count=%lu",
               (unsigned long)xTaskGetTickCount() * portTICK_PERIOD_MS,
               led ? "ON" : "OFF", on_count, off_count);
      serial_puts(line);
      vTaskDelay(pdMS_TO_TICKS(1000));
}
}

/* Main application entry point */
int main(void) {

serial_init();                                   // SERCOM5 UART
PORT->Group[LEDPORT].DIRSET.reg = LEDMASK;       // LED pin as output
PORT->Group[LEDPORT].OUTCLR.reg = LEDMASK;

led_mutex = xSemaphoreCreateMutex();

xTaskCreate(task_on, "ON", 256, NULL, PRIO_ON, NULL);
xTaskCreate(task_off, "OFF", 256, NULL, PRIO_OFF, NULL);
xTaskCreate(task_status, "STATUS", 512, NULL, PRIO_STATUS, NULL);
vTaskStartScheduler();

while (1) {
      // Will never get executed
}
return 0;
}
