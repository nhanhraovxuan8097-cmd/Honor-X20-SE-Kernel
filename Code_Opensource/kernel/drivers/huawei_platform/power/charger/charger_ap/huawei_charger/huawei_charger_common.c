/*
 * huawei_charger_common.c
 *
 * common interface for huawei charger driver
 *
 * Copyright (c) 2012-2019 Huawei Technologies Co., Ltd.
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

#include <huawei_platform/log/hw_log.h>
#include <huawei_platform/charger/huawei_charger.h>
#include <huawei_platform/power/huawei_charger_common.h>
#include <mt-plat/charger_class.h>
#include <mt-plat/mtk_charger.h>
#include <mtk_charger_intf.h>
#include <../pmic/mt6360/inc/mt6360_pmu_chg.h>
#include <mtk_switch_charging.h>

#define HWLOG_TAG huawei_charger_common
HWLOG_REGIST();

static struct charge_extra_ops *g_extra_ops;
struct charge_device_ops *g_device_ops;
extern struct charger_device *get_charger_dev(void);
static int g_cur_eoc_delay_count = 0;

int charge_extra_ops_register(struct charge_extra_ops *ops)
{
	int ret = 0;

	if (ops) {
		g_extra_ops = ops;
		hwlog_info("charge extra ops register ok\n");
	} else {
		hwlog_err("charge extra ops register fail\n");
		ret = -EPERM;
	}

	return ret;
}

int charge_ops_register(struct charge_device_ops *ops)
{
	int ret = 0;

	if (ops) {
		g_device_ops = ops;
		hwlog_info("charge ops register ok\n");
	} else {
		hwlog_err("charge ops register fail\n");
		ret = -EPERM;
	}

	return ret;
}

int get_charger_vbus_vol(void)
{
	unsigned int vbus_vol = 0;
	struct charger_device *pdev = NULL;

	pdev = get_charger_dev();
	if (!pdev) {
		hwlog_err("g_charge_ops or get_vbus is null\n");
		return 0;
	}
	
	charger_dev_get_vbus(pdev, &vbus_vol);
	return vbus_vol;
}

int get_charger_ibus_curr(void)
{
	unsigned int ibus_curr = 0;
	struct charger_device *pdev = NULL;

	pdev = get_charger_dev();
	if (!pdev) {
		hwlog_err("g_charge_ops or get_vbus is null\n");
		return 0;
	}
	
	charger_dev_get_ibus(pdev, &ibus_curr);
	return ibus_curr;
}

int charge_check_input_dpm_state(void)
{
	struct charger_device *pdev = NULL;

	pdev = get_charger_dev();
	if (!pdev) {
		hwlog_err("g_charge_ops or get_vbus is null\n");
		return 0;
	}
	
	return charger_dev_check_input_dpm_state(pdev);
}

int charge_check_charger_plugged(void)
{
	struct charger_device *pdev = NULL;

	pdev = get_charger_dev();
	if (!pdev) {
		hwlog_err("g_charge_ops or get_vbus is null\n");
		return 0;
	}

	return charger_dev_check_charger_plugged(pdev);
}

int charge_enable_hz(bool hz_enable)
{
	struct charger_device *pdev = NULL;

	pdev = get_charger_dev();
	if (!pdev) {
		hwlog_err("g_charge_ops or get_vbus is null\n");
		return 0;
	}

	return charger_dev_enable_hz(pdev, hz_enable);
}

int charge_enable_powerpath(bool path_enable)
{
	struct charger_device *pdev = NULL;

	pdev = get_charger_dev();
	if (!pdev) {
		hwlog_err("pdev is null\n");
		return 0;
	}

	return charger_dev_enable_powerpath(pdev, path_enable);
}

int charge_enable_force_sleep(bool enable)
{
	struct charger_device *pdev = NULL;

	pdev = get_charger_dev();
	if (!pdev) {
		hwlog_err("g_charge_ops is null\n");
		return 0;
	}

	return charger_dev_enable_force_enable(pdev, enable);
}

int charge_enable_eoc(bool eoc_enable)
{
	struct charger_device *pdev = NULL;

	pdev = get_charger_dev();
	if (!pdev) {
		hwlog_err("g_charge_ops or get_vbus is null\n");
		return 0;
	}

	return charger_dev_enable_eoc(pdev, eoc_enable);
}

bool huawei_charge_done(void)
{
	struct charger_manager *pinfo = huawei_get_pinfo();
	if (!pinfo) {
		hwlog_err("pinfo is null\n");
		return 0;
	}
	return pinfo->charge_done;
}

int mt_get_charger_status(void)
{
	struct charger_manager *pinfo = huawei_get_pinfo();
	struct switch_charging_alg_data *swchgalg = NULL;

	if (!pinfo)
		return CHR_ERROR;

	swchgalg = pinfo->algorithm_data;
	if (!swchgalg)
		return CHR_ERROR;

	return swchgalg->state;
}

int get_charger_bat_id_vol(void)
{
	int id_volt;

	if (!g_extra_ops || !g_extra_ops->get_batt_by_usb_id)
		return 0;

	id_volt = g_extra_ops->get_batt_by_usb_id();
	return id_volt;
}

bool charge_check_charger_ts(void)
{
	return false;
}

bool charge_check_charger_otg_state(void)
{
	return false;
}

int charge_set_charge_state(int state)
{
	return 0;
}

int get_charge_current_max(void)
{
	return 0;
}

void reset_cur_delay_eoc_count(void)
{
	g_cur_eoc_delay_count = get_eoc_max_delay_count();
	hwlog_info("reset g_cur_eoc_delay_count = %d\n", g_cur_eoc_delay_count);
}

void clear_cur_delay_eoc_count(void)
{
	g_cur_eoc_delay_count = 0;
	hwlog_info("clear g_cur_eoc_delay_count = 0\n");
}

void huawei_check_delay_en_eoc(void)
{
	if (g_cur_eoc_delay_count == 0) {
		hwlog_info("g_cur_delay_eoc_count = 0, enable eoc\n");
		charge_enable_eoc(true);
	} else if (g_cur_eoc_delay_count > 0) {
		hwlog_info("g_cur_eoc_delay_count = %d, do nothing\n", g_cur_eoc_delay_count);
		g_cur_eoc_delay_count--;
	} else {
		hwlog_err("invalid g_cur_eoc_delay_count = %d, reset to 0\n", g_cur_eoc_delay_count);
		g_cur_eoc_delay_count = 0;
	}
}
