/*
 * power_supply_interface.c
 *
 * power supply interface for power module
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

#include <chipset_common/hwpower/power_supply_interface.h>
#include <chipset_common/hwpower/power_printk.h>

#define HWLOG_TAG power_psy
HWLOG_REGIST();

void power_supply_sync_changed(const char *name)
{
	struct power_supply *psy = NULL;

	if (!name) {
		hwlog_err("name is null\n");
		return;
	}

	psy = power_supply_get_by_name(name);
	if (!psy) {
		hwlog_err("power supply %s not exist\n", name);
		return;
	}

	power_supply_changed(psy);
}

int power_supply_get_property_value(const char *name,
	enum power_supply_property psp, union power_supply_propval *val)
{
	int ret;
	struct power_supply *psy = NULL;

	if (!name || !val) {
		hwlog_err("name or val is null\n");
		return -EINVAL;
	}

	psy = power_supply_get_by_name(name);
	if (!psy) {
		hwlog_err("power supply %s not exist\n", name);
		return -EINVAL;
	}

	ret = power_supply_get_property(psy, psp, val);
	power_supply_put(psy);

	return ret;
}

int power_supply_set_property_value(const char *name,
	enum power_supply_property psp, const union power_supply_propval *val)
{
	int ret;
	struct power_supply *psy = NULL;

	if (!name || !val) {
		hwlog_err("name or val is null\n");
		return -EINVAL;
	}

	psy = power_supply_get_by_name(name);
	if (!psy) {
		hwlog_err("power supply %s not exist\n", name);
		return -EINVAL;
	}

	ret = power_supply_set_property(psy, psp, val);
	power_supply_put(psy);

	return ret;
}

int power_supply_get_int_property_value(const char *name,
	enum power_supply_property psp, int *val)
{
	int ret;
	union power_supply_propval union_val;

	if (!name || !val) {
		hwlog_err("name or val is null\n");
		return -EINVAL;
	}

	ret = power_supply_get_property_value(name, psp, &union_val);
	if (!ret)
		*val = union_val.intval;

	return ret;
}

int power_supply_set_int_property_value(const char *name,
	enum power_supply_property psp, int val)
{
	union power_supply_propval union_val;

	if (!name) {
		hwlog_err("name is null\n");
		return -EINVAL;
	}

	union_val.intval = val;
	return power_supply_set_property_value(name, psp, &union_val);
}
