/*
 * QTest testcase for K230 SRAM
 *
 * K230 Technical Reference Manual V0.3.1 (2024-11-18):
 * https://github.com/revyos/external-docs/blob/master/K230/en-us/K230_Technical_Reference_Manual_V0.3.1_20241118.pdf
 *
 * The K230 shared SRAM is 2 MB at 0x80200000, accessed directly via the
 * AXI bus.  It has no software-visible controller registers, so the tests
 * focus on MMIO read/write consistency across the full range.
 *
 * Copyright (c) 2026 The QEMU K230 Camp Contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "libqtest.h"

/* K230 SRAM MMIO base address and size */
#define K230_SRAM_BASE  0x80200000
#define K230_SRAM_SIZE  (2 * 1024 * 1024) /* 2 MiB */

static void test_address_mapped(void)
{
    QTestState *qts = qtest_init("-machine k230");

    /* Write a known pattern at the start of SRAM */
    const uint32_t pattern = 0xDEADBEEF;
    qtest_writel(qts, K230_SRAM_BASE, pattern);

    /* Read back and verify */
    uint32_t value = qtest_readl(qts, K230_SRAM_BASE);
    g_assert_cmphex(value, ==, pattern);

    qtest_quit(qts);
}

static void test_read_write_consistency(void)
{
    QTestState *qts = qtest_init("-machine k230");

    /* Write and read back a sequence of patterns at different offsets */
    static const struct {
        const char *desc;
        uint32_t offset;
        uint32_t pattern;
    } test_cases[] = {
        { "start",       0x000000, 0x12345678 },
        { "mid",         0x100000, 0x9ABCDEF0 }, /* 1 MiB */
        { "end-4",       K230_SRAM_SIZE - 4, 0xCAFEBABE },
        { "aligned-256", 0x000100, 0x55AA55AA },
    };

    for (size_t i = 0; i < G_N_ELEMENTS(test_cases); i++) {
        uint32_t addr = K230_SRAM_BASE + test_cases[i].offset;
        qtest_writel(qts, addr, test_cases[i].pattern);

        uint32_t value = qtest_readl(qts, addr);
        g_assert_cmphex(value, ==, test_cases[i].pattern);
    }

    qtest_quit(qts);
}

static void test_byte_access(void)
{
    QTestState *qts = qtest_init("-machine k230");

    /* Verify byte-level access */
    uint32_t base = K230_SRAM_BASE;

    qtest_writeb(qts, base + 0, 0xAA);
    qtest_writeb(qts, base + 1, 0xBB);
    qtest_writeb(qts, base + 2, 0xCC);
    qtest_writeb(qts, base + 3, 0xDD);

    uint8_t b0 = qtest_readb(qts, base + 0);
    uint8_t b1 = qtest_readb(qts, base + 1);
    uint8_t b2 = qtest_readb(qts, base + 2);
    uint8_t b3 = qtest_readb(qts, base + 3);

    g_assert_cmphex(b0, ==, 0xAA);
    g_assert_cmphex(b1, ==, 0xBB);
    g_assert_cmphex(b2, ==, 0xCC);
    g_assert_cmphex(b3, ==, 0xDD);

    /* Verify 32-bit read sees the combined value (LE) */
    uint32_t word = qtest_readl(qts, base);
    g_assert_cmphex(word, ==, 0xDDCCBBAA);

    qtest_quit(qts);
}

static void test_initial_zeroed(void)
{
    QTestState *qts = qtest_init("-machine k230");

    /* SRAM should be zero-initialized by memory_region_init_ram */
    uint32_t value = qtest_readl(qts, K230_SRAM_BASE);
    g_assert_cmphex(value, ==, 0);

    value = qtest_readl(qts, K230_SRAM_BASE + K230_SRAM_SIZE - 4);
    g_assert_cmphex(value, ==, 0);

    qtest_quit(qts);
}

static void test_unmapped_beyond_range(void)
{
    QTestState *qts = qtest_init("-machine k230");

    /*
     * Writes beyond the SRAM region should not crash QEMU.
     * The exact behaviour (unassigned access / abort) is platform-specific;
     * we just verify the write does not take the process down.
     */
    qtest_writel(qts, K230_SRAM_BASE + K230_SRAM_SIZE, 0xBADF00D);
    qtest_readl(qts, K230_SRAM_BASE + K230_SRAM_SIZE);

    qtest_quit(qts);
}

static void test_write_and_verify_range(void)
{
    QTestState *qts = qtest_init("-machine k230");

    /*
     * Write a walking-bit pattern across the full 2 MiB range in
     * 64 KiB steps, verifying each word writes correctly.
     */
    for (uint32_t offset = 0; offset < K230_SRAM_SIZE; offset += 0x10000) {
        uint32_t addr = K230_SRAM_BASE + offset;
        uint32_t pattern = offset | 0x3;  /* keep bottom bits set */

        qtest_writel(qts, addr, pattern);
        uint32_t value = qtest_readl(qts, addr);
        g_assert_cmphex(value, ==, pattern);
    }

    qtest_quit(qts);
}

int main(int argc, char *argv[])
{
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/k230-sram/address_mapped",
                   test_address_mapped);
    qtest_add_func("/k230-sram/read_write_consistency",
                   test_read_write_consistency);
    qtest_add_func("/k230-sram/byte_access",
                   test_byte_access);
    qtest_add_func("/k230-sram/initial_zeroed",
                   test_initial_zeroed);
    qtest_add_func("/k230-sram/unmapped_beyond_range",
                   test_unmapped_beyond_range);
    qtest_add_func("/k230-sram/write_and_verify_range",
                   test_write_and_verify_range);

    return g_test_run();
}
