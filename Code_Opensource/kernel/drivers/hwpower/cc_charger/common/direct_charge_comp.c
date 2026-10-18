/*
 * direct_charge_comp.c
 *
 * compensation parameter interface for direct charger
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

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/math64.h>
#include <chipset_common/hwpower/power_dts.h>
#include <chipset_common/hwpower/power_common.h>
#include <chipset_common/hwpower/power_printk.h>
#include <chipset_common/hwpower/direct_charge/direct_charge_comp.h>

#define HWLOG_TAG direct_charge_comp
HWLOG_REGIST();

void dc_get_vbat_comp_para(int main_ic_id,
	int aux_ic_id, int ic_mode, struct dc_comp_para *info)
{
	int i;
	int count = 0;

	for (i = 0; i < info->vbat_comp_group_size; i++) {
		if (count == CHARGE_IC_TYPE_MAX)
			break;

		if ((main_ic_id == info->vbat_comp_para[i].ic_id) &&
			(ic_mode == info->vbat_comp_para[i].ic_mode)) {
			info->vbat_comp[CHARGE_IC_TYPE_MAIN] = info->vbat_comp_para[i].vbat_comp;
			count++;
		}

		if ((aux_ic_id == info->vbat_comp_para[i].ic_id) &&
			(ic_mode == info->vbat_comp_para[i].ic_mode)) {
			info->vbat_comp[CHARGE_IC_TYPE_AUX] = info->vbat_comp_para[i].vbat_comp;
			count++;
		}
	}

	hwlog_info("main_ic_id=%d, aux_ic_id=%d, vbat_comp_main=%d, vbat_comp_aux=%d\n",
		main_ic_id, aux_ic_id, info->vbat_comp[0], info->vbat_comp[1]);
}

static void dc_parse_vbat_comp_para(struct device_node *np,
	struct dc_comp_para *info)
{
	int i, j, len, ret;
	u32 para[DC_VBAT_COMP_PARA_MAX * DC_VBAT_COMP_TOTAL] = { 0 };

	len = power_dts_read_u32_count(power_dts_tag(HWLOG_TAG), np,
 		"vbat_comp_para", DC_VBAT_COMP_PARA_MAX, DC_VBAT_COMP_TOTAL);
	if (len < 0) {
		info->vbat_comp_group_size = 0;
		return;
	}

	ret = power_dts_read_u32_array(power_dts_tag(HWLOG_TAG), np,
		"vbat_comp_para", para, len);
	if (ret < 0) {
		info->vbat_comp_group_size = 0;
		return;
        }

	info->vbat_comp_group_size = len / DC_VBAT_COMP_TOTAL;

	for (i = 0; i < info->vbat_comp_group_size; i++) {
		j = DC_VBAT_COMP_TOTAL * i;
		info->vbat_comp_para[i].ic_id = para[j + DC_IC_ID];
		info->vbat_comp_para[i].ic_mode = para[j + DC_IC_MODE];
		info->vbat_comp_para[i].vbat_comp = para[j + DC_VBAT_COMP_VALUE];
	}

	for (i = 0; i < info->vbat_comp_group_size; i++)
		hwlog_info("ic_id=%d,ic_mode=%d,vbat_comp_value=%d\n",
			info->vbat_comp_para[i].ic_id,
			info->vbat_comp_para[i].ic_mode,
			info->vbat_comp_para[i].vbat_comp);
}

void dc_comp_parse_dts(struct device_node *np, struct dc_comp_para *info)
{
	if (!np || !info)
		return;

	dc_parse_vbat_comp_para(np, info);
}
