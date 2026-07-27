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
/* Device State                                                        */
/* ------------------------------------------------------------------ */

struct K230SdhciState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion container;         /* top-level container (0x1000 bytes) */
    SDHCIState sdhci;               /* embedded standard SDHCI child */
    BusState *bus;                  /* sd-bus from child (for SD card attach) */
};

#endif /* K230_SDHCI_H */
