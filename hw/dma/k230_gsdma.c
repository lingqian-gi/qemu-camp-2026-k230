/*
 * K230 GSDMA (General System DMA) — Device Model
 *
 * Phase 1: flat register array with correct reset defaults.
 * All registers accept 32-bit reads and writes.
 *
 * Reference:
 *   K230 TRM V0.3.1 (2024-11-18), Chapter 10.2.3 GSDMA
 *
 * Copyright (c) 2026 The QEMU K230 Camp Contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/core/sysbus.h"
#include "hw/dma/k230_gsdma.h"
#include "migration/vmstate.h"
#include "trace.h"

/* ------------------------------------------------------------------ */
/* Reset                                                               */
/* ------------------------------------------------------------------ */

static void k230_gsdma_reset_hold(Object *obj, ResetType type)
{
    K230GSDMAState *s = K230_GSDMA(obj);

    memset(s->regs, 0, sizeof(s->regs));

    /* DMA_CFG reset value per TRM */
    s->regs[K230_GSDMA_DMA_CFG / 4] = K230_GSDMA_CFG_RESET;
}

/* ------------------------------------------------------------------ */
/* MMIO Read                                                           */
/* ------------------------------------------------------------------ */

static uint64_t k230_gsdma_read(void *opaque, hwaddr addr,
                                unsigned int size)
{
    K230GSDMAState *s = K230_GSDMA(opaque);

    if (addr >= sizeof(s->regs)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: read out of bounds addr=0x%" HWADDR_PRIx "\n",
                      __func__, addr);
        return 0;
    }

    return s->regs[addr / 4];
}

/* ------------------------------------------------------------------ */
/* MMIO Write                                                          */
/* ------------------------------------------------------------------ */

static void k230_gsdma_write(void *opaque, hwaddr addr,
                             uint64_t value, unsigned int size)
{
    K230GSDMAState *s = K230_GSDMA(opaque);

    if (addr >= sizeof(s->regs)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: write out of bounds addr=0x%" HWADDR_PRIx "\n",
                      __func__, addr);
        return;
    }

    s->regs[addr / 4] = (uint32_t)value;
}

/* ------------------------------------------------------------------ */
/* Memory Region Ops                                                   */
/* ------------------------------------------------------------------ */

static const MemoryRegionOps k230_gsdma_ops = {
    .read  = k230_gsdma_read,
    .write = k230_gsdma_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false,
    },
};

/* ------------------------------------------------------------------ */
/* Realize                                                             */
/* ------------------------------------------------------------------ */

static void k230_gsdma_realize(DeviceState *dev, Error **errp)
{
    K230GSDMAState *s = K230_GSDMA(dev);

    memory_region_init_io(&s->mmio, OBJECT(dev), &k230_gsdma_ops, s,
                          TYPE_K230_GSDMA, K230_GSDMA_MMIO_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->mmio);
}

/* ------------------------------------------------------------------ */
/* VMState                                                             */
/* ------------------------------------------------------------------ */

static const VMStateDescription vmstate_k230_gsdma = {
    .name = "k230.gsdma",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, K230GSDMAState, 0x100),
        VMSTATE_END_OF_LIST()
    },
};

/* ------------------------------------------------------------------ */
/* Class Init                                                          */
/* ------------------------------------------------------------------ */

static void k230_gsdma_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->realize = k230_gsdma_realize;
    dc->vmsd = &vmstate_k230_gsdma;
    dc->desc = "K230 GSDMA (General System DMA)";
    rc->phases.hold = k230_gsdma_reset_hold;
}

/* ------------------------------------------------------------------ */
/* TypeInfo                                                            */
/* ------------------------------------------------------------------ */

static const TypeInfo k230_gsdma_info = {
    .name          = TYPE_K230_GSDMA,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(K230GSDMAState),
    .class_init    = k230_gsdma_class_init,
};

static void k230_gsdma_register_type(void)
{
    type_register_static(&k230_gsdma_info);
}

type_init(k230_gsdma_register_type)
