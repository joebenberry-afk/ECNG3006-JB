/* main.c, ECNG3006 Lab 3 Question 2 - unit test for the NVM driver

     Name: Joseph Berry
     ID:   620163707

     Platform: Arduino MKR Zero (SAMD21), PlatformIO, CMSIS <sam.h>.
               The Arduino core is used ONLY inside serial.cpp as the
               USB serial library (115200, pio device monitor).

   TEST
     1. init_nvm, and argument checks that must be rejected
     2. boot counter in word 0: read, +1, write. Press RESET and it goes
        up by one, so the value survived losing power to the CPU
     3. print the block stored by the previous run (persistence again)
     4. TEST_CYCLES times, once a second:
          singlewrite / singleread a changing word, compare
          rewrite the same word: must skip the erase (fast)
          burstwrite / burstread (all, and limited to 5 bytes), compare
          is_done_nvm after every write
        Write time is measured: the CPU stalls while Flash is erased.
     5. print the totals every 5 s (writes stop to save Flash wear)
*/

#include <stdio.h>
#include <string.h>
#include "nvm.h"
#include "serial.h"

#define TEST_CYCLES  5u         /* ponytail: writes stop after this many
                                     cycles; each costs 2 erases of ~25k  */
#define BOOT_WORD    0u           /* word index of the boot counter        */
#define PARTIAL      5            /* bytes for the limited burst read      */

static unsigned long passed, failed;

/* check, count and print one test result
 */
static void check(bool ok, const char *what)
{
    char line[96];

    if (ok) {
        passed++;
    }
    else {
        failed++;
    }
    snprintf(line, sizeof line, "  %s  %s", ok ? "PASS" : "FAIL", what);
    serial_puts(line);
}

/* test_args, calls with bad arguments must be refused
 */
static void test_args(void)
{
    uint8_t buf[4] = { 0 };

    check(!singlewrite_nvm(NVM_WORD_COUNT, 1u), "singlewrite index out of range rejected");
    check(singleread_nvm(NVM_WORD_COUNT) == NVM_ERASED_WORD, "singleread index out of range -> erased word");
    check(!burstwrite_nvm(buf, 0), "burstwrite 0 bytes rejected");
    check(!burstwrite_nvm(buf, NVM_BLOCK_MAX + 1), "burstwrite too many bytes rejected");
    check(!burstwrite_nvm(NULL, 4), "burstwrite NULL rejected");
}

/* test_boot_count, increment the boot counter kept in Flash, return it
 */
static uint32_t test_boot_count(void)
{
    char line[96];
    uint32_t boots = singleread_nvm(BOOT_WORD);

    if (boots == NVM_ERASED_WORD) {
        boots = 0u;                               /* first run after upload */
    }
    boots++;
    check(singlewrite_nvm(BOOT_WORD, boots) && singleread_nvm(BOOT_WORD) == boots,
          "boot counter written");
    snprintf(line, sizeof line, "  boot count = %lu  (press RESET: it must go up by 1)",
             (unsigned long)boots);
    serial_puts(line);
    return boots;
}

/* show_previous_block, print the block left by the previous run
 */
static void show_previous_block(void)
{
    char text[NVM_BLOCK_MAX + 1];
    char line[NVM_BLOCK_MAX + 48];
    int n = burstread_nvm((uint8_t *)text, 0);

    text[n] = '\0';
    snprintf(line, sizeof line, "  block from previous run (%d bytes): %s",
             n, n ? text : "<none>");
    serial_puts(line);
}

/* test_cycle, one write/read round of every routine. The value holds the
 * boot count so every boot writes NEW data (a value left from the previous
 * boot would correctly skip the erase and spoil the timing check).
 */
static void test_cycle(uint32_t boot, uint32_t cycle)
{
    char line[96];
    char text[NVM_BLOCK_MAX + 1];
    char back[NVM_BLOCK_MAX + 1];
    uint32_t index = 1u + cycle % 8u;             /* spread over words 1..8 */
    uint32_t value = 0xC0DE0000u | ((boot & 0xFFu) << 8) | (cycle & 0xFFu);
    unsigned long t0, t_write, t_same;
    int len, n;
    bool ok;

    snprintf(line, sizeof line, "cycle %lu", (unsigned long)cycle);
    serial_puts(line);

    t0 = micros();
    ok = singlewrite_nvm(index, value);
    t_write = micros() - t0;
    check(ok && is_done_nvm(), "singlewrite (erase + write) and is_done");
    check(singleread_nvm(index) == value, "singleread returns value written");

    t0 = micros();
    ok = singlewrite_nvm(index, value);
    t_same = micros() - t0;
    check(ok && t_same < t_write, "same value again skips the erase");

    snprintf(line, sizeof line, "  word[%lu]=0x%08lX  write %lu us, unchanged %lu us",
             (unsigned long)index, (unsigned long)singleread_nvm(index),
             t_write, t_same);
    serial_puts(line);

    len = snprintf(text, sizeof text, "ECNG3006 620163707 block written in cycle %lu",
                   (unsigned long)cycle);
    check(burstwrite_nvm((const uint8_t *)text, len) && is_done_nvm(),
          "burstwrite and is_done");

    memset(back, 0, sizeof back);
    n = burstread_nvm((uint8_t *)back, 0);
    check(n == len && memcmp(back, text, (size_t)len) == 0, "burstread all matches");

    memset(back, 0, sizeof back);
    n = burstread_nvm((uint8_t *)back, PARTIAL);
    check(n == PARTIAL && memcmp(back, text, PARTIAL) == 0, "burstread limited to 5 bytes");
}

/* Main application entry point */
int main(void)
{
    char line[96];
    uint32_t boot, cycle;

    serial_init();
    delay(4000);                                  /* time to open monitor */

    serial_puts("=== ECNG3006 Lab 3 Q2 - NVM (internal Flash) driver test ===");
    serial_puts("Joseph Berry 620163707, Arduino MKR Zero (SAMD21G18A)");

    check(init_nvm(), "init_nvm");
    test_args();
    boot = test_boot_count();
    show_previous_block();

    for (cycle = 1u; cycle <= TEST_CYCLES; cycle++) {
        test_cycle(boot, cycle);
        delay(1000);
    }

    for (;;) {
        snprintf(line, sizeof line, "=== DONE: %lu passed, %lu failed (boot %lu) ===",
                 passed, failed, (unsigned long)singleread_nvm(BOOT_WORD));
        serial_puts(line);
        delay(5000);
    }
    return 0;
}
