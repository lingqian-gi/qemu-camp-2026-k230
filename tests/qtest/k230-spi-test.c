/*
 * QTest testcase for K230 SPI / QSPI Controller
 *
 * Copyright (c) 2026 The QEMU K230 Camp Contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * K230 Technical Reference Manual V0.3.1 (2024-11-18), Chapter 12.3:
 * https://github.com/revyos/external-docs/blob/master/K230/en-us/K230_Technical_Reference_Manual_V0.3.1_20241118.pdf
 *
 * Tests the register-level MMIO behaviour of the three DW SSI instances
 * (QSPI0 / QSPI1 / SPI).  Data transfer, DMA and XIP are not covered.
 */

#include "qemu/osdep.h"
#include "qemu/bitops.h"
#include "libqtest.h"
#include "hw/ssi/k230_spi.h"

/* QSPI0 is the primary test target (XIP-capable, flash-boot path) */
#define QSPI0_BASE  0x91582000
#define QSPI1_BASE  0x91583000
#define SPI_BASE    0x91584000

/* ------------------------------------------------------------------ */
/*  1. CTRLR0 reset default                                            */
/* ------------------------------------------------------------------ */

static void test_ctrlr0_reset(void)
{
    QTestState *qts = qtest_init("-machine k230");

    uint32_t ctrlr0 = qtest_readl(qts, QSPI0_BASE + K230_SPI_CTRLR0);
    g_assert_cmphex(ctrlr0, ==, K230_SPI_CTRLR0_RESET);

    qtest_quit(qts);
}

/* ------------------------------------------------------------------ */
/*  2. Version / ID registers (read-only constants)                    */
/* ------------------------------------------------------------------ */

static void test_version_id_registers(void)
{
    QTestState *qts = qtest_init("-machine k230");

    uint32_t id = qtest_readl(qts, QSPI0_BASE + K230_SPI_IDR);
    g_assert_cmphex(id, ==, K230_SPI_IDR_VAL);

    uint32_t ver = qtest_readl(qts, QSPI0_BASE + K230_SPI_SSIC_VERSION_ID);
    g_assert_cmphex(ver, ==, K230_SPI_SSIC_VERSION_ID_VAL);

    qtest_quit(qts);
}

/* ------------------------------------------------------------------ */
/*  3. SR returns idle (non-BUSY) — prevents probe busy-wait           */
/* ------------------------------------------------------------------ */

static void test_sr_returns_idle(void)
{
    QTestState *qts = qtest_init("-machine k230");

    uint32_t sr = qtest_readl(qts, QSPI0_BASE + K230_SPI_SR);
    /* TFNF=1, TFE=1, BUSY=0  →  0x06 */
    g_assert_cmphex(sr & K230_SPI_SR_BUSY, ==, 0);
    g_assert_cmphex(sr & K230_SPI_SR_TFE,  ==, K230_SPI_SR_TFE);
    g_assert_cmphex(sr & K230_SPI_SR_TFNF, ==, K230_SPI_SR_TFNF);

    /*
     * Even after writing a value with BUSY=1 to SR, the read path
     * should still force BUSY=0 and TFNF=1 | TFE=1.
     */
    qtest_writel(qts, QSPI0_BASE + K230_SPI_SR, 0xFF);
    sr = qtest_readl(qts, QSPI0_BASE + K230_SPI_SR);
    g_assert_cmphex(sr & K230_SPI_SR_BUSY, ==, 0);
    g_assert_cmphex(sr & K230_SPI_SR_TFNF, ==, K230_SPI_SR_TFNF);
    g_assert_cmphex(sr & K230_SPI_SR_TFE,  ==, K230_SPI_SR_TFE);

    qtest_quit(qts);
}

/* ------------------------------------------------------------------ */
/*  4. RW register write-then-read consistency                         */
/* ------------------------------------------------------------------ */

static void test_rw_register_consistency(void)
{
    QTestState *qts = qtest_init("-machine k230");

    /* CTRLR1: write 0xDEAD and read back (NDF[15:0] mask) */
    qtest_writel(qts, QSPI0_BASE + K230_SPI_CTRLR1, 0xDEADBEEF);
    g_assert_cmphex(qtest_readl(qts, QSPI0_BASE + K230_SPI_CTRLR1),
                    ==, 0x0000BEEF);

    /* SER: full 32-bit write */
    qtest_writel(qts, QSPI0_BASE + K230_SPI_SER, 0xCAFEBABE);
    g_assert_cmphex(qtest_readl(qts, QSPI0_BASE + K230_SPI_SER),
                    ==, 0xCAFEBABE);

    /* BAUDR: write masked by SCKDV (even values) */
    qtest_writel(qts, QSPI0_BASE + K230_SPI_BAUDR, 0xFFFFAAAA);
    g_assert_cmphex(qtest_readl(qts, QSPI0_BASE + K230_SPI_BAUDR),
                    ==, (0xFFFFAAAA & K230_SPI_BAUDR_SCKDV_MASK));

    /* IMR: full 32-bit */
    qtest_writel(qts, QSPI0_BASE + K230_SPI_IMR, 0x12345678);
    g_assert_cmphex(qtest_readl(qts, QSPI0_BASE + K230_SPI_IMR), ==, 0x12345678);

    /* Extension: RX_SAMPLE_DELAY */
    qtest_writel(qts, QSPI0_BASE + K230_SPI_RX_SAMPLE_DELAY, 0xA5A5A5A5);
    g_assert_cmphex(qtest_readl(qts, QSPI0_BASE + K230_SPI_RX_SAMPLE_DELAY),
                    ==, 0xA5A5A5A5);

    qtest_quit(qts);
}

/* ------------------------------------------------------------------ */
/*  5. SSIENR bit-0 masking                                            */
/* ------------------------------------------------------------------ */

static void test_ssienr_mask(void)
{
    QTestState *qts = qtest_init("-machine k230");

    /* Reset value: 0 */
    g_assert_cmphex(qtest_readl(qts, QSPI0_BASE + K230_SPI_SSIENR), ==, 0);

    /* Write all-ones — only bit 0 should survive */
    qtest_writel(qts, QSPI0_BASE + K230_SPI_SSIENR, 0xFFFFFFFF);
    g_assert_cmphex(qtest_readl(qts, QSPI0_BASE + K230_SPI_SSIENR), ==,
                    K230_SPI_SSIENR_EN);

    /* Write 0 to disable */
    qtest_writel(qts, QSPI0_BASE + K230_SPI_SSIENR, 0);
    g_assert_cmphex(qtest_readl(qts, QSPI0_BASE + K230_SPI_SSIENR), ==, 0);

    qtest_quit(qts);
}

/* ------------------------------------------------------------------ */
/*  6. WO clear registers always read as 0                             */
/* ------------------------------------------------------------------ */

static void test_wo_registers_read_zero(void)
{
    QTestState *qts = qtest_init("-machine k230");

    static const uint32_t wo_regs[] = {
        K230_SPI_TXOICR,  K230_SPI_RXOICR,  K230_SPI_RXUICR,
        K230_SPI_MSTICR,  K230_SPI_ICR,
        K230_SPI_XRXOICR, K230_SPI_SPITECR, K230_SPI_AXIECR, K230_SPI_DONECR,
    };

    for (size_t i = 0; i < G_N_ELEMENTS(wo_regs); i++) {
        /* Write non-zero to the WO register */
        qtest_writel(qts, QSPI0_BASE + wo_regs[i], 0xDEADBEEF);

        /* Read should always return 0 */
        uint32_t val = qtest_readl(qts, QSPI0_BASE + wo_regs[i]);
        g_assert_cmphex(val, ==, 0);
    }

    qtest_quit(qts);
}

/* ------------------------------------------------------------------ */
/*  7. RO registers silently ignore writes                             */
/* ------------------------------------------------------------------ */

static void test_ro_registers_ignore_writes(void)
{
    QTestState *qts = qtest_init("-machine k230");

    /*
     * TXFLR (0x20) and RXFLR (0x24) are read-only.
     * Writing should not change the read-back value.
     */
    uint32_t txflr_before = qtest_readl(qts, QSPI0_BASE + K230_SPI_TXFLR);
    qtest_writel(qts, QSPI0_BASE + K230_SPI_TXFLR, 0xFF);
    g_assert_cmphex(qtest_readl(qts, QSPI0_BASE + K230_SPI_TXFLR),
                    ==, txflr_before);

    uint32_t rxflr_before = qtest_readl(qts, QSPI0_BASE + K230_SPI_RXFLR);
    qtest_writel(qts, QSPI0_BASE + K230_SPI_RXFLR, 0xFF);
    g_assert_cmphex(qtest_readl(qts, QSPI0_BASE + K230_SPI_RXFLR),
                    ==, rxflr_before);

    /*
     * IDR and SSIC_VERSION_ID are hardware constants.
     */
    qtest_writel(qts, QSPI0_BASE + K230_SPI_IDR, 0xFFFFFFFF);
    g_assert_cmphex(qtest_readl(qts, QSPI0_BASE + K230_SPI_IDR), ==,
                    K230_SPI_IDR_VAL);

    qtest_writel(qts, QSPI0_BASE + K230_SPI_SSIC_VERSION_ID, 0xFFFFFFFF);
    g_assert_cmphex(qtest_readl(qts, QSPI0_BASE + K230_SPI_SSIC_VERSION_ID), ==,
                    K230_SPI_SSIC_VERSION_ID_VAL);

    /* RISR is read-only */
    uint32_t risr_before = qtest_readl(qts, QSPI0_BASE + K230_SPI_RISR);
    qtest_writel(qts, QSPI0_BASE + K230_SPI_RISR, 0xFFFFFFFF);
    g_assert_cmphex(qtest_readl(qts, QSPI0_BASE + K230_SPI_RISR), ==,
                    risr_before);

    qtest_quit(qts);
}

/* ------------------------------------------------------------------ */
/*  8. TXFTLR / RXFTLR 8-bit masking                                   */
/* ------------------------------------------------------------------ */

static void test_fifo_threshold_mask(void)
{
    QTestState *qts = qtest_init("-machine k230");

    qtest_writel(qts, QSPI0_BASE + K230_SPI_TXFTLR, 0xFFFFFF00);
    g_assert_cmphex(qtest_readl(qts, QSPI0_BASE + K230_SPI_TXFTLR),
                    ==, 0x00000000);

    qtest_writel(qts, QSPI0_BASE + K230_SPI_RXFTLR, 0xFFFFFF00);
    g_assert_cmphex(qtest_readl(qts, QSPI0_BASE + K230_SPI_RXFTLR),
                    ==, 0x00000000);

    qtest_quit(qts);
}

/* ------------------------------------------------------------------ */
/*  9. All three instances are mapped and have independent state        */
/* ------------------------------------------------------------------ */

static void test_all_three_instances(void)
{
    QTestState *qts = qtest_init("-machine k230");

    static const struct {
        const char *name;
        uint32_t base;
    } instances[] = {
        { "QSPI0", QSPI0_BASE },
        { "QSPI1", QSPI1_BASE },
        { "SPI",   SPI_BASE   },
    };

    for (size_t i = 0; i < G_N_ELEMENTS(instances); i++) {
        uint32_t base = instances[i].base;

        /* Each instance should have CTRLR0 at reset value */
        uint32_t ctrlr0 = qtest_readl(qts, base + K230_SPI_CTRLR0);
        g_assert_cmphex(ctrlr0, ==, K230_SPI_CTRLR0_RESET);

        /* Each instance can independently set SER */
        uint32_t pattern = 0xA0000000 | (uint32_t)i;
        qtest_writel(qts, base + K230_SPI_SER, pattern);
        g_assert_cmphex(qtest_readl(qts, base + K230_SPI_SER), ==, pattern);
    }

    /* Verify instances are independent — QSPI0 SER unchanged by QSPI1 */
    g_assert_cmphex(qtest_readl(qts, QSPI0_BASE + K230_SPI_SER),
                    ==, 0xA0000000);
    g_assert_cmphex(qtest_readl(qts, QSPI1_BASE + K230_SPI_SER),
                    ==, 0xA0000001);
    g_assert_cmphex(qtest_readl(qts, SPI_BASE + K230_SPI_SER),
                    ==, 0xA0000002);

    qtest_quit(qts);
}

/* ------------------------------------------------------------------ */
/*  10. WO clear interrupt handling (ISR clear via TXOICR et al.)      */
/* ------------------------------------------------------------------ */

static void test_wo_clear_isr(void)
{
    QTestState *qts = qtest_init("-machine k230");

    /* ISR starts at 0 */
    g_assert_cmphex(qtest_readl(qts, QSPI0_BASE + K230_SPI_ISR), ==, 0);

    /* Can't directly write ISR (it's RO), but WO clear is a no-op on 0 */
    qtest_writel(qts, QSPI0_BASE + K230_SPI_TXOICR, 0xFFFFFFFF);
    g_assert_cmphex(qtest_readl(qts, QSPI0_BASE + K230_SPI_ISR), ==, 0);

    qtest_quit(qts);
}

/* ------------------------------------------------------------------ */
/*  Test registration                                                  */
/* ------------------------------------------------------------------ */

int main(int argc, char *argv[])
{
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/k230-spi/ctrlr0_reset",           test_ctrlr0_reset);
    qtest_add_func("/k230-spi/version_id_registers",    test_version_id_registers);
    qtest_add_func("/k230-spi/sr_returns_idle",         test_sr_returns_idle);
    qtest_add_func("/k230-spi/rw_register_consistency", test_rw_register_consistency);
    qtest_add_func("/k230-spi/ssienr_mask",             test_ssienr_mask);
    qtest_add_func("/k230-spi/wo_registers_read_zero",  test_wo_registers_read_zero);
    qtest_add_func("/k230-spi/ro_registers_ignore_writes",
                                                       test_ro_registers_ignore_writes);
    qtest_add_func("/k230-spi/fifo_threshold_mask",     test_fifo_threshold_mask);
    qtest_add_func("/k230-spi/all_three_instances",     test_all_three_instances);
    qtest_add_func("/k230-spi/wo_clear_isr",            test_wo_clear_isr);

    return g_test_run();
}
