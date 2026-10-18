/*
 * wireless_tx_ic_intf.c
 *
 * wireless tx ic interfaces
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

#include <chipset_common/hwpower/wireless_charge/wireless_tx_ic_intf.h>
#include <chipset_common/hwpower/power_printk.h>

#define HWLOG_TAG wireless_tx_ic_intf
HWLOG_REGIST();

static struct wltx_ic_ops *g_wltx_ic_ops[WLTRX_IC_TYPE_MAX];

int wltx_ic_ops_register(struct wltx_ic_ops *ops, unsigned int type)
{
	if (!ops || (type >= WLTRX_IC_TYPE_MAX)) {
		hwlog_err("ops_register: invalid para\n");
		return -1;
	}
	if (g_wltx_ic_ops[type]) {
		hwlog_err("ops_register: type=%u, already registered\n", type);
		return -1;
	}

	g_wltx_ic_ops[type] = ops;
	hwlog_info("[ops_register] succ, type=%u\n", type);
	return 0;
}

static struct wltx_ic_ops *wltx_ic_get_ops(unsigned int type)
{
	switch (type) {
	case WLTRX_IC_MAIN:
		return g_wltx_ic_ops[WLTRX_IC_TYPE_MAIN];
	case WLTRX_IC_AUX:
		return g_wltx_ic_ops[WLTRX_IC_TYPE_AUX];
	default:
		break;
	}

	return NULL;
}

struct device_node *wltx_get_dev_node(unsigned int type)
{
	struct wltx_ic_ops *ops = wltx_ic_get_ops(type);

	if (ops && ops->get_dev_node)
		return ops->get_dev_node(ops->dev_data);

	return NULL;
}
