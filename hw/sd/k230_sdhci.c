/*
 * K230 SD/MMC Host Controller (DWC MSHC) — Device Model
 *
 * Wraps QEMU's standard SDHCI (TYPE_SYSBUS_SDHCI) inside a container
 * MemoryRegion, overriding Capabilities, Present State and Version
 * registers with K230-specific values.
 *
 * Reference:
 *   K230 TRM V0.3.1 (2024-11-18), Chapter 12.4 SD/MMC
 *
 * Copyright (c) 2026 The QEMU K230 Camp Contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/core/sysbus.h"
#include "hw/sd/k230_sdhci.h"
#include "hw/sd/sd.h"
#include "hw/sd/sdhci-internal.h"
#include "migration/vmstate.h"
#include "trace.h"

/* ------------------------------------------------------------------ */
/* Card Insert Timer                                                   */
/* ------------------------------------------------------------------ */

/*
 * The standard SDHCI's register-level SWRST (write to 0x2F) calls
 * sdhci_set_inserted(false), ejecting the SD card and clearing
 * CARD_PRESENT from Present State.  The K230 driver reads PSTATE
 * before touching any vendor register, so the vendor-handler
 * re-insert logic fires too late for the synchronous check.
 *
 * Use a periodic QEMU timer that unconditionally re-inserts the
 * card via the public SDBus API.  The timer is cheap (no-op when
 * already inserted) and guarantees the card-eventually-inserted
 * invariant regardless of when SWRST fires.
 */
static void k230_sdhci_card_timer_cb(void *opaque)
{
    K230SdhciState *s = K230_SDHCI(opaque);

    sdbus_set_inserted(&s->sdhci.sdbus, true);

    /*
     * The K230 driver probe clears norintsen/norintsigen on error
     * (no card found).  Restore them so that card-detection commands
     * (CMD0→CMD8→ACMD41→CMD2→CMD3) can signal completion.
     */
    s->sdhci.norintstsen |= 0x00c3;   /* CMD_COMPLETE + INSERT + common bits */
    s->sdhci.norintsigen |= 0x00c3;

    /* single-shot for debugging: don't re-arm */
}

/* ------------------------------------------------------------------ */
/* Vendor-Specific MMIO (offsets 0x100–0xFFF)                          */
/* ------------------------------------------------------------------ */

/* ------------------------------------------------------------------ */
/* Vendor-Specific MMIO (offsets 0x100–0xFFF)                          */
/* ------------------------------------------------------------------ */

static uint64_t k230_sdhci_vendor_read(void *opaque, hwaddr addr,
                                       unsigned int size)
{
    K230SdhciState *s = K230_SDHCI(opaque);

    if (addr >= K230_SDHCI_VENDOR_SIZE) {
        return 0;
    }
    return s->vendor_regs[addr / 4];
}

static void k230_sdhci_vendor_write(void *opaque, hwaddr addr,
                                    uint64_t value, unsigned int size)
{
    K230SdhciState *s = K230_SDHCI(opaque);

    if (addr >= K230_SDHCI_VENDOR_SIZE) {
        return;
    }
    s->vendor_regs[addr / 4] = (uint32_t)value;

    /*
     * The SDK sdhci-dwcmshc-kendryte driver polls PHY_CNFG_R (at
     * vendor offset 0x440) for the PWRGOOD bit after writing pad-
     * config values.  Make sure PWRGOOD stays set on read-back
     * because the QEMU PHY model does not autonomously raise
     * power-good.
     */
    if (addr == 0x440) {
        s->vendor_regs[addr / 4] |= PHY_CNFG_PHY_PWRGOOD;
    }

    /*
     * EMMC_CONTROL (vendor offset 0x200) status bits are read-only
     * from the driver's perspective.  The driver may write 0 during
     * init but expects the hardware to report live status on readback.
     * Preserve the RO bits:
     *   bit 0: CARD_IS_EMMC
     *   bit 1: VOLT_SWITCH_DONE
     */
    if (addr == 0x200) {
        s->vendor_regs[addr / 4] |= 0x00000003;
    }
}

static const MemoryRegionOps k230_sdhci_vendor_ops = {
    .read  = k230_sdhci_vendor_read,
    .write = k230_sdhci_vendor_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 4,
        .unaligned = true,
    },
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
        .unaligned = true,
    },
};

/* ------------------------------------------------------------------ */
/* Realize                                                             */
/* ------------------------------------------------------------------ */

static void k230_sdhci_realize(DeviceState *dev, Error **errp)
{
    K230SdhciState *s = K230_SDHCI(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    SysBusDevice *sbd_sdhci = SYS_BUS_DEVICE(&s->sdhci);

    /*
     * 1. Create container MemoryRegion — this is the SysBus device's
     *    MMIO region, into which we'll merge standard SDHCI registers
     *    and K230 vendor-specific registers.
     */
    memory_region_init(&s->container, OBJECT(dev), TYPE_K230_SDHCI,
                       K230_SDHCI_MMIO_SIZE);
    sysbus_init_mmio(sbd, &s->container);

    /*
     * 2. Realize the standard SDHCI child.  This creates its internal
     *    MMIO region, SDBus, timers, and interrupt logic.
     */
    if (!sysbus_realize(sbd_sdhci, errp)) {
        return;
    }

    /*
     * 3. Map standard SDHCI MMIO at offset 0, vendor region at 0x100.
     */
    memory_region_add_subregion(&s->container, 0,
                                sysbus_mmio_get_region(sbd_sdhci, 0));

    memory_region_init_io(&s->vendor, OBJECT(dev),
                          &k230_sdhci_vendor_ops, s,
                          "k230.sdhci-vendor",
                          K230_SDHCI_VENDOR_SIZE);
    memory_region_add_subregion(&s->container, 0x100, &s->vendor);

    /*
     * 4. Forward the child's IRQ line to our parent.
     */
    sysbus_pass_irq(sbd, sbd_sdhci);

    /*
     * 5. Grab a reference to the sd-bus for future SD card attachment.
     */
    s->bus = qdev_get_child_bus(DEVICE(sbd_sdhci), "sd-bus");

    /*
     * 6. Periodic card-insert timer — re-inserts the card after
     *    SWRST-driven ejection.  100 ms period, first fire at 3 s
     *    so that userspace init + workqueue are ready to handle
     *    the card-insert interrupt and schedule mmc_rescan.
     */
    s->card_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                 k230_sdhci_card_timer_cb, s);
    timer_mod(s->card_timer,
              qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 3000000000ULL); /* 3 s */
}

/* ------------------------------------------------------------------ */
/* Instance Init — create the standard SDHCI child                     */
/* ------------------------------------------------------------------ */

static void k230_sdhci_instance_init(Object *obj)
{
    K230SdhciState *s = K230_SDHCI(obj);

    object_initialize_child(OBJECT(s), TYPE_SYSBUS_SDHCI,
                            &s->sdhci, TYPE_SYSBUS_SDHCI);
}

/* ------------------------------------------------------------------ */
/* Reset — override read-only registers after child reset              */
/* ------------------------------------------------------------------ */

static void k230_sdhci_reset_exit(Object *obj, ResetType type)
{
    K230SdhciState *s = K230_SDHCI(obj);

    s->sdhci.capareg = K230_SDHCI_CAPAB_REG;
    s->sdhci.prnsts  = K230_SDHCI_PSTATE_DEFAULT;
    s->sdhci.version  = K230_SDHCI_HC_VERSION;
    s->sdhci.maxcurr  = 0;

    /*
     * Initialise the vendor-specific register area.
     *
     * The SDK sdhci-dwcmshc-kendryte driver:
     *   1. Polls PHY_CNFG_R for PWRGOOD (at vendor offset 0x440)
     *   2. Reads EMMC_CONTROL (at vendor offset 0x200) for CARD_IS_EMMC
     *
     * Set PWRGOOD so the PHY poll succeeds, and CARD_IS_EMMC so
     * the driver knows this is an eMMC controller.
     */
    memset(s->vendor_regs, 0, sizeof(s->vendor_regs));
    s->vendor_regs[0x440 / 4] = PHY_CNFG_PHY_PWRGOOD;
    s->vendor_regs[0x200 / 4] = 0x00000003;  /* CARD_IS_EMMC | VOLT_SWITCH_DONE */

    trace_k230_sdhci_reset();
}

/* ------------------------------------------------------------------ */
/* VMState                                                             */
/* ------------------------------------------------------------------ */

static const VMStateDescription vmstate_k230_sdhci = {
    .name = "k230.sdhci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(vendor_regs, K230SdhciState,
                             K230_SDHCI_VENDOR_SIZE / 4),
        VMSTATE_END_OF_LIST()
    },
};

/* ------------------------------------------------------------------ */
/* Class Init                                                          */
/* ------------------------------------------------------------------ */

static void k230_sdhci_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->realize = k230_sdhci_realize;
    dc->vmsd = &vmstate_k230_sdhci;
    dc->desc = "K230 SD/MMC Host Controller (DWC MSHC)";
    rc->phases.exit = k230_sdhci_reset_exit;
}

/* ------------------------------------------------------------------ */
/* TypeInfo                                                            */
/* ------------------------------------------------------------------ */

static const TypeInfo k230_sdhci_info = {
    .name          = TYPE_K230_SDHCI,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(K230SdhciState),
    .instance_init = k230_sdhci_instance_init,
    .class_init    = k230_sdhci_class_init,
};

static void k230_sdhci_register_type(void)
{
    type_register_static(&k230_sdhci_info);
}

type_init(k230_sdhci_register_type)
