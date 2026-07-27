/*
 * K230 SD/MMC Host Controller (DWC MSHC)
 *
 * The K230 integrates a Synopsys DesignWare Cores Mobile Storage Host
 * Controller (DWC MSHC), compatible with the SD Host Controller Standard
 * Specification v4.  Each of the two instances supports eMMC / SD / SDIO.
 *
 * This model implements register-level MMIO compatibility sufficient for
 * the Linux sdhci-of driver to probe successfully.  Actual SD card command /
 * data transfer, ADMA2 descriptor engine and Command Queuing Engine (CQE)
 * are not implemented in this version.
 *
 * Reference:
 *   K230 TRM V0.3.1 (2024-11-18), Chapter 12.4 SD/MMC
 *
 * Copyright (c) 2026 The QEMU K230 Camp Contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef K230_SDHCI_H
#define K230_SDHCI_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_K230_SDHCI "riscv.k230.sdhci"
OBJECT_DECLARE_SIMPLE_TYPE(K230SdhciState, K230_SDHCI)

#define K230_SDHCI_MMIO_SIZE 0x1000

/* ------------------------------------------------------------------ */
/* Register Offsets (SD Host Controller Standard Spec v4)              */
/* ------------------------------------------------------------------ */

enum K230SdhciRegisters {
    K230_SDHCI_SDMASA              = 0x00, /* SDMA System Address */
    K230_SDHCI_BLOCKSIZE           = 0x04, /* Block Size (16-bit) */
    K230_SDHCI_BLOCKCOUNT          = 0x06, /* Block Count (16-bit) */
    K230_SDHCI_ARGUMENT            = 0x08, /* Argument */
    K230_SDHCI_XFER_MODE           = 0x0C, /* Transfer Mode (16-bit) */
    K230_SDHCI_CMD                 = 0x0E, /* Command (16-bit) */
    K230_SDHCI_RESPONSE            = 0x10, /* Response[0..3] (4 × 32-bit RO) */
    K230_SDHCI_BUF_DATA            = 0x20, /* Buffer Data Port */
    K230_SDHCI_PSTATE              = 0x24, /* Present State (RO) */
    K230_SDHCI_HOST_CTRL1          = 0x28, /* Host Control 1 (8-bit) */
    K230_SDHCI_PWR_CTRL            = 0x29, /* Power Control (8-bit) */
    K230_SDHCI_BLKGAP              = 0x2A, /* Block Gap Control (8-bit) */
    K230_SDHCI_WAKEUP_CTRL         = 0x2B, /* Wakeup Control (8-bit) */
    K230_SDHCI_CLK_CTRL            = 0x2C, /* Clock Control (16-bit) */
    K230_SDHCI_TOUT_CTRL           = 0x2E, /* Timeout Control (8-bit) */
    K230_SDHCI_SW_RST              = 0x2F, /* Software Reset (8-bit) */
    K230_SDHCI_NORMAL_INT_STAT     = 0x30, /* Normal Interrupt Status (R/W1C) */
    K230_SDHCI_ERROR_INT_STAT      = 0x32, /* Error Interrupt Status (R/W1C) */
    K230_SDHCI_NORMAL_INT_STAT_EN  = 0x34, /* Normal Int Status Enable */
    K230_SDHCI_ERROR_INT_STAT_EN   = 0x36, /* Error Int Status Enable */
    K230_SDHCI_NORMAL_INT_SIG_EN   = 0x38, /* Normal Int Signal Enable */
    K230_SDHCI_ERROR_INT_SIG_EN    = 0x3A, /* Error Int Signal Enable */
    K230_SDHCI_AUTO_CMD_STAT       = 0x3C, /* Auto CMD Status (RO) */
    K230_SDHCI_HOST_CTRL2          = 0x3E, /* Host Control 2 */
    K230_SDHCI_CAPAB               = 0x40, /* Capabilities (2 × 32-bit RO) */
    K230_SDHCI_MAX_CURR            = 0x48, /* Max Current (64-bit RO) */
    K230_SDHCI_ADMA_ERR_STAT       = 0x54, /* ADMA Error Status (RO) */
    K230_SDHCI_ADMA_ADDR_LO        = 0x58, /* ADMA System Address low */
    K230_SDHCI_ADMA_ADDR_HI        = 0x5C, /* ADMA System Address high */
    K230_SDHCI_SLOT_INT_STATUS     = 0xFC, /* Slot Interrupt Status + Version */
};

/* ------------------------------------------------------------------ */
/* Capabilities Register (64-bit, 0x40 / 0x44)                        */
/* ------------------------------------------------------------------ */

/*
 * Low 32 bits (0x40):
 *   TOCLKFREQ=52, BASECLKFREQ=52 MHz, MAXBLOCKLENGTH=512 bytes
 *   EMBEDDED_8BIT, ADMA2, HIGHSPEED, SDMA, SUSPRESUME
 *   V33, V30, V18, BUS64BIT
 * High 32 bits (0x44):
 *   CLOCK_MULT=4 (208 MHz), RETUNING_MODE=1, TIMER_RETUNING=4
 */
#define K230_SDHCI_CAPAB_LO   0x057834b4
#define K230_SDHCI_CAPAB_HI   0x00000000

/* ------------------------------------------------------------------ */
/* Present State Register (0x24) bit definitions                       */
/* ------------------------------------------------------------------ */

#define K230_SDHCI_PSTATE_CMD_INHIBIT      BIT(0)
#define K230_SDHCI_PSTATE_DAT_INHIBIT      BIT(1)
#define K230_SDHCI_PSTATE_CARD_INSERTED    BIT(16)
#define K230_SDHCI_PSTATE_CARD_STABLE      BIT(17)

/* Default: card inserted, power stable, no command/data in progress */
#define K230_SDHCI_PSTATE_DEFAULT \
    (K230_SDHCI_PSTATE_CARD_INSERTED | K230_SDHCI_PSTATE_CARD_STABLE)

/* ------------------------------------------------------------------ */
/* Slot Interrupt Status (0xFC)                                        */
/* ------------------------------------------------------------------ */

#define K230_SDHCI_SDHCI_VERSION      0x0004  /* SDHCI v4.0  */
#define K230_SDHCI_SLOT_INT_STATUS_VAL  \
    (K230_SDHCI_SDHCI_VERSION << 16)        /* bits [31:16] = version */

/* ------------------------------------------------------------------ */
/* Device State                                                        */
/* ------------------------------------------------------------------ */

struct K230SdhciState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion mmio;
    qemu_irq irq;

    /* --- SDHCI Standard Registers --- */
    uint32_t sdmasa;                /* 0x00 */
    uint16_t blksize;               /* 0x04 */
    uint16_t blkcnt;                /* 0x06 */
    uint32_t argument;              /* 0x08 */
    uint16_t xfer_mode;             /* 0x0C */
    uint16_t cmd;                   /* 0x0E */
    uint32_t response[4];           /* 0x10–0x1C */
    uint32_t buf_data;              /* 0x20 */
    uint8_t  host_ctrl1;            /* 0x28 */
    uint8_t  pwr_ctrl;              /* 0x29 */
    uint8_t  blkgap;                /* 0x2A */
    uint8_t  wakeup_ctrl;           /* 0x2B */
    uint16_t clk_ctrl;              /* 0x2C */
    uint8_t  tout_ctrl;             /* 0x2E */
    uint8_t  sw_rst;                /* 0x2F */

    /* Interrupt registers */
    uint16_t normal_int_stat;       /* 0x30 — R/W1C */
    uint16_t error_int_stat;        /* 0x32 — R/W1C */
    uint16_t normal_int_stat_en;    /* 0x34 */
    uint16_t error_int_stat_en;     /* 0x36 */
    uint16_t normal_int_sig_en;     /* 0x38 */
    uint16_t error_int_sig_en;      /* 0x3A */

    uint16_t host_ctrl2;            /* 0x3E */
    uint64_t capareg;               /* 0x40/0x44 — RO */
    uint64_t maxcurr;               /* 0x48/0x4C — RO */
    uint64_t adma_addr;             /* 0x58/0x5C */
};

#endif /* K230_SDHCI_H */
