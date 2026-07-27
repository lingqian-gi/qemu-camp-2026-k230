/*
 * K230 CMU (Clock Management Unit)
 *
 * The K230 CMU at 0x91100000 manages all SoC clocks through 4 PLLs
 * plus a set of per-peripheral clock gate / divider / mux registers.
 *
 * Phase 1 implements a minimal register-level model:
 *   - PLL lock status always returns locked
 *   - PLL clock output always enabled
 *   - All other registers accept writes and read back stored values
 *
 * Reference:
 *   K230 TRM V0.3.1 (2024-11-18), Chapter 2.2 System Controller
 *   drivers/clk/clk-k230.c (upstream Linux patch v12)
 *
 * Copyright (c) 2026 The QEMU K230 Camp Contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef K230_CMU_H
#define K230_CMU_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_K230_CMU "riscv.k230.cmu"
OBJECT_DECLARE_SIMPLE_TYPE(K230CmuState, K230_CMU)

#define K230_CMU_MMIO_SIZE 0x1000

/*
 * PLL register offsets (4 PLLs, 0x10 bytes each).
 * Index N: PLL0_XXX at base + 0x00 + N*0x10
 */
#define K230_CMU_PLL_CFG0    0x00  /* PLL divider config  (RW) */
#define K230_CMU_PLL_CFG1    0x04  /* PLL bypass/test    (RW) */
#define K230_CMU_PLL_CTL     0x08  /* PLL init/pwrdn/clk_oe (RW) */
#define K230_CMU_PLL_STAT    0x0C  /* PLL lock status     (RO) */

/* PLL_CTL bits */
#define K230_CMU_PLL_CLK_OE      BIT(2)   /* clock output enable */
#define K230_CMU_PLL_GATE_WR     BIT(18)  /* write-enable gate bit */

/* PLL_STAT bits */
#define K230_CMU_PLL_LOCK        BIT(0)   /* PLL locked (1=locked) */
#define K230_CMU_PLL_FSM_READY   (0x2 << 4)  /* FSM_PLL_READY state */

/*
 * Default PLL_CTL value seen by driver: clock enabled, write-enable active
 */
#define K230_CMU_PLL_CTL_DEFAULT  (K230_CMU_PLL_CLK_OE | K230_CMU_PLL_GATE_WR)

/*
 * Default PLL_STAT value: locked, FSM in READY state
 */
#define K230_CMU_PLL_STAT_DEFAULT (K230_CMU_PLL_LOCK | K230_CMU_PLL_FSM_READY)

struct K230CmuState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion mmio;

    /*
     * Flat register array covering the full 4KB CMU window.
     * Phase 1 stores all written values so reads return what was
     * last written.  PLL_STAT and PLL_CTL are initialised to
     * "locked + enabled" in reset but remain writable from guest.
     */
    uint32_t regs[K230_CMU_MMIO_SIZE / 4];
};

#endif /* K230_CMU_H */
