/*
 * K230 GSDMA (General System DMA) — Device Model Header
 *
 * The K230 GSDMA at 0x80800000 combines GDMA (graphics DMA, 1 channel)
 * and SDMA (system DMA, 4 channels) behind a shared AXI master.
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
#include "qemu/timer.h"
#include "qom/object.h"

#define TYPE_K230_GSDMA "riscv.k230.gsdma"
OBJECT_DECLARE_SIMPLE_TYPE(K230GSDMAState, K230_GSDMA)

#define K230_GSDMA_MMIO_SIZE 0x4000

/* ------------------------------------------------------------------ */
/* Register Offsets                                                    */
/* ------------------------------------------------------------------ */

#define K230_GSDMA_CH_EN        0x00  /* channel enable (RW) */
#define K230_GSDMA_INT_MASK     0x04  /* interrupt mask (RW) */
#define K230_GSDMA_INT_STAT     0x08  /* interrupt status (W1C) */
#define K230_GSDMA_DMA_CFG      0x0C  /* DMA config (RW) */

#define K230_GSDMA_GDMA_CTRL      0x10  /* GDMA control (WO1) */
#define K230_GSDMA_LLI_BASE       0x14  /* GDMA LLI base addr (RW) */
#define K230_GSDMA_CH0_CNT        0x18
#define K230_GSDMA_CH1_CNT        0x1C
#define K230_GSDMA_CH2_CNT        0x20
#define K230_GSDMA_CH3_CNT        0x24
#define K230_GSDMA_CH4_CNT        0x28
#define K230_GSDMA_CH5_CNT        0x2C
#define K230_GSDMA_CH6_CNT        0x30
#define K230_GSDMA_CH7_CNT        0x34
#define K230_GSDMA_CURRENT_LLT    0x38  /* current LLI addr (RO) */
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
#define K230_GSDMA_INT_CHX_LLT_PAUSE(x)   BIT(8 + (x))
#define K230_GSDMA_INT_CHX_LLT_ITEM(x)    BIT(4 + (x))
#define K230_GSDMA_INT_CHX_TR_DONE(x)     BIT(0 + (x))

/* GDMA_CTRL */
#define K230_GSDMA_GDMA_RESUME    BIT(2)
#define K230_GSDMA_GDMA_STOP      BIT(1)
#define K230_GSDMA_GDMA_START     BIT(0)

/* DMA_CFG reset default */
#define K230_GSDMA_CFG_RESET      0x000007FF

/* ------------------------------------------------------------------ */
/* GDMA LLI Descriptor (24 bytes, 6 × uint32_t)                        */
/* ------------------------------------------------------------------ */

enum {
    GSDMA_LLI_CTRL          = 0,
    GSDMA_LLI_SRC_ADDR      = 4,
    GSDMA_LLI_WIDTH_HEIGHT  = 8,
    GSDMA_LLI_SRC_DST_STRIDE = 12,
    GSDMA_LLI_DST_ADDR      = 16,
    GSDMA_LLI_NEXT          = 20,
    GSDMA_LLI_SIZE          = 24,
};

/* LLI ctrl field bits */
#define GSDMA_LLI_ROTATION_MASK      3
#define GSDMA_LLI_X_MIRROR           BIT(2)
#define GSDMA_LLI_Y_MIRROR           BIT(3)
#define GSDMA_LLI_PIXEL_WIDTH_SHIFT  8
#define GSDMA_LLI_PIXEL_WIDTH_MASK   (3 << 8)
#define GSDMA_LLI_CH_ID_SHIFT        16
#define GSDMA_LLI_CH_ID_MASK         (7 << 16)
#define GSDMA_LLI_PAUSE_ENABLE       BIT(29)
#define GSDMA_LLI_INTERRUPT_ENABLE   BIT(30)

/* LLI width_height field */
#define GSDMA_LLI_WIDTH_MASK         0xFFFF
#define GSDMA_LLI_HEIGHT_SHIFT       16
#define GSDMA_LLI_HEIGHT_MASK        (0xFFFF << 16)

/* LLI src_dst_stride field */
#define GSDMA_LLI_SRC_STRIDE_MASK    0xFFFF
#define GSDMA_LLI_DST_STRIDE_SHIFT   16
#define GSDMA_LLI_DST_STRIDE_MASK    (0xFFFF << 16)

/* ------------------------------------------------------------------ */
/* Device State                                                        */
/* ------------------------------------------------------------------ */

struct K230GSDMAState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion mmio;
    qemu_irq irq;

    /* Flat register array (4KB / 4 = 256 words) */
    uint32_t regs[0x100];

    /*
     * Phase 2 GDMA state — accumulated interrupt flags not yet
     * reflected in regs[DMA_INT_STAT].
     */
    uint32_t pending_int;

    /*
     * Phase 3 async transfer state
     */
    QEMUTimer *transfer_timer;
    bool gdma_running;          /* true while LLI chain is being processed */
    hwaddr next_lli_addr;       /* next descriptor to process */
    uint32_t lli_buffer[6];     /* currently executing descriptor */
    int lli_nodes_done;         /* nodes completed so far (for CHx_CNT) */
};

#endif /* K230_GSDMA_H */
