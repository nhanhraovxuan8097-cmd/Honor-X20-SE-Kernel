/*
 * direct_charge_ic_manager.c
 *
 * direct charge ic management interface
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
#include <chipset_common/hwpower/power_common.h>
#include <chipset_common/hwpower/power_calibration.h>
#include <chipset_common/hwpower/power_printk.h>
#include <chipset_common/hwpower/direct_charge/direct_charge_ic_manager.h>

#define HWLOG_TAG direct_charge_ic_manager
HWLOG_REGIST();

static struct dc_batinfo_ops *g_sc_batinfo_ops[CHARGE_IC_MAX_NUM];
static struct dc_batinfo_ops *g_lvc_batinfo_ops[CHARGE_IC_MAX_NUM];
static struct dc_ic_ops *g_lvc_ic_ops[CHARGE_IC_MAX_NUM];
static struct dc_ic_ops *g_sc_ic_ops[CHARGE_IC_MAX_NUM];

static struct dc_ic_ops *dc_get_ic_ops(int mode, unsigned int type)
{
	if (type >= CHARGE_IC_TYPE_MAX)
		return NULL;

	switch (mode) {
	case LVC_MODE:
		return g_lvc_ic_ops[type];
	case SC_MODE:
	case HSC_MODE:
		return g_sc_ic_ops[type];
	default:
		return NULL;
	}
}

static struct dc_batinfo_ops *dc_get_battinfo_ops(int mode, unsigned int type)
{
	if (type >= CHARGE_IC_TYPE_MAX)
		return NULL;

	switch (mode) {
	case LVC_MODE:
		return g_lvc_batinfo_ops[type];
	case SC_MODE:
	case HSC_MODE:
		return g_sc_batinfo_ops[type];
	default:
		return NULL;
	}
}

static inline int dc_check_ic_type(unsigned int type)
{
	if (!(type & CHARGE_MULTI_IC))
		return -1;

	return 0;
}

static struct dc_ic_ops *dc_get_current_ic_ops(int mode, unsigned int type)
{
	switch (type) {
	case CHARGE_MULTI_IC:
	case CHARGE_IC_MAIN:
		return dc_get_ic_ops(mode, CHARGE_IC_TYPE_MAIN);
	case CHARGE_IC_AUX:
		return dc_get_ic_ops(mode, CHARGE_IC_TYPE_AUX);
	default:
		return NULL;
	}
}

static struct dc_batinfo_ops *dc_get_current_battinfo_ops(int mode, unsigned int type)
{
	switch (type) {
	case CHARGE_MULTI_IC:
	case CHARGE_IC_MAIN:
		return dc_get_battinfo_ops(mode, CHARGE_IC_TYPE_MAIN);
	case CHARGE_IC_AUX:
		return dc_get_battinfo_ops(mode, CHARGE_IC_TYPE_AUX);
	default:
		return NULL;
	}
}

int dc_ic_ops_register(int mode, unsigned int type, struct dc_ic_ops *ops)
{
	if (!ops || type >= CHARGE_IC_TYPE_MAX)
		return -1;

	switch (mode) {
	case LVC_MODE:
		g_lvc_ic_ops[type] = ops;
		break;
	case SC_MODE:
	case HSC_MODE:
		g_sc_ic_ops[type] = ops;
		break;
	default:
		hwlog_err("mode is error\n");
		return -1;
	}

	hwlog_info("mode %d, type %u ic register ok\n", mode, type);
	return 0;
}

int dc_batinfo_ops_register(int mode, unsigned int type,
	struct dc_batinfo_ops *ops)
{
	if (!ops || type >= CHARGE_IC_TYPE_MAX)
		return -1;

	switch (mode) {
	case LVC_MODE:
		g_lvc_batinfo_ops[type] = ops;
		break;
	case SC_MODE:
	case HSC_MODE:
		g_sc_batinfo_ops[type] = ops;
		break;
	default:
		hwlog_err("mode is error\n");
		return -1;
	}

	hwlog_info("mode %d, type %u batinfo register ok\n", mode, type);
	return 0;
}

int dc_set_vusb_uv_det(int mode, unsigned int type, u8 val)
{
	int ret = 0;
	struct dc_ic_ops *temp_ops = NULL;

	if (dc_check_ic_type(type))
		return -1;

	if (type & CHARGE_IC_MAIN) {
		temp_ops = dc_get_ic_ops(mode, CHARGE_IC_TYPE_MAIN);
		if (temp_ops && temp_ops->vusb_uv_det_enable) {
			hwlog_info("%s CHARGE_IC_TYPE_MAIN\n", __func__);
			temp_ops->vusb_uv_det_enable(val, temp_ops->dev_data);
		}
	}
	if (type & CHARGE_IC_AUX) {
		temp_ops = dc_get_ic_ops(mode, CHARGE_IC_TYPE_AUX);
		if (temp_ops && temp_ops->vusb_uv_det_enable) {
			hwlog_info("%s CHARGE_IC_TYPE_AUX\n", __func__);
			temp_ops->vusb_uv_det_enable(val, temp_ops->dev_data);
		}
	}

	return ret;
}

int dc_ic_init(int mode, unsigned int type)
{
	int ret = 0;
	struct dc_ic_ops *temp_ops = NULL;

	hwlog_info("ic init type %u\n", type);

	if (dc_check_ic_type(type))
		return -1;

	if (type & CHARGE_IC_MAIN) {
		temp_ops = dc_get_ic_ops(mode, CHARGE_IC_TYPE_MAIN);
		if (!temp_ops || !temp_ops->ic_init)
			return -1;
		ret = temp_ops->ic_init(temp_ops->dev_data);
	}
	if (type & CHARGE_IC_AUX) {
		temp_ops = dc_get_ic_ops(mode, CHARGE_IC_TYPE_AUX);
		if (!temp_ops || !temp_ops->ic_init)
			return -1;
		ret += temp_ops->ic_init(temp_ops->dev_data);
	}

	return ret;
}

int dc_ic_exit(int mode, unsigned int type)
{
	int ret = 0;
	struct dc_ic_ops *temp_ops = NULL;

	hwlog_info("ic exit type %u\n", type);

	if (dc_check_ic_type(type))
		return -1;

	if (type & CHARGE_IC_MAIN) {
		temp_ops = dc_get_ic_ops(mode, CHARGE_IC_TYPE_MAIN);
		if (!temp_ops || !temp_ops->ic_exit)
			return -1;
		ret = temp_ops->ic_exit(temp_ops->dev_data);
	}
	if (type & CHARGE_IC_AUX) {
		temp_ops = dc_get_ic_ops(mode, CHARGE_IC_TYPE_AUX);
		if (!temp_ops || !temp_ops->ic_exit)
			return -1;
		ret += temp_ops->ic_exit(temp_ops->dev_data);
	}

	return ret;
}

int dc_ic_enable(int mode, unsigned int type, int enable)
{
	int ret = 0;
	struct dc_ic_ops *temp_ops = NULL;

	hwlog_info("ic enable type %u, enable %d\n", type, enable);

	if (dc_check_ic_type(type))
		return -1;

	if (type & CHARGE_IC_MAIN) {
		temp_ops = dc_get_ic_ops(mode, CHARGE_IC_TYPE_MAIN);
		if (!temp_ops || !temp_ops->ic_enable)
			return -1;
		ret = temp_ops->ic_enable(enable, temp_ops->dev_data);
	}
	if (type & CHARGE_IC_AUX) {
		temp_ops = dc_get_ic_ops(mode, CHARGE_IC_TYPE_AUX);
		if (!temp_ops || !temp_ops->ic_enable)
			return -1;
		ret += temp_ops->ic_enable(enable, temp_ops->dev_data);
	}

	return ret;
}

int dc_ic_vac_insert_control(int mode, unsigned int type, int enable)
{
	int ret = 0;
	struct dc_ic_ops *temp_ops = NULL;

	hwlog_info("ic vac insert: mode %d, type %u, enable %d\n", mode, type, enable);

	if (dc_check_ic_type(type))
		return -1;

	if (type & CHARGE_IC_MAIN) {
		temp_ops = dc_get_ic_ops(mode, CHARGE_IC_TYPE_MAIN);
		if (!temp_ops || !temp_ops->ic_vac_insert_control)
			return -1;
		ret = temp_ops->ic_vac_insert_control(enable, temp_ops->dev_data);
	}
	if (type & CHARGE_IC_AUX) {
		temp_ops = dc_get_ic_ops(mode, CHARGE_IC_TYPE_AUX);
		if (!temp_ops || !temp_ops->ic_vac_insert_control)
			return -1;
		ret += temp_ops->ic_vac_insert_control(enable, temp_ops->dev_data);
	}

	return ret;
}

int dc_ic_adc_enable(int mode, unsigned int type, int enable)
{
	int ret = 0;
	struct dc_ic_ops *temp_ops = NULL;

	if (dc_check_ic_type(type))
		return -1;

	if (type & CHARGE_IC_MAIN) {
		temp_ops = dc_get_ic_ops(mode, CHARGE_IC_TYPE_MAIN);
		if (!temp_ops || !temp_ops->ic_adc_enable)
			return -1;
		ret = temp_ops->ic_adc_enable(enable, temp_ops->dev_data);
	}
	if (type & CHARGE_IC_AUX) {
		temp_ops = dc_get_ic_ops(mode, CHARGE_IC_TYPE_AUX);
		if (!temp_ops || !temp_ops->ic_adc_enable)
			return -1;
		ret += temp_ops->ic_adc_enable(enable, temp_ops->dev_data);
	}

	hwlog_info("ic adc enable type %u, enable %d\n", type, enable);
	return ret;
}

int dc_ic_discharge(int mode, unsigned int type, int enable)
{
	int ret = 0;
	struct dc_ic_ops *temp_ops = NULL;

	if (dc_check_ic_type(type))
		return -1;

	if (type & CHARGE_IC_MAIN) {
		temp_ops = dc_get_ic_ops(mode, CHARGE_IC_TYPE_MAIN);
		if (!temp_ops || !temp_ops->ic_discharge)
			return -1;
		ret = temp_ops->ic_discharge(enable, temp_ops->dev_data);
	}
	if (type & CHARGE_IC_AUX) {
		temp_ops = dc_get_ic_ops(mode, CHARGE_IC_TYPE_AUX);
		if (!temp_ops || !temp_ops->ic_discharge)
			return -1;
		ret += temp_ops->ic_discharge(enable, temp_ops->dev_data);
	}

	return ret;
}

int dc_is_ic_close(int mode, unsigned int type)
{
	int ret = 0;
	struct dc_ic_ops *temp_ops = NULL;

	if (dc_check_ic_type(type))
		return -1;

	if (type & CHARGE_IC_MAIN) {
		temp_ops = dc_get_ic_ops(mode, CHARGE_IC_TYPE_MAIN);
		if (!temp_ops || !temp_ops->is_ic_close)
			return -1;
		ret = temp_ops->is_ic_close(temp_ops->dev_data);
	}
	if (type & CHARGE_IC_AUX) {
		temp_ops = dc_get_ic_ops(mode, CHARGE_IC_TYPE_AUX);
		if (!temp_ops || !temp_ops->is_ic_close)
			return -1;
		ret += temp_ops->is_ic_close(temp_ops->dev_data);
	}

	return ret;
}

bool dc_is_ic_support_prepare(int mode, unsigned int type)
{
	struct dc_ic_ops *temp_ops = NULL;

	if (dc_check_ic_type(type))
		return false;

	temp_ops = dc_get_current_ic_ops(mode, type);
	if (temp_ops && temp_ops->ic_enable_prepare)
		return true;

	return false;
}

int dc_ic_enable_prepare(int mode, unsigned int type)
{
	int ret = 0;
	struct dc_ic_ops *temp_ops = NULL;

	if (dc_check_ic_type(type))
		return -1;

	if (type & CHARGE_IC_MAIN) {
		temp_ops = dc_get_ic_ops(mode, CHARGE_IC_TYPE_MAIN);
		if (!temp_ops || !temp_ops->ic_enable_prepare)
			return -1;
		ret = temp_ops->ic_enable_prepare(temp_ops->dev_data);
	}
	if (type & CHARGE_IC_AUX) {
		temp_ops = dc_get_ic_ops(mode, CHARGE_IC_TYPE_AUX);
		if (!temp_ops || !temp_ops->ic_enable_prepare)
			return -1;
		ret += temp_ops->ic_enable_prepare(temp_ops->dev_data);
	}

	return ret;
}

int dc_config_ic_watchdog(int mode, unsigned int type, int time)
{
	int ret = 0;
	struct dc_ic_ops *temp_ops = NULL;

	if (dc_check_ic_type(type))
		return -1;

	if (type & CHARGE_IC_MAIN) {
		temp_ops = dc_get_ic_ops(mode, CHARGE_IC_TYPE_MAIN);
		if (!temp_ops || !temp_ops->config_ic_watchdog)
			return -1;
		ret = temp_ops->config_ic_watchdog(time, temp_ops->dev_data);
	}
	if (type & CHARGE_IC_AUX) {
		temp_ops = dc_get_ic_ops(mode, CHARGE_IC_TYPE_AUX);
		if (!temp_ops || !temp_ops->config_ic_watchdog)
			return -1;
		ret += temp_ops->config_ic_watchdog(time, temp_ops->dev_data);
	}

	return ret;
}

int dc_kick_ic_watchdog(int mode, unsigned int type)
{
	int ret = 0;
	struct dc_ic_ops *temp_ops = NULL;

	if (dc_check_ic_type(type))
		return -1;

	if (type & CHARGE_IC_MAIN) {
		temp_ops = dc_get_ic_ops(mode, CHARGE_IC_TYPE_MAIN);
		if (!temp_ops || !temp_ops->kick_ic_watchdog)
			return -1;
		ret = temp_ops->kick_ic_watchdog(temp_ops->dev_data);
	}
	if (type & CHARGE_IC_AUX) {
		temp_ops = dc_get_ic_ops(mode, CHARGE_IC_TYPE_AUX);
		if (!temp_ops || !temp_ops->kick_ic_watchdog)
			return -1;
		ret += temp_ops->kick_ic_watchdog(temp_ops->dev_data);
	}

	return ret;
}

int dc_get_ic_id(int mode, unsigned int type)
{
	struct dc_ic_ops *temp_ops = NULL;

	if (dc_check_ic_type(type))
		return -1;

	temp_ops = dc_get_current_ic_ops(mode, type);
	if (temp_ops && temp_ops->get_ic_id)
		return temp_ops->get_ic_id(temp_ops->dev_data);

	return -1;
}

int dc_get_ic_status(int mode, unsigned int type)
{
	int ret = 0;
	struct dc_ic_ops *temp_ops = NULL;

	if (dc_check_ic_type(type))
		return -1;

	if (type & CHARGE_IC_MAIN) {
		temp_ops = dc_get_ic_ops(mode, CHARGE_IC_TYPE_MAIN);
		if (!temp_ops || !temp_ops->get_ic_status)
			return -1;
		ret = temp_ops->get_ic_status(temp_ops->dev_data);
	}
	if (type & CHARGE_IC_AUX) {
		temp_ops = dc_get_ic_ops(mode, CHARGE_IC_TYPE_AUX);
		if (!temp_ops || !temp_ops->get_ic_status)
			return -1;
		ret += temp_ops->get_ic_status(temp_ops->dev_data);
	}

	return ret;
}

int dc_set_ic_buck_enable(int mode, unsigned int type, int enable)
{
	int ret = 0;
	struct dc_ic_ops *temp_ops = NULL;

	if (dc_check_ic_type(type))
		return -1;

	if (type & CHARGE_IC_MAIN) {
		temp_ops = dc_get_ic_ops(mode, CHARGE_IC_TYPE_MAIN);
		if (!temp_ops || !temp_ops->set_ic_buck_enable)
			return -1;
		ret = temp_ops->set_ic_buck_enable(enable, temp_ops->dev_data);
	}
	if (type & CHARGE_IC_AUX) {
		temp_ops = dc_get_ic_ops(mode, CHARGE_IC_TYPE_AUX);
		if (!temp_ops || !temp_ops->set_ic_buck_enable)
			return -1;
		ret += temp_ops->set_ic_buck_enable(enable, temp_ops->dev_data);
	}

	hwlog_info("ic buck enable type %u, enable %d\n", type, enable);
	return ret;
}

int dc_ic_reg_reset_and_init(int mode, unsigned int type)
{
	int ret = 0;
	struct dc_ic_ops *temp_ops = NULL;

	hwlog_info("ic reg reset and init, type: %u\n", type);

	if (dc_check_ic_type(type))
		return -1;

	if (type & CHARGE_IC_MAIN) {
		temp_ops = dc_get_ic_ops(mode, CHARGE_IC_TYPE_MAIN);
		if (!temp_ops || !temp_ops->ic_reg_reset_and_init)
			return -1;
		ret = temp_ops->ic_reg_reset_and_init(temp_ops->dev_data);
	}
	if (type & CHARGE_IC_AUX) {
		temp_ops = dc_get_ic_ops(mode, CHARGE_IC_TYPE_AUX);
		if (!temp_ops || !temp_ops->ic_reg_reset_and_init)
			return -1;
		ret += temp_ops->ic_reg_reset_and_init(temp_ops->dev_data);
	}

	return ret;
}

const char *dc_get_ic_name(int mode, unsigned int type)
{
	struct dc_ic_ops *temp_ops = NULL;

	if (dc_check_ic_type(type))
		return "invalid ic";

	temp_ops = dc_get_current_ic_ops(mode, type);
	if (temp_ops && temp_ops->dev_name)
		return temp_ops->dev_name;

	return "invalid ic";
}

int dc_ic_set_sc_mode(int mode, unsigned int type, int sc_mode)
{
	int ret = 0;
	struct dc_ic_ops *temp_ops = NULL;

	hwlog_info("dc_ic_set_sc_mode type %d\n", type);

	if (dc_check_ic_type(type))
		return -1;

	if (type & CHARGE_IC_MAIN) {
		temp_ops = dc_get_ic_ops(mode, CHARGE_IC_TYPE_MAIN);
		if (!temp_ops || !temp_ops->ic_set_sc_mode)
			return 0;
		ret = temp_ops->ic_set_sc_mode(sc_mode, temp_ops->dev_data);
	}
	if (type & CHARGE_IC_AUX) {
		temp_ops = dc_get_ic_ops(mode, CHARGE_IC_TYPE_AUX);
		if (!temp_ops || !temp_ops->ic_set_sc_mode)
			return 0;
		ret += temp_ops->ic_set_sc_mode(sc_mode, temp_ops->dev_data);
	}

	return ret;
}

int dc_ic_set_ovp_gate(int mode, unsigned int type, int ovp_gate)
{
	int ret = 0;
	struct dc_ic_ops *temp_ops = NULL;

	if (dc_check_ic_type(type))
		return -1;

	if (type & CHARGE_IC_MAIN) {
		temp_ops = dc_get_ic_ops(mode, CHARGE_IC_TYPE_MAIN);
		if (!temp_ops || !temp_ops->ic_set_ovp_gate)
			return 0;
		ret = temp_ops->ic_set_ovp_gate(ovp_gate, temp_ops->dev_data);
	}
	if (type & CHARGE_IC_AUX) {
		temp_ops = dc_get_ic_ops(mode, CHARGE_IC_TYPE_AUX);
		if (!temp_ops || !temp_ops->ic_set_ovp_gate)
			return 0;
		ret += temp_ops->ic_set_ovp_gate(ovp_gate, temp_ops->dev_data);
	}

	return ret;
}

int dc_batinfo_init(int mode, unsigned int type)
{
	int ret = 0;
	struct dc_batinfo_ops *temp_ops = NULL;

	if (dc_check_ic_type(type))
		return -1;

	if (type & CHARGE_IC_MAIN) {
		temp_ops = dc_get_battinfo_ops(mode, CHARGE_IC_TYPE_MAIN);
		if (!temp_ops || !temp_ops->init)
			return -1;
		ret = temp_ops->init(temp_ops->dev_data);
	}
	if (type & CHARGE_IC_AUX) {
		temp_ops = dc_get_battinfo_ops(mode, CHARGE_IC_TYPE_AUX);
		if (!temp_ops || !temp_ops->init)
			return -1;
		ret += temp_ops->init(temp_ops->dev_data);
	}

	return ret;
}

int dc_batinfo_exit(int mode, unsigned int type)
{
	int ret = 0;
	struct dc_batinfo_ops *temp_ops = NULL;

	if (dc_check_ic_type(type))
		return -1;

	if (type & CHARGE_IC_MAIN) {
		temp_ops = dc_get_battinfo_ops(mode, CHARGE_IC_TYPE_MAIN);
		if (!temp_ops || !temp_ops->exit)
			return -1;
		ret = temp_ops->exit(temp_ops->dev_data);
	}
	if (type & CHARGE_IC_AUX) {
		temp_ops = dc_get_battinfo_ops(mode, CHARGE_IC_TYPE_AUX);
		if (!temp_ops || !temp_ops->exit)
			return -1;
		ret += temp_ops->exit(temp_ops->dev_data);
	}

	return ret;
}

int dc_get_bat_btb_voltage(int mode, unsigned int type)
{
	struct dc_batinfo_ops *temp_ops = NULL;

	if (dc_check_ic_type(type))
		return -1;

	temp_ops = dc_get_current_battinfo_ops(mode, type);
	if (temp_ops && temp_ops->get_bat_btb_voltage)
		return temp_ops->get_bat_btb_voltage(temp_ops->dev_data);

	return -1;
}

int dc_get_bat_btb_voltage_with_comp(int mode, unsigned int type, const int *vbat_comp)
{
	int vbat;
	int comp;

	if (!vbat_comp)
		return -1;

	if (dc_check_ic_type(type))
		return -1;

	/* vbat_comp[0] : main ic comp, vbat_comp[1] : aux ic comp */
	switch (type) {
	case CHARGE_MULTI_IC:
	case CHARGE_IC_MAIN:
		comp = vbat_comp[CHARGE_IC_TYPE_MAIN];
		break;
	case CHARGE_IC_AUX:
		comp = vbat_comp[CHARGE_IC_TYPE_AUX];
		break;
	default:
		comp = 0;
		break;
	}

	vbat = dc_get_bat_btb_voltage(mode, type);
	if (vbat < 0)
		return -1;
	else if (vbat == 0)
		return vbat;

	return vbat + comp;
}

int dc_get_bat_max_btb_voltage_with_comp(int mode, const int *vbat_comp)
{
	int vbat_main;
	int vbat_aux;

	if (!vbat_comp)
		return -1;

	vbat_main = dc_get_bat_btb_voltage_with_comp(mode, CHARGE_IC_MAIN, vbat_comp);
	vbat_aux = dc_get_bat_btb_voltage_with_comp(mode, CHARGE_IC_AUX, vbat_comp);

	return (vbat_main > vbat_aux) ? vbat_main : vbat_aux;
}

int dc_get_bat_package_voltage(int mode, unsigned int type)
{
	struct dc_batinfo_ops *temp_ops = NULL;

	if (dc_check_ic_type(type))
		return -1;

	temp_ops = dc_get_current_battinfo_ops(mode, type);
	if (temp_ops && temp_ops->get_bat_package_voltage)
		return temp_ops->get_bat_package_voltage(temp_ops->dev_data);

	return -1;
}

int dc_get_vbus_voltage(int mode, unsigned int type, int *vbus)
{
	struct dc_batinfo_ops *temp_ops = NULL;

	if (!vbus)
		return -1;

	if (dc_check_ic_type(type))
		return -1;

	temp_ops = dc_get_current_battinfo_ops(mode, type);
	if (temp_ops && temp_ops->get_vbus_voltage)
		return temp_ops->get_vbus_voltage(vbus, temp_ops->dev_data);

	return -1;
}

int dc_get_bat_current(int mode, unsigned int type, int *ibat)
{
	struct dc_batinfo_ops *temp_ops = NULL;

	if (!ibat)
		return -1;

	if (dc_check_ic_type(type))
		return -1;

	temp_ops = dc_get_current_battinfo_ops(mode, type);
	if (temp_ops && temp_ops->get_bat_current)
		return temp_ops->get_bat_current(ibat, temp_ops->dev_data);

	return -1;
}

static void dc_get_ibat_cali_para(unsigned int type, int *c_offset_a, int *c_offset_b)
{
	if (type & CHARGE_IC_MAIN) {
		/* 0:CUR_A_OFFSET 1:CUR_B_OFFSET */
		power_cali_common_get_data("dc", 0, c_offset_a);
		power_cali_common_get_data("dc", 1, c_offset_b);
		return;
	}
	if (type & CHARGE_IC_AUX) {
		/* 2:AUX_CUR_A_OFFSET 3:AUX_CUR_B_OFFSET */
		power_cali_common_get_data("dc", 2, c_offset_a);
		power_cali_common_get_data("dc", 3, c_offset_b);
		return;
	}
}

int dc_get_bat_current_with_calibration(int mode, unsigned int type, int *ibat, int comb)
{
	s64 temp;
	int c_offset_a = 0;
	int c_offset_b = 0;
	int ret;

	if (!ibat)
		return -1;

	if (dc_check_ic_type(type))
		return -1;

	if (comb) {
		ret = dc_get_total_bat_current(mode, ibat);
	} else {
		ret = dc_get_bat_current(mode, type, ibat);
	}
	if (ret)
		return -1;

	hwlog_info("cali_b: bat_cur=%d\n", *ibat);
	dc_get_ibat_cali_para(type, &c_offset_a, &c_offset_b);
	if (c_offset_a) {
		temp = (*ibat * (s64)c_offset_a) + c_offset_b;
		*ibat = (int)div_s64(temp, 1000000); /* base 1000000 */
		hwlog_info("cali_a: bat_cur=%d,c_offset_a=%d,c_offset_b=%d\n",
			*ibat, c_offset_a, c_offset_b);
	}
	return 0;
}

int dc_get_ic_ibus(int mode, unsigned int type, int *ibus)
{
	int main_ibus = 0;
	int aux_ibus = 0;
	int ic_count = 0;
	struct dc_batinfo_ops *temp_ops = NULL;

	if (!ibus)
		return -1;

	if (dc_check_ic_type(type))
		return -1;

	if (type & CHARGE_IC_MAIN) {
		temp_ops = dc_get_battinfo_ops(mode, CHARGE_IC_TYPE_MAIN);
		if (temp_ops && temp_ops->get_ic_ibus &&
			!temp_ops->get_ic_ibus(&main_ibus, temp_ops->dev_data))
			ic_count++;
	}
	if (type & CHARGE_IC_AUX) {
		temp_ops = dc_get_battinfo_ops(mode, CHARGE_IC_TYPE_AUX);
		if (temp_ops && temp_ops->get_ic_ibus &&
			!temp_ops->get_ic_ibus(&aux_ibus, temp_ops->dev_data))
			ic_count++;
	}

	if (!ic_count)
		return -1;

	*ibus = main_ibus + aux_ibus;
	if (type == CHARGE_MULTI_IC)
		hwlog_info("main_ibus = %d, aux_ibus = %d\n", main_ibus, aux_ibus);

	return 0;
}

int dc_get_ic_temp(int mode, unsigned int type, int *temp)
{
	struct dc_batinfo_ops *temp_ops = NULL;

	if (!temp)
		return -1;

	if (dc_check_ic_type(type))
		return -1;

	temp_ops = dc_get_current_battinfo_ops(mode, type);
	if (temp_ops && temp_ops->get_ic_temp)
		return temp_ops->get_ic_temp(temp, temp_ops->dev_data);

	return 0;
}

int dc_get_ic_vusb(int mode, unsigned int type, int *vusb)
{
	struct dc_batinfo_ops *temp_ops = NULL;

	if (!vusb)
		return -1;

	if (dc_check_ic_type(type))
		return -1;

	temp_ops = dc_get_current_battinfo_ops(mode, type);
	if (temp_ops && temp_ops->get_ic_vusb)
		return temp_ops->get_ic_vusb(vusb, temp_ops->dev_data);

	return -1;
}

int dc_get_ic_vout(int mode, unsigned int type, int *vout)
{
	struct dc_batinfo_ops *temp_ops = NULL;

	if (!vout)
		return -1;

	if (dc_check_ic_type(type))
		return -1;

	temp_ops = dc_get_current_battinfo_ops(mode, type);
	if (temp_ops && temp_ops->get_ic_vout)
		return temp_ops->get_ic_vout(vout, temp_ops->dev_data);

	return -1;
}

int dc_get_ic_max_temp(int mode, int *temp)
{
	int temp1 = 0;
	int temp2 = 0;
	int ret1;
	int ret2;

	if (!temp)
		return -1;

	ret1 = dc_get_ic_temp(mode, CHARGE_IC_MAIN, &temp1);
	ret2 = dc_get_ic_temp(mode, CHARGE_IC_AUX, &temp2);

	*temp = (temp1 > temp2 ? temp1 : temp2);

	return (ret1 && ret2);
}

int dc_get_total_bat_current(int mode, int *ibat)
{
	int main_ibat = 0;
	int aux_ibat = 0;
	int ic_count = 0;

	if (!ibat)
		return -1;

	if (!dc_get_bat_current(mode, CHARGE_IC_MAIN, &main_ibat))
		ic_count++;
	if (!dc_get_bat_current(mode, CHARGE_IC_AUX, &aux_ibat))
		ic_count++;

	hwlog_info("main ibat %d, aux ibat %d\n", main_ibat, aux_ibat);

	if (!ic_count)
		return -1;

	*ibat = main_ibat + aux_ibat;

	return 0;
}

int dc_get_bat_btb_avg_voltage(int mode, int *vbat)
{
	int main_vbat;
	int aux_vbat;
	int ic_count = 0;

	if (!vbat)
		return -1;

	main_vbat = dc_get_bat_btb_voltage(mode, CHARGE_IC_MAIN);
	if (main_vbat > 0)
		ic_count++;

	aux_vbat = dc_get_bat_btb_voltage(mode, CHARGE_IC_AUX);
	if (aux_vbat > 0)
		ic_count++;

	if (!ic_count)
		return -1;

	*vbat = (main_vbat + aux_vbat) / ic_count;

	return 0;
}

int dc_get_bat_package_avg_voltage(int mode, int *vbat)
{
	int main_vbat;
	int aux_vbat;
	int ic_count = 0;

	if (!vbat)
		return -1;

	main_vbat = dc_get_bat_package_voltage(mode, CHARGE_IC_MAIN);
	if (main_vbat > 0)
		ic_count++;

	aux_vbat = dc_get_bat_package_voltage(mode, CHARGE_IC_AUX);
	if (aux_vbat > 0)
		ic_count++;

	if (!ic_count)
		return -1;

	*vbat = (main_vbat + aux_vbat) / ic_count;

	return 0;
}

int dc_get_vbus_avg_voltage(int mode, int *vbus)
{
	int main_vbus = 0;
	int aux_vbus = 0;
	int ic_count = 0;

	if (!vbus)
		return -1;

	if (!dc_get_vbus_voltage(mode, CHARGE_IC_MAIN, &main_vbus))
		ic_count++;
	if (!dc_get_vbus_voltage(mode, CHARGE_IC_AUX, &aux_vbus))
		ic_count++;

	if (!ic_count)
		return -1;

	*vbus = (main_vbus + aux_vbus) / ic_count;

	return 0;
}
