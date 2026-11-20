/* main.c, ECNG3006 Lab 2 Q3 - relative task performance of the Q2 app
   under four scheduler configurations

     Platform: Arduino MKR Zero (SAMD21), PlatformIO,
               FreeRTOS (briscoetech/FreeRTOS_SAMD21), CMSIS <sam.h>.
               Configuration: include/FreeRTOSConfig.h (project copy).
               Serial: USB serial (serial.cpp wraps the Arduino core's
               Serial object; nothing in this file calls Arduino).

   TASKS (same as Q2; the LED pin PB08 is guarded by gpio_lock)
     Task 1 ON     - take lock, LED on, ACTIVELY wait 0.5 s, give, yield
     Task 2 OFF    - take lock, LED off, give, task-delay 1 s
     Task 3 STATUS - take lock, read LED, give, print status, delay 1 s
     MONITOR       - instrumentation only, highest priority, never touches
                     the LED: prints every task's counters once a second
                     so there is evidence even if Tasks 2/3 never run

   BUILD CASES (platformio.ini sets the macros)
     same    T1=T2=T3=1,     co-operative (run to completion), mutex
     case_a  T1=3 T2=2 T3=1, co-operative (run to completion), mutex
     case_b  T1=3 T2=2 T3=1, preemptive, binary semaphore (no inheritance)
     case_c  T1=3 T2=2 T3=1, preemptive, mutex (priority inheritance)

   MONITOR LINE FIELDS
     runs   - how many times each task has completed its loop body
     gap    - worst time (ms) between two runs of a task; ideal is
              T1 ~500, T2 1000, T3 1000. Larger = the task was kept waiting
     wait   - worst time (ms) a task blocked on the lock
*/

#include <sam.h>
#include <stdio.h>
#include <FreeRTOS.h>
#include <task.h>
#include <semphr.h>
#include "serial.h"

#ifndef USE_MUTEX
#define USE_MUTEX 1
#endif

#ifndef CASE_NAME
#define CASE_NAME "case_c"
#endif

/* task priorities, one const int each (idle task is priority 0) */
#ifdef SAME_PRIO
const int PRIO_T1 = 1;
const int PRIO_T2 = 1;
const int PRIO_T3 = 1;
#else
const int PRIO_T1 = 3;
const int PRIO_T2 = 2;
const int PRIO_T3 = 1;
#endif
const int PRIO_MON = 4;

#if configUSE_PREEMPTION
#define SCHED "preemptive"
#else
#define SCHED "run-to-completion"
#endif

#if USE_MUTEX
#define LOCKNAME "mutex"
#else
#define LOCKNAME "binary-sem"
#endif

#define LEDPORT 1         // on-board LED is PB08: port group B,

#define LEDMASK PORT_PB08 // pin 8

#define T1      0         // task numbers

#define T2      1

#define T3      2

static SemaphoreHandle_t gpio_lock;

static volatile unsigned long runs[3], gap[3], wait[3];

static TickType_t last[3];

/* print, send one whole line; scheduler paused so lines never interleave
   (interrupts stay on, so the USB driver keeps working)
*/
static void print(const char *line) {

vTaskSuspendAll();
serial_puts(line);
xTaskResumeAll();
}

/* lock_take, take the GPIO lock and record the worst time it took
*/
static void lock_take(unsigned int me) {

TickType_t asked = xTaskGetTickCount(), waited;

xSemaphoreTake(gpio_lock, portMAX_DELAY);
waited = xTaskGetTickCount() - asked;

if (waited > wait[me]) wait[me] = waited;
}

/* ran, count one pass of a task's loop and record the worst gap
*/
static void ran(unsigned int me) {

TickType_t now = xTaskGetTickCount();

if (runs[me] && (now - last[me]) > gap[me]) gap[me] = now - last[me];
last[me] = now;
runs[me]++;
}

/* active_wait, burn CPU for ms without blocking (task stays Running)
*/
static void active_wait(unsigned int ms) {

TickType_t start = xTaskGetTickCount();

while ((xTaskGetTickCount() - start) < pdMS_TO_TICKS(ms)) { }
}

/* task1_on, LED on, actively wait 0.5 s, then yield
*/
static void task1_on(void *params) {

(void)params;

for (;;) {

      lock_take(T1);
      PORT->Group[LEDPORT].OUTSET.reg = LEDMASK;
      active_wait(500);
      xSemaphoreGive(gpio_lock);
      ran(T1);
      taskYIELD();
}
}

/* task2_off, LED off, then task-delay 1 s
*/
static void task2_off(void *params) {

(void)params;

for (;;) {

      lock_take(T2);
      PORT->Group[LEDPORT].OUTCLR.reg = LEDMASK;
      xSemaphoreGive(gpio_lock);
      ran(T2);
      vTaskDelay(pdMS_TO_TICKS(1000));
}
}

/* task3_status, read the LED under the lock, print a status line
*/
static void task3_status(void *params) {

char line[64];
unsigned int led;

(void)params;

for (;;) {

      lock_take(T3);
      led = (PORT->Group[LEDPORT].OUT.reg & LEDMASK) ? 1 : 0;
      xSemaphoreGive(gpio_lock);
      snprintf(line, sizeof line, "  T3 status: t=%lu ms LED=%s",
               (unsigned long)xTaskGetTickCount() * portTICK_PERIOD_MS,
               led ? "ON" : "OFF");
      print(line);
      ran(T3);
      vTaskDelay(pdMS_TO_TICKS(1000));
}
}

/* monitor, print every task's counters once a second (instrumentation)
*/
static void monitor(void *params) {

char line[200];

(void)params;

for (;;) {

      snprintf(line, sizeof line,
               "[%s %s %s prio %d/%d/%d] t=%lu ms | runs T1=%lu T2=%lu T3=%lu"
               " | gap ms T1=%lu T2=%lu T3=%lu | wait ms T1=%lu T2=%lu T3=%lu",
               CASE_NAME, SCHED, LOCKNAME, PRIO_T1, PRIO_T2, PRIO_T3,
               (unsigned long)xTaskGetTickCount() * portTICK_PERIOD_MS,
               runs[T1], runs[T2], runs[T3],
               gap[T1] * portTICK_PERIOD_MS, gap[T2] * portTICK_PERIOD_MS,
               gap[T3] * portTICK_PERIOD_MS, wait[T1] * portTICK_PERIOD_MS,
               wait[T2] * portTICK_PERIOD_MS, wait[T3] * portTICK_PERIOD_MS);
      print(line);
      vTaskDelay(pdMS_TO_TICKS(1000));
}
}

/* Main application entry point */
int main(void) {

serial_init();                                   // USB serial first,
                                                 // it resets pins
PORT->Group[LEDPORT].DIRSET.reg = LEDMASK;       // LED pin as output
PORT->Group[LEDPORT].OUTCLR.reg = LEDMASK;

#if USE_MUTEX
gpio_lock = xSemaphoreCreateMutex();             // priority inheritance
#else
gpio_lock = xSemaphoreCreateBinary();            // no inheritance,
xSemaphoreGive(gpio_lock);                       // starts empty
#endif

xTaskCreate(task1_on, "T1_ON", 256, NULL, PRIO_T1, NULL);
xTaskCreate(task2_off, "T2_OFF", 256, NULL, PRIO_T2, NULL);
xTaskCreate(task3_status, "T3_STATUS", 384, NULL, PRIO_T3, NULL);
xTaskCreate(monitor, "MONITOR", 512, NULL, PRIO_MON, NULL);
vTaskStartScheduler();

while (1) {
      // Will never get executed
}
return 0;
}
