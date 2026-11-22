/* nvm.h, ECNG3006 Lab 3 Question 2 - non-volatile storage driver

     Name: Joseph Berry
     ID:   620163707

     Platform:   Arduino MKR Zero (ATSAMD21G18A), PlatformIO, CMSIS <sam.h>
     Peripheral: NVM Controller (NVMCTRL) driving the on-chip Flash,
                 used as emulated EEPROM. No SD card or external chip.

   STORAGE AREA (reserved inside the application image, 256-byte aligned)
     Row 0  word area   NVM_WORD_COUNT 32-bit words, index 0..63,
                        used by singleread_nvm / singlewrite_nvm
     Row 1  block area  one header word (magic + length) followed by up
                        to NVM_BLOCK_MAX bytes, used by burstread_nvm /
                        burstwrite_nvm

   FLASH RULES THIS DRIVER HANDLES
     - A 1 bit can only be cleared by erasing a whole 256-byte row, so
       every write is read-modify-write: copy row to RAM, erase, write
       the 4 pages of 64 bytes back, verify.
     - ~25k erase cycles per row: unchanged data is never rewritten.
     - The CPU stalls for ~ms while a row is erased/written (no
       read-while-write section on the G18A), so writes disturb timing.
     - Not re-entrant: under FreeRTOS guard calls with one mutex.
     - Re-uploading the program re-flashes the area to its erased state.
*/

#ifndef NVM_H
#define NVM_H

#include <sam.h>        /* NVMCTRL, PM register definitions (CMSIS) */
#include <stdbool.h>
#include <stdint.h>

#define NVM_ROW_SIZE    256u                          /* erase unit (bytes) */
#define NVM_PAGE_SIZE   64u                           /* write unit (bytes) */
#define NVM_WORD_COUNT  (NVM_ROW_SIZE / 4u)           /* 64 words           */
#define NVM_BLOCK_MAX   (NVM_ROW_SIZE - 4u)           /* 252 bytes          */
#define NVM_ERASED_WORD 0xFFFFFFFFu                   /* blank Flash value  */

/* init_nvm, configure NVMCTRL (clock, wait states, manual page write,
 * cache), clear error flags and check the storage area is page-size
 * correct, aligned, outside the bootloader and not region-locked.
 * Returns true when the area is ready to use.
 */
bool init_nvm(void);

/* is_done_nvm, true when NVMCTRL has finished its last command
 * (erase / page write) and can accept a new one.
 */
bool is_done_nvm(void);

/* singleread_nvm, return word 'index' (0..NVM_WORD_COUNT-1) of the word
 * area. Blank words and an out-of-range index return NVM_ERASED_WORD.
 */
uint32_t singleread_nvm(uint32_t index);

/* singlewrite_nvm, store 'value' in word 'index' of the word area.
 * Skips the erase if the value is already stored. Returns true when the
 * value was written and verified.
 */
bool singlewrite_nvm(uint32_t index, uint32_t value);

/* burstread_nvm, copy the stored block into 'buffer'. 'maxbytes' limits
 * the copy, 0 = read all available. Returns the number of bytes copied
 * (0 if no valid block is stored).
 */
int burstread_nvm(uint8_t *buffer, int maxbytes);

/* burstwrite_nvm, store 'nbytes' (1..NVM_BLOCK_MAX) from 'data' as the
 * block, replacing the previous one. Returns true when written and
 * verified.
 */
bool burstwrite_nvm(const uint8_t *data, int nbytes);

#endif /* NVM_H */
