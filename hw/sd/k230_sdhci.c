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
#include "hw/sd/sdhci-internal.h"
#include "migration/vmstate.h"
#include "trace.h"

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
     *    and (in future phases) K230 vendor-specific registers.
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
     * 3. Map the standard SDHCI's MMIO region into our container at
     *    offset 0.  This means CPU accesses to 0x91580000+offset hit
     *    the standard SDHCI read/write dispatch (sdhci_read/sdhci_write)
     *    for offsets 0x00–0xFF.
     */
    memory_region_add_subregion(&s->container, 0,
                                sysbus_mmio_get_region(sbd_sdhci, 0));

    /*
     * 4. Forward the child's IRQ line to our parent.  The standard
     *    SDHCI raises/lowers its IRQ via qemu_set_irq; sysbus_pass_irq
     *    makes that appear as our own SysBus IRQ pin.
     */
    sysbus_pass_irq(sbd, sbd_sdhci);

    /*
     * 5. Grab a reference to the sd-bus for future SD card attachment
     *    (e.g. qdev_new("sd-card") → sdbus_reparent_card).
     */
    s->bus = qdev_get_child_bus(DEVICE(sbd_sdhci), "sd-bus");
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

/*
 * We override in the exit phase so that the child's hold phase
 * has already set its own defaults — we then overwrite the
 * K230-specific values on top.
 */
static void k230_sdhci_reset_exit(Object *obj, ResetType type)
{
    K230SdhciState *s = K230_SDHCI(obj);

    /* Override standard SDHCI defaults with K230-specific values */
    s->sdhci.capareg = K230_SDHCI_CAPAB_REG;
    s->sdhci.prnsts  = K230_SDHCI_PSTATE_DEFAULT;
    s->sdhci.version  = K230_SDHCI_HC_VERSION;
    s->sdhci.maxcurr  = 0;

    trace_k230_sdhci_reset();
}

/* ------------------------------------------------------------------ */
/* VMState                                                             */
/* ------------------------------------------------------------------ */

/*
 * All register state lives inside the child SDHCIState, which has
 * its own VMState registered by the QOM framework.  Our wrapper
 * has no additional state to migrate.
 */
static const VMStateDescription vmstate_k230_sdhci = {
    .name = "k230.sdhci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
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
