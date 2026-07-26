/*
 * K230 SPI / QSPI Controller (Synopsys DesignWare SSI)
 *
 * K230 Technical Reference Manual V0.3.1 (2024-11-18), Chapter 12.3:
 * https://github.com/revyos/external-docs/blob/master/K230/en-us/K230_Technical_Reference_Manual_V0.3.1_20241118.pdf
 *
 * The K230 integrates three instances of the DW APB SSI core:
 *   - SSI0 (QSPI0): 0x91582000  — XIP-capable, connected to FMC for flash boot
 *   - SSI1 (QSPI1): 0x91583000  — second QSPI channel via FMC
 *   - SSI2 (SPI):   0x91584000  — general-purpose SPI master
 *
 * This model implements register-level MMIO compatibility sufficient for
 * the Linux dw_spi_mmio driver to probe and perform PIO data transfers
 * via the SSI bus to a m25p80 flash slave device.  DMA engine, XIP mode
 * and DDR/Octal modes are not yet implemented.
 *
 * Copyright (c) 2026 The QEMU K230 Camp Contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef K230_SPI_H
#define K230_SPI_H

#include "hw/core/sysbus.h"
#include "hw/ssi/ssi.h"
#include "qom/object.h"

#define TYPE_K230_SPI "riscv.k230.spi"
OBJECT_DECLARE_SIMPLE_TYPE(K230SpiState, K230_SPI)

#define K230_SPI_MMIO_SIZE 0x1000

/*
 * DW SSI Standard Registers (0x00 – 0x5c) and K230 extensions
 * (0xf0 – 0x134).  Offset addresses per TRM Table 12-3-1.
 */
enum K230SpiRegisters {
    /* --- DW SSI Standard Registers --- */
    K230_SPI_CTRLR0          = 0x00,   /* Control Register 0           */
    K230_SPI_CTRLR1          = 0x04,   /* Control Register 1           */
    K230_SPI_SSIENR          = 0x08,   /* SSI Enable Register          */
    K230_SPI_MWCR            = 0x0c,   /* Microwire Control Register   */
    K230_SPI_SER             = 0x10,   /* Slave Enable Register        */
    K230_SPI_BAUDR           = 0x14,   /* Baud Rate Select             */
    K230_SPI_TXFTLR          = 0x18,   /* Transmit FIFO Threshold      */
    K230_SPI_RXFTLR          = 0x1c,   /* Receive FIFO Threshold       */
    K230_SPI_TXFLR           = 0x20,   /* Transmit FIFO Level          */
    K230_SPI_RXFLR           = 0x24,   /* Receive FIFO Level           */
    K230_SPI_SR              = 0x28,   /* Status Register              */
    K230_SPI_IMR             = 0x2c,   /* Interrupt Mask Register      */
    K230_SPI_ISR             = 0x30,   /* Interrupt Status Register    */
    K230_SPI_RISR            = 0x34,   /* Raw Interrupt Status         */
    K230_SPI_TXOICR          = 0x38,   /* TX FIFO Overflow IC          */
    K230_SPI_RXOICR          = 0x3c,   /* RX FIFO Overflow IC          */
    K230_SPI_RXUICR          = 0x40,   /* RX FIFO Underflow IC         */
    K230_SPI_MSTICR          = 0x44,   /* Multi-Master IC              */
    K230_SPI_ICR             = 0x48,   /* Interrupt Clear Register     */
    K230_SPI_DMACR           = 0x4c,   /* DMA Control Register         */
    K230_SPI_DMATDLR         = 0x50,   /* DMA Transmit Data Level      */
    K230_SPI_DMARDLR         = 0x54,   /* DMA Receive Data Level       */
    K230_SPI_IDR             = 0x58,   /* Identification Register      */
    K230_SPI_SSIC_VERSION_ID = 0x5c,   /* Component Version            */
    K230_SPI_DR_BASE         = 0x60,   /* Data Register 0 (DR0-DR35)   */

    /* --- K230 Extension Registers --- */
    K230_SPI_RX_SAMPLE_DELAY = 0xf0,   /* RX Sample Delay              */
    K230_SPI_SPI_CTRLR0      = 0xf4,   /* SPI Control Register 0       */
    K230_SPI_DDR_DRIVE_EDGE  = 0xf8,   /* DDR Drive Edge               */
    K230_SPI_XRXOICR         = 0x110,  /* XIP RX Overflow IC           */
    K230_SPI_XIP_CNT_TIME_OUT = 0x114, /* XIP Count Time-out           */
    K230_SPI_SPI_CTRLR1      = 0x118,  /* SPI Control Register 1       */
    K230_SPI_SPITECR         = 0x11c,  /* SPI TX Error IC              */
    K230_SPI_SPIDR           = 0x120,  /* SPI Device Register          */
    K230_SPI_SPIAR           = 0x124,  /* SPI Device Address Register  */
    K230_SPI_AXIAR0          = 0x128,  /* AXI Address Register 0       */
    K230_SPI_AXIAR1          = 0x12c,  /* AXI Address Register 1       */
    K230_SPI_AXIECR          = 0x130,  /* AXI Error IC                 */
    K230_SPI_DONECR          = 0x134,  /* Transfer Done IC             */
};

/*
 * CTRLR0 (offset 0x00) — Reset value 0x00004007
 */
#define K230_SPI_CTRLR0_SSI_IS_MST   BIT(31)
#define K230_SPI_CTRLR0_SPI_FRF_MASK 0x00c00000
#define K230_SPI_CTRLR0_SPI_FRF_SHIFT 22
#define K230_SPI_CTRLR0_SPI_FRF_STD  0x0   /* Standard SPI */
#define K230_SPI_CTRLR0_SPI_FRF_DUAL 0x1   /* Dual SPI     */
#define K230_SPI_CTRLR0_SPI_FRF_QUAD 0x2   /* Quad SPI     */
#define K230_SPI_CTRLR0_SPI_FRF_OCTAL 0x3  /* Octal SPI    */
#define K230_SPI_CTRLR0_CFS_MASK     0x000f0000
#define K230_SPI_CTRLR0_SSTE         BIT(14)
#define K230_SPI_CTRLR0_TMOD_MASK    0x00000c00
#define K230_SPI_CTRLR0_TMOD_SHIFT   10
#define K230_SPI_CTRLR0_TMOD_TX_RX   0x0
#define K230_SPI_CTRLR0_TMOD_TX_ONLY 0x1
#define K230_SPI_CTRLR0_TMOD_RX_ONLY 0x2
#define K230_SPI_CTRLR0_TMOD_EEPROM  0x3
#define K230_SPI_CTRLR0_SCPOL        BIT(9)
#define K230_SPI_CTRLR0_SCPH         BIT(8)
#define K230_SPI_CTRLR0_DFS_MASK     0x0000001f

/* CTRLR0 reset default */
#define K230_SPI_CTRLR0_RESET        0x00004007

/*
 * SSIENR (offset 0x08) — Reset value 0x00000000
 */
#define K230_SPI_SSIENR_EN           BIT(0)

/*
 * BAUDR (offset 0x14) — Reset value 0x00000000
 *   SCKDV[15:0]   clock divider (even values 2..65534)
 */
#define K230_SPI_BAUDR_SCKDV_MASK    0x0000fffe

/*
 * SR — Status Register (offset 0x28) — Reset value 0x00000006
 *   We return TFNF(bit2)=1 + TFE(bit1)=1, i.e. TX FIFO empty and not full,
 *   which is the safe idle state that prevents probe busy-wait loops.
 */
#define K230_SPI_SR_BUSY             BIT(0)
#define K230_SPI_SR_TFE              BIT(1)
#define K230_SPI_SR_TFNF             BIT(2)
#define K230_SPI_SR_TXE              BIT(3)
#define K230_SPI_SR_RFNE             BIT(4)
#define K230_SPI_SR_RFF              BIT(5)
#define K230_SPI_SR_RXFE             BIT(6)
#define K230_SPI_SR_DCOL             BIT(7)

/* Safe idle value: TX FIFO empty + not full, all status clear */
#define K230_SPI_SR_IDLE             0x00000006

/*
 * ISR — Interrupt Status Register (offset 0x30)
 * Bit 4: RXFI — RX FIFO interrupt (data available), per DW SSI spec
 */
#define K230_SPI_ISR_RXFI            BIT(4)

/*
 * IDR — Identification Register (offset 0x58) — constant
 */
#define K230_SPI_IDR_VAL             0xa1b2c3d5

/*
 * SSIC_VERSION_ID (offset 0x5c) — constant
 *   ASCII "1.03*" = 0x3130332a
 */
#define K230_SPI_SSIC_VERSION_ID_VAL 0x3130332a

/*
 * TXFLR / RXFLR (offsets 0x20 / 0x24) — reads return 0 (FIFOs idle)
 */

struct K230SpiState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion mmio;
    qemu_irq irq;

    /* --- DW SSI Standard Registers --- */
    uint32_t ctrlr0;          /* 0x00 */
    uint32_t ctrlr1;          /* 0x04 */
    uint32_t ssienr;          /* 0x08 */
    uint32_t mwcr;            /* 0x0c */
    uint32_t ser;             /* 0x10 */
    uint32_t baudr;           /* 0x14 */
    uint32_t txftlr;          /* 0x18 */
    uint32_t rxftlr;          /* 0x1c */
    uint32_t txflr;           /* 0x20 — read-only, FIFO level */
    uint32_t rxflr;           /* 0x24 — read-only, FIFO level */
    uint32_t sr;              /* 0x28 — read-only (mostly) */
    uint32_t imr;             /* 0x2c */
    uint32_t isr;             /* 0x30 */
    uint32_t risr;            /* 0x34 — read-only */
    uint32_t txoicr;          /* 0x38 — WO clear */
    uint32_t rxoicr;          /* 0x3c — WO clear */
    uint32_t rxuicr;          /* 0x40 — WO clear */
    uint32_t msticr;          /* 0x44 — WO clear */
    uint32_t icr;             /* 0x48 — WO clear */
    uint32_t dmacr;           /* 0x4c */
    uint32_t dmatdlr;         /* 0x50 */
    uint32_t dmardlr;         /* 0x54 */

    /* --- K230 Extension Registers --- */
    uint32_t rx_sample_delay;     /* 0xf0 */
    uint32_t spi_ctrlr0;          /* 0xf4 */
    uint32_t ddr_drive_edge;      /* 0xf8 */
    uint32_t xrxoicr;             /* 0x110 — WO clear */
    uint32_t xip_cnt_time_out;    /* 0x114 */
    uint32_t spi_ctrlr1;          /* 0x118 */
    uint32_t spitecr;             /* 0x11c — WO clear */
    uint32_t spidr;               /* 0x120 */
    uint32_t spiar;               /* 0x124 */
    uint32_t axiar0;              /* 0x128 */
    uint32_t axiar1;              /* 0x12c */
    uint32_t axiecr;              /* 0x130 — WO clear */
    uint32_t donecr;              /* 0x134 — WO clear */

    /* SSI bus + RX FIFO (256 deep, matching reported FIFO size) */
    SSIBus *spi_bus;              /* SSI bus for slave device attachment */
    uint32_t rx_fifo[256];        /* RX FIFO: stores ssi_transfer responses */
    uint32_t rx_fifo_count;       /* Number of valid entries in rx_fifo */

    /* TX FIFO shadow — always reports empty in current model */
    uint32_t tx_fifo_count;       /* TX FIFO level (always 0) */

    /* NDF burst tracking: true between SSIENR enable and first DR write */
    bool ndf_pending;
};

#endif /* K230_SPI_H */
