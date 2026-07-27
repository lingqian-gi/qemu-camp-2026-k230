/*
 * K230 GSDMA (General System DMA) — Device Model
 *
 * Phase 3: async timer-based LLI traversal with actual data transfer.
 * The GDMA_CTRL.START write arms a 10 µs QEMU timer.  Each timer
 * tick processes one LLI descriptor node: reads the 24-byte
 * descriptor from system memory, copies data from src to dst via
 * QEMU DMA helpers, then advances to the next node.  Per-node
 * interrupts are aggregated and delivered via PLIC IRQ 140.
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
#include "hw/core/irq.h"
#include "hw/core/sysbus.h"
#include "system/address-spaces.h"
#include "system/dma.h"
#include "hw/dma/k230_gsdma.h"
#include "migration/vmstate.h"
#include "trace.h"

/* ------------------------------------------------------------------ */
/* Constants                                                           */
/* ------------------------------------------------------------------ */

#define GSDMA_TRANSFER_DELAY_NS   10000ULL  /* 10 µs per descriptor node */
#define GSDMA_MAX_NODES           1024      /* safety ceiling */

/* ------------------------------------------------------------------ */
/* LLI Descriptor Read                                                 */
/* ------------------------------------------------------------------ */

static void k230_gsdma_read_lli(hwaddr base, uint32_t *lli)
{
    for (int i = 0; i < 6; i++) {
        lli[i] = address_space_ldl_le(&address_space_memory, base + i * 4,
                                      MEMTXATTRS_UNSPECIFIED, NULL);
    }
}

/* ------------------------------------------------------------------ */
/* Data Transfer (memcpy in system memory)                             */
/* ------------------------------------------------------------------ */

/*
 * Compute the byte count for a GDMA descriptor node.
 * Data size = src_stride × height.
 */
static size_t k230_gsdma_xfer_size(const uint32_t *lli)
{
    uint32_t wh  = lli[GSDMA_LLI_WIDTH_HEIGHT / 4];
    uint32_t str = lli[GSDMA_LLI_SRC_DST_STRIDE / 4];
    uint32_t height = (wh >> GSDMA_LLI_HEIGHT_SHIFT);
    uint32_t src_stride = str & GSDMA_LLI_SRC_STRIDE_MASK;

    return (size_t)src_stride * height;
}

/*
 * Perform the data copy for one descriptor node.
 * Uses QEMU's dma_memory_read / dma_memory_write to touch guest
 * physical memory, then directly copies the buffer.
 */
static void k230_gsdma_do_transfer(const uint32_t *lli)
{
    hwaddr src = lli[GSDMA_LLI_SRC_ADDR / 4];
    hwaddr dst = lli[GSDMA_LLI_DST_ADDR / 4];
    size_t size = k230_gsdma_xfer_size(lli);
    uint8_t buf[4096];  /* slice buffer */
    size_t off;

    if (src == 0 || dst == 0 || size == 0) {
        return;
    }

    for (off = 0; off < size; off += sizeof(buf)) {
        size_t chunk = MIN(size - off, sizeof(buf));

        dma_memory_read(&address_space_memory, src + off,
                        buf, chunk, MEMTXATTRS_UNSPECIFIED);
        dma_memory_write(&address_space_memory, dst + off,
                         buf, chunk, MEMTXATTRS_UNSPECIFIED);
    }
}

/* ------------------------------------------------------------------ */
/* Async Transfer Timer                                                */
/* ------------------------------------------------------------------ */

/*
 * Called once per QEMU timer tick to process the next LLI node.
 * For the last node, the timer is deleted and transfer-done
 * interrupt is raised.
 */
static void k230_gsdma_transfer_tick(void *opaque)
{
    K230GSDMAState *s = K230_GSDMA(opaque);
    uint32_t ctrl, ch_id, mask;

    if (!s->gdma_running || s->next_lli_addr == 0
        || s->lli_nodes_done >= GSDMA_MAX_NODES) {
        goto done;
    }

    /* Remove this node from the LLI base for CURRENT_LLT visibility */
    s->regs[K230_GSDMA_CURRENT_LLT / 4] = (uint32_t)s->next_lli_addr;

    /* Read + execute one descriptor */
    k230_gsdma_read_lli(s->next_lli_addr, s->lli_buffer);
    ctrl = s->lli_buffer[GSDMA_LLI_CTRL / 4];

    /* Data copy */
    k230_gsdma_do_transfer(s->lli_buffer);

    /* Channel counter */
    ch_id = (ctrl & GSDMA_LLI_CH_ID_MASK) >> GSDMA_LLI_CH_ID_SHIFT;
    if (ch_id < 8) {
        s->regs[(K230_GSDMA_CH0_CNT + ch_id * 4) / 4]++;
    }

    /* Node-level interrupt? */
    if (ctrl & GSDMA_LLI_INTERRUPT_ENABLE) {
        s->pending_int |= K230_GSDMA_INT_GDMA_LLI_ITEM;
    }

    /* Pause? */
    if (ctrl & GSDMA_LLI_PAUSE_ENABLE) {
        s->pending_int |= K230_GSDMA_INT_GDMA_LLI_PAUSE;
        s->next_lli_addr = s->lli_buffer[GSDMA_LLI_NEXT / 4];
        s->gdma_running = false;
        goto deliver;
    }

    /* Advance to next descriptor */
    s->next_lli_addr = s->lli_buffer[GSDMA_LLI_NEXT / 4];
    s->lli_nodes_done++;

    if (s->next_lli_addr != 0 && s->lli_nodes_done < GSDMA_MAX_NODES) {
        timer_mod(s->transfer_timer,
                  qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL)
                  + GSDMA_TRANSFER_DELAY_NS);
        goto deliver;  /* deliver node interrupt now if any */
    }

done:
    s->pending_int |= K230_GSDMA_INT_GDMA_TR_DONE;
    s->gdma_running = false;
    timer_del(s->transfer_timer);

deliver:
    mask = s->regs[K230_GSDMA_INT_MASK / 4];
    if (s->pending_int & mask) {
        qemu_set_irq(s->irq, 1);
    }
}

/* ------------------------------------------------------------------ */
/* GDMA Chain Start                                                    */
/* ------------------------------------------------------------------ */

static void k230_gsdma_start(K230GSDMAState *s)
{
    uint32_t ch_en = s->regs[K230_GSDMA_CH_EN / 4];
    uint32_t lli_base = s->regs[K230_GSDMA_LLI_BASE / 4];

    if (!(ch_en & K230_GSDMA_CH_EN_GDMA) || lli_base == 0) {
        return;
    }
    if (lli_base & 7) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: LLI base 0x%x not 8-byte aligned\n",
                      __func__, lli_base);
        return;
    }

    s->next_lli_addr   = lli_base;
    s->lli_nodes_done  = 0;
    s->gdma_running    = true;

    timer_mod(s->transfer_timer,
              qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL)
              + GSDMA_TRANSFER_DELAY_NS);
}

/* ------------------------------------------------------------------ */
/* Reset                                                               */
/* ------------------------------------------------------------------ */

static void k230_gsdma_reset_hold(Object *obj, ResetType type)
{
    K230GSDMAState *s = K230_GSDMA(obj);

    trace_k230_gsdma_reset();

    memset(s->regs, 0, sizeof(s->regs));
    s->pending_int = 0;
    s->gdma_running = false;
    timer_del(s->transfer_timer);

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

    if (addr == K230_GSDMA_INT_STAT) {
        return s->regs[addr / 4] | s->pending_int;
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

    switch (addr) {
    case K230_GSDMA_INT_STAT:
        s->pending_int &= ~value;
        s->regs[addr / 4] &= ~value;
        qemu_set_irq(s->irq, 0);
        break;

    case K230_GSDMA_GDMA_CTRL:
        if (value & K230_GSDMA_GDMA_START) {
            k230_gsdma_start(s);
        }
        if (value & K230_GSDMA_GDMA_STOP) {
            s->gdma_running = false;
            timer_del(s->transfer_timer);
        }
        if (value & K230_GSDMA_GDMA_RESUME && !s->gdma_running) {
            /* Resume from pause: restart timer from next_lli_addr */
            if (s->next_lli_addr != 0) {
                s->gdma_running = true;
                timer_mod(s->transfer_timer,
                          qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL)
                          + GSDMA_TRANSFER_DELAY_NS);
            }
        }
        break;

    default:
        s->regs[addr / 4] = (uint32_t)value;
        break;
    }
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
    sysbus_init_irq(SYS_BUS_DEVICE(dev), &s->irq);

    s->transfer_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                     k230_gsdma_transfer_tick, s);
}

/* ------------------------------------------------------------------ */
/* VMState                                                             */
/* ------------------------------------------------------------------ */

static const VMStateDescription vmstate_k230_gsdma = {
    .name = "k230.gsdma",
    .version_id = 3,
    .minimum_version_id = 3,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, K230GSDMAState, 0x100),
        VMSTATE_UINT32(pending_int, K230GSDMAState),
        VMSTATE_BOOL(gdma_running, K230GSDMAState),
        VMSTATE_UINT64(next_lli_addr, K230GSDMAState),
        VMSTATE_UINT32_ARRAY(lli_buffer, K230GSDMAState, 6),
        VMSTATE_INT32(lli_nodes_done, K230GSDMAState),
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
