/*
 * K230 SPI / QSPI Controller (Synopsys DesignWare SSI)
 *
 * K230 Technical Reference Manual V0.3.1 (2024-11-18), Chapter 12.3:
 * https://github.com/revyos/external-docs/blob/master/K230/en-us/K230_Technical_Reference_Manual_V0.3.1_20241118.pdf
 *
 * Minimal register-level model covering the DW SSI standard registers
 * and K230 extensions.  Actual SPI data transfer, DMA engine, XIP and
 * DDR/Octal modes are NOT implemented — the model only ensures that the
 * Linux dw_spi_mmio driver probe() completes without hanging.
 *
 * Copyright (c) 2026 The QEMU K230 Camp Contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/bitops.h"
#include "qemu/log.h"
#include "hw/core/irq.h"
#include "hw/core/sysbus.h"
#include "hw/ssi/k230_spi.h"
#include "migration/vmstate.h"
#include "trace.h"

/* ------------------------------------------------------------------ */
/*  Reset                                                             */
/* ------------------------------------------------------------------ */

static void k230_spi_reset(DeviceState *dev)
{
    K230SpiState *s = K230_SPI(dev);

    trace_k230_spi_reset();

    /* DW SSI Standard Registers — per TRM reset values */
    s->ctrlr0   = K230_SPI_CTRLR0_RESET;        /* 0x00004007 */
    s->ctrlr1   = 0x00000000;
    s->ssienr   = 0x00000000;
    s->mwcr     = 0x00000000;
    s->ser      = 0x00000000;
    s->baudr    = 0x00000000;
    s->txftlr   = 0x00000000;
    s->rxftlr   = 0x00000000;
    s->txflr    = 0x00000000;
    s->rxflr    = 0x00000000;
    s->sr       = K230_SPI_SR_IDLE;             /* 0x00000006 */
    s->imr      = 0x00000000;
    s->isr      = 0x00000000;
    s->risr     = 0x00000000;
    s->txoicr   = 0x00000000;
    s->rxoicr   = 0x00000000;
    s->rxuicr   = 0x00000000;
    s->msticr   = 0x00000000;
    s->icr      = 0x00000000;
    s->dmacr    = 0x00000000;
    s->dmatdlr  = 0x00000000;
    s->dmardlr  = 0x00000000;

    /* K230 Extension Registers */
    s->rx_sample_delay  = 0x00000000;
    s->spi_ctrlr0       = 0x00000000;
    s->ddr_drive_edge   = 0x00000000;
    s->xrxoicr          = 0x00000000;
    s->xip_cnt_time_out = 0x00000000;
    s->spi_ctrlr1       = 0x00000000;
    s->spitecr          = 0x00000000;
    s->spidr            = 0x00000000;
    s->spiar            = 0x00000000;
    s->axiar0           = 0x00000000;
    s->axiar1           = 0x00000000;
    s->axiecr           = 0x00000000;
    s->donecr           = 0x00000000;

    qemu_set_irq(s->irq, 0);
}

/* ------------------------------------------------------------------ */
/*  Read                                                              */
/* ------------------------------------------------------------------ */

static uint64_t k230_spi_read(void *opaque, hwaddr addr, unsigned int size)
{
    K230SpiState *s = K230_SPI(opaque);
    uint32_t value = 0;

    switch (addr) {
    /* ---- DW SSI Standard Registers ---- */
    case K230_SPI_CTRLR0:
        value = s->ctrlr0;
        break;
    case K230_SPI_CTRLR1:
        value = s->ctrlr1;
        break;
    case K230_SPI_SSIENR:
        value = s->ssienr;
        break;
    case K230_SPI_MWCR:
        value = s->mwcr;
        break;
    case K230_SPI_SER:
        value = s->ser;
        break;
    case K230_SPI_BAUDR:
        value = s->baudr;
        break;
    case K230_SPI_TXFTLR:
        value = s->txftlr;
        break;
    case K230_SPI_RXFTLR:
        value = s->rxftlr;
        break;
    case K230_SPI_TXFLR:
        value = s->txflr;
        break;
    case K230_SPI_RXFLR:
        value = s->rxflr;
        break;
    case K230_SPI_SR:
        /*
         * Return safe idle value (TFNF=1, TFE=1, BUSY=0).
         * The actual value is written by the guest, but we keep the
         * idle snapshot so that the driver busy-wait on entering a
         * disabled state always completes immediately.
         */
        value = (s->sr & ~K230_SPI_SR_BUSY) | K230_SPI_SR_TFNF | K230_SPI_SR_TFE;
        break;
    case K230_SPI_IMR:
        value = s->imr;
        break;
    case K230_SPI_ISR:
        value = s->isr;
        break;
    case K230_SPI_RISR:
        value = s->risr;
        break;
    case K230_SPI_DMACR:
        value = s->dmacr;
        break;
    case K230_SPI_DMATDLR:
        value = s->dmatdlr;
        break;
    case K230_SPI_DMARDLR:
        value = s->dmardlr;
        break;
    case K230_SPI_IDR:
        value = K230_SPI_IDR_VAL;
        break;
    case K230_SPI_SSIC_VERSION_ID:
        value = K230_SPI_SSIC_VERSION_ID_VAL;
        break;

    /* ---- K230 Extension Registers ---- */
    case K230_SPI_RX_SAMPLE_DELAY:
        value = s->rx_sample_delay;
        break;
    case K230_SPI_SPI_CTRLR0:
        value = s->spi_ctrlr0;
        break;
    case K230_SPI_DDR_DRIVE_EDGE:
        value = s->ddr_drive_edge;
        break;
    case K230_SPI_XIP_CNT_TIME_OUT:
        value = s->xip_cnt_time_out;
        break;
    case K230_SPI_SPI_CTRLR1:
        value = s->spi_ctrlr1;
        break;
    case K230_SPI_SPIDR:
        value = s->spidr;
        break;
    case K230_SPI_SPIAR:
        value = s->spiar;
        break;
    case K230_SPI_AXIAR0:
        value = s->axiar0;
        break;
    case K230_SPI_AXIAR1:
        value = s->axiar1;
        break;

    /* ---- WO clear registers (always read as 0) ---- */
    case K230_SPI_TXOICR:
    case K230_SPI_RXOICR:
    case K230_SPI_RXUICR:
    case K230_SPI_MSTICR:
    case K230_SPI_ICR:
    case K230_SPI_XRXOICR:
    case K230_SPI_SPITECR:
    case K230_SPI_AXIECR:
    case K230_SPI_DONECR:
        break;

    /* ---- Data Registers DR0 – DR35 (0x60 – 0xec) ---- */
    default:
        if (addr >= K230_SPI_DR_BASE && addr < K230_SPI_RX_SAMPLE_DELAY) {
            /* DR is not modelled yet, return 0 */
            break;
        }
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: unimplemented read at addr 0x%" HWADDR_PRIx "\n",
                      __func__, addr);
        break;
    }

    trace_k230_spi_read(addr, value);
    return value;
}

/* ------------------------------------------------------------------ */
/*  Write                                                             */
/* ------------------------------------------------------------------ */

static void k230_spi_write(void *opaque, hwaddr addr,
                           uint64_t value, unsigned int size)
{
    K230SpiState *s = K230_SPI(opaque);

    trace_k230_spi_write(addr, value);

    switch (addr) {
    /* ---- DW SSI Standard Registers ---- */
    case K230_SPI_CTRLR0:
        s->ctrlr0 = value;
        break;
    case K230_SPI_CTRLR1:
        s->ctrlr1 = value & 0x0000ffff;   /* NDF[15:0] */
        break;
    case K230_SPI_SSIENR:
        s->ssienr = value & K230_SPI_SSIENR_EN;
        break;
    case K230_SPI_MWCR:
        s->mwcr = value & 0x00000007;
        break;
    case K230_SPI_SER:
        s->ser = value;
        break;
    case K230_SPI_BAUDR:
        s->baudr = value & K230_SPI_BAUDR_SCKDV_MASK;
        break;
    case K230_SPI_TXFTLR:
        s->txftlr = value & 0x000000ff;
        break;
    case K230_SPI_RXFTLR:
        s->rxftlr = value & 0x000000ff;
        break;
    case K230_SPI_SR:
        /* SR is technically read-only except BUSY (cleared by SSIENR=0),
         * but we accept writes to avoid stubbing complaints. */
        s->sr = value;
        break;
    case K230_SPI_IMR:
        s->imr = value;
        break;
    case K230_SPI_DMACR:
        s->dmacr = value;
        break;
    case K230_SPI_DMATDLR:
        s->dmatdlr = value & 0x0000003f;
        break;
    case K230_SPI_DMARDLR:
        s->dmardlr = value & 0x0000003f;
        break;

    /* ---- WO clear registers ---- */
    case K230_SPI_TXOICR:
    case K230_SPI_RXOICR:
    case K230_SPI_RXUICR:
    case K230_SPI_MSTICR:
    case K230_SPI_ICR:
        /* WO clear — accept write, clear interrupt if needed */
        s->isr &= ~value;
        break;

    /* ---- K230 Extension Registers ---- */
    case K230_SPI_RX_SAMPLE_DELAY:
        s->rx_sample_delay = value;
        break;
    case K230_SPI_SPI_CTRLR0:
        s->spi_ctrlr0 = value;
        break;
    case K230_SPI_DDR_DRIVE_EDGE:
        s->ddr_drive_edge = value;
        break;
    case K230_SPI_XRXOICR:
        break;  /* WO clear, not modelled further */
    case K230_SPI_XIP_CNT_TIME_OUT:
        s->xip_cnt_time_out = value;
        break;
    case K230_SPI_SPI_CTRLR1:
        s->spi_ctrlr1 = value;
        break;
    case K230_SPI_SPITECR:
        break;  /* WO clear */
    case K230_SPI_SPIDR:
        s->spidr = value;
        break;
    case K230_SPI_SPIAR:
        s->spiar = value;
        break;
    case K230_SPI_AXIAR0:
        s->axiar0 = value;
        break;
    case K230_SPI_AXIAR1:
        s->axiar1 = value;
        break;
    case K230_SPI_AXIECR:
        break;  /* WO clear */
    case K230_SPI_DONECR:
        break;  /* WO clear */

    /* ---- Read-only registers, silently ignore ---- */
    case K230_SPI_TXFLR:
    case K230_SPI_RXFLR:
    case K230_SPI_ISR:
    case K230_SPI_RISR:
    case K230_SPI_IDR:
    case K230_SPI_SSIC_VERSION_ID:
        break;

    default:
        if (addr >= K230_SPI_DR_BASE && addr < K230_SPI_RX_SAMPLE_DELAY) {
            /* DR is not modelled yet, discard the write */
            break;
        }
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: unimplemented write at addr 0x%" HWADDR_PRIx
                      " value 0x%" PRIx64 "\n",
                      __func__, addr, value);
        break;
    }
}

/* ------------------------------------------------------------------ */
/*  MemoryRegionOps                                                   */
/* ------------------------------------------------------------------ */

static const MemoryRegionOps k230_spi_ops = {
    .read  = k230_spi_read,
    .write = k230_spi_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false,
    },
};

/* ------------------------------------------------------------------ */
/*  VMState                                                           */
/* ------------------------------------------------------------------ */

static const VMStateDescription vmstate_k230_spi = {
    .name = "k230.spi",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        /* DW SSI Standard Registers */
        VMSTATE_UINT32(ctrlr0, K230SpiState),
        VMSTATE_UINT32(ctrlr1, K230SpiState),
        VMSTATE_UINT32(ssienr, K230SpiState),
        VMSTATE_UINT32(mwcr, K230SpiState),
        VMSTATE_UINT32(ser, K230SpiState),
        VMSTATE_UINT32(baudr, K230SpiState),
        VMSTATE_UINT32(txftlr, K230SpiState),
        VMSTATE_UINT32(rxftlr, K230SpiState),
        VMSTATE_UINT32(sr, K230SpiState),
        VMSTATE_UINT32(imr, K230SpiState),
        VMSTATE_UINT32(isr, K230SpiState),
        VMSTATE_UINT32(dmacr, K230SpiState),
        VMSTATE_UINT32(dmatdlr, K230SpiState),
        VMSTATE_UINT32(dmardlr, K230SpiState),
        /* K230 Extension Registers */
        VMSTATE_UINT32(rx_sample_delay, K230SpiState),
        VMSTATE_UINT32(spi_ctrlr0, K230SpiState),
        VMSTATE_UINT32(ddr_drive_edge, K230SpiState),
        VMSTATE_UINT32(xip_cnt_time_out, K230SpiState),
        VMSTATE_UINT32(spi_ctrlr1, K230SpiState),
        VMSTATE_UINT32(spidr, K230SpiState),
        VMSTATE_UINT32(spiar, K230SpiState),
        VMSTATE_UINT32(axiar0, K230SpiState),
        VMSTATE_UINT32(axiar1, K230SpiState),
        VMSTATE_END_OF_LIST()
    }
};

/* ------------------------------------------------------------------ */
/*  Realize  /  Class  /  Type                                        */
/* ------------------------------------------------------------------ */

static void k230_spi_realize(DeviceState *dev, Error **errp)
{
    K230SpiState *s = K230_SPI(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    memory_region_init_io(&s->mmio, OBJECT(dev), &k230_spi_ops, s,
                          TYPE_K230_SPI, K230_SPI_MMIO_SIZE);
    sysbus_init_mmio(sbd, &s->mmio);
    sysbus_init_irq(sbd, &s->irq);
}

static void k230_spi_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = k230_spi_realize;
    device_class_set_legacy_reset(dc, k230_spi_reset);
    dc->vmsd = &vmstate_k230_spi;
    dc->desc = "K230 SPI / QSPI controller (Synopsys DW SSI)";
}

static const TypeInfo k230_spi_info = {
    .name          = TYPE_K230_SPI,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(K230SpiState),
    .class_init    = k230_spi_class_init,
};

static void k230_spi_register_type(void)
{
    type_register_static(&k230_spi_info);
}
type_init(k230_spi_register_type)
