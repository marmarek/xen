/*
 * QEMU i440FX/PIIX3 PCI Bridge Emulation
 *
 * Copyright (c) 2006 Fabrice Bellard
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "vl.h"
typedef uint32_t pci_addr_t;
#include "hw/pci_host.h"

typedef PCIHostState I440FXState;

static void i440fx_addr_writel(void* opaque, uint32_t addr, uint32_t val)
{
    I440FXState *s = opaque;
    s->config_reg = val;
}

static uint32_t i440fx_addr_readl(void* opaque, uint32_t addr)
{
    I440FXState *s = opaque;
    return s->config_reg;
}

static void i440fx_set_irq(PCIDevice *pci_dev, void *pic, int intx, int level)
{
    xc_hvm_set_pci_intx_level(xc_handle, domid, 0, 0, pci_dev->devfn >> 3,
                              intx, level);
}

PCIBus *i440fx_init(void)
{
    PCIBus *b;
    PCIDevice *d;
    I440FXState *s;

    s = qemu_mallocz(sizeof(I440FXState));
    b = pci_register_bus(i440fx_set_irq, NULL, 0);
    s->bus = b;

    register_ioport_write(0xcf8, 4, 4, i440fx_addr_writel, s);
    register_ioport_read(0xcf8, 4, 4, i440fx_addr_readl, s);

    register_ioport_write(0xcfc, 4, 1, pci_host_data_writeb, s);
    register_ioport_write(0xcfc, 4, 2, pci_host_data_writew, s);
    register_ioport_write(0xcfc, 4, 4, pci_host_data_writel, s);
    register_ioport_read(0xcfc, 4, 1, pci_host_data_readb, s);
    register_ioport_read(0xcfc, 4, 2, pci_host_data_readw, s);
    register_ioport_read(0xcfc, 4, 4, pci_host_data_readl, s);

    d = pci_register_device(b, "i440FX", sizeof(PCIDevice), 0, 
                            NULL, NULL);

    d->config[0x00] = 0x86; // vendor_id
    d->config[0x01] = 0x80;
    d->config[0x02] = 0x37; // device_id
    d->config[0x03] = 0x12;
    d->config[0x08] = 0x02; // revision
    d->config[0x0a] = 0x00; // class_sub = host2pci
    d->config[0x0b] = 0x06; // class_base = PCI_bridge
    d->config[0x0e] = 0x00; // header_type
    return b;
}

/* PIIX3 PCI to ISA bridge */

static PCIDevice *piix3_dev;

static void piix3_write_config(PCIDevice *d, 
                               uint32_t address, uint32_t val, int len)
{
    int i;

    /* Scan for updates to PCI link routes (0x60-0x63). */
    for (i = 0; i < len; i++) {
        uint8_t v = (val >> (8*i)) & 0xff;
        if (v & 0x80)
            v = 0;
        v &= 0xf;
        if (((address+i) >= 0x60) && ((address+i) <= 0x63))
            xc_hvm_set_pci_link_route(xc_handle, domid, address + i - 0x60, v);
    }

    /* Hand off to default logic. */
    pci_default_write_config(d, address, val, len);
}

static void piix3_reset(PCIDevice *d)
{
    uint8_t *pci_conf = d->config;

    pci_conf[0x04] = 0x07; // master, memory and I/O
    pci_conf[0x07] = 0x02; // PCI_status_devsel_medium
    pci_conf[0x4c] = 0x4d;
    pci_conf[0x4e] = 0x03;
    pci_conf[0x60] = 0x80;
    pci_conf[0x61] = 0x80;
    pci_conf[0x62] = 0x80;
    pci_conf[0x63] = 0x80;
    pci_conf[0x69] = 0x02;
    pci_conf[0x70] = 0x80;
    pci_conf[0x76] = 0x0c;
    pci_conf[0x77] = 0x0c;
    pci_conf[0x78] = 0x02;
    pci_conf[0xa0] = 0x08;
    pci_conf[0xa0] = 0x08;
    pci_conf[0xa8] = 0x0f;
}

int piix3_init(PCIBus *bus)
{
    PCIDevice *d;
    uint8_t *pci_conf;

    d = pci_register_device(bus, "PIIX3", sizeof(PCIDevice),
                                    -1, NULL, piix3_write_config);
    register_savevm("PIIX3", 0, 1, generic_pci_save, generic_pci_load, d);

    piix3_dev = d;
    pci_conf = d->config;

    pci_conf[0x00] = 0x86; // Intel
    pci_conf[0x01] = 0x80;
    pci_conf[0x02] = 0x00; // 82371SB PIIX3 PCI-to-ISA bridge (Step A1)
    pci_conf[0x03] = 0x70;
    pci_conf[0x0a] = 0x01; // class_sub = PCI_ISA
    pci_conf[0x0b] = 0x06; // class_base = PCI_bridge
    pci_conf[0x0e] = 0x80; // header_type = PCI_multifunction, generic

    piix3_reset(d);
    return d->devfn;
}

void pci_bios_init(void) {}
