/*
 * QTest testcase for K230 SD/MMC Host Controller (DWC MSHC)
 *
 * Copyright (c) 2026 The QEMU K230 Camp Contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "hw/sd/k230_sdhci.h"

#define SDHCI0_BASE  0x91580000
#define SDHCI1_BASE  0x91581000

/*
 * Standard SDHCI register offsets (SD Host Controller Spec v4).
 * These match the defines in hw/sd/sdhci-internal.h but are
 * duplicated here to avoid pulling in internal SDHCI headers
 * that have forward-declaration dependencies.
 */
#define SDHC_SYSAD         0x00
#define SDHC_BLKSIZE       0x04
#define SDHC_BLKCNT        0x06
#define SDHC_ARGUMENT      0x08
#define SDHC_TRNMOD        0x0C
#define SDHC_CMDREG        0x0E
#define SDHC_RSPREG0       0x10
#define SDHC_BDATA         0x20
#define SDHC_PRNSTS        0x24
#define SDHC_HOSTCTL       0x28
#define SDHC_PWRCON        0x29
#define SDHC_BLKGAP        0x2A
#define SDHC_WAKCON        0x2B
#define SDHC_CLKCON        0x2C
#define SDHC_TIMEOUTCON    0x2E
#define SDHC_SWRST         0x2F
#define SDHC_NORINTSTS     0x30
#define SDHC_ERRINTSTS     0x32
#define SDHC_NORINTSTSEN   0x34
#define SDHC_ERRINTSTSEN   0x36
#define SDHC_NORINTSIGEN   0x38
#define SDHC_ERRINTSIGEN   0x3A
#define SDHC_ACMD12ERRSTS  0x3C
#define SDHC_HOSTCTL2      0x3E
#define SDHC_CAPAB         0x40
#define SDHC_MAXCURR       0x48
#define SDHC_ADMAERR       0x54
#define SDHC_ADMASYSADDR   0x58
#define SDHC_SLOT_INT_STATUS 0xFC

/* ------------------------------------------------------------------ */
/*  1. Capabilities registers                                          */
/* ------------------------------------------------------------------ */

static void test_capabilities(void)
{
    QTestState *qts = qtest_init("-machine k230");

    uint32_t cap_lo = qtest_readl(qts, SDHCI0_BASE + SDHC_CAPAB);
    g_assert_cmphex(cap_lo, ==, K230_SDHCI_CAPAB_LO);

    uint32_t cap_hi = qtest_readl(qts, SDHCI0_BASE + SDHC_CAPAB + 4);
    g_assert_cmphex(cap_hi, ==, K230_SDHCI_CAPAB_HI);

    qtest_quit(qts);
}

/* ------------------------------------------------------------------ */
/*  2. Present State — card inserted + stable                          */
/* ------------------------------------------------------------------ */

static void test_present_state(void)
{
    QTestState *qts = qtest_init("-machine k230");

    uint32_t pstate = qtest_readl(qts, SDHCI0_BASE + SDHC_PRNSTS);
    /* At minimum, CARD_PRESENT (bit 16) must be set */
    g_assert_true(pstate & K230_SDHCI_PSTATE_CARD_INSERTED);
    g_assert_true(pstate & K230_SDHCI_PSTATE_CARD_STABLE);

    qtest_quit(qts);
}

/* ------------------------------------------------------------------ */
/*  3. Slot Interrupt Status + Version                                 */
/* ------------------------------------------------------------------ */

static void test_slot_int_status(void)
{
    QTestState *qts = qtest_init("-machine k230");

    uint32_t val = qtest_readl(qts, SDHCI0_BASE + SDHC_SLOT_INT_STATUS);
    /* bits [31:16] = version */
    g_assert_cmphex((val >> 16) & 0xffff, ==, K230_SDHCI_HC_VERSION);

    qtest_quit(qts);
}

/* ------------------------------------------------------------------ */
/*  4. Max Current (read-only, zero)                                   */
/* ------------------------------------------------------------------ */

static void test_max_current_zero(void)
{
    QTestState *qts = qtest_init("-machine k230");

    uint32_t cur_lo = qtest_readl(qts, SDHCI0_BASE + SDHC_MAXCURR);
    g_assert_cmphex(cur_lo, ==, 0);

    uint32_t cur_hi = qtest_readl(qts, SDHCI0_BASE + SDHC_MAXCURR + 4);
    g_assert_cmphex(cur_hi, ==, 0);

    qtest_quit(qts);
}

/* ------------------------------------------------------------------ */
/*  5. RO registers ignore writes                                      */
/* ------------------------------------------------------------------ */

static void test_ro_registers_ignore_writes(void)
{
    QTestState *qts = qtest_init("-machine k230");

    qtest_writel(qts, SDHCI0_BASE + SDHC_CAPAB, 0xDEADBEEF);
    qtest_writel(qts, SDHCI0_BASE + SDHC_CAPAB + 4, 0xCAFEBABE);

    uint32_t cap_lo = qtest_readl(qts, SDHCI0_BASE + SDHC_CAPAB);
    g_assert_cmphex(cap_lo, ==, K230_SDHCI_CAPAB_LO);

    uint32_t cap_hi = qtest_readl(qts, SDHCI0_BASE + SDHC_CAPAB + 4);
    g_assert_cmphex(cap_hi, ==, K230_SDHCI_CAPAB_HI);

    qtest_quit(qts);
}

/* ------------------------------------------------------------------ */
/*  6. RW register consistency                                         */
/* ------------------------------------------------------------------ */

static void test_rw_register_consistency(void)
{
    QTestState *qts = qtest_init("-machine k230");

    /* SDMA System Address (0x00) */
    qtest_writel(qts, SDHCI0_BASE + SDHC_SYSAD, 0x12345678);
    uint32_t sysad = qtest_readl(qts, SDHCI0_BASE + SDHC_SYSAD);
    g_assert_cmphex(sysad, ==, 0x12345678);

    /* Argument (0x08) */
    qtest_writel(qts, SDHCI0_BASE + SDHC_ARGUMENT, 0x9ABCDEF0);
    uint32_t arg = qtest_readl(qts, SDHCI0_BASE + SDHC_ARGUMENT);
    g_assert_cmphex(arg, ==, 0x9ABCDEF0);

    /* Block Size (0x04) — 16-bit mask */
    qtest_writel(qts, SDHCI0_BASE + SDHC_BLKSIZE, 0x80000200);
    uint32_t blksize = qtest_readl(qts, SDHCI0_BASE + SDHC_BLKSIZE);
    g_assert_cmphex(blksize & 0xffff, ==, 0x0200);

    /* Block Count (0x06) — 16-bit mask */
    qtest_writel(qts, SDHCI0_BASE + SDHC_BLKCNT, 0xFFFF0010);
    uint32_t blkcnt = qtest_readl(qts, SDHCI0_BASE + SDHC_BLKCNT);
    g_assert_cmphex(blkcnt & 0xffff, ==, 0x0010);

    qtest_quit(qts);
}

/* ------------------------------------------------------------------ */
/*  7. Interrupt status W1C behaviour                                  */
/* ------------------------------------------------------------------ */

static void test_interrupt_w1c(void)
{
    QTestState *qts = qtest_init("-machine k230");

    /* Initially 0 */
    uint32_t nor = qtest_readl(qts, SDHCI0_BASE + SDHC_NORINTSTS);
    g_assert_cmphex(nor, ==, 0);

    /* W1C: write 0x0001 → stays 0 (already clear) */
    qtest_writel(qts, SDHCI0_BASE + SDHC_NORINTSTS, 0x0001);
    nor = qtest_readl(qts, SDHCI0_BASE + SDHC_NORINTSTS);
    g_assert_cmphex(nor, ==, 0);

    qtest_quit(qts);
}

/* ------------------------------------------------------------------ */
/*  8. Control register consistency                                    */
/* ------------------------------------------------------------------ */

static void test_control_registers(void)
{
    QTestState *qts = qtest_init("-machine k230");

    /* Host Control (8-bit at 0x28) */
    qtest_writel(qts, SDHCI0_BASE + SDHC_HOSTCTL, 0x00000007);
    uint32_t hc = qtest_readl(qts, SDHCI0_BASE + SDHC_HOSTCTL);
    g_assert_cmphex(hc, ==, 0x07);

    /* Timeout Control (8-bit at 0x2E) */
    qtest_writel(qts, SDHCI0_BASE + SDHC_TIMEOUTCON, 0x0000000E);
    uint32_t tout = qtest_readl(qts, SDHCI0_BASE + SDHC_TIMEOUTCON);
    g_assert_cmphex(tout, ==, 0x0E);

    qtest_quit(qts);
}

/* ------------------------------------------------------------------ */
/*  9. Both instances independently accessible                         */
/* ------------------------------------------------------------------ */

static void test_both_instances(void)
{
    QTestState *qts = qtest_init("-machine k230");

    uint32_t c0 = qtest_readl(qts, SDHCI0_BASE + SDHC_CAPAB);
    g_assert_cmphex(c0, ==, K230_SDHCI_CAPAB_LO);

    uint32_t c1 = qtest_readl(qts, SDHCI1_BASE + SDHC_CAPAB);
    g_assert_cmphex(c1, ==, K230_SDHCI_CAPAB_LO);

    /* Write different values, verify independence */
    qtest_writel(qts, SDHCI0_BASE + SDHC_SYSAD, 0xAAAAAAAA);
    qtest_writel(qts, SDHCI1_BASE + SDHC_SYSAD, 0xBBBBBBBB);

    uint32_t s0 = qtest_readl(qts, SDHCI0_BASE + SDHC_SYSAD);
    uint32_t s1 = qtest_readl(qts, SDHCI1_BASE + SDHC_SYSAD);
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

    /* ADMA Error Status — default 0 */
    uint32_t aerr = qtest_readl(qts, SDHCI0_BASE + SDHC_ADMAERR);
    g_assert_cmphex(aerr, ==, 0);

    /* ADMA System Address — 64-bit */
    qtest_writel(qts, SDHCI0_BASE + SDHC_ADMASYSADDR, 0xDEADBEEF);
    qtest_writel(qts, SDHCI0_BASE + SDHC_ADMASYSADDR + 4, 0x0000CAFE);

    uint32_t lo = qtest_readl(qts, SDHCI0_BASE + SDHC_ADMASYSADDR);
    uint32_t hi = qtest_readl(qts, SDHCI0_BASE + SDHC_ADMASYSADDR + 4);
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
