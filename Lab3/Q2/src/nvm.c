/* nvm.c, ECNG3006 Lab 3 Question 2 - non-volatile storage driver

     Name: Joseph Berry
     ID:   620163707

     SAMD21 internal Flash used as emulated EEPROM through the NVM
     Controller (NVMCTRL). See nvm.h for the storage layout and rules.

   NVMCTRL SEQUENCE (datasheet, NVMCTRL chapter)
     erase row   ADDR = byte address / 2, CTRLA = KEY | ER, wait READY
     write page  CTRLA = KEY | PBC (clear page buffer), write the page
                 with 32-bit stores to its Flash address (fills the page
                 buffer), ADDR = page address / 2, CTRLA = KEY | WP,
                 wait READY
     After each command STATUS.PROGE / LOCKE / NVME report errors.
*/

#include <stddef.h>
#include "nvm.h"

#define AREA_BYTES     (2u * NVM_ROW_SIZE)        /* word row + block row  */
#define BOOTLOADER_END 0x2000u                    /* first 8 KB: bootloader */
#define LOCK_REGIONS   16u                        /* NVMCTRL lock regions  */
#define READY_TIMEOUT  1000000u                   /* poll limit            */
#define BLOCK_MAGIC    0xB10C0000u                /* header high half      */
#define BLOCK_LENMASK  0x0000FFFFu                /* header low half       */
#define STATUS_ERRORS  (NVMCTRL_STATUS_PROGE | NVMCTRL_STATUS_LOCKE | \
                        NVMCTRL_STATUS_NVME)

/* The storage area: a const array the linker places in Flash (.rodata),
 * initialised to the erased state. It must NOT be declared volatile:
 * GCC then puts it in RAM (.data). Every access instead goes through the
 * volatile pointers below so reads really go to Flash.
 */
__attribute__((aligned(NVM_ROW_SIZE), used))
static const uint8_t nvm_area[AREA_BYTES] = {
    [0 ... AREA_BYTES - 1] = 0xFF
};

#define WORD_ROW  ((const volatile uint32_t *)(uintptr_t)&nvm_area[0])
#define BLOCK_ROW ((const volatile uint32_t *)(uintptr_t)&nvm_area[NVM_ROW_SIZE])

static uint32_t rowbuf[NVM_WORD_COUNT];           /* RAM copy of one row   */

/* wait_ready, poll INTFLAG.READY; false if NVMCTRL never becomes ready
 */
static bool wait_ready(void)
{
    uint32_t spin = READY_TIMEOUT;

    while (!(NVMCTRL->INTFLAG.reg & NVMCTRL_INTFLAG_READY)) {
        if (--spin == 0u) {
            return false;
        }
    }
    return true;
}

/* run_cmd, execute one NVMCTRL command on the Flash byte address 'addr'
 * and report whether it completed without a programming/lock error
 */
static bool run_cmd(uint16_t cmd, uint32_t addr)
{
    if (!wait_ready()) {
        return false;
    }

    NVMCTRL->STATUS.reg = STATUS_ERRORS;          /* write 1 to clear      */
    NVMCTRL->ADDR.reg = addr / 2u;                /* 16-bit word address   */
    NVMCTRL->CTRLA.reg = NVMCTRL_CTRLA_CMDEX_KEY | cmd;

    if (!wait_ready()) {
        return false;
    }
    return (NVMCTRL->STATUS.reg & STATUS_ERRORS) == 0u;
}

/* row_matches, true if the Flash row already holds 'src'
 */
static bool row_matches(const volatile uint32_t *row, const uint32_t *src)
{
    uint32_t i;

    for (i = 0; i < NVM_WORD_COUNT; i++) {
        if (row[i] != src[i]) {
            return false;
        }
    }
    return true;
}

/* write_row, replace one Flash row with 'src': erase, program its pages,
 * verify. Skips everything if the row is unchanged (saves wear).
 */
static bool write_row(const volatile uint32_t *row, const uint32_t *src)
{
    const uint32_t words_per_page = NVM_PAGE_SIZE / 4u;
    uint32_t addr = (uint32_t)row;
    uint32_t ctrlb = NVMCTRL->CTRLB.reg;
    volatile uint32_t *page;
    uint32_t p, i;
    bool ok;

    if (row_matches(row, src)) {
        return true;
    }

    NVMCTRL->CTRLB.reg = ctrlb | NVMCTRL_CTRLB_CACHEDIS;  /* no stale cache
                                                              while writing */
    ok = run_cmd(NVMCTRL_CTRLA_CMD_ER, addr);

    for (p = 0; ok && p < NVM_ROW_SIZE / NVM_PAGE_SIZE; p++) {
        ok = run_cmd(NVMCTRL_CTRLA_CMD_PBC, addr);
        page = (volatile uint32_t *)(addr + p * NVM_PAGE_SIZE);

        for (i = 0; ok && i < words_per_page; i++) {
            page[i] = src[p * words_per_page + i];        /* -> page buffer */
        }
        if (ok) {
            ok = run_cmd(NVMCTRL_CTRLA_CMD_WP, (uint32_t)page);
        }
    }

    (void)run_cmd(NVMCTRL_CTRLA_CMD_INVALL, addr);        /* drop old lines */
    NVMCTRL->CTRLB.reg = ctrlb;

    return ok && row_matches(row, src);
}

/* copy_row, Flash row -> RAM buffer
 */
static void copy_row(uint32_t *dst, const volatile uint32_t *row)
{
    uint32_t i;

    for (i = 0; i < NVM_WORD_COUNT; i++) {
        dst[i] = row[i];
    }
}

bool init_nvm(void)
{
    const uint32_t region_size = FLASH_SIZE / LOCK_REGIONS;
    uint32_t first = (uint32_t)nvm_area;
    uint32_t last = first + AREA_BYTES - 1u;

    PM->APBBMASK.reg |= PM_APBBMASK_NVMCTRL;              /* bus clock on */

    /* never presume reset defaults: 1 wait state (48 MHz at 3.3 V),
       manual page write (WP command), wake on access, cache enabled */
    NVMCTRL->CTRLB.reg = NVMCTRL_CTRLB_RWS(1) |
                         NVMCTRL_CTRLB_MANW |
                         NVMCTRL_CTRLB_SLEEPPRM_WAKEONACCESS |
                         NVMCTRL_CTRLB_READMODE_NO_MISS_PENALTY;

    if (!wait_ready()) {
        return false;
    }
    NVMCTRL->STATUS.reg = STATUS_ERRORS;

    if ((8u << NVMCTRL->PARAM.bit.PSZ) != NVM_PAGE_SIZE) {
        return false;                                     /* wrong device */
    }
    if ((first % NVM_ROW_SIZE) != 0u || first < BOOTLOADER_END ||
        last >= FLASH_SIZE) {
        return false;                                     /* bad placement */
    }
    if (!(NVMCTRL->LOCK.reg & (1u << (first / region_size))) ||
        !(NVMCTRL->LOCK.reg & (1u << (last / region_size)))) {
        return false;                                     /* 0 = locked   */
    }
    return true;
}

bool is_done_nvm(void)
{
    return (NVMCTRL->INTFLAG.reg & NVMCTRL_INTFLAG_READY) != 0u;
}

uint32_t singleread_nvm(uint32_t index)
{
    if (index >= NVM_WORD_COUNT) {
        return NVM_ERASED_WORD;
    }
    return WORD_ROW[index];
}

bool singlewrite_nvm(uint32_t index, uint32_t value)
{
    if (index >= NVM_WORD_COUNT) {
        return false;
    }

    copy_row(rowbuf, WORD_ROW);
    rowbuf[index] = value;
    return write_row(WORD_ROW, rowbuf);
}

int burstread_nvm(uint8_t *buffer, int maxbytes)
{
    const volatile uint8_t *data = (const volatile uint8_t *)&BLOCK_ROW[1];
    uint32_t header = BLOCK_ROW[0];
    int count, i;

    if (buffer == NULL || maxbytes < 0) {
        return 0;
    }
    if ((header & ~BLOCK_LENMASK) != BLOCK_MAGIC) {
        return 0;                                         /* nothing stored */
    }

    count = (int)(header & BLOCK_LENMASK);
    if (count > (int)NVM_BLOCK_MAX) {
        return 0;                                         /* corrupt header */
    }
    if (maxbytes != 0 && maxbytes < count) {
        count = maxbytes;
    }

    for (i = 0; i < count; i++) {
        buffer[i] = data[i];
    }
    return count;
}

bool burstwrite_nvm(const uint8_t *data, int nbytes)
{
    uint8_t *bytes = (uint8_t *)&rowbuf[1];
    int i;

    if (data == NULL || nbytes < 1 || nbytes > (int)NVM_BLOCK_MAX) {
        return false;
    }

    for (i = 0; i < (int)NVM_WORD_COUNT; i++) {
        rowbuf[i] = NVM_ERASED_WORD;
    }
    rowbuf[0] = BLOCK_MAGIC | (uint32_t)nbytes;
    for (i = 0; i < nbytes; i++) {
        bytes[i] = data[i];
    }
    return write_row(BLOCK_ROW, rowbuf);
}
