/*
 * K230 SRAM Controller
 *
 * K230 Technical Reference Manual V0.3.1 (2024-11-18):
 * https://github.com/revyos/external-docs/blob/master/K230/en-us/K230_Technical_Reference_Manual_V0.3.1_20241118.pdf
 *
 * The K230 shared SRAM (2 MB at 0x80200000) has no software-visible
 * controller registers.  This device wraps the SRAM as a SysBusDevice
 * so that it appears in the QOM tree, supports migration (VMState),
 * and can be introspected by management tools.
 *
 * Clock gating is controlled by the CMU at 0x91100000 (shrm_CLK_CFG,
 * offset 0x5c, bit 10: sram_aclk_enable).  Reset is controlled by the
 * RMU at 0x91101000 (SRAM_RST_TIM/SRAM_RST_CTL, offsets 0x60/0x64).
 * Those peripherals are not modelled yet, so SRAM is always enabled
 * in the current QEMU implementation.
 *
 * Copyright (c) 2026 The QEMU K230 Camp Contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "hw/core/sysbus.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "hw/riscv/k230_sram.h"

static void k230_sram_realize(DeviceState *dev, Error **errp)
{
    K230SramState *s = K230_SRAM(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    memory_region_init_ram(&s->sram, OBJECT(dev), "k230.sram",
                           2 * MiB, &error_fatal);
    sysbus_init_mmio(sbd, &s->sram);
}

static void k230_sram_reset(DeviceState *dev)
{
    /*
     * No software-visible registers to reset.  SRAM content is preserved
     * across warm reset on real hardware; a cold reset would clear it,
     * but QEMU memory_region_init_ram already zeroes the region on init.
     */
}

static const VMStateDescription vmstate_k230_sram = {
    .name = "k230.sram",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_END_OF_LIST()
    },
};

static void k230_sram_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = k230_sram_realize;
    device_class_set_legacy_reset(dc, k230_sram_reset);
    dc->vmsd = &vmstate_k230_sram;
    dc->desc = "K230 SRAM";
}

static const TypeInfo k230_sram_info = {
    .name          = TYPE_K230_SRAM,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(K230SramState),
    .class_init    = k230_sram_class_init,
};

static void k230_sram_register_type(void)
{
    type_register_static(&k230_sram_info);
}

type_init(k230_sram_register_type)
