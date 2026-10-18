/*
 * battery_basp.c
 *
 * driver adapter for basp.
 *
 * Copyright (c) 2020-2020 Huawei Technologies Co., Ltd.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 */

#include "battery_basp.h"
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/sysfs.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/power/huawei_power_proxy.h>
#include <huawei_platform/log/hw_log.h>
#include <chipset_common/hwpower/power_dsm.h>
#include <chipset_common/hwpower/power_dts.h>
#include <chipset_common/hwpower/power_supply.h>
#include <chipset_common/hwpower/power_supply_interface.h>

#ifdef HWLOG_TAG
#undef HWLOG_TAG
#endif
#define HWLOG_TAG battery_basp
HWLOG_REGIST();

static unsigned int g_ndc_vstep;

#ifdef CONFIG_SYSFS
static ssize_t basp_fcc_records_show(struct device *dev,
	struct device_attribute *attr, char *buff)
{
	int i, len;
	union power_supply_propval val;

	if (power_supply_get_property_value("battery",
		POWER_SUPPLY_PROP_CHARGE_FULL, &val)) {
		val.intval = POWER_SUPPLY_DEFAULT_CHARGE_FULL;
		hwlog_err("get charge full of battery failed\n");
	}

	len = scnprintf(buff, PAGE_SIZE, "%d", val.intval);
	for (i = 1; i < BASP_MAX_RECORDS_CNT; i++)
		len += scnprintf(buff + len, PAGE_SIZE - len, ",%d", val.intval);
	len += scnprintf(buff + len, PAGE_SIZE - len, "\n");

	return len;
}

static ssize_t basp_coul_vdec_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	int vdec;

	if (kstrtoint(buf, 0, &vdec))
		return -1;

	if (power_proxy_set_vterm_dec(vdec))
		return -1;

	return count;
}

static ssize_t basp_ndc_vstep_show(struct device *dev,
	struct device_attribute *attr, char *buff)
{
	return scnprintf(buff, PAGE_SIZE, "%d\n", g_ndc_vstep);
}

static DEVICE_ATTR_RO(basp_fcc_records);
static DEVICE_ATTR_RO(basp_ndc_vstep);
static DEVICE_ATTR_WO(basp_coul_vdec);
static struct attribute *g_basp_attrs[] = {
	&dev_attr_basp_fcc_records.attr,
	&dev_attr_basp_ndc_vstep.attr,
	&dev_attr_basp_coul_vdec.attr,
	NULL,
};

static struct attribute_group g_basp_group = {
	.name = "basp",
	.attrs = g_basp_attrs,
};

static int basp_sysfs_create_group(struct bsoh_device *di)
{
	return sysfs_create_group(&di->dev->kobj, &g_basp_group);
}

static void basp_sysfs_remove_group(struct bsoh_device *di)
{
	sysfs_remove_group(&di->dev->kobj, &g_basp_group);
}
#else
static int basp_sysfs_create_group(struct bsoh_device *di)
{
	return 0;
}

static void basp_sysfs_remove_group(struct bsoh_device *di)
{
}
#endif /* CONFIG_SYSFS */

static int basp_sys_init(struct bsoh_device *di)
{
	(void)power_dts_read_u32(power_dts_tag(HWLOG_TAG),
		di->dev->of_node, "ndc_vterm_step",
		&g_ndc_vstep, BASP_DEFAULT_NDC_VSTEP);

	return basp_sysfs_create_group(di);
}

static void basp_sys_exit(struct bsoh_device *di)
{
	basp_sysfs_remove_group(di);
}

static const struct bsoh_sub_sys g_basp_sys = {
	.sys_init = basp_sys_init,
	.sys_exit = basp_sys_exit,
	.event_notify = NULL,
	.dmd_prepare = NULL,
	.type_name = "basp",
	.notify_node = NULL,
};

static int __init basp_init(void)
{
	bsoh_register_sub_sys(BSOH_SUB_SYS_BASP, &g_basp_sys);
	return 0;
}

static void __exit basp_exit(void)
{
}

subsys_initcall(basp_init);
module_exit(basp_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("battery basp driver");
MODULE_AUTHOR("Huawei Technologies Co., Ltd.");
