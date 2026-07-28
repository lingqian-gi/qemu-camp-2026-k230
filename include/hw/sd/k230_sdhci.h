/*
 * K230 SD/MMC Host Controller (DWC MSHC)
 *
 * The K230 integrates a Synopsys DesignWare Cores Mobile Storage Host
 * Controller (DWC MSHC), compatible with the SD Host Controller Standard
 * Specification v4.  Each of the two instances supports eMMC / SD / SDIO.
 *
 * This model wraps QEMU's standard SDHCI (TYPE_SYSBUS_SDHCI) inside a
 * container MemoryRegion, overriding Capabilities, Present State and
 * Version registers with K230-specific values.
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
#include "hw/sd/sdhci.h"
#include "qemu/timer.h"
#include "qom/object.h"

#define TYPE_K230_SDHCI "riscv.k230.sdhci"
OBJECT_DECLARE_SIMPLE_TYPE(K230SdhciState, K230_SDHCI)

#define K230_SDHCI_MMIO_SIZE 0x1000

/* ------------------------------------------------------------------ */
/* Capabilities Register (64-bit) — K230-specific values               */
/* ------------------------------------------------------------------ */

/*
 * Low 32 bits (0x40):
 *   TOCLKFREQ=52, BASECLKFREQ=52 MHz, MAXBLOCKLENGTH=512 bytes
 *   EMBEDDED_8BIT, ADMA2, HIGHSPEED, SDMA, SUSPRESUME
 *   V33, V30, V18, BUS64BIT
 * High 32 bits (0x44): 0 (no UHS-II, no ADMA3)
 */
#define K230_SDHCI_CAPAB_LO   0x057834b4
#define K230_SDHCI_CAPAB_HI   0x00000000
#define K230_SDHCI_CAPAB_REG  \
    (((uint64_t)K230_SDHCI_CAPAB_HI << 32) | K230_SDHCI_CAPAB_LO)

/* ------------------------------------------------------------------ */
/* Present State Register (0x24) — K230-specific default               */
/* ------------------------------------------------------------------ */

#define K230_SDHCI_PSTATE_CARD_INSERTED    BIT(16)
#define K230_SDHCI_PSTATE_CARD_STABLE      BIT(17)
#define K230_SDHCI_PSTATE_DEFAULT \
    (K230_SDHCI_PSTATE_CARD_INSERTED | K230_SDHCI_PSTATE_CARD_STABLE)

/* ------------------------------------------------------------------ */
/* Host Controller Version (0xFC bits [31:16])                         */
/* ------------------------------------------------------------------ */

#define K230_SDHCI_HC_VERSION   0x0004  /* SDHCI v4.0 */

/* ------------------------------------------------------------------ */
/* DWC MSHC Vendor-Specific Register Offsets (host-relative)          */
/* ------------------------------------------------------------------ */
/*
 * PHY registers (Synopsys DWC MSHC PHY block), relative to host MMIO.
 * The PHY block is accessed at host_base + DWC_MSHC_PTR_PHY_R + offset.
 */
#define DWC_MSHC_PTR_PHY_R         0x100

/* PHY configuration register (32-bit, at host_base + 0x110) */
#define PHY_CNFG_R_OFF             0x010  /* relative to DWC_MSHC_PTR_PHY_R */
#define PHY_CNFG_RSTN_DEASSERT     BIT(0)
#define PHY_CNFG_PHY_PWRGOOD       BIT(1)

/* PHY command/response pad config (16-bit each) */
#define PHY_CMDPAD_CNFG_R          0x01c
#define PHY_DATAPAD_CNFG_R         0x01e
#define PHY_CLKPAD_CNFG_R          0x020
#define PHY_RSTNPAD_CNFG_R         0x022
#define PHY_STBPAD_CNFG_R          0x024

/* PHY delay line config */
#define PHY_COMMDL_CNFG            0x11c
#define PHY_COMMDL_CNFG_DLSTEP_SEL BIT(0)
#define PHY_SDCLKDL_CNFG_R         0x11d
#define PHY_SDCLKDL_DC_R           0x11e
#define PHY_SMPLDL_CNFG_R          0x120
#define PHY_ATDL_CNFG_R            0x121

/* eMMC control / auto-tuning / host control */
#define DWCMSHC_EMMC_CONTROL       0x200
#define DWCMSHC_EMMC_ATCTRL        0x214
#define DWCMSHC_AT_STAT            0x215
#define DWCMSHC_HOST_CTRL3         0x21f

/* Default PHY_CNFG_R value: PWRGOOD=1, RSTN_DEASSERT=1, pad drive=default */
#define K230_SDHCI_PHY_CNFG_DEFAULT \
    (PHY_CNFG_PHY_PWRGOOD | PHY_CNFG_RSTN_DEASSERT | \
     (0x09 << 16) | (0x08 << 20))

/* ------------------------------------------------------------------ */
/* Device State                                                        */
/* ------------------------------------------------------------------ */

#define K230_SDHCI_VENDOR_SIZE    (K230_SDHCI_MMIO_SIZE - 0x100)

struct K230SdhciState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion container;         /* top-level container (0x1000 bytes) */
    MemoryRegion vendor;            /* vendor-specific area (0x100-0xFFF) */
    SDHCIState sdhci;               /* embedded standard SDHCI child */
    BusState *bus;                  /* sd-bus from child (for SD card attach) */
    QEMUTimer *card_timer;          /* periodic card-reinsert timer */

    /* Flat array backing the vendor-specific MMIO region (0xF00 bytes) */
    uint32_t vendor_regs[K230_SDHCI_VENDOR_SIZE / 4];
};

#endif /* K230_SDHCI_H */
