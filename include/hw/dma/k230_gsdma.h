/*
 * K230 GSDMA (General System DMA) — Device Model Header
 *
 * The K230 GSDMA at 0x80800000 combines GDMA (graphics DMA, 1 channel)
 * and SDMA (system DMA, 4 channels) behind a shared AXI master.
 *
 * Phase 1 implements a minimal register-level model: all registers
 * accept reads and writes, with correct reset defaults.
 *
 * Reference:
 *   K230 TRM V0.3.1 (2024-11-18), Chapter 10.2.3 GSDMA
 *
 * Copyright (c) 2026 The QEMU K230 Camp Contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef K230_GSDMA_H
#define K230_GSDMA_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_K230_GSDMA "riscv.k230.gsdma"
OBJECT_DECLARE_SIMPLE_TYPE(K230GSDMAState, K230_GSDMA)

#define K230_GSDMA_MMIO_SIZE 0x4000

/* ------------------------------------------------------------------ */
/* Register Offsets                                                    */
/* ------------------------------------------------------------------ */

/* Global registers */
#define K230_GSDMA_CH_EN        0x00  /* channel enable (RW) */
#define K230_GSDMA_INT_MASK     0x04  /* interrupt mask (RW) */
#define K230_GSDMA_INT_STAT     0x08  /* interrupt status (W1C) */
#define K230_GSDMA_DMA_CFG      0x0C  /* DMA config (RW) */

/* GDMA registers */
#define K230_GSDMA_GDMA_CTRL      0x10  /* GDMA control (WO1) */
#define K230_GSDMA_LLI_BASE       0x14  /* GDMA LLI base addr (RW) */
#define K230_GSDMA_CH0_CNT        0x18  /* GDMA ch0 counter (W0C) */
#define K230_GSDMA_CH1_CNT        0x1C  /* GDMA ch1 counter */
#define K230_GSDMA_CH2_CNT        0x20
#define K230_GSDMA_CH3_CNT        0x24
#define K230_GSDMA_CH4_CNT        0x28
#define K230_GSDMA_CH5_CNT        0x2C
#define K230_GSDMA_CH6_CNT        0x30
#define K230_GSDMA_CH7_CNT        0x34
#define K230_GSDMA_CURRENT_LLT    0x38  /* current LLI addr (RO) */

/* Arbitration */
#define K230_GSDMA_WEIGHT         0x48  /* DMA weight (RW) */

/* ------------------------------------------------------------------ */
/* Bit Definitions                                                     */
/* ------------------------------------------------------------------ */

/* DMA_CH_EN */
#define K230_GSDMA_CH_EN_GDMA      BIT(4)
#define K230_GSDMA_CH_EN_CH0       BIT(0)
#define K230_GSDMA_CH_EN_CH1       BIT(1)
#define K230_GSDMA_CH_EN_CH2       BIT(2)
#define K230_GSDMA_CH_EN_CH3       BIT(3)

/* DMA_INT_MASK / DMA_INT_STAT (same bit layout) */
#define K230_GSDMA_INT_GDMA_TR_DONE       BIT(18)
#define K230_GSDMA_INT_GDMA_LLI_ITEM      BIT(17)
#define K230_GSDMA_INT_GDMA_LLI_PAUSE     BIT(16)
#define K230_GSDMA_INT_CHX_LLT_PAUSE(x)   BIT(8 + (x))   /* x=0..3 */
#define K230_GSDMA_INT_CHX_LLT_ITEM(x)    BIT(4 + (x))
#define K230_GSDMA_INT_CHX_TR_DONE(x)     BIT(0 + (x))

/* GDMA_CTRL */
#define K230_GSDMA_GDMA_RESUME    BIT(2)
#define K230_GSDMA_GDMA_STOP      BIT(1)
#define K230_GSDMA_GDMA_START     BIT(0)

/* DMA_CFG reset default */
#define K230_GSDMA_CFG_RESET      0x000007FF

/* ------------------------------------------------------------------ */
/* Device State                                                        */
/* ------------------------------------------------------------------ */

struct K230GSDMAState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion mmio;

    /* Flat register array covering 4KB of GSDMA registers */
    uint32_t regs[0x100];
};

#endif /* K230_GSDMA_H */
