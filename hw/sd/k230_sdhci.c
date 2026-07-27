/*
 * K230 SD/MMC Host Controller (DWC MSHC) — Device Model
 *
 * Register-level model covering the standard SDHCI register set
 * sufficient for the Linux sdhci-of driver to probe successfully.
 * Actual SD card command/data transfer, ADMA2 descriptor engine,
 * and Command Queuing Engine (CQE) are not implemented here.
 *
 * Reference:
 *   K230 TRM V0.3.1 (2024-11-18), Chapter 12.4 SD/MMC
 *
 * Copyright (c) 2026 The QEMU K230 Camp Contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/core/irq.h"
#include "hw/core/sysbus.h"
#include "hw/sd/k230_sdhci.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "trace.h"

/* ------------------------------------------------------------------ */
/* Reset                                                               */
/* ------------------------------------------------------------------ */

static void k230_sdhci_reset_hold(Object *obj, ResetType type)
{
    K230SdhciState *s = K230_SDHCI(obj);

    trace_k230_sdhci_reset();

    /* SDHCI Standard Registers — per TRM default values */
    s->sdmasa    = 0x00000000;
    s->blksize   = 0x0000;
    s->blkcnt    = 0x0000;
    s->argument  = 0x00000000;
    s->xfer_mode = 0x0000;
    s->cmd       = 0x0000;
    memset(s->response, 0, sizeof(s->response));
    s->buf_data  = 0x00000000;

    /* Present State — card inserted + stable power */
    /* (RO in hardware; stored here for migration but always overridden) */

    s->host_ctrl1  = 0x00;
    s->pwr_ctrl    = 0x00;
    s->blkgap      = 0x00;
    s->wakeup_ctrl = 0x00;
    s->clk_ctrl    = 0x0000;
    s->tout_ctrl   = 0x00;
    s->sw_rst      = 0x00;

    /* Interrupt registers */
    s->normal_int_stat    = 0x0000;
    s->error_int_stat     = 0x0000;
    s->normal_int_stat_en = 0x0000;
    s->error_int_stat_en  = 0x0000;
    s->normal_int_sig_en  = 0x0000;
    s->error_int_sig_en   = 0x0000;

    s->host_ctrl2 = 0x0000;
    s->capareg    = ((uint64_t)K230_SDHCI_CAPAB_HI << 32) | K230_SDHCI_CAPAB_LO;
    s->maxcurr    = 0x0000000000000000ULL;
    s->adma_addr  = 0x0000000000000000ULL;

    qemu_set_irq(s->irq, 0);
}

/* ------------------------------------------------------------------ */
/* MMIO Read                                                           */
/* ------------------------------------------------------------------ */

static uint64_t k230_sdhci_read(void *opaque, hwaddr addr,
                                unsigned int size)
{
    K230SdhciState *s = K230_SDHCI(opaque);
    uint32_t value = 0;

    switch (addr) {

    /* --- Transaction Registers --- */
    case K230_SDHCI_SDMASA:
        value = s->sdmasa;
        break;

    case K230_SDHCI_BLOCKSIZE:
        value = s->blksize;
        break;

    case K230_SDHCI_BLOCKCOUNT:
        value = s->blkcnt;
        break;

    case K230_SDHCI_ARGUMENT:
        value = s->argument;
        break;

    case K230_SDHCI_XFER_MODE:
        value = s->xfer_mode & 0xffff;
        break;

    case K230_SDHCI_CMD:
        value = s->cmd & 0xffff;
        break;

    case K230_SDHCI_RESPONSE:
    case K230_SDHCI_RESPONSE + 4:
    case K230_SDHCI_RESPONSE + 8:
    case K230_SDHCI_RESPONSE + 12:
        value = s->response[(addr - K230_SDHCI_RESPONSE) >> 2];
        break;

    case K230_SDHCI_BUF_DATA:
        value = s->buf_data;
        break;

    /* --- Present State (RO) — card inserted, stable power --- */
    case K230_SDHCI_PSTATE:
        value = K230_SDHCI_PSTATE_DEFAULT;
        break;

    /* --- Host Control / Power / Clock --- */
    case K230_SDHCI_HOST_CTRL1:
        value = s->host_ctrl1;
        break;

    case K230_SDHCI_PWR_CTRL:
        value = s->pwr_ctrl;
        break;

    case K230_SDHCI_BLKGAP:
        value = s->blkgap;
        break;

    case K230_SDHCI_WAKEUP_CTRL:
        value = s->wakeup_ctrl;
        break;

    case K230_SDHCI_CLK_CTRL:
        value = s->clk_ctrl & 0xffff;
        break;

    case K230_SDHCI_TOUT_CTRL:
        value = s->tout_ctrl;
        break;

    case K230_SDHCI_SW_RST:
        value = s->sw_rst;
        break;

    /* --- Interrupt Registers --- */
    case K230_SDHCI_NORMAL_INT_STAT:
        value = s->normal_int_stat;
        break;

    case K230_SDHCI_ERROR_INT_STAT:
        value = s->error_int_stat;
        break;

    case K230_SDHCI_NORMAL_INT_STAT_EN:
        value = s->normal_int_stat_en;
        break;

    case K230_SDHCI_ERROR_INT_STAT_EN:
        value = s->error_int_stat_en;
        break;

    case K230_SDHCI_NORMAL_INT_SIG_EN:
        value = s->normal_int_sig_en;
        break;

    case K230_SDHCI_ERROR_INT_SIG_EN:
        value = s->error_int_sig_en;
        break;

    /* --- Auto CMD Status (RO) --- */
    case K230_SDHCI_AUTO_CMD_STAT:
        break;

    /* --- Host Control 2 --- */
    case K230_SDHCI_HOST_CTRL2:
        value = s->host_ctrl2;
        break;

    /* --- Capabilities (RO, 64-bit) --- */
    case K230_SDHCI_CAPAB:
        value = (uint32_t)(s->capareg & 0xffffffffULL);
        break;

    case K230_SDHCI_CAPAB + 4:
        value = (uint32_t)(s->capareg >> 32);
        break;

    /* --- Max Current (RO, 64-bit) --- */
    case K230_SDHCI_MAX_CURR:
        value = (uint32_t)(s->maxcurr & 0xffffffffULL);
        break;

    case K230_SDHCI_MAX_CURR + 4:
        value = (uint32_t)(s->maxcurr >> 32);
        break;

    /* --- ADMA Error Status (RO) --- */
    case K230_SDHCI_ADMA_ERR_STAT:
        break;

    /* --- ADMA System Address (64-bit RW) --- */
    case K230_SDHCI_ADMA_ADDR_LO:
        value = (uint32_t)(s->adma_addr & 0xffffffffULL);
        break;

    case K230_SDHCI_ADMA_ADDR_HI:
        value = (uint32_t)(s->adma_addr >> 32);
        break;

    /* --- Slot Interrupt Status + Version --- */
    case K230_SDHCI_SLOT_INT_STATUS:
        value = K230_SDHCI_SLOT_INT_STATUS_VAL;
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: unimplemented read addr=0x%" HWADDR_PRIx "\n",
                      __func__, addr);
        break;
    }

    trace_k230_sdhci_read(addr, value);
    return value;
}

/* ------------------------------------------------------------------ */
/* MMIO Write                                                          */
/* ------------------------------------------------------------------ */

static void k230_sdhci_write(void *opaque, hwaddr addr,
                             uint64_t value, unsigned int size)
{
    K230SdhciState *s = K230_SDHCI(opaque);

    trace_k230_sdhci_write(addr, value);

    switch (addr) {

    /* --- Transaction Registers --- */
    case K230_SDHCI_SDMASA:
        s->sdmasa = (uint32_t)value;
        break;

    case K230_SDHCI_BLOCKSIZE:
        s->blksize = (uint16_t)(value & 0xffff);
        break;

    case K230_SDHCI_BLOCKCOUNT:
        s->blkcnt = (uint16_t)(value & 0xffff);
        break;

    case K230_SDHCI_ARGUMENT:
        s->argument = (uint32_t)value;
        break;

    case K230_SDHCI_XFER_MODE:
        s->xfer_mode = (uint16_t)(value & 0xffff);
        break;

    case K230_SDHCI_CMD:
        s->cmd = (uint16_t)(value & 0xffff);
        break;

    case K230_SDHCI_BUF_DATA:
        s->buf_data = (uint32_t)value;
        break;

    /* --- Response[0..3] — RO in hardware, writes ignored --- */
    case K230_SDHCI_RESPONSE:
    case K230_SDHCI_RESPONSE + 4:
    case K230_SDHCI_RESPONSE + 8:
    case K230_SDHCI_RESPONSE + 12:
        break;

    /* --- Present State — RO, writes ignored --- */
    case K230_SDHCI_PSTATE:
        break;

    /* --- Host Control / Power / Clock --- */
    case K230_SDHCI_HOST_CTRL1:
        s->host_ctrl1 = (uint8_t)value;
        break;

    case K230_SDHCI_PWR_CTRL:
        s->pwr_ctrl = (uint8_t)value;
        break;

    case K230_SDHCI_BLKGAP:
        s->blkgap = (uint8_t)value;
        break;

    case K230_SDHCI_WAKEUP_CTRL:
        s->wakeup_ctrl = (uint8_t)value;
        break;

    case K230_SDHCI_CLK_CTRL:
        s->clk_ctrl = (uint16_t)(value & 0xffff);
        break;

    case K230_SDHCI_TOUT_CTRL:
        s->tout_ctrl = (uint8_t)value;
        break;

    case K230_SDHCI_SW_RST:
        s->sw_rst = (uint8_t)value;
        break;

    /* --- Interrupt Status — W1C (write-1-to-clear) --- */
    case K230_SDHCI_NORMAL_INT_STAT:
        s->normal_int_stat &= ~((uint16_t)value);
        break;

    case K230_SDHCI_ERROR_INT_STAT:
        s->error_int_stat &= ~((uint16_t)value);
        break;

    /* --- Interrupt Enable Registers --- */
    case K230_SDHCI_NORMAL_INT_STAT_EN:
        s->normal_int_stat_en = (uint16_t)(value & 0xffff);
        break;

    case K230_SDHCI_ERROR_INT_STAT_EN:
        s->error_int_stat_en = (uint16_t)(value & 0xffff);
        break;

    case K230_SDHCI_NORMAL_INT_SIG_EN:
        s->normal_int_sig_en = (uint16_t)(value & 0xffff);
        break;

    case K230_SDHCI_ERROR_INT_SIG_EN:
        s->error_int_sig_en = (uint16_t)(value & 0xffff);
        break;

    /* --- Auto CMD Status — RO, writes ignored --- */
    case K230_SDHCI_AUTO_CMD_STAT:
        break;

    /* --- Host Control 2 --- */
    case K230_SDHCI_HOST_CTRL2:
        s->host_ctrl2 = (uint16_t)(value & 0xffff);
        break;

    /* --- Capabilities — RO, writes ignored --- */
    case K230_SDHCI_CAPAB:
    case K230_SDHCI_CAPAB + 4:
        break;

    /* --- Max Current — RO, writes ignored --- */
    case K230_SDHCI_MAX_CURR:
    case K230_SDHCI_MAX_CURR + 4:
        break;

    /* --- ADMA Error Status — RO, writes ignored --- */
    case K230_SDHCI_ADMA_ERR_STAT:
        break;

    /* --- ADMA System Address --- */
    case K230_SDHCI_ADMA_ADDR_LO:
        s->adma_addr = deposit64(s->adma_addr, 0, 32, value);
        break;

    case K230_SDHCI_ADMA_ADDR_HI:
        s->adma_addr = deposit64(s->adma_addr, 32, 32, value);
        break;

    /* --- Slot Interrupt Status — RO, writes ignored --- */
    case K230_SDHCI_SLOT_INT_STATUS:
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: unimplemented write addr=0x%" HWADDR_PRIx
                      " value=0x%" PRIx64 "\n",
                      __func__, addr, value);
        break;
    }
}

/* ------------------------------------------------------------------ */
/* Memory Region Ops                                                   */
/* ------------------------------------------------------------------ */

static const MemoryRegionOps k230_sdhci_ops = {
    .read  = k230_sdhci_read,
    .write = k230_sdhci_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
        .unaligned = true,
    },
    .impl = {
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

    memory_region_init_io(&s->mmio, OBJECT(dev), &k230_sdhci_ops, s,
                          TYPE_K230_SDHCI, K230_SDHCI_MMIO_SIZE);
    sysbus_init_mmio(sbd, &s->mmio);
    sysbus_init_irq(sbd, &s->irq);
}

/* ------------------------------------------------------------------ */
/* VMState                                                             */
/* ------------------------------------------------------------------ */

static const VMStateDescription vmstate_k230_sdhci = {
    .name = "k230.sdhci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(sdmasa, K230SdhciState),
        VMSTATE_UINT16(blksize, K230SdhciState),
        VMSTATE_UINT16(blkcnt, K230SdhciState),
        VMSTATE_UINT32(argument, K230SdhciState),
        VMSTATE_UINT16(xfer_mode, K230SdhciState),
        VMSTATE_UINT16(cmd, K230SdhciState),
        VMSTATE_UINT32_ARRAY(response, K230SdhciState, 4),
        VMSTATE_UINT32(buf_data, K230SdhciState),
        VMSTATE_UINT8(host_ctrl1, K230SdhciState),
        VMSTATE_UINT8(pwr_ctrl, K230SdhciState),
        VMSTATE_UINT8(blkgap, K230SdhciState),
        VMSTATE_UINT8(wakeup_ctrl, K230SdhciState),
        VMSTATE_UINT16(clk_ctrl, K230SdhciState),
        VMSTATE_UINT8(tout_ctrl, K230SdhciState),
        VMSTATE_UINT8(sw_rst, K230SdhciState),
        VMSTATE_UINT16(normal_int_stat, K230SdhciState),
        VMSTATE_UINT16(error_int_stat, K230SdhciState),
        VMSTATE_UINT16(normal_int_stat_en, K230SdhciState),
        VMSTATE_UINT16(error_int_stat_en, K230SdhciState),
        VMSTATE_UINT16(normal_int_sig_en, K230SdhciState),
        VMSTATE_UINT16(error_int_sig_en, K230SdhciState),
        VMSTATE_UINT16(host_ctrl2, K230SdhciState),
        VMSTATE_UINT64(capareg, K230SdhciState),
        VMSTATE_UINT64(maxcurr, K230SdhciState),
        VMSTATE_UINT64(adma_addr, K230SdhciState),
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
    rc->phases.hold = k230_sdhci_reset_hold;
}

/* ------------------------------------------------------------------ */
/* TypeInfo                                                            */
/* ------------------------------------------------------------------ */

static const TypeInfo k230_sdhci_info = {
    .name          = TYPE_K230_SDHCI,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(K230SdhciState),
    .class_init    = k230_sdhci_class_init,
};

static void k230_sdhci_register_type(void)
{
    type_register_static(&k230_sdhci_info);
}

type_init(k230_sdhci_register_type)
