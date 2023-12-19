/*

 * Xen-based ITS implementation for a GICv3-based system
 *
 * Copyright (c) 2015 Samsung Electronics Co., Ltd.
 * Written by Pavel Fedin <p.fedin@samsung.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "hw/intc/arm_gicv3_its_common.h"
#include "hw/xen/xen-hvm-common.h"
#include "hw/qdev-properties.h"
#include "sysemu/runstate.h"
#include "migration/blocker.h"
#include "qom/object.h"
#include "hw/pci/msi.h"
#include "qemu/log.h"

#define TYPE_XEN_ARM_ITS "arm-its-xen"

static MemTxResult gicv3_its_trans_read(void *opaque, hwaddr offset,
                                        uint64_t *data, unsigned size,
                                        MemTxAttrs attrs)
{
    *data = 0;
    return MEMTX_OK;
}

static MemTxResult gicv3_its_trans_write(void *opaque, hwaddr offset,
                                         uint64_t value, unsigned size,
                                         MemTxAttrs attrs)
{
    if (offset == 0x0040 && ((size == 2) || (size == 4))) {
        GICv3ITSState *s = ARM_GICV3_ITS_COMMON(opaque);
        GICv3ITSCommonClass *c = ARM_GICV3_ITS_COMMON_GET_CLASS(s);
        int ret = c->send_msi(s, le64_to_cpu(value), attrs.requester_id);

        if (ret < 0) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "ITS: Error sending MSI: %s\n", strerror(-ret));
        }
    } else {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "ITS: Error sending MSI: %s\n", strerror(EINVAL));
    }
    return MEMTX_OK;
}
static const MemoryRegionOps gicv3_its_trans_ops = {
    .read_with_attrs = gicv3_its_trans_read,
    .write_with_attrs = gicv3_its_trans_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static int xen_its_send_msi(GICv3ITSState *s, uint32_t value, uint16_t devid)
{
    if (unlikely(!s->translater_gpa_known)) {
        MemoryRegion *mr = &s->iomem_its_translation;
        MemoryRegionSection mrs;

        mrs = memory_region_find(mr, 0, 1);
        memory_region_unref(mrs.mr);
        s->gits_translater_gpa = mrs.offset_within_address_space + 0x40;
        s->translater_gpa_known = true;
    }
    xendevicemodel_inject_msi2(xen_dmod, xen_domid, s->gits_translater_gpa, devid, value);
    return 0;
}

static void xen_arm_its_realize(DeviceState *dev, Error **errp)
{
    GICv3ITSState *s = ARM_GICV3_ITS_COMMON(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(s);

    memory_region_init_io(&s->iomem_its_translation, OBJECT(s),
                            &gicv3_its_trans_ops, s,
                          "translation", ITS_TRANS_SIZE);

    memory_region_init(&s->iomem_main, OBJECT(s), "gicv3_its", ITS_SIZE);
    memory_region_add_subregion(&s->iomem_main, ITS_CONTROL_SIZE,
                                &s->iomem_its_translation);
    sysbus_init_mmio(sbd, &s->iomem_main);

    msi_nonbroken = true;
    return;
}

static void xen_arm_its_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    GICv3ITSCommonClass *icc = ARM_GICV3_ITS_COMMON_CLASS(klass);

    dc->realize = xen_arm_its_realize;
    icc->send_msi = xen_its_send_msi;
}

static const TypeInfo xen_arm_its_info = {
    .name = TYPE_XEN_ARM_ITS,
    .parent = TYPE_ARM_GICV3_ITS_COMMON,
    .instance_size = sizeof(GICv3ITSState),
    .class_init = xen_arm_its_class_init,
};

static void xen_arm_its_register_types(void)
{
    type_register_static(&xen_arm_its_info);
}

type_init(xen_arm_its_register_types)
