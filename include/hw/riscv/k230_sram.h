/*
 * K230 SRAM Controller
 *
 * K230 Technical Reference Manual V0.3.1 (2024-11-18):
 * https://github.com/revyos/external-docs/blob/master/K230/en-us/K230_Technical_Reference_Manual_V0.3.1_20241118.pdf
 *
 * The K230 shared SRAM (2 MB at 0x80200000) has no software-visible
 * controller registers: it is a pure on-chip RAM block accessed directly
 * via the AXI bus.  Clock gating (CMU at 0x91100000, offset 0x5c) and
 * reset control (RMU at 0x91101000, offsets 0x60/0x64) are handled by
 * separate system-controller peripherals and are not modelled here yet.
 *
 * Copyright (c) 2026 The QEMU K230 Camp Contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef K230_SRAM_H
#define K230_SRAM_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_K230_SRAM "riscv.k230.sram"
OBJECT_DECLARE_SIMPLE_TYPE(K230SramState, K230_SRAM)

struct K230SramState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion sram;          /* 2 MB SRAM storage */
};

#endif /* K230_SRAM_H */
