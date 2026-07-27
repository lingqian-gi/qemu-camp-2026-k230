/*
 * K230 CMU (Clock Management Unit) — Device Model
 *
 * Phase 1: full 4KB register array with PLL lock/enable defaults.
 * All registers accept 32-bit reads and writes.
 *
 * Reference:
 *   K230 TRM V0.3.1 (2024-11-18), Chapter 2.2 System Controller
 *   drivers/clk/clk-k230.c (upstream Linux patch v12)
 *
 * Copyright (c) 2026 The QEMU K230 Camp Contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/core/sysbus.h"
#include "hw/misc/k230_cmu.h"
#include "migration/vmstate.h"
#include "trace.h"

/* ------------------------------------------------------------------ */
/* Reset                                                               */
/* ------------------------------------------------------------------ */

static void k230_cmu_reset_hold(Object *obj, ResetType type)
{
    K230CmuState *s = K230_CMU(obj);
    uint32_t i;

    trace_k230_cmu_reset();

    memset(s->regs, 0, sizeof(s->regs));

    /* Override PLL registers to return "locked + enabled" */
    for (i = 0; i < 4; i++) {
        uint32_t base = i * 0x10;
        s->regs[(base + K230_CMU_PLL_CTL)  / 4] = K230_CMU_PLL_CTL_DEFAULT;
        s->regs[(base + K230_CMU_PLL_STAT) / 4] = K230_CMU_PLL_STAT_DEFAULT;
    }
}

/* ------------------------------------------------------------------ */
/* MMIO Read                                                           */
/* ------------------------------------------------------------------ */

static uint64_t k230_cmu_read(void *opaque, hwaddr addr,
                              unsigned int size)
{
    K230CmuState *s = K230_CMU(opaque);
    uint32_t value;

    if (addr >= K230_CMU_MMIO_SIZE) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: read out of bounds addr=0x%" HWADDR_PRIx "\n",
                      __func__, addr);
        return 0;
    }

    value = s->regs[addr / 4];
    trace_k230_cmu_read(addr, value);
    return value;
}

/* ------------------------------------------------------------------ */
/* MMIO Write                                                          */
/* ------------------------------------------------------------------ */

static void k230_cmu_write(void *opaque, hwaddr addr,
                           uint64_t value, unsigned int size)
{
    K230CmuState *s = K230_CMU(opaque);

    trace_k230_cmu_write(addr, value);

    if (addr >= K230_CMU_MMIO_SIZE) {
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

static const MemoryRegionOps k230_cmu_ops = {
    .read  = k230_cmu_read,
    .write = k230_cmu_write,
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

static void k230_cmu_realize(DeviceState *dev, Error **errp)
{
    K230CmuState *s = K230_CMU(dev);

    memory_region_init_io(&s->mmio, OBJECT(dev), &k230_cmu_ops, s,
                          TYPE_K230_CMU, K230_CMU_MMIO_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->mmio);
}

/* ------------------------------------------------------------------ */
/* VMState                                                             */
/* ------------------------------------------------------------------ */

static const VMStateDescription vmstate_k230_cmu = {
    .name = "k230.cmu",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, K230CmuState, K230_CMU_MMIO_SIZE / 4),
        VMSTATE_END_OF_LIST()
    },
};

/* ------------------------------------------------------------------ */
/* Class Init                                                          */
/* ------------------------------------------------------------------ */

static void k230_cmu_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->realize = k230_cmu_realize;
    dc->vmsd = &vmstate_k230_cmu;
    dc->desc = "K230 CMU (Clock Management Unit)";
    rc->phases.hold = k230_cmu_reset_hold;
}

/* ------------------------------------------------------------------ */
/* TypeInfo                                                            */
/* ------------------------------------------------------------------ */

static const TypeInfo k230_cmu_info = {
    .name          = TYPE_K230_CMU,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(K230CmuState),
    .class_init    = k230_cmu_class_init,
};

static void k230_cmu_register_type(void)
{
    type_register_static(&k230_cmu_info);
}

type_init(k230_cmu_register_type)
