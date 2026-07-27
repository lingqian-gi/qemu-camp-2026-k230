/*
 * QTest testcase for K230 SD/MMC Host Controller (DWC MSHC)
 *
 * Copyright (c) 2026 The QEMU K230 Camp Contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * K230 Technical Reference Manual V0.3.1 (2024-11-18), Chapter 12.4:
 * https://github.com/revyos/external-docs/blob/master/K230/en-us/K230_Technical_Reference_Manual_V0.3.1_20241118.pdf
 *
 * Tests the register-level MMIO behaviour of the two SDHCI instances
 * (SDHCI0 / SDHCI1).  Actual SD card command/data transfer and DMA
 * are not covered.
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "hw/sd/k230_sdhci.h"

#define SDHCI0_BASE  0x91580000
#define SDHCI1_BASE  0x91581000

/* ------------------------------------------------------------------ */
/*  1. Capabilities registers (read-only constants)                    */
/* ------------------------------------------------------------------ */

static void test_capabilities(void)
{
    QTestState *qts = qtest_init("-machine k230");

    uint32_t cap_lo = qtest_readl(qts, SDHCI0_BASE + K230_SDHCI_CAPAB);
    g_assert_cmphex(cap_lo, ==, K230_SDHCI_CAPAB_LO);

    uint32_t cap_hi = qtest_readl(qts, SDHCI0_BASE + K230_SDHCI_CAPAB + 4);
    g_assert_cmphex(cap_hi, ==, K230_SDHCI_CAPAB_HI);

    qtest_quit(qts);
}

/* ------------------------------------------------------------------ */
/*  2. Present State (read-only, returns card-inserted status)         */
/* ------------------------------------------------------------------ */

static void test_present_state(void)
{
    QTestState *qts = qtest_init("-machine k230");

    uint32_t pstate = qtest_readl(qts, SDHCI0_BASE + K230_SDHCI_PSTATE);
    g_assert_cmphex(pstate, ==, K230_SDHCI_PSTATE_DEFAULT);

    /* Verify individual bits */
    g_assert_true(pstate & K230_SDHCI_PSTATE_CARD_INSERTED);
    g_assert_true(pstate & K230_SDHCI_PSTATE_CARD_STABLE);
    g_assert_false(pstate & K230_SDHCI_PSTATE_CMD_INHIBIT);
    g_assert_false(pstate & K230_SDHCI_PSTATE_DAT_INHIBIT);

    qtest_quit(qts);
}

/* ------------------------------------------------------------------ */
/*  3. Slot Interrupt Status + Version (read-only)                     */
/* ------------------------------------------------------------------ */

static void test_slot_int_status(void)
{
    QTestState *qts = qtest_init("-machine k230");

    uint32_t val = qtest_readl(qts, SDHCI0_BASE + K230_SDHCI_SLOT_INT_STATUS);
    g_assert_cmphex(val, ==, K230_SDHCI_SLOT_INT_STATUS_VAL);
    /* Version in bits [31:16] */
    g_assert_cmphex((val >> 16) & 0xffff, ==, K230_SDHCI_SDHCI_VERSION);

    qtest_quit(qts);
}

/* ------------------------------------------------------------------ */
/*  4. Max Current register (read-only, zero)                          */
/* ------------------------------------------------------------------ */

static void test_max_current_zero(void)
{
    QTestState *qts = qtest_init("-machine k230");

    uint32_t cur_lo = qtest_readl(qts, SDHCI0_BASE + K230_SDHCI_MAX_CURR);
    g_assert_cmphex(cur_lo, ==, 0);

    uint32_t cur_hi = qtest_readl(qts, SDHCI0_BASE + K230_SDHCI_MAX_CURR + 4);
    g_assert_cmphex(cur_hi, ==, 0);

    qtest_quit(qts);
}

/* ------------------------------------------------------------------ */
/*  5. RO registers ignore writes                                      */
/* ------------------------------------------------------------------ */

static void test_ro_registers_ignore_writes(void)
{
    QTestState *qts = qtest_init("-machine k230");

    /* Attempt to write to read-only registers */
    qtest_writel(qts, SDHCI0_BASE + K230_SDHCI_CAPAB, 0xDEADBEEF);
    qtest_writel(qts, SDHCI0_BASE + K230_SDHCI_CAPAB + 4, 0xCAFEBABE);
    qtest_writel(qts, SDHCI0_BASE + K230_SDHCI_PSTATE, 0xFFFFFFFF);
    qtest_writel(qts, SDHCI0_BASE + K230_SDHCI_SLOT_INT_STATUS, 0x12345678);

    /* Values must be unchanged */
    uint32_t cap_lo = qtest_readl(qts, SDHCI0_BASE + K230_SDHCI_CAPAB);
    g_assert_cmphex(cap_lo, ==, K230_SDHCI_CAPAB_LO);

    uint32_t cap_hi = qtest_readl(qts, SDHCI0_BASE + K230_SDHCI_CAPAB + 4);
    g_assert_cmphex(cap_hi, ==, K230_SDHCI_CAPAB_HI);

    uint32_t pstate = qtest_readl(qts, SDHCI0_BASE + K230_SDHCI_PSTATE);
    g_assert_cmphex(pstate, ==, K230_SDHCI_PSTATE_DEFAULT);

    qtest_quit(qts);
}

/* ------------------------------------------------------------------ */
/*  6. RW register consistency (SDMASA, Argument)                      */
/* ------------------------------------------------------------------ */

static void test_rw_register_consistency(void)
{
    QTestState *qts = qtest_init("-machine k230");

    /* SDMA System Address (0x00) */
    qtest_writel(qts, SDHCI0_BASE + K230_SDHCI_SDMASA, 0x12345678);
    uint32_t sdmasa = qtest_readl(qts, SDHCI0_BASE + K230_SDHCI_SDMASA);
    g_assert_cmphex(sdmasa, ==, 0x12345678);

    /* Argument (0x08) */
    qtest_writel(qts, SDHCI0_BASE + K230_SDHCI_ARGUMENT, 0x9ABCDEF0);
    uint32_t arg = qtest_readl(qts, SDHCI0_BASE + K230_SDHCI_ARGUMENT);
    g_assert_cmphex(arg, ==, 0x9ABCDEF0);

    /* Block Size (0x04) — 16-bit, low 16 bits only */
    qtest_writel(qts, SDHCI0_BASE + K230_SDHCI_BLOCKSIZE, 0x80000200);
    uint32_t blksize = qtest_readl(qts, SDHCI0_BASE + K230_SDHCI_BLOCKSIZE);
    g_assert_cmphex(blksize & 0xffff, ==, 0x0200);

    /* Block Count (0x06) — 16-bit */
    qtest_writel(qts, SDHCI0_BASE + K230_SDHCI_BLOCKCOUNT, 0xFFFF0010);
    uint32_t blkcnt = qtest_readl(qts, SDHCI0_BASE + K230_SDHCI_BLOCKCOUNT);
    g_assert_cmphex(blkcnt & 0xffff, ==, 0x0010);

    qtest_quit(qts);
}

/* ------------------------------------------------------------------ */
/*  7. Interrupt status W1C (write-1-to-clear) behaviour               */
/* ------------------------------------------------------------------ */

static void test_interrupt_w1c(void)
{
    QTestState *qts = qtest_init("-machine k230");

    /* Initially 0 */
    uint32_t nor = qtest_readl(qts, SDHCI0_BASE
                                     + K230_SDHCI_NORMAL_INT_STAT);
    g_assert_cmphex(nor, ==, 0);

    /* W1C: write 0x0001 → clear bit 0 (was already 0, stays 0) */
    qtest_writel(qts, SDHCI0_BASE + K230_SDHCI_NORMAL_INT_STAT, 0x0001);
    nor = qtest_readl(qts, SDHCI0_BASE + K230_SDHCI_NORMAL_INT_STAT);
    g_assert_cmphex(nor, ==, 0);

    /* Write 0x0000 → no change */
    qtest_writel(qts, SDHCI0_BASE + K230_SDHCI_NORMAL_INT_STAT, 0x0000);
    nor = qtest_readl(qts, SDHCI0_BASE + K230_SDHCI_NORMAL_INT_STAT);
    g_assert_cmphex(nor, ==, 0);

    /* ERROR_INT_STAT same behaviour */
    uint32_t err = qtest_readl(qts, SDHCI0_BASE + K230_SDHCI_ERROR_INT_STAT);
    g_assert_cmphex(err, ==, 0);

    qtest_writew(qts, SDHCI0_BASE + K230_SDHCI_ERROR_INT_STAT, 0x0001);
    err = qtest_readl(qts, SDHCI0_BASE + K230_SDHCI_ERROR_INT_STAT);
    g_assert_cmphex(err, ==, 0);

    qtest_quit(qts);
}

/* ------------------------------------------------------------------ */
/*  8. Host Control and Clock Control register consistency             */
/* ------------------------------------------------------------------ */

static void test_control_registers(void)
{
    QTestState *qts = qtest_init("-machine k230");

    /* Host Control 1 (8-bit at 0x28) */
    qtest_writel(qts, SDHCI0_BASE + K230_SDHCI_HOST_CTRL1, 0x00000007);
    uint32_t hc1 = qtest_readl(qts, SDHCI0_BASE + K230_SDHCI_HOST_CTRL1);
    g_assert_cmphex(hc1, ==, 0x07);

    /* Clock Control (16-bit at 0x2C) */
    qtest_writel(qts, SDHCI0_BASE + K230_SDHCI_CLK_CTRL, 0x00000102);
    uint32_t clk = qtest_readl(qts, SDHCI0_BASE + K230_SDHCI_CLK_CTRL);
    g_assert_cmphex(clk & 0xffff, ==, 0x0102);

    /* Power Control (8-bit at 0x29) */
    qtest_writel(qts, SDHCI0_BASE + K230_SDHCI_PWR_CTRL, 0x0000000F);
    uint32_t pwr = qtest_readl(qts, SDHCI0_BASE + K230_SDHCI_PWR_CTRL);
    g_assert_cmphex(pwr, ==, 0x0F);

    /* Timeout Control (8-bit at 0x2E) */
    qtest_writel(qts, SDHCI0_BASE + K230_SDHCI_TOUT_CTRL, 0x0000000E);
    uint32_t tout = qtest_readl(qts, SDHCI0_BASE + K230_SDHCI_TOUT_CTRL);
    g_assert_cmphex(tout, ==, 0x0E);

    qtest_quit(qts);
}

/* ------------------------------------------------------------------ */
/*  9. Both instances (SDHCI0 / SDHCI1) mapped independently           */
/* ------------------------------------------------------------------ */

static void test_both_instances(void)
{
    QTestState *qts = qtest_init("-machine k230");

    /* SDHCI0 capabilities */
    uint32_t c0 = qtest_readl(qts, SDHCI0_BASE + K230_SDHCI_CAPAB);
    g_assert_cmphex(c0, ==, K230_SDHCI_CAPAB_LO);

    /* SDHCI1 capabilities — same value, independent instance */
    uint32_t c1 = qtest_readl(qts, SDHCI1_BASE + K230_SDHCI_CAPAB);
    g_assert_cmphex(c1, ==, K230_SDHCI_CAPAB_LO);

    /* Write different values to SDMASA, verify independence */
    qtest_writel(qts, SDHCI0_BASE + K230_SDHCI_SDMASA, 0xAAAAAAAA);
    qtest_writel(qts, SDHCI1_BASE + K230_SDHCI_SDMASA, 0xBBBBBBBB);

    uint32_t s0 = qtest_readl(qts, SDHCI0_BASE + K230_SDHCI_SDMASA);
    uint32_t s1 = qtest_readl(qts, SDHCI1_BASE + K230_SDHCI_SDMASA);
    g_assert_cmphex(s0, ==, 0xAAAAAAAA);
    g_assert_cmphex(s1, ==, 0xBBBBBBBB);

    qtest_quit(qts);
}

/* ------------------------------------------------------------------ */
/* 10. ADMA registers                                                  */
/* ------------------------------------------------------------------ */

static void test_adma_registers(void)
{
    QTestState *qts = qtest_init("-machine k230");

    /* ADMA Error Status — read-only, default 0 */
    uint32_t aerr = qtest_readl(qts, SDHCI0_BASE + K230_SDHCI_ADMA_ERR_STAT);
    g_assert_cmphex(aerr, ==, 0);

    /* ADMA System Address — 64-bit RW */
    qtest_writel(qts, SDHCI0_BASE + K230_SDHCI_ADMA_ADDR_LO,
                 0xDEADBEEF);
    qtest_writel(qts, SDHCI0_BASE + K230_SDHCI_ADMA_ADDR_HI,
                 0x0000CAFE);

    uint32_t lo = qtest_readl(qts, SDHCI0_BASE + K230_SDHCI_ADMA_ADDR_LO);
    uint32_t hi = qtest_readl(qts, SDHCI0_BASE + K230_SDHCI_ADMA_ADDR_HI);
    g_assert_cmphex(lo, ==, 0xDEADBEEF);
    g_assert_cmphex(hi, ==, 0x0000CAFE);

    qtest_quit(qts);
}

/* ------------------------------------------------------------------ */
/* Test Registration                                                    */
/* ------------------------------------------------------------------ */

int main(int argc, char *argv[])
{
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/k230-sdhci/capabilities",
                   test_capabilities);
    qtest_add_func("/k230-sdhci/present_state",
                   test_present_state);
    qtest_add_func("/k230-sdhci/slot_int_status",
                   test_slot_int_status);
    qtest_add_func("/k230-sdhci/max_current_zero",
                   test_max_current_zero);
    qtest_add_func("/k230-sdhci/ro_registers_ignore_writes",
                   test_ro_registers_ignore_writes);
    qtest_add_func("/k230-sdhci/rw_register_consistency",
                   test_rw_register_consistency);
    qtest_add_func("/k230-sdhci/interrupt_w1c",
                   test_interrupt_w1c);
    qtest_add_func("/k230-sdhci/control_registers",
                   test_control_registers);
    qtest_add_func("/k230-sdhci/both_instances",
                   test_both_instances);
    qtest_add_func("/k230-sdhci/adma_registers",
                   test_adma_registers);

    return g_test_run();
}
