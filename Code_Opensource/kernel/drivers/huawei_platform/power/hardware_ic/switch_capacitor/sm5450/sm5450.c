/*
 * sm5450.c
 *
 * sm5450 driver
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

#include "sm5450.h"
#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/jiffies.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/of_gpio.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/notifier.h>
#include <linux/mutex.h>
#include <linux/raid/pq.h>
#include <huawei_platform/log/hw_log.h>
#include <chipset_common/hwpower/power_i2c.h>
#include <chipset_common/hwpower/power_log.h>
#include <chipset_common/hwpower/power_gpio.h>
#include <huawei_platform/power/direct_charger/direct_charger.h>
#include <huawei_platform/power/huawei_charger_common.h>
#include <huawei_platform/power/common_module/power_platform.h>

#ifdef HWLOG_TAG
#undef HWLOG_TAG
#endif

#define HWLOG_TAG sm5450
HWLOG_REGIST();
static DEFINE_MUTEX(g_sm5450_log_lock);

static struct sm5450_device_info *g_sm5450_scp_dev;
static u32 scp_error_flag;
static bool adp_plugout;

#define MSG_LEN                             2
#define BUF_LEN                             80
#define PAGE0_NUM                           0x12
#define PAGE1_NUM                           0x07
#define PAGE0_BASE                          SM5450_CONTROL1_REG
#define PAGE1_BASE                          SM5450_SCP_CTL_REG
#define SM5450_DBG_VAL_SIZE                 6

static int sm5450_config_rlt_ovp_ref(int rltovp_rate, void *dev_data);
static int sm5450_config_rlt_uvp_ref(int rltuvp_rate, void *dev_data);
static int sm5450_config_vbuscon_ovp_ref_mv(int ovp_threshold, void *dev_data);
static int sm5450_config_vbus_ovp_ref_mv(int ovp_threshold, void *dev_data);
static int sm5450_config_ibus_ocp_ref_ma(int ocp_threshold, int chg_mode, void *dev_data);
static int sm5450_config_ibus_ucp_ref_ma(int ucp_threshold, void *dev_data);
static int sm5450_config_vbat_ovp_ref_mv(int ovp_threshold, void *dev_data);
static int sm5450_config_ibat_ocp_ref_ma(int ocp_threshold, void *dev_data);
static int sm5450_config_ibat_reg_ref_ma(int ibat_regulation, void *dev_data);
static int sm5450_config_vbat_reg_ref_mv(int vbat_regulation, void *dev_data);
static int sm5450_reg_init(void *dev_data);

static int sm5450_read_block(struct sm5450_device_info *di,
	u8 *value, u8 reg, unsigned int num_bytes)
{
	return power_i2c_read_block(di->client, &reg, 1, value, num_bytes);
}

static int sm5450_write_byte(struct sm5450_device_info *di,
	u8 reg, u8 value)
{
	if (!di || di->chip_already_init == 0) {
		hwlog_err("chip not init\n");
		return -EIO;
	}

	return power_i2c_u8_write_byte(di->client, reg, value);
}

static int sm5450_read_byte(struct sm5450_device_info *di,
	u8 reg, u8 *value)
{
	if (!di || di->chip_already_init == 0) {
		hwlog_err("chip not init\n");
		return -EIO;
	}

	return power_i2c_u8_read_byte(di->client, reg, value);
}

static int sm5450_write_mask(struct sm5450_device_info *di,
	u8 reg, u8 mask, u8 shift, u8 value)
{
	int ret;
	u8 val = 0;

	ret = sm5450_read_byte(di, reg, &val);
	if (ret < 0)
		return ret;

	val &= ~mask;
	val |= ((value << shift) & mask);

	return sm5450_write_byte(di, reg, val);
}

static void sm5450_dump_register(void *dev_data)
{
	u8 i;
	int ret;
	u8 val = 0;
	u8 val1[2] = {0}; /* read two bytes */
	struct sm5450_device_info *di = (struct sm5450_device_info *)dev_data;

	if (!di)
		return;

	for (i = SM5450_CONTROL1_REG; i <= SM5450_ADCCTRL_REG; i++) {
		if (!((i == SM5450_FLAG1_REG) || (i == SM5450_FLAG2_REG) ||
			(i == SM5450_FLAG3_REG))) {
			ret = sm5450_read_byte(di, i, &val);
			if (ret)
				hwlog_err("dump_register read fail\n");
			hwlog_info("reg [%x]=0x%x\n", i, val);
		}
	}

	ret = sm5450_read_block(di, &val1[0], SM5450_VBUSADC_H_REG, 2);
	if (ret)
		hwlog_err("dump_register read fail\n");
	else
		hwlog_info("reg [%x]=0x%x, reg [%x]=0x%x\n",
			SM5450_VBUSADC_H_REG, val1[0], SM5450_VBUSADC_L_REG, val1[1]);
	ret = sm5450_read_block(di, &val1[0], SM5450_IBATADC_H_REG, 2);
	if (ret)
		hwlog_err("dump_register read fail\n");
	else
		hwlog_info("reg [%x]=0x%x, reg [%x]=0x%x\n",
			SM5450_IBATADC_H_REG, val1[0], SM5450_IBATADC_L_REG, val1[1]);
	ret = sm5450_read_block(di, &val1[0], SM5450_IBUSADC_H_REG, 2);
	if (ret)
		hwlog_err("dump_register read fail\n");
	else
		hwlog_info("reg [%x]=0x%x, reg [%x]=0x%x\n",
			SM5450_IBUSADC_H_REG, val1[0], SM5450_IBUSADC_L_REG, val1[1]);
	ret = sm5450_read_block(di, &val1[0], SM5450_VBATADC_H_REG, 2);
	if (ret)
		hwlog_err("dump_register read fail\n");
	else
		hwlog_info("reg [%x]=0x%x, reg [%x]=0x%x\n",
			SM5450_VBATADC_H_REG, val1[0], SM5450_VBATADC_L_REG, val1[1]);

	ret = sm5450_read_byte(di, SM5450_TDIEADC_REG, &val);
	if (ret)
		hwlog_err("dump_register read fail\n");
	else
		hwlog_info("reg [%x]=0x%x\n", SM5450_VBATADC_H_REG, val);

	for (i = SM5450_SCP_CTL_REG; i <= SM5450_SCP_STIMER_REG; i++) {
		if (!((i == SM5450_SCP_ISR1_REG) || (i == SM5450_SCP_ISR2_REG))) {
			ret = sm5450_read_byte(di, i, &val);
			if (ret)
				hwlog_err("dump_register read fail\n");
			hwlog_info("reg [%x]=0x%x\n", i, val);
		}
	}
}

static int sm5450_reg_reset(void *dev_data)
{
	int ret;
	u8 reg = 0;
	struct sm5450_device_info *di = (struct sm5450_device_info *)dev_data;

	ret = sm5450_write_mask(di, SM5450_CONTROL1_REG,
		SM5450_CONTROL1_RESET_MASK, SM5450_CONTROL1_RESET_SHIFT,
		SM5450_CONTROL1_RST_ENABLE);
	if (ret)
		return -1;
	usleep_range(1000, 1100); /* wait soft reset ready, min:500us */
	ret = sm5450_read_byte(di, SM5450_CONTROL1_REG, &reg);
	if (ret)
		return -1;

	hwlog_info("reg_reset [%x]=0x%x\n", SM5450_CONTROL1_REG, reg);
	return 0;
}

static int sm5450_reg_reset_and_init(void *dev_data)
{
	int ret;

	ret = sm5450_reg_reset(dev_data);
	ret += sm5450_reg_init(dev_data);

	hwlog_info("%s finish, ret = %d\n", __func__, ret);
	return 0;
}

static void sm5450_common_opt_regs(void *dev_data);
static void sm5450_lvc_opt_regs(void *dev_data);
static int sm5450_lvc_charge_enable(int enable, void *dev_data)
{
	int ret;
	u8 reg = 0;
	u8 value = enable ? SM5450_CONTROL1_FBYPASSMODE : SM5450_CONTROL1_OFFMODE;
	struct sm5450_device_info *di = (struct sm5450_device_info *)dev_data;

	if (value == SM5450_CONTROL1_FBYPASSMODE)
		sm5450_lvc_opt_regs(di);

	/* 1. OFFMODE */
	ret = sm5450_write_mask(di, SM5450_CONTROL1_REG, SM5450_CONTROL1_OPMODE_MASK,
		SM5450_CONTROL1_OPMODE_SHIFT, SM5450_CONTROL1_OFFMODE);
	if (ret)
		return -1;

	/* 2. Enable SC mode */
	ret = sm5450_write_mask(di, SM5450_CONTROL1_REG, SM5450_CONTROL1_OPMODE_MASK,
		SM5450_CONTROL1_OPMODE_SHIFT, value);
	if (ret)
		return -1;

	if (value == SM5450_CONTROL1_OFFMODE)
		sm5450_common_opt_regs(di);

	ret = sm5450_read_byte(di, SM5450_CONTROL1_REG, &reg);
	if (ret)
		return -1;

	hwlog_info("charge_enable [%x]=0x%x\n", SM5450_CONTROL1_REG, reg);
	return 0;
}

static void sm5450_sc_opt_regs(void *dev_data);
static int sm5450_sc_charge_enable(int enable, void *dev_data)
{
	int ret;
	u8 reg = 0;
	u8 value = enable ? SM5450_CONTROL1_FCHGPUMPMODE :
		SM5450_CONTROL1_OFFMODE;
	struct sm5450_device_info *di = (struct sm5450_device_info *)dev_data;

	if (value == SM5450_CONTROL1_FCHGPUMPMODE)
		sm5450_sc_opt_regs(di);

	/* 1. OFFMODE */
	ret = sm5450_write_mask(di, SM5450_CONTROL1_REG, SM5450_CONTROL1_OPMODE_MASK,
		SM5450_CONTROL1_OPMODE_SHIFT, SM5450_CONTROL1_OFFMODE);
	if (ret)
		return -1;
	/* 2. Enable SC mode */
	ret = sm5450_write_mask(di, SM5450_CONTROL1_REG, SM5450_CONTROL1_OPMODE_MASK,
		SM5450_CONTROL1_OPMODE_SHIFT, value);
	if (ret)
		return -1;

	if (value == SM5450_CONTROL1_OFFMODE)
		sm5450_common_opt_regs(di);

	ret = sm5450_read_byte(di, SM5450_CONTROL1_REG, &reg);
	if (ret)
		return -1;

	hwlog_info("ic_role = %d, charge_enable [%x]=0x%x\n", di->ic_role, SM5450_CONTROL1_REG, reg);
	return 0;
}

static int sm5450_discharge(int enable, void *dev_data)
{
	int ret;
	u8 reg = 0;
	u8 value = enable ? 0x1 : 0x0;
	struct sm5450_device_info *di = (struct sm5450_device_info *)dev_data;

	/* VBUS PD : 0(Auto working), 1(Manual Pull down) */
	ret = sm5450_write_mask(di, SM5450_PULLDOWN_REG,
		SM5450_PULLDOWN_EN_VBUS_PD_MASK,
		SM5450_PULLDOWN_EN_VBUS_PD_SHIFT, value);
	if (ret)
		return -1;

	ret = sm5450_read_byte(di, SM5450_PULLDOWN_REG, &reg);
	if (ret)
		return -1;

	hwlog_info("discharge [%x]=0x%x\n", SM5450_PULLDOWN_REG, reg);
	return 0;
}

static int sm5450_is_device_close(void *dev_data)
{
	u8 reg = 0;
	u8 value;
	int ret;
	struct sm5450_device_info *di = (struct sm5450_device_info *)dev_data;

	ret = sm5450_read_byte(di, SM5450_CONTROL1_REG, &reg);
	if (ret)
		return 1;

	value = (reg & SM5450_CONTROL1_OPMODE_MASK) >> SM5450_CONTROL1_OPMODE_SHIFT;
	if ((value < SM5450_CONTROL1_FBYPASSMODE) || (value > SM5450_CONTROL1_FCHGPUMPMODE))
		return 1;

	return 0;
}

static int sm5450_get_device_id(void *dev_data)
{
	u8 part_info = 0;
	int ret;
	struct sm5450_device_info *di = (struct sm5450_device_info *)dev_data;

	if (!di) {
		hwlog_err("di is null\n");
		return -1;
	}

	if (di->get_id_time == SM5450_USED)
		return di->device_id;

	di->get_id_time = SM5450_USED;
	ret = sm5450_read_byte(di, SM5450_DEVICE_INFO_REG, &part_info);
	if (ret) {
		di->get_id_time = SM5450_NOT_USED;
		hwlog_err("get_device_id read fail\n");
		return -1;
	}
	hwlog_info("get_device_id [%x]=0x%x\n", SM5450_DEVICE_INFO_REG, part_info);

	part_info = part_info & SM5450_DEVICE_INFO_DEVID_MASK;
	switch (part_info) {
	case SM5450_DEVICE_ID_SM5450:
	case SY69636_DEVICE_ID_SY69636:
		di->device_id = SWITCHCAP_SM_SM5450;
		break;
	default:
		di->device_id = -1;
		hwlog_err("switchcap get dev_id fail\n");
		break;
	}

	hwlog_info("device_id : 0x%x\n", di->device_id);

	return di->device_id;
}

static int sm5450_get_revision_id(void *dev_data)
{
	u8 rev_info = 0;
	int ret;
	struct sm5450_device_info *di = (struct sm5450_device_info *)dev_data;

	if (!di) {
		hwlog_err("di is null\n");
		return -1;
	}

	if (di->get_rev_time == SM5450_USED)
		return di->rev_id;

	di->get_rev_time = SM5450_USED;
	ret = sm5450_read_byte(di, SM5450_DEVICE_INFO_REG, &rev_info);
	if (ret) {
		di->get_rev_time = SM5450_NOT_USED;
		hwlog_err("get_revision_id read fail\n");
		return -1;
	}
	hwlog_info("get_revision_id [%x]=0x%x\n", SM5450_DEVICE_INFO_REG, rev_info);

	di->rev_id = (rev_info & SM5450_DEVICE_INFO_REVID_MASK) >> SM5450_DEVICE_INFO_REVID_SHIFT;

	hwlog_info("revision_id : 0x%x\n", di->rev_id);
	return di->rev_id;
}

static int sm5450_get_vbat_mv(void *dev_data)
{
	u8 reg_high;
	u8 reg_low;
	s16 voltage;
	u8 val[2] = {0}; /* read two bytes */
	struct sm5450_device_info *di = (struct sm5450_device_info *)dev_data;

	if (di->is_use_platform_battery_voltage != 1) {
		if (sm5450_read_block(di, &val[0], SM5450_VBATADC_H_REG, 2)) {
			return -1;
		}
		reg_high = val[0];
		reg_low = val[1];
		hwlog_info("VBAT_ADC1=0x%x, VBAT_ADC0=0x%x\n", reg_high, reg_low);

		voltage = ((reg_high << SM5450_LENGTH_OF_BYTE) + reg_low) * SM5450_ADC_RESOLUTION_2;
	}
	else {
		voltage = power_platform_get_battery_voltage();
	}

	hwlog_info("%s get voltage = %d\n", __func__, voltage);

	return (int)(voltage);
}

static int sm5450_get_ibat_ma(int *ibat, void *dev_data)
{
	u8 reg_high;
	u8 reg_low;
	s16 curr;
	u8 val[2] = {0}; /* read two bytes */
	struct sm5450_device_info *di = (struct sm5450_device_info *)dev_data;

	if (!ibat)
		return -1;

	if (sm5450_read_block(di, &val[0], SM5450_IBATADC_H_REG, 2))
		return -1;

	reg_high = val[0];
	reg_low = val[1];
	hwlog_info("IBAT_ADC1=0x%x, IBAT_ADC0=0x%x\n", reg_high, reg_low);

	curr = ((reg_high << SM5450_LENGTH_OF_BYTE) + reg_low) * SM5450_ADC_RESOLUTION_2;
	*ibat = ((int)curr) * di->sense_r_config;
	*ibat /= di->sense_r_actual;

	return 0;
}

static int sm5450_get_ibus_ma(int *ibus, void *dev_data)
{
	u8 reg_high;
	u8 reg_low;
	s16 curr;
	u8 val[2] = {0}; /* read two bytes */
	struct sm5450_device_info *di = (struct sm5450_device_info *)dev_data;

	if (!ibus)
		return -1;

	if (sm5450_read_block(di, &val[0], SM5450_IBUSADC_H_REG, 2))
		return -1;

	reg_high = val[0];
	reg_low = val[1];
	hwlog_info("IBUS_ADC1=0x%x, IBUS_ADC0=0x%x\n", reg_high, reg_low);

	curr = ((reg_high << SM5450_LENGTH_OF_BYTE) + reg_low) * SM5450_ADC_RESOLUTION_2;
	*ibus = (int)(curr);

	return 0;
}

static int sm5450_get_vbus_mv(int *vbus, void *dev_data)
{
	u8 reg_high;
	u8 reg_low;
	s16 voltage;
	u8 val[2] = {0}; /* read two bytes */
	struct sm5450_device_info *di = (struct sm5450_device_info *)dev_data;

	if (!vbus)
		return -1;

	if (sm5450_read_block(di, &val[0], SM5450_VBUSADC_H_REG, 2))
		return -1;

	reg_high = val[0];
	reg_low = val[1];
	hwlog_info("VBUS_ADC1=0x%x, VBUS_ADC0=0x%x\n", reg_high, reg_low);

	voltage = ((reg_high << SM5450_LENGTH_OF_BYTE) + reg_low) * SM5450_ADC_RESOLUTION_4;
	*vbus = (int)(voltage);

	return 0;
}

static int sm5450_get_device_temp(int *temp, void *dev_data)
{
	u8 reg = 0;
	int ret;
	struct sm5450_device_info *di = (struct sm5450_device_info *)dev_data;

	if (!temp)
		return -1;

	ret = sm5450_read_byte(di, SM5450_TDIEADC_REG, &reg);
	if (ret)
		return -1;
	hwlog_info("TDIE_ADC=0x%x\n", reg);

	*temp = reg - SM5450_ADC_STANDARD_TDIE;

	return 0;
}

static int sm5450_set_nwatchdog(int disable, void *dev_data)
{
	int ret;
	u8 reg = 0;
	u8 value = disable ? 0x1 : 0x0;
	struct sm5450_device_info *di = (struct sm5450_device_info *)dev_data;

	if (di->rev_id == 0) {
		hwlog_info("Force Set watchdog to disable\n");
		value = 1;
	}

	ret = sm5450_write_mask(di, SM5450_CONTROL1_REG, SM5450_CONTROL1_NEN_WATCHDOG_MASK,
		SM5450_CONTROL1_NEN_WATCHDOG_SHIFT, value);
	if (ret)
		return -1;

	ret = sm5450_read_byte(di, SM5450_CONTROL1_REG, &reg);
	if (ret)
		return -1;

	hwlog_info("watchdog [%x]=0x%x\n", SM5450_CONTROL1_REG, reg);
	return 0;
}

static int sm5450_config_watchdog_ms(int time, void *dev_data)
{
	u8 val;
	int ret;
	u8 reg = 0;
	struct sm5450_device_info *di = (struct sm5450_device_info *)dev_data;

	if (time >= SM5450_WTD_CONFIG_TIMING_80000MS)
		val = 7;
	else if (time >= SM5450_WTD_CONFIG_TIMING_40000MS)
		val = 6;
	else if (time >= SM5450_WTD_CONFIG_TIMING_20000MS)
		val = 5;
	else if (time >= SM5450_WTD_CONFIG_TIMING_10000MS)
		val = 4;
	else if (time >= SM5450_WTD_CONFIG_TIMING_5000MS)
		val = 3;
	else if (time >= SM5450_WTD_CONFIG_TIMING_2000MS)
		val = 2;
	else if (time >= SM5450_WTD_CONFIG_TIMING_1000MS)
		val = 1;
	else
		val = 0;

	ret = sm5450_write_mask(di, SM5450_CONTROL1_REG, SM5450_CONTROL1_WATCHDOG_REF_MASK,
		SM5450_CONTROL1_WATCHDOG_REF_SHIFT, val);
	if (ret)
		return -1;

	ret = sm5450_read_byte(di, SM5450_CONTROL1_REG, &reg);
	if (ret)
		return -1;

	hwlog_info("config_watchdog_ms [%x]=0x%x\n", SM5450_CONTROL1_REG, reg);
	return 0;
}

static int sm5450_config_rlt_uvp_ref(int rltuvp_rate, void *dev_data)
{
	u8 value;
	int ret;
	struct sm5450_device_info *di = (struct sm5450_device_info *)dev_data;

	if (rltuvp_rate >= SM5450_RLTVUVP_REF_1P04)
		rltuvp_rate = SM5450_RLTVUVP_REF_1P04;
	value = rltuvp_rate;

	ret = sm5450_write_mask(di, SM5450_CONTROL3_REG, SM5450_CONTROL3_RLTVUVP_REF_MASK,
		SM5450_CONTROL3_RLTVUVP_REF_SHIFT, value);
	if (ret)
		return -1;

	hwlog_info("config_rlt_uvp_ref [%x]=0x%x\n", SM5450_CONTROL3_REG, value);
	return 0;
}

static int sm5450_config_rlt_ovp_ref(int rltovp_rate, void *dev_data)
{
	u8 value;
	int ret;
	struct sm5450_device_info *di = (struct sm5450_device_info *)dev_data;

	if (rltovp_rate >= SM5450_RLTVOVP_REF_1P25)
		rltovp_rate = SM5450_RLTVOVP_REF_1P25;
	value = rltovp_rate;

	ret = sm5450_write_mask(di, SM5450_CONTROL3_REG, SM5450_CONTROL3_RLTVOVP_REF_MASK,
		SM5450_CONTROL3_RLTVOVP_REF_SHIFT, value);
	if (ret)
		return -1;

	hwlog_info("config_rlt_ovp_ref [%x]=0x%x\n", SM5450_CONTROL3_REG, value);
	return 0;
}

static int sm5450_config_vbat_ovp_ref_mv(int ovp_threshold, void *dev_data)
{
	u8 value;
	int ret;
	struct sm5450_device_info *di = (struct sm5450_device_info *)dev_data;

	if (ovp_threshold < SM5450_BAT_OVP_BASE_4000MV)
		ovp_threshold = SM5450_BAT_OVP_BASE_4000MV;

	if (ovp_threshold > SM5450_BAT_OVP_BASE_5575MV)
		ovp_threshold = SM5450_BAT_OVP_BASE_5575MV;

	value = (u8)((ovp_threshold - SM5450_BAT_OVP_BASE_4000MV) /
		SM5450_BAT_OVP_STEP);
	ret = sm5450_write_mask(di, SM5450_VBATOVP_REG, SM5450_VBATOVP_VBATOVP_REF_MASK,
		SM5450_VBATOVP_VBATOVP_REF_SHIFT, value);
	if (ret)
		return -1;

	hwlog_info("config_vbat_ovp_ref_mv [%x]=0x%x\n", SM5450_VBATOVP_REG, value);
	return 0;
}

static int sm5450_config_vbat_reg_ref_mv(int vbat_regulation, void *dev_data)
{
	u8 value;
	int ret;
	struct sm5450_device_info *di = (struct sm5450_device_info *)dev_data;

	if (vbat_regulation <= SM5450_VBATREG_REF_BELOW_50MV)
		value = ((SM5450_VBATREG_REF_BELOW_50MV - SM5450_VBATREG_REF_BELOW_50MV) /
			SM5450_VBATREG_REF_BELOW_STEP);
	else if (vbat_regulation <= SM5450_VBATREG_REF_BELOW_100MV)
		value = ((SM5450_VBATREG_REF_BELOW_100MV - SM5450_VBATREG_REF_BELOW_50MV) /
			SM5450_VBATREG_REF_BELOW_STEP);
	else if (vbat_regulation <= SM5450_VBATREG_REF_BELOW_150MV)
		value = ((SM5450_VBATREG_REF_BELOW_150MV - SM5450_VBATREG_REF_BELOW_50MV) /
			SM5450_VBATREG_REF_BELOW_STEP);
	else if (vbat_regulation <= SM5450_VBATREG_REF_BELOW_200MV)
		value = ((SM5450_VBATREG_REF_BELOW_200MV - SM5450_VBATREG_REF_BELOW_50MV) /
			SM5450_VBATREG_REF_BELOW_STEP);
	else
		value = (SM5450_VBATREG_REF_BELOW_50MV /
			SM5450_VBATREG_REF_BELOW_STEP) - 1;

	ret = sm5450_write_mask(di, SM5450_REGULATION_REG, SM5450_REGULATION_VBATREG_REF_MASK,
		SM5450_REGULATION_VBATREG_REF_SHIFT, value);
	if (ret)
		return -1;

	hwlog_info("config_vbat_reg_ref_mv [%x]=0x%x\n", SM5450_REGULATION_REG, value);
	return 0;
}

static int sm5450_config_ibat_ocp_ref_ma(int ocp_threshold, void *dev_data)
{
	u8 value;
	int ret;
	struct sm5450_device_info *di = (struct sm5450_device_info *)dev_data;

	if (ocp_threshold < SM5450_BAT_OCP_BASE_2000MA)
		ocp_threshold = SM5450_BAT_OCP_BASE_2000MA;

	if (ocp_threshold > SM5450_BAT_OCP_BASE_8300MA)
		ocp_threshold = SM5450_BAT_OCP_BASE_8300MA;

	value = (u8)((ocp_threshold - SM5450_BAT_OCP_BASE_2000MA) /
		SM5450_BAT_OCP_STEP);
	ret = sm5450_write_mask(di, SM5450_IBATOCP_REG, SM5450_IBATOCP_IBATOCP_REF_MASK,
		SM5450_IBATOCP_IBATOCP_REF_SHIFT, value);
	if (ret)
		return -1;

	hwlog_info("config_ibat_ocp_ref_ma [%x]=0x%x\n", SM5450_IBATOCP_REG, value);
	return 0;
}

static int sm5450_config_ibat_reg_ref_ma(int ibat_regulation, void *dev_data)
{
	u8 value;
	int ret;
	struct sm5450_device_info *di = (struct sm5450_device_info *)dev_data;

	if (ibat_regulation <= SM5450_IBATREG_REF_BELOW_200MA)
		value = (SM5450_IBATREG_REF_BELOW_200MA - SM5450_IBATREG_REF_BELOW_200MA) /
			SM5450_IBATREG_REF_BELOW_STEP;
	else if (ibat_regulation <= SM5450_IBATREG_REF_BELOW_300MA)
		value = (SM5450_IBATREG_REF_BELOW_300MA - SM5450_IBATREG_REF_BELOW_200MA) /
			SM5450_IBATREG_REF_BELOW_STEP;
	else if (ibat_regulation <= SM5450_IBATREG_REF_BELOW_400MA)
		value = (SM5450_IBATREG_REF_BELOW_400MA - SM5450_IBATREG_REF_BELOW_200MA) /
			SM5450_IBATREG_REF_BELOW_STEP;
	else if (ibat_regulation <= SM5450_IBATREG_REF_BELOW_500MA)
		value = (SM5450_IBATREG_REF_BELOW_500MA - SM5450_IBATREG_REF_BELOW_200MA) /
			SM5450_IBATREG_REF_BELOW_STEP;
	else
		value = (SM5450_IBATREG_REF_BELOW_200MA - SM5450_IBATREG_REF_BELOW_200MA) /
			SM5450_IBATREG_REF_BELOW_STEP;

	ret = sm5450_write_mask(di, SM5450_REGULATION_REG, SM5450_REGULATION_IBATREG_REF_MASK,
		SM5450_REGULATION_IBATREG_REF_SHIFT, value);
	if (ret)
		return -1;

	hwlog_info("config_ibat_reg_regulation_mv [%x]=0x%x\n", SM5450_REGULATION_REG, value);
	return 0;
}

static int sm5450_config_vbuscon_ovp_ref_mv(int ovp_threshold, void *dev_data)
{
	u8 value;
	int ret;
	struct sm5450_device_info *di = (struct sm5450_device_info *)dev_data;

	if (ovp_threshold < SM5450_VBUSCON_OVP_BASE_4000MV)
		ovp_threshold = SM5450_VBUSCON_OVP_BASE_4000MV;

	if (ovp_threshold > SM5450_VBUSCON_OVP_MAX_19000MV)
		ovp_threshold = SM5450_VBUSCON_OVP_MAX_19000MV;

	value = (u8)((ovp_threshold - SM5450_VBUSCON_OVP_BASE_4000MV) /
		SM5450_VBUSCON_OVP_STEP);
	ret = sm5450_write_mask(di, SM5450_VBUSCON_OVP_REG, SM5450_VBUSCON_OVP_VBUSCON_OVP_REF_MASK,
		SM5450_VBUSCON_OVP_VBUSCON_OVP_REF_SHIFT, value);
	if (ret)
		return -1;

	hwlog_info("config_ac_ovp_threshold_mv [%x]=0x%x\n", SM5450_VBUSCON_OVP_REG, value);
	return 0;
}

static int sm5450_config_vbus_ovp_ref_mv(int ovp_threshold, void *dev_data)
{
	u8 value;
	int ret;
	struct sm5450_device_info *di = (struct sm5450_device_info *)dev_data;

	if (ovp_threshold < SM5450_VBUS_OVP_BASE_4000MV)
		ovp_threshold = SM5450_VBUS_OVP_BASE_4000MV;

	if (ovp_threshold > SM5450_VBUS_OVP_MAX_14000MV)
		ovp_threshold = SM5450_VBUS_OVP_MAX_14000MV;

	value = (u8)((ovp_threshold - SM5450_VBUS_OVP_BASE_4000MV) /
		SM5450_VBUS_OVP_STEP);
	ret = sm5450_write_mask(di, SM5450_VBUSOVP_REG, SM5450_VBUSOVP_VBUSOVP_REF_MASK,
		SM5450_VBUSOVP_VBUSOVP_REF_SHIFT, value);
	if (ret)
		return -1;

	hwlog_info("config_vbus_ovp_ref_mv [%x]=0x%x\n", SM5450_VBUSOVP_REG, value);
	return 0;
}

static int sm5450_config_ibus_ucp_ref_ma(int ucp_threshold, void *dev_data)
{
	u8 value = 0;
	u8 ucp_th_value;
	int ret;
	u8 pre_opmode;
	struct sm5450_device_info *di = (struct sm5450_device_info *)dev_data;

	if (ucp_threshold <= SM5450_BUS_UCP_BASE_300MA_150MA) {
		ucp_threshold = SM5450_BUS_UCP_BASE_300MA_150MA;
		ucp_th_value = 0;
	} else if (ucp_threshold <= SM5450_BUS_UCP_BASE_500MA_250MA) {
		ucp_threshold = SM5450_BUS_UCP_BASE_500MA_250MA;
		ucp_th_value = 1;
	} else {
		ucp_threshold = SM5450_BUS_UCP_BASE_300MA_150MA;
		ucp_th_value = 0;
	}

	/* 1. Save previous OPMODE */
	ret = sm5450_read_byte(di, SM5450_CONTROL1_REG, &value);
	if (ret)
		return -1;
	pre_opmode = ((value & SM5450_CONTROL1_OPMODE_MASK) >> SM5450_CONTROL1_OPMODE_SHIFT);

	/* 2. Set OPMODE to init mode */
	ret = sm5450_write_mask(di, SM5450_CONTROL1_REG,
		SM5450_CONTROL1_OPMODE_MASK, SM5450_CONTROL1_OPMODE_SHIFT,
		SM5450_CONTROL1_OFFMODE);
	if (ret)
		return -1;

	/* 3. Set IBUSUCP REF */
	ret = sm5450_write_mask(di, SM5450_IBUS_OCP_UCP_REG, SM5450_IBUS_OCP_UCP_IBUSUCP_REF_MASK,
		SM5450_IBUS_OCP_UCP_IBUSUCP_REF_SHIFT, ucp_th_value);
	if (ret)
		return -1;

	/* 4. Set OPMODE to pre mode */
	ret = sm5450_write_mask(di, SM5450_CONTROL1_REG, SM5450_CONTROL1_OPMODE_MASK,
		SM5450_CONTROL1_OPMODE_SHIFT, pre_opmode);
	if (ret)
		return -1;

	hwlog_info("config_ibus_ucp_threshold_ma [%x]=0x%x\n", SM5450_IBUS_OCP_UCP_REG, ucp_th_value);
	return 0;
}

static int sm5450_config_ibus_ocp_ref_ma(int ocp_threshold, int chg_mode, void *dev_data)
{
	u8 value;
	int ret;
	struct sm5450_device_info *di = (struct sm5450_device_info *)dev_data;

	if (chg_mode == LVC_MODE) {
		if (ocp_threshold < SM5450_IBUS_OCP_BP_BASE_2500MA)
			ocp_threshold = SM5450_IBUS_OCP_BP_BASE_2500MA;

		if (ocp_threshold > SM5450_IBUS_OCP_BP_MAX_5600MA)
			ocp_threshold = SM5450_IBUS_OCP_BP_MAX_5600MA;

		value = (u8)((ocp_threshold - SM5450_IBUS_OCP_BP_BASE_2500MA) /
			SM5450_IBUS_OCP_STEP);
	} else if (chg_mode == SC_MODE) {
		if (ocp_threshold < SM5450_IBUS_OCP_CP_BASE_500MA)
			ocp_threshold = SM5450_IBUS_OCP_CP_BASE_500MA;

		if (ocp_threshold > SM5450_IBUS_OCP_CP_MAX_3600MA)
			ocp_threshold = SM5450_IBUS_OCP_CP_MAX_3600MA;

		value = (u8)((ocp_threshold - SM5450_IBUS_OCP_CP_BASE_500MA) /
			SM5450_IBUS_OCP_STEP);
	} else {
		hwlog_err("CHG Mode error : chg_mode <%d>\n", chg_mode);
		return -1;
	}

	ret = sm5450_write_mask(di, SM5450_IBUS_OCP_UCP_REG,
		SM5450_IBUS_OCP_UCP_IBUSOCP_REF_MASK,
		SM5450_IBUS_OCP_UCP_IBUSOCP_REF_SHIFT, value);
	if (ret)
		return -1;

	hwlog_info("config_ibus_ocp_threshold_ma [%x]=0x%x\n",
		SM5450_IBUS_OCP_UCP_REG, value);
	return 0;
}

static int sm5450_config_switching_frequency(int data, void *dev_data)
{
	int freq;
	int freq_shift;
	int ret;
	struct sm5450_device_info *di = (struct sm5450_device_info *)dev_data;

	switch (data) {
	case SM5450_SW_FREQ_450KHZ:
		freq = SM5450_FSW_SET_SW_FREQ_500KHZ;
		freq_shift = SM5450_SW_FREQ_SHIFT_M_P10;
		break;
	case SM5450_SW_FREQ_500KHZ:
		freq = SM5450_FSW_SET_SW_FREQ_500KHZ;
		freq_shift = SM5450_SW_FREQ_SHIFT_NORMAL;
		break;
	case SM5450_SW_FREQ_550KHZ:
		freq = SM5450_FSW_SET_SW_FREQ_500KHZ;
		freq_shift = SM5450_SW_FREQ_SHIFT_P_P10;
		break;
	case SM5450_SW_FREQ_675KHZ:
		freq = SM5450_FSW_SET_SW_FREQ_750KHZ;
		freq_shift = SM5450_SW_FREQ_SHIFT_M_P10;
		break;
	case SM5450_SW_FREQ_750KHZ:
		freq = SM5450_FSW_SET_SW_FREQ_750KHZ;
		freq_shift = SM5450_SW_FREQ_SHIFT_NORMAL;
		break;
	case SM5450_SW_FREQ_825KHZ:
		freq = SM5450_FSW_SET_SW_FREQ_750KHZ;
		freq_shift = SM5450_SW_FREQ_SHIFT_P_P10;
		break;
	case SM5450_SW_FREQ_1000KHZ:
		freq = SM5450_FSW_SET_SW_FREQ_1000KHZ;
		freq_shift = SM5450_SW_FREQ_SHIFT_NORMAL;
		break;
	case SM5450_SW_FREQ_1250KHZ:
		freq = SM5450_FSW_SET_SW_FREQ_1250KHZ;
		freq_shift = SM5450_SW_FREQ_SHIFT_NORMAL;
		break;
	case SM5450_SW_FREQ_1500KHZ:
		freq = SM5450_FSW_SET_SW_FREQ_1500KHZ;
		freq_shift = SM5450_SW_FREQ_SHIFT_NORMAL;
		break;
	default:
		freq = SM5450_FSW_SET_SW_FREQ_500KHZ;
		freq_shift = SM5450_SW_FREQ_SHIFT_NORMAL;
		break;
	}

	if (di->rev_id == 0) {
		if (freq >= SM5450_FSW_SET_SW_FREQ_1250KHZ) {
			freq = SM5450_FSW_SET_SW_FREQ_1000KHZ;
			hwlog_info("Force set switching_frequency to [%x]=0x%x\n",
				SM5450_CONTROL2_REG, freq);
		}
	}

	ret = sm5450_write_mask(di, SM5450_CONTROL2_REG, SM5450_CONTROL2_FREQ_MASK,
		SM5450_CONTROL2_FREQ_SHIFT, freq);
	if (ret)
		return -1;

	ret = sm5450_write_mask(di, SM5450_CONTROL2_REG, SM5450_CONTROL2_ADJUST_FREQ_MASK,
		SM5450_CONTROL2_ADJUST_FREQ_SHIFT, freq_shift);
	if (ret)
		return -1;

	hwlog_info("config_switching_frequency [%x]=0x%x\n",
		SM5450_CONTROL2_REG, freq);
	hwlog_info("config_adjustable_switching_frequency [%x]=0x%x\n",
		SM5450_CONTROL2_REG, freq_shift);

	return 0;
}

static void sm5450_common_opt_regs(void *dev_data)
{
	struct sm5450_device_info *di = (struct sm5450_device_info *)dev_data;

	/* Needed setting value */
	sm5450_config_rlt_ovp_ref(SM5450_RLTVOVP_REF_1P25, di);
	sm5450_config_rlt_uvp_ref(SM5450_RLTVUVP_REF_1P01, di);
	sm5450_config_vbuscon_ovp_ref_mv(SM5450_VBUSCON_OVP_REF_INIT, di);
	sm5450_config_vbus_ovp_ref_mv(SM5450_VBUS_OVP_REF_INIT, di);
	sm5450_config_ibus_ocp_ref_ma(SM5450_IBUS_OCP_REF_INIT, SC_MODE, di);
	sm5450_config_ibus_ucp_ref_ma(SM5450_BUS_UCP_BASE_300MA_150MA, di);
	sm5450_config_vbat_ovp_ref_mv(SM5450_VBAT_OVP_REF_INIT, di);
	sm5450_config_ibat_ocp_ref_ma(SM5450_IBAT_OCP_REF_INIT, di);
	sm5450_config_ibat_reg_ref_ma(SM5450_IBATREG_REF_BELOW_200MA, di);
	sm5450_config_vbat_reg_ref_mv(SM5450_VBATREG_REF_BELOW_50MV, di);
}

static void sm5450_lvc_opt_regs(void *dev_data)
{
	int ret = 0;
	struct sm5450_device_info *di = (struct sm5450_device_info *)dev_data;

	/* Needed setting value */
	ret += sm5450_config_rlt_ovp_ref(SM5450_RLTVOVP_REF_1P25, di);
	ret += sm5450_config_rlt_uvp_ref(SM5450_RLTVUVP_REF_1P01, di);
	ret += sm5450_config_vbuscon_ovp_ref_mv(12000, di); /* 12V */
	ret += sm5450_config_vbus_ovp_ref_mv(11500, di); /* 11.5V */
	ret += sm5450_config_ibus_ocp_ref_ma(5000, LVC_MODE, di); /* 5.0A */
	ret += sm5450_config_ibus_ucp_ref_ma(SM5450_BUS_UCP_BASE_300MA_150MA, di); /* 300/150mA */
	ret += sm5450_config_vbat_ovp_ref_mv(4500, di); /* 4.5V */
	ret += sm5450_config_ibat_ocp_ref_ma(6000, di); /* 6A  */
	ret += sm5450_config_ibat_reg_ref_ma(SM5450_IBATREG_REF_BELOW_500MA, di); /* ibat_ocp_ref - 0.5 = 5.5A */
	ret += sm5450_config_vbat_reg_ref_mv(SM5450_VBATREG_REF_BELOW_50MV, di); /* vbat_ovp_ref - 0.05 = 4.45V */
	if (ret)
		hwlog_err("lvc_opt_regs fail\n");
	else
		hwlog_info("lvc_opt_regs ok\n");
}

static void sm5450_sc_opt_regs(void *dev_data)
{
	int ret = 0;
	struct sm5450_device_info *di = (struct sm5450_device_info *)dev_data;

	/* Needed setting value */
	ret += sm5450_config_rlt_ovp_ref(SM5450_RLTVOVP_REF_1P25, di);
	ret += sm5450_config_rlt_uvp_ref(SM5450_RLTVUVP_REF_1P01, di);
	ret += sm5450_config_vbuscon_ovp_ref_mv(12000, di); /* 12V */
	ret += sm5450_config_vbus_ovp_ref_mv(12000, di); /* 12V */
	ret += sm5450_config_ibus_ocp_ref_ma(2800, SC_MODE, di); /* 2.8A */
	ret += sm5450_config_ibus_ucp_ref_ma(SM5450_BUS_UCP_BASE_300MA_150MA, di); /* 300/150mA */
	ret += sm5450_config_vbat_ovp_ref_mv(4800, di); /* 4.8V */
	ret += sm5450_config_ibat_ocp_ref_ma(6000, di); /* 6A  */
	ret += sm5450_config_ibat_reg_ref_ma(SM5450_IBATREG_REF_BELOW_500MA, di); /* ibat_ocp_ref - 0.5 = 5.5A */
	ret += sm5450_config_vbat_reg_ref_mv(SM5450_VBATREG_REF_BELOW_50MV, di); /* vbat_ovp_ref - 0.05 = 4.45V */
	if (ret)
		hwlog_err("sc_opt_regs fail\n");
	else
		hwlog_info("sc_opt_regs ok\n");
}

static int sm5450_chip_init(void *dev_data)
{
	return 0;
}

static int sm5450_reg_init(void *dev_data)
{
	int ret = 0;
	struct sm5450_device_info *di = (struct sm5450_device_info *)dev_data;
	u8 reg_val1 = 0;

	/* Enable Watchdog */
	if (di->rev_id == 0) {
		ret += sm5450_set_nwatchdog(1, di);
	} else {
		ret += sm5450_config_watchdog_ms(SM5450_WTD_CONFIG_TIMING_2000MS, di);
		ret += sm5450_set_nwatchdog(0, di);
	}

	/* Enable ENCOMP */
	if (di->rev_id == 0)
		ret += sm5450_write_mask(di, SM5450_CONTROL3_REG,
			SM5450_CONTROL3_EN_COMP_MASK, SM5450_CONTROL3_EN_COMP_SHIFT, 1);
	else
		ret += sm5450_write_mask(di, SM5450_CONTROL3_REG,
			SM5450_CONTROL3_EN_COMP_MASK, SM5450_CONTROL3_EN_COMP_SHIFT, 0);
	(void)sm5450_read_byte(di, SM5450_CONTROL3_REG, &reg_val1);
	hwlog_info("di->rev_id =%d reg:0x[%x]=0x%x\n", di->rev_id, SM5450_CONTROL3_REG, reg_val1);

	ret += sm5450_config_rlt_ovp_ref(SM5450_RLTVOVP_REF_1P10, di);
	ret += sm5450_config_rlt_uvp_ref(SM5450_RLTVUVP_REF_1P04, di);
	ret += sm5450_config_vbat_ovp_ref_mv(SM5450_VBAT_OVP_REF_INIT, di); /* VBAT_OVP 4.35V */
	ret += sm5450_config_ibat_ocp_ref_ma(SM5450_IBAT_OCP_REF_INIT, di); /* IBAT_OCP 7.2A */
	ret += sm5450_config_vbuscon_ovp_ref_mv(SM5450_VBUSCON_OVP_REF_INIT, di); /* VBUSCON_OVP 12V */
	ret += sm5450_config_vbus_ovp_ref_mv(SM5450_VBUS_OVP_REF_INIT, di); /* VBUS_OVP 12V */
	ret += sm5450_config_ibus_ocp_ref_ma(SM5450_IBUS_OCP_REF_INIT, SC_MODE, di); /* IBUS_OCP 3A */
	ret += sm5450_config_ibus_ocp_ref_ma(SM5450_IBUS_OCP_REF_INIT, LVC_MODE, di); /* IBUS_OCP 3A */
	ret += sm5450_config_ibus_ucp_ref_ma(SM5450_BUS_UCP_BASE_300MA_150MA, di); /* 300/150mA */
	ret += sm5450_config_ibat_reg_ref_ma(SM5450_IBATREG_REF_BELOW_500MA, di);
	ret += sm5450_config_vbat_reg_ref_mv(SM5450_VBATREG_REF_BELOW_50MV, di);
	ret += sm5450_config_switching_frequency(di->switching_frequency, di);
	ret += sm5450_write_byte(di, SM5450_FLAG_MASK1_REG, SM5450_FLAG_MASK1_INIT_REG);
	ret += sm5450_write_byte(di, SM5450_FLAG_MASK2_REG, SM5450_FLAG_MASK2_INIT_REG);
	ret += sm5450_write_byte(di, SM5450_FLAG_MASK3_REG, SM5450_FLAG_MASK3_INIT_REG);
	ret += sm5450_write_mask(di, SM5450_SCP_PING_REG,
		SM5450_SCP_PING_WAIT_TIME_MASK, SM5450_SCP_PING_WAIT_TIME_SHIFT, 1);

	/* Enable ENADC */
	ret += sm5450_write_mask(di, SM5450_ADCCTRL_REG,
		SM5450_ADCCTRL_ENADC_MASK, SM5450_ADCCTRL_ENADC_SHIFT, 1);

	/* Continuous mode for rev_id '0' */
	if (di->rev_id == 0)
		ret += sm5450_write_mask(di, SM5450_ADCCTRL_REG,
			SM5450_ADCCTRL_ADCMODE_MASK, SM5450_ADCCTRL_ADCMODE_SHIFT, 1);

	/* Enable automatic VBUS Pull-down */
	ret += sm5450_write_mask(di, SM5450_PULLDOWN_REG,
		SM5450_PULLDOWN_EN_VBUS_PD_MASK, SM5450_PULLDOWN_EN_VBUS_PD_SHIFT, 0);

	/* SCP Interrupt Mask Init */
	ret += sm5450_write_byte(di, SM5450_SCP_MASK1_REG, 0xFF);
	ret += sm5450_write_byte(di, SM5450_SCP_MASK2_REG, 0xFF);
	if (ret) {
		hwlog_err("reg_init fail\n");
		return -1;
	}

	return 0;
}

static int sm5450_charge_init(void *dev_data)
{
	struct sm5450_device_info *di = (struct sm5450_device_info *)dev_data;

	if (!di) {
		hwlog_err("di is null\n");
		return -1;
	}

	di->device_id = sm5450_get_device_id((void *)di);
	if (di->device_id == -1)
		return -1;

	di->rev_id = sm5450_get_revision_id(di);
	if (di->rev_id == -1)
		return -1;

	if (sm5450_reg_init(di))
		return -1;

	hwlog_info("switchcap sm5450 device id is %d, revision id is %d\n",
		di->device_id, di->rev_id);

	di->init_finish_flag = SM5450_INIT_FINISH;
	return 0;
}

static int sm5450_charge_exit(void *dev_data)
{
	int ret;
	struct sm5450_device_info *di = (struct sm5450_device_info *)dev_data;

	if (!di) {
		hwlog_err("di is null\n");
		return -1;
	}

	ret = sm5450_sc_charge_enable(SM5450_SWITCHCAP_DISABLE, di);
	di->init_finish_flag = SM5450_NOT_INIT;
	di->int_notify_enable_flag = SM5450_DISABLE_INT_NOTIFY;

	return ret;
}

static int sm5450_batinfo_exit(void *dev_data)
{
	return 0;
}

static int sm5450_batinfo_init(void *dev_data)
{
	struct sm5450_device_info *di = (struct sm5450_device_info *)dev_data;

	if (sm5450_chip_init(di)) {
		hwlog_err("batinfo init fail\n");
		return -1;
	}

	return 0;
}

static int sm5450_scp_wdt_reset_by_sw(void *dev_data)
{
	int ret;
	struct sm5450_device_info *di = (struct sm5450_device_info *)dev_data;

	ret = sm5450_write_byte(di, SM5450_SCP_CTL_REG, SM5450_SCP_CTL_WDT_RESET);
	ret += sm5450_write_byte(di, SM5450_SCP_STIMER_REG, SM5450_SCP_STIMER_WDT_RESET);
	ret += sm5450_write_byte(di, SM5450_RT_BUFFER_0_REG, SM5450_RT_BUFFER_0_WDT_RESET);
	ret += sm5450_write_byte(di, SM5450_RT_BUFFER_1_REG, SM5450_RT_BUFFER_1_WDT_RESET);
	ret += sm5450_write_byte(di, SM5450_RT_BUFFER_2_REG, SM5450_RT_BUFFER_2_WDT_RESET);
	ret += sm5450_write_byte(di, SM5450_RT_BUFFER_3_REG, SM5450_RT_BUFFER_3_WDT_RESET);
	ret += sm5450_write_byte(di, SM5450_RT_BUFFER_4_REG, SM5450_RT_BUFFER_4_WDT_RESET);
	ret += sm5450_write_byte(di, SM5450_RT_BUFFER_5_REG, SM5450_RT_BUFFER_5_WDT_RESET);
	ret += sm5450_write_byte(di, SM5450_RT_BUFFER_6_REG, SM5450_RT_BUFFER_6_WDT_RESET);
	ret += sm5450_write_byte(di, SM5450_RT_BUFFER_7_REG, SM5450_RT_BUFFER_7_WDT_RESET);
	ret += sm5450_write_byte(di, SM5450_RT_BUFFER_8_REG, SM5450_RT_BUFFER_8_WDT_RESET);
	ret += sm5450_write_byte(di, SM5450_RT_BUFFER_9_REG, SM5450_RT_BUFFER_9_WDT_RESET);
	ret += sm5450_write_byte(di, SM5450_RT_BUFFER_10_REG, SM5450_RT_BUFFER_10_WDT_RESET);

	if (ret) {
		hwlog_err("%s fail\n", __func__);
		return -1;
	}
	hwlog_info("%s success\n", __func__);
	return 0;
}

static int sm5450_is_support_scp(void *dev_data)
{
	struct sm5450_device_info *di = (struct sm5450_device_info *)dev_data;

	if (!di) {
		hwlog_err("sm5450_device_info is null\n");
		return -ENOMEM;
	}

	if (di->param_dts.scp_support != 0) {
		hwlog_info("support scp charge\n");
		return 0;
	}
	return 1;
}

static int sm5450_is_support_fcp(void *dev_data)
{
	struct sm5450_device_info *di = (struct sm5450_device_info *)dev_data;

	if (!di) {
		hwlog_err("sm5450_device_info is null\n");
		return -ENOMEM;
	}

	if (di->param_dts.fcp_support != 0) {
		hwlog_info("support fcp charge\n");
		return 0;
	}
	return 1;
}

static int sm5450_scp_cmd_transfer_check(void *dev_data)
{
	u8 reg_val1 = 0;
	u8 reg_val2 = 0;
	int i = 0;
	int ret0;
	int ret1;
	struct sm5450_device_info *di = (struct sm5450_device_info *)dev_data;

	if (!di) {
		hwlog_err("sm5450_device_info is null\n");
		return -ENOMEM;
	}

	do {
		if (adp_plugout) {
			hwlog_err("transfer check fail, adp plugout\n");
			break;
		}
		usleep_range(50000, 51000); /* wait 50ms for each cycle */
		ret0 = sm5450_read_byte(di, SM5450_SCP_ISR1_REG, &reg_val1);
		ret1 = sm5450_read_byte(di, SM5450_SCP_ISR2_REG, &reg_val2);
		if (ret0 || ret1) {
			hwlog_err("reg read failed\n");
			break;
		}
		hwlog_info("%s : reg_val1(0x%x), reg_val2(0x%x), scp_isr_backup[0] = 0x%x, scp_isr_backup[1] = 0x%x\n",
			__func__, reg_val1, reg_val2, di->scp_isr_backup[0], di->scp_isr_backup[1]);
		/* Interrupt work can hook the interrupt value first. So it is necessily to do backup via isr_backup. */
		reg_val1 |= di->scp_isr_backup[0];
		reg_val2 |= di->scp_isr_backup[1];
		if (reg_val1 || reg_val2) {
			if (((reg_val2 & SM5450_SCP_ISR2_ACK_MASK) &&  (reg_val2 & SM5450_SCP_ISR2_CMD_CPL_MASK)) &&
				!(reg_val1 & (SM5450_SCP_ISR1_ACK_CRCRX_MASK | SM5450_SCP_ISR1_ACK_PARRX_MASK |
				SM5450_SCP_ISR1_ERR_ACK_L_MASK))) {
				return 0;
			} else if (reg_val1 & (SM5450_SCP_ISR1_ACK_CRCRX_MASK |
				SM5450_SCP_ISR1_ENABLE_HAND_NO_RESPOND_MASK)) {
				hwlog_info("SCP_TRANSFER_FAIL,slave status changed: ISR1=0x%x,ISR2=0x%x\n",
					reg_val1, reg_val2);
				return -1;
			} else if (reg_val2 & SM5450_SCP_ISR2_NACK_MASK) {
				hwlog_info("SCP_TRANSFER_FAIL,slave nack: ISR1=0x%x,ISR2=0x%x\n",
					reg_val1, reg_val2);
				return -1;
			} else if (reg_val1 & (SM5450_SCP_ISR1_ACK_CRCRX_MASK | SM5450_SCP_ISR1_ACK_PARRX_MASK |
				SM5450_SCP_ISR1_TRANS_HAND_NO_RESPOND_MASK)) {
				hwlog_info("SCP_TRANSFER_FAIL, CRCRX_PARRX_ERROR:ISR1=0x%x,ISR2=0x%x\n",
					reg_val1, reg_val2);
				return -1;
			}
			hwlog_info("SCP_TRANSFER_FAIL, ISR1=0x%x,ISR2=0x%x, index = %d\n",
				reg_val1, reg_val2, i);
		}
		i++;
		if (di->dc_ibus_ucp_happened)
			i = SM5450_SCP_ACK_RETRY_CYCLE;
	} while (i < SM5450_SCP_ACK_RETRY_CYCLE);

	hwlog_info("scp adapter transfer time out\n");
	return -1;
}

static int sm5450_scp_cmd_transfer_check_1(void *dev_data)
{
	u8 reg_val1 = 0;
	u8 reg_val2 = 0;
	u8 pre_val1 = 0;
	u8 pre_val2 = 0;
	int i = 0;
	int ret0;
	int ret1;
	struct sm5450_device_info *di = (struct sm5450_device_info *)dev_data;

	if (!di) {
		hwlog_err("sm5450_device_info is null\n");
		return -ENOMEM;
	}

	do {
		if (adp_plugout) {
			hwlog_err("transfer check fail, adp plugout\n");
			break;
		}
		usleep_range(12000, 13000); /* wait 12ms between each cycle */
		ret0 = sm5450_read_byte(di, SM5450_SCP_ISR1_REG, &pre_val1);
		ret1 = sm5450_read_byte(di, SM5450_SCP_ISR2_REG, &pre_val2);
		if (ret0 || ret1) {
			hwlog_err("reg read failed\n");
			break;
		}
		hwlog_info("%s : pre_val1(0x%x), pre_val2(0x%x), scp_isr_backup[0](0x%x), scp_isr_backup[1](0x%x)\n",
			__func__, pre_val1, pre_val2, di->scp_isr_backup[0], di->scp_isr_backup[1]);
		/* Save insterrupt value to reg_val1/2 from starting SCP cmd to SLV_R_CPL interrupt */
		reg_val1 |= pre_val1;
		reg_val2 |= pre_val2;
		/* Interrupt work can hook the interrupt value first. So it is necessily to do backup via isr_backup. */
		reg_val1 |= di->scp_isr_backup[0];
		reg_val2 |= di->scp_isr_backup[1];
		if (reg_val1 || reg_val2) {
			if (((reg_val2 & SM5450_SCP_ISR2_ACK_MASK) && (reg_val2 & SM5450_SCP_ISR2_CMD_CPL_MASK) &&
				(reg_val2 & SM5450_SCP_ISR2_SLV_R_CPL_MASK)) && !(reg_val1 & (SM5450_SCP_ISR1_ACK_CRCRX_MASK |
				SM5450_SCP_ISR1_ACK_PARRX_MASK | SM5450_SCP_ISR1_ERR_ACK_L_MASK))) {
				return 0;
			} else if (reg_val1 & (SM5450_SCP_ISR1_ACK_CRCRX_MASK |
				SM5450_SCP_ISR1_ENABLE_HAND_NO_RESPOND_MASK)) {
				hwlog_info("SCP_TRANSFER_FAIL,slave status changed: ISR1=0x%x,ISR2=0x%x\n",
					reg_val1, reg_val2);
				return -1;
			} else if (reg_val2 & SM5450_SCP_ISR2_NACK_MASK) {
				hwlog_info("SCP_TRANSFER_FAIL,slave nack: ISR1=0x%x,ISR2=0x%x\n",
					reg_val1, reg_val2);
				return -1;
			} else if (reg_val1 & (SM5450_SCP_ISR1_ACK_CRCRX_MASK | SM5450_SCP_ISR1_ACK_PARRX_MASK |
				SM5450_SCP_ISR1_TRANS_HAND_NO_RESPOND_MASK)) {
				hwlog_info("SCP_TRANSFER_FAIL, CRCRX_PARRX_ERROR:ISR1=0x%x,ISR2=0x%x\n",
					reg_val1, reg_val2);
				return -1;
			}
			hwlog_info("SCP_TRANSFER_FAIL, ISR1=0x%x,ISR2=0x%x, index = %d\n",
				reg_val1, reg_val2, i);
		}
		i++;
		if (di->dc_ibus_ucp_happened)
			i = SM5450_SCP_ACK_RETRY_CYCLE_1;
	} while (i < SM5450_SCP_ACK_RETRY_CYCLE_1);

	hwlog_info("scp adapter transfer time out\n");
	return -1;
}

static void sm5450_scp_protocol_restart(void *dev_data)
{
	u8 reg_val = 0;
	int ret;
	int i;
	struct sm5450_device_info *di = (struct sm5450_device_info *)dev_data;

	if (!di) {
		hwlog_err("sm5450_device_info is null\n");
		return;
	}

	mutex_lock(&di->scp_detect_lock);

	/* Detect scp charger, wait for ping succ */
	for (i = 0; i < SM5450_RESTART_TIME; i++) {
		usleep_range(9000, 10000); /* wait 9ms for each cycle */
		ret = sm5450_read_byte(di, SM5450_SCP_STATUS_REG, &reg_val);
		if (ret) {
			hwlog_err("read det attach err,ret:%d\n", ret);
			continue;
		}

		if (reg_val & SM5450_SCP_STATUS_ENABLE_HAND_SUCCESS_MASK)
			break;
	}

	if (i == SM5450_RESTART_TIME) {
		hwlog_err("wait for slave fail\n");
		mutex_unlock(&di->scp_detect_lock);
		return;
	}
	mutex_unlock(&di->scp_detect_lock);
	hwlog_info("disable and enable scp protocol accp status is 0x%x\n", reg_val);
}

static int sm5450_scp_adapter_reg_read(u8 *val, u8 reg)
{
	int ret;
	int i;
	u8 reg_val1 = 0;
	u8 reg_val2 = 0;
	struct sm5450_device_info *di = g_sm5450_scp_dev;

	if (!di) {
		hwlog_err("sm5450_device_info is null\n");
		return -ENOMEM;
	}

	mutex_lock(&di->accp_adapter_reg_lock);
	hwlog_info("%s : CMD = 0x%x, REG = 0x%x\n", __func__, SM5450_CHG_SCP_CMD_SBRRD, reg);
	for (i = 0; i < SM5450_SCP_RETRY_TIME; i++) {
		/* Init */
		sm5450_write_mask(di, SM5450_SCP_CTL_REG, SM5450_SCP_CTL_SNDCMD_MASK,
			SM5450_SCP_CTL_SNDCMD_SHIFT, SM5450_SCP_CTL_SNDCMD_RESET);

		/* before send cmd, clear ISR interrupt registers */
		ret = sm5450_read_byte(di, SM5450_SCP_ISR1_REG, &reg_val1);
		ret += sm5450_read_byte(di, SM5450_SCP_ISR2_REG, &reg_val2);
		ret += sm5450_write_byte(di, SM5450_RT_BUFFER_0_REG, SM5450_CHG_SCP_CMD_SBRRD);
		ret += sm5450_write_byte(di, SM5450_RT_BUFFER_1_REG, reg);
		ret += sm5450_write_byte(di, SM5450_RT_BUFFER_2_REG, 1);
		/* Initial scp_isr_backup[0],[1] due to catching the missing isr by interrupt_work. */
		di->scp_isr_backup[0] = 0;
		di->scp_isr_backup[1] = 0;
		ret += sm5450_write_mask(di, SM5450_SCP_CTL_REG, SM5450_SCP_CTL_SNDCMD_MASK,
			SM5450_SCP_CTL_SNDCMD_SHIFT, SM5450_SCP_CTL_SNDCMD_START);
		if (ret) {
			hwlog_err("write error, ret is %d\n", ret);
			/* Manual Init */
			sm5450_write_mask(di, SM5450_SCP_CTL_REG, SM5450_SCP_CTL_SNDCMD_MASK,
				 SM5450_SCP_CTL_SNDCMD_SHIFT, SM5450_SCP_CTL_SNDCMD_RESET);
			mutex_unlock(&di->accp_adapter_reg_lock);
			return -1;
		}

		/* check cmd transfer success or fail */
		if (di->rev_id == 0) {
			if (sm5450_scp_cmd_transfer_check(di) == 0) {
				/* recived data from adapter */
				ret = sm5450_read_byte(di, SM5450_RT_BUFFER_12_REG, val);
				break;
			}
		} else {
			if (sm5450_scp_cmd_transfer_check_1(di) == 0) {
				/* recived data from adapter */
				ret = sm5450_read_byte(di, SM5450_RT_BUFFER_12_REG, val);
				break;
			}
		}

		sm5450_scp_protocol_restart(di);
		if (di->dc_ibus_ucp_happened)
			i = SM5450_SCP_RETRY_TIME;
	}
	if (i >= SM5450_SCP_RETRY_TIME) {
		hwlog_err("ack error,retry %d times\n", i);
		ret = -1;
	}
	/* Manual Init */
	sm5450_write_mask(di, SM5450_SCP_CTL_REG, SM5450_SCP_CTL_SNDCMD_MASK,
		SM5450_SCP_CTL_SNDCMD_SHIFT, SM5450_SCP_CTL_SNDCMD_RESET);
	usleep_range(10000, 11000); /* wait 10ms for operate effective */

	mutex_unlock(&di->accp_adapter_reg_lock);

	return ret;
}

static int sm5450_scp_adapter_reg_read_block(u8 reg, u8 *val, u8 num,
	void *dev_data)
{
	int ret;
	int i;
	u8 reg_val1 = 0;
	u8 reg_val2 = 0;
	u8 *p = val;
	u8 data_len = (num < SM5450_SCP_DATA_LEN) ? num : SM5450_SCP_DATA_LEN;
	struct sm5450_device_info *di = g_sm5450_scp_dev;

	if (!di) {
		hwlog_err("%s sm5450_device_info is null\n", __func__);
		return -ENOMEM;
	}
	mutex_lock(&di->accp_adapter_reg_lock);

	hwlog_info("%s : CMD = 0x%x, REG = 0x%x, Num = 0x%x\n", __func__,
		SM5450_CHG_SCP_CMD_MBRRD, reg, data_len);

	for (i = 0; i < SM5450_SCP_RETRY_TIME; i++) {
		/* Init */
		sm5450_write_mask(di, SM5450_SCP_CTL_REG, SM5450_SCP_CTL_SNDCMD_MASK,
			SM5450_SCP_CTL_SNDCMD_SHIFT, SM5450_SCP_CTL_SNDCMD_RESET);

		/* Before sending cmd, clear ISR registers */
		ret = sm5450_read_byte(di, SM5450_SCP_ISR1_REG, &reg_val1);
		ret += sm5450_read_byte(di, SM5450_SCP_ISR2_REG, &reg_val2);
		ret += sm5450_write_byte(di, SM5450_RT_BUFFER_0_REG, SM5450_CHG_SCP_CMD_MBRRD);
		ret += sm5450_write_byte(di, SM5450_RT_BUFFER_1_REG, reg);
		ret += sm5450_write_byte(di, SM5450_RT_BUFFER_2_REG, data_len);
		/* Initial scp_isr_backup[0],[1] */
		di->scp_isr_backup[0] = 0;
		di->scp_isr_backup[1] = 0;
		ret += sm5450_write_mask(di, SM5450_SCP_CTL_REG, SM5450_SCP_CTL_SNDCMD_MASK,
			SM5450_SCP_CTL_SNDCMD_SHIFT, SM5450_SCP_CTL_SNDCMD_START);
		if (ret) {
			hwlog_err("read error ret is %d\n", ret);
			/* Manual Init */
			sm5450_write_mask(di, SM5450_SCP_CTL_REG, SM5450_SCP_CTL_SNDCMD_MASK,
				SM5450_SCP_CTL_SNDCMD_SHIFT, SM5450_SCP_CTL_SNDCMD_RESET);
			mutex_unlock(&di->accp_adapter_reg_lock);
			return -1;
		}

		/* Check cmd transfer success or fail */
		if (di->rev_id == 0) {
			if (sm5450_scp_cmd_transfer_check(di) == 0) {
				/* recived data from adapter */
				ret = sm5450_read_block(di, p, SM5450_RT_BUFFER_12_REG, data_len);
				break;
			}
		} else {
			if (sm5450_scp_cmd_transfer_check_1(di) == 0) {
				/* recived data from adapter */
				ret = sm5450_read_block(di, p, SM5450_RT_BUFFER_12_REG, data_len);
				break;
			}
		}

		sm5450_scp_protocol_restart(di);
		if (di->dc_ibus_ucp_happened)
			i = SM5450_SCP_RETRY_TIME;
	}
	if (i >= SM5450_SCP_RETRY_TIME) {
		hwlog_err("ack error, retry %d times\n", i);
		ret = -1;
	}
	mutex_unlock(&di->accp_adapter_reg_lock);

	if (ret) {
		/* Manual Init */
		sm5450_write_mask(di, SM5450_SCP_CTL_REG, SM5450_SCP_CTL_SNDCMD_MASK,
			SM5450_SCP_CTL_SNDCMD_SHIFT, SM5450_SCP_CTL_SNDCMD_RESET);
		return ret;
	}

	num -= data_len;
	/* Max is SM5450_SCP_DATA_LEN. Remaining data is read in below. */
	if (num) {
		p += data_len;
		reg += data_len;
		ret = sm5450_scp_adapter_reg_read_block(reg, p, num, di);
		if (ret) {
			/* Manual Init */
			sm5450_write_mask(di, SM5450_SCP_CTL_REG, SM5450_SCP_CTL_SNDCMD_MASK,
				SM5450_SCP_CTL_SNDCMD_SHIFT, SM5450_SCP_CTL_SNDCMD_RESET);
			hwlog_err("read error, ret is %d\n", ret);
			return -1;
		}
	}
	/* Manual Init */
	sm5450_write_mask(di, SM5450_SCP_CTL_REG, SM5450_SCP_CTL_SNDCMD_MASK,
		SM5450_SCP_CTL_SNDCMD_SHIFT, SM5450_SCP_CTL_SNDCMD_RESET);
	usleep_range(10000, 11000); /* wait 10ms for operate effective */

	return 0;
}

static int sm5450_scp_adapter_reg_write(u8 val, u8 reg)
{
	int ret;
	int i;
	u8 reg_val1 = 0;
	u8 reg_val2 = 0;
	struct sm5450_device_info *di = g_sm5450_scp_dev;

	if (!di) {
		hwlog_err("sm5450_device_info is null\n");
		return -ENOMEM;
	}
	mutex_lock(&di->accp_adapter_reg_lock);
	hwlog_info("%s : CMD = 0x%x, REG = 0x%x, val = 0x%x\n", __func__,
		SM5450_CHG_SCP_CMD_SBRWR, reg, val);
	for (i = 0; i < SM5450_SCP_RETRY_TIME; i++) {
		/* Init */
		sm5450_write_mask(di, SM5450_SCP_CTL_REG, SM5450_SCP_CTL_SNDCMD_MASK,
			SM5450_SCP_CTL_SNDCMD_SHIFT, SM5450_SCP_CTL_SNDCMD_RESET);

		/* Before send cmd, clear accp interrupt registers */
		ret = sm5450_read_byte(di, SM5450_SCP_ISR1_REG, &reg_val1);
		ret += sm5450_read_byte(di, SM5450_SCP_ISR2_REG, &reg_val2);
		ret += sm5450_write_byte(di, SM5450_RT_BUFFER_0_REG, SM5450_CHG_SCP_CMD_SBRWR);
		ret += sm5450_write_byte(di, SM5450_RT_BUFFER_1_REG, reg);
		ret += sm5450_write_byte(di, SM5450_RT_BUFFER_2_REG, 1);
		ret += sm5450_write_byte(di, SM5450_RT_BUFFER_3_REG, val);
		/* Initial scp_isr_backup[0],[1] */
		di->scp_isr_backup[0] = 0;
		di->scp_isr_backup[1] = 0;
		ret += sm5450_write_mask(di, SM5450_SCP_CTL_REG, SM5450_SCP_CTL_SNDCMD_MASK,
			SM5450_SCP_CTL_SNDCMD_SHIFT, SM5450_SCP_CTL_SNDCMD_START);
		if (ret) {
			hwlog_err("write error, ret is %d\n", ret);
			/* Manual Init */
			sm5450_write_mask(di, SM5450_SCP_CTL_REG, SM5450_SCP_CTL_SNDCMD_MASK,
				SM5450_SCP_CTL_SNDCMD_SHIFT, SM5450_SCP_CTL_SNDCMD_RESET);
			mutex_unlock(&di->accp_adapter_reg_lock);
			return -1;
		}

		/* Check cmd transfer success or fail */
		if (di->rev_id == 0) {
			if (sm5450_scp_cmd_transfer_check(di) == 0)
				break;
		} else {
			if (sm5450_scp_cmd_transfer_check_1(di) == 0)
				break;
		}

		sm5450_scp_protocol_restart(di);
		if (di->dc_ibus_ucp_happened)
			i = SM5450_SCP_RETRY_TIME;
	}
	if (i >= SM5450_SCP_RETRY_TIME) {
		hwlog_err("ack error, retry %d times\n", i);
		ret = -1;
	}
	/* Manual Init */
	sm5450_write_mask(di, SM5450_SCP_CTL_REG, SM5450_SCP_CTL_SNDCMD_MASK,
		SM5450_SCP_CTL_SNDCMD_SHIFT, SM5450_SCP_CTL_SNDCMD_RESET);
	usleep_range(10000, 11000); /* wait 10ms for operate effective */

	mutex_unlock(&di->accp_adapter_reg_lock);
	return ret;
}

static int sm5450_fcp_adapter_vol_check(int adapter_vol_mv, void *dev_data)
{
	int i;
	int ret;
	int adc_vol = 0;
	struct sm5450_device_info *di = (struct sm5450_device_info *)dev_data;

	if ((adapter_vol_mv < SM5450_FCP_ADAPTER_MIN_VOL) ||
		(adapter_vol_mv > SM5450_FCP_ADAPTER_MAX_VOL)) {
		hwlog_err("check vol out of range, input vol = %dmV\n", adapter_vol_mv);
		return -1;
	}

	for (i = 0; i < SM5450_FCP_ADAPTER_VOL_CHECK_TIMEOUT; i++) {
		if (adp_plugout) {
			hwlog_err("fcp_adapter_vol_check exit, vbus disconnect.\n");
			return -1;
		}
		ret = sm5450_get_vbus_mv((unsigned int *)&adc_vol, di);
		if (ret)
			continue;
		if ((adc_vol > (adapter_vol_mv - SM5450_FCP_ADAPTER_VOL_CHECK_ERROR)) &&
			(adc_vol < (adapter_vol_mv + SM5450_FCP_ADAPTER_VOL_CHECK_ERROR)))
			break;
		msleep(SM5450_FCP_ADAPTER_VOL_CHECK_POLLTIME);
	}

	if (i == SM5450_FCP_ADAPTER_VOL_CHECK_TIMEOUT) {
		hwlog_err("check vol timeout, input vol = %dmV\n", adapter_vol_mv);
		return -1;
	}
	hwlog_info("check vol success, input vol = %dmV, spend %dms\n",
		adapter_vol_mv, i * SM5450_FCP_ADAPTER_VOL_CHECK_POLLTIME);
	return 0;
}

static int sm5450_fcp_master_reset(void *dev_data)
{
	struct sm5450_device_info *di = g_sm5450_scp_dev;
	int ret;

	if (!di) {
		hwlog_info("sm5450_device_info is null\n");
		return -1;
	}

	ret = sm5450_scp_wdt_reset_by_sw(di);
	if (ret) {
		hwlog_err("sm5450_fcp_master_reset fail\n");
		return -1;
	}
	usleep_range(10000, 11000); /* wait 10ms for operate effective */

	return sm5450_write_byte(di, SM5450_SCP_CTL_REG, SM5450_SCP_CTL_REG_INIT); /* Clear SCP_CTL, Enabled EN_STIMER */
}

static int sm5450_fcp_adapter_reset(void *dev_data)
{
	int ret;
	struct sm5450_device_info *di = g_sm5450_scp_dev;

	if (!di) {
		hwlog_info("sm5450_device_info is null\n");
		return -1;
	}

	ret = sm5450_write_mask(di, SM5450_SCP_CTL_REG, SM5450_SCP_CTL_MSTR_RST_MASK,
		SM5450_SCP_CTL_MSTR_RST_SHIFT, TRUE);
	usleep_range(20000, 21000); /* wait 20ms for operate effective */

	return ret;
}

static int sm5450_fcp_read_switch_status(void *dev_data)
{
	return 0;
}

static int sm5450_is_fcp_charger_type(void *dev_data)
{
	u8 reg_val = 0;
	int ret;
	struct sm5450_device_info *di = g_sm5450_scp_dev;

	if (!di) {
		hwlog_info("sm5450_device_info is null\n");
		return -1;
	}

	if (sm5450_is_support_fcp(di)) {
		hwlog_err("%s:NOT SUPPORT FCP\n", __func__);
		return 0;
	}

	ret = sm5450_read_byte(di, SM5450_SCP_STATUS_REG, &reg_val);
	if (ret) {
		hwlog_err("reg read fail\n");
		return 0;
	}
	if (reg_val & SM5450_SCP_STATUS_ENABLE_HAND_SUCCESS_MASK)
		return 1;
	return 0;
}

static bool sm5450_is_vbus_ok(struct sm5450_device_info *di)
{
	int ret;
	u8 flag;

	ret = sm5450_read_byte(di, SM5450_FLAG3_REG, &flag);
	if (flag & SM5450_FLAG_MASK3_VBUSPOK_FLAG_MSK_MASK) {
		return true;
	}

	return false;
}

static int sm5450_fcp_adapter_detect(struct sm5450_device_info *di)
{
	u8 reg_val = 0;
	int i;
	int ret;
	u8 reg_val1 = 0;

	if ((adp_plugout == true) && sm5450_is_vbus_ok(di))
		adp_plugout = false;

	mutex_lock(&di->scp_detect_lock);
	/* Temp */
	ret = sm5450_write_mask(di, SM5450_CONTROL3_REG, SM5450_CONTROL3_EN_COMP_MASK,
		SM5450_CONTROL3_EN_COMP_SHIFT, 1);
	ret += sm5450_read_byte(di, SM5450_SCP_STATUS_REG, &reg_val);
	ret += sm5450_read_byte(di, SM5450_CONTROL3_REG, &reg_val1);

	hwlog_info("SM5450_SCP_STATUS_REG:0x%x reg_val1=0x%x\n", reg_val, reg_val1);
	if (ret) {
		hwlog_err("read det attach err, ret:%d\n", ret);
		mutex_unlock(&di->scp_detect_lock);
		return -1;
	}

	/* Confirm Enable Hand Success Status */
	if (reg_val & SM5450_SCP_STATUS_ENABLE_HAND_SUCCESS_MASK) {
		mutex_unlock(&di->scp_detect_lock);
		hwlog_info("scp adapter detect ok\n");
		return ADAPTER_DETECT_SUCC;
	}

	sm5450_scp_wdt_reset_by_sw(di);
	usleep_range(20000, 21000); /* wait 20ms for reset */

	ret = sm5450_write_mask(di, SM5450_SCP_CTL_REG, SM5450_SCP_CTL_EN_SCP_MASK,
		SM5450_SCP_CTL_EN_SCP_SHIFT, TRUE);
	ret += sm5450_write_mask(di, SM5450_SCP_CTL_REG, SM5450_SCP_CTL_SCP_DET_EN_MASK,
		SM5450_SCP_CTL_SCP_DET_EN_SHIFT, TRUE);
	if (ret) {
		hwlog_err("SCP enable detect fail, ret is %d\n", ret);
		sm5450_fcp_adapter_reset(di);
		sm5450_scp_wdt_reset_by_sw(di); /* Reset SCP registers when EN_SCP is changed to 0 */
		mutex_unlock(&di->scp_detect_lock);
		return -1;
	}
	/* Waiting for scp_set */
	for (i = 0; i < SM5450_CHG_SCP_DETECT_MAX_COUT; i++) {
		ret = sm5450_read_byte(di, SM5450_SCP_STATUS_REG, &reg_val);
		hwlog_info("SM5450_SCP_STATUS_REG 0x%x\n", reg_val);
		if (ret) {
			hwlog_err("read det attach err, ret:%d\n", ret);
			continue;
		}
		if (adp_plugout) {
			hwlog_err("0x%x vbus uv happen, adapter plug out\n", adp_plugout);
			break;
		}
		if (reg_val & SM5450_SCP_STATUS_ENABLE_HAND_SUCCESS_MASK)
			break;
		msleep(SM5450_CHG_SCP_POLL_TIME);
	}
	if (i == SM5450_CHG_SCP_DETECT_MAX_COUT || adp_plugout) {
		sm5450_fcp_adapter_reset(di);
		sm5450_scp_wdt_reset_by_sw(di); /* Reset SCP registers when EN_SCP is changed to 0 */
		hwlog_err("CHG_SCP_ADAPTER_DETECT_OTHER return\n");
		mutex_unlock(&di->scp_detect_lock);
		return adp_plugout ? ADAPTER_DETECT_FAIL : ADAPTER_DETECT_OTHER;
	}

	mutex_unlock(&di->scp_detect_lock);
	return ret;
}

static int sm5450_fcp_stop_charge_config(void *dev_data)
{
	struct sm5450_device_info *di = g_sm5450_scp_dev;

	hwlog_info("sm5450_fcp_master_reset");

	if (!di) {
		hwlog_info("sm5450_device_info is null\n");
		return -1;
	}

	sm5450_fcp_master_reset(di);
	sm5450_write_mask(di, SM5450_SCP_CTL_REG, SM5450_SCP_CTL_SCP_DET_EN_MASK,
		SM5450_SCP_CTL_SCP_DET_EN_SHIFT, FALSE);

	return 0;
}

static int scp_adapter_reg_read(u8 *val, u8 reg)
{
	int ret;

	if (scp_error_flag) {
		hwlog_err("scp timeout happened, do not read reg = %d\n", reg);
		return -1;
	}

	ret = sm5450_scp_adapter_reg_read(val, reg);
	if (ret) {
		hwlog_err("error reg = %d\n", reg);
		if (reg != SCP_PROTOCOL_ADP_TYPE0)
			scp_error_flag = SM5450_SCP_IS_ERR;

		return -1;
	}

	return 0;
}

static int scp_adapter_reg_write(u8 val, u8 reg)
{
	int ret;

	if (scp_error_flag) {
		hwlog_err("scp timeout happened, do not write reg = %d\n", reg);
		return -1;
	}

	ret = sm5450_scp_adapter_reg_write(val, reg);
	if (ret) {
		hwlog_err("error reg = %d\n", reg);
		scp_error_flag = SM5450_SCP_IS_ERR;
		return -1;
	}

	return 0;
}

static int sm5450_self_check(void *dev_data)
{
	return 0;
}

static int sm5450_scp_chip_reset(void *dev_data)
{
	int ret;

	ret = sm5450_fcp_master_reset(dev_data);
	if (ret) {
		hwlog_err("sm5450_fcp_master_reset fail\n");
		return -1;
	}

	return 0;
}

static int sm5450_scp_reg_read_block(int reg, int *val, int num,
	void *dev_data)
{
	int ret;
	int i;
	u8 data = 0;

	if (!val) {
		hwlog_info("val is null\n");
		return -1;
	}

	scp_error_flag = SM5450_SCP_NO_ERR;

	for (i = 0; i < num; i++) {
		ret = scp_adapter_reg_read(&data, reg + i);
		if (ret) {
			hwlog_err("scp read failed, reg=0x%x\n", reg + i);
			return -1;
		}
		val[i] = data;
	}

	return 0;
}

static int sm5450_scp_reg_write_block(int reg, const int *val, int num,
	void *dev_data)
{
	int ret;
	int i;

	if (!val) {
		hwlog_info("val is null\n");
		return -1;
	}

	scp_error_flag = SM5450_SCP_NO_ERR;

	for (i = 0; i < num; i++) {
		ret = scp_adapter_reg_write(val[i], reg + i);
		if (ret) {
			hwlog_err("scp write failed, reg=0x%x\n", reg + i);
			return -1;
		}
	}

	return 0;
}

static int sm5450_scp_detect_adapter(void *dev_data)
{
	struct sm5450_device_info *di = g_sm5450_scp_dev;

	if (!di) {
		hwlog_info("sm5450_device_info is null\n");
		return -1;
	}

	return sm5450_fcp_adapter_detect(di);
}

int sm5450_fcp_reg_read_block(int reg, int *val, int num,
	void *dev_data)
{
	int ret, i;
	u8 data = 0;

	if (!val) {
		hwlog_err("val is null\n");
		return -1;
	}

	scp_error_flag = SM5450_SCP_NO_ERR;

	for (i = 0; i < num; i++) {
		ret = scp_adapter_reg_read(&data, reg + i);
		if (ret) {
			hwlog_err("fcp read failed, reg=0x%x\n", reg + i);
			return -1;
		}
		val[i] = data;
	}
	return 0;
}

static int sm5450_fcp_reg_write_block(int reg, const int *val, int num,
	void *dev_data)
{
	int ret, i;

	if (!val) {
		hwlog_err("val is null\n");
		return -1;
	}

	scp_error_flag = SM5450_SCP_NO_ERR;

	for (i = 0; i < num; i++) {
		ret = scp_adapter_reg_write(val[i], reg + i);
		if (ret) {
			hwlog_err("fcp write failed, reg=0x%x\n", reg + i);
			return -1;
		}
	}

	return 0;
}

static int sm5450_fcp_detect_adapter(void *dev_data)
{
	struct sm5450_device_info *di = g_sm5450_scp_dev;

	if (!di) {
		hwlog_err("sm5450_device_info is null\n");
		return -1;
	}

	return sm5450_fcp_adapter_detect(di);
}

static int sm5450_pre_init(void *dev_data)
{
	int ret;
	struct sm5450_device_info *di = g_sm5450_scp_dev;

	ret = sm5450_self_check(di);
	if (ret) {
		hwlog_err("sm5450_self_check fail\n");
		return ret;
	}

	return ret;
}

static int sm5450_scp_adapter_reset(void *dev_data)
{
	return sm5450_fcp_adapter_reset(dev_data);
}

static void sm5450_fault_handle(struct sm5450_device_info *di,
	struct nty_data *data)
{
	int val = 0;
	u8 flag0 = data->event1;
	u8 flag1 = data->event2;
	struct atomic_notifier_head *fault_notifier_list = NULL;

	sc_get_fault_notifier(&fault_notifier_list);
	if (flag0 & SM5450_FLAG1_VBUSCON_OVP_FLAG_MASK) {
		hwlog_info("AC OVP happened\n");
		atomic_notifier_call_chain(fault_notifier_list,
			DC_FAULT_AC_OVP, data);
	}
	if (flag1 & SM5450_FLAG2_VBATOVP_FLAG_MASK) {
		hwlog_info("BAT OVP happened\n");
		val = sm5450_get_vbat_mv(di);
		if (val >= SM5450_VBAT_OVP_REF_INIT) {
			hwlog_info("BAT OVP happened [%d]\n", val);
			atomic_notifier_call_chain(fault_notifier_list,
				DC_FAULT_VBAT_OVP, data);
		}
	}
	if (flag1 & SM5450_FLAG2_IBATOCP_FLAG_MASK) {
		hwlog_info("BAT OCP happened\n");
		sm5450_get_ibat_ma(&val, di);
		if (val >= SM5450_IBAT_OCP_REF_INIT) {
			hwlog_info("BAT OCP happened [%d]\n", val);
			atomic_notifier_call_chain(fault_notifier_list,
				DC_FAULT_IBAT_OCP, data);
		}
	}
	if (flag0 & SM5450_FLAG1_VBUSOVP_FLAG_MASK) {
		hwlog_info("BUS OVP happened\n");
		sm5450_get_vbus_mv(&val, di);
		if (val >= SM5450_VBUS_OVP_REF_INIT) {
			hwlog_info("BUS OVP happened [%d]\n", val);
			atomic_notifier_call_chain(fault_notifier_list,
				DC_FAULT_VBUS_OVP, data);
		}
	}
	if (flag0 & SM5450_FLAG1_IBUSOCP_FLAG_MASK) {
		hwlog_info("BUS OCP happened\n");
		sm5450_get_ibus_ma(&val, di);
		if (val >= SM5450_IBUS_OCP_REF_INIT) {
			hwlog_info("BUS OCP happened [%d]\n", val);
			atomic_notifier_call_chain(fault_notifier_list,
				DC_FAULT_IBUS_OCP, data);
		}
	}
	if (flag1 & SM5450_FLAG2_TSD_FLAG_MASK)
		hwlog_info("DIE TEMP OTP happened\n");
}

static int sm5450_interrupt_clear(struct sm5450_device_info *di)
{
	u8 flag[5] = {0}; /* 5:read 5 byte */
	int ret;

	if (!di) {
		hwlog_err("sm5450 irq_clear : di is null\n");
		return -1;
	}

	hwlog_info("sm5450 irq_clear start\n");

	/* To confirm the interrupt */
	ret = sm5450_read_byte(di, SM5450_FLAG1_REG, &flag[0]);
	ret += sm5450_read_byte(di, SM5450_FLAG2_REG, &flag[1]);
	ret += sm5450_read_byte(di, SM5450_FLAG3_REG, &flag[2]);
	/* To confirm the latest Status */
	ret = sm5450_read_byte(di, SM5450_FLAG1_REG, &flag[0]);
	ret += sm5450_read_byte(di, SM5450_FLAG2_REG, &flag[1]);
	ret += sm5450_read_byte(di, SM5450_FLAG3_REG, &flag[2]);
	/* To confirm the SCP interrupt */
	ret += sm5450_read_byte(di, SM5450_SCP_ISR1_REG, &flag[3]);
	ret += sm5450_read_byte(di, SM5450_SCP_ISR2_REG, &flag[4]);
	di->scp_isr_backup[0] |= flag[3];
	di->scp_isr_backup[1] |= flag[4];
	if (ret)
		hwlog_err("SCP irq_clear read fail\n");

	hwlog_info("FLAG1 [%x]=0x%x, FLAG2 [%x]=0x%x, FLAG3 [%x]=0x%x\n",
		SM5450_FLAG1_REG, flag[0], SM5450_FLAG2_REG, flag[1], SM5450_FLAG3_REG, flag[2]);
	hwlog_info("ISR1 [%x]=0x%x, ISR2 [%x]=0x%x\n",
		SM5450_SCP_ISR1_REG, flag[3], SM5450_SCP_ISR2_REG, flag[4]);

	hwlog_info("sm5450 irq_clear end\n");

	return 1;
}

static void sm5450_interrupt_work(struct work_struct *work)
{
	int ret;
	u8 flag[5] = {0}; /* read 5 bytes */
	struct sm5450_device_info *di = NULL;
	struct nty_data *data = NULL;

	if (!work) {
		hwlog_err("work is null\n");
		return;
	}

	di = container_of(work, struct sm5450_device_info, irq_work);
	if (!di || !di->client) {
		hwlog_err("di is null\n");
		return;
	}

	/* To confirm the interrupt */
	ret = sm5450_read_byte(di, SM5450_FLAG1_REG, &flag[0]);
	ret += sm5450_read_byte(di, SM5450_FLAG2_REG, &flag[1]);
	ret += sm5450_read_byte(di, SM5450_FLAG3_REG, &flag[2]);
	/* To confirm the latest Status */
	ret = sm5450_read_byte(di, SM5450_FLAG1_REG, &flag[0]);
	ret += sm5450_read_byte(di, SM5450_FLAG2_REG, &flag[1]);
	ret += sm5450_read_byte(di, SM5450_FLAG3_REG, &flag[2]);
	/* To confirm the SCP interrupt */
	ret += sm5450_read_byte(di, SM5450_SCP_ISR1_REG, &flag[3]);
	ret += sm5450_read_byte(di, SM5450_SCP_ISR2_REG, &flag[4]);
	if (ret)
		hwlog_err("SCP irq_work read fail\n");
	di->scp_isr_backup[0] |= flag[3];
	di->scp_isr_backup[1] |= flag[4];

	data = &(di->nty_data);
	data->event1 = flag[0];
	data->event2 = flag[1];
	data->event3 = flag[2];
	data->addr = di->client->addr;

	if (di->int_notify_enable_flag == SM5450_ENABLE_INT_NOTIFY) {
		sm5450_fault_handle(di, data);
		sm5450_dump_register(di);
	}

	hwlog_info("FLAG1 [%x]=0x%x, FLAG2 [%x]=0x%x, FLAG3 [%x]=0x%x\n",
		SM5450_FLAG1_REG, flag[0], SM5450_FLAG2_REG, flag[1], SM5450_FLAG3_REG, flag[2]);
	hwlog_info("ISR1 [%x]=0x%x, ISR2 [%x]=0x%x\n",
		SM5450_SCP_ISR1_REG, flag[3], SM5450_SCP_ISR2_REG, flag[4]);

	if (flag[2] & SM5450_FLAG_MASK3_VBUSCON_UVLO_FLAG_MSK_MASK) {
		adp_plugout = true;
		hwlog_info("vbus uvlo, scp wdt reset.\n");
	}
	if (flag[2] & SM5450_FLAG_MASK3_VBUSPOK_FLAG_MSK_MASK) {
		hwlog_info("vbus ok.\n");
		adp_plugout = false;
	}
	/* clear irq */
	enable_irq(di->irq_int);
}

static irqreturn_t sm5450_interrupt(int irq, void *_di)
{
	struct sm5450_device_info *di = _di;

	if (!di) {
		hwlog_err("di is null\n");
		return -1;
	}

	if (di->init_finish_flag == SM5450_INIT_FINISH)
		di->int_notify_enable_flag = SM5450_ENABLE_INT_NOTIFY;

	hwlog_info("sm5450 int happened\n");

	disable_irq_nosync(di->irq_int);
	schedule_work(&di->irq_work);

	return IRQ_HANDLED;
}

static void sm5450_parse_dts(struct device_node *np,
	struct sm5450_device_info *di)
{
	(void)power_dts_read_u32(power_dts_tag(HWLOG_TAG), np,
		"switching_frequency", &di->switching_frequency,
		SM5450_SW_FREQ_550KHZ);
	(void)power_dts_read_u32(power_dts_tag(HWLOG_TAG), np, "scp_support",
		(u32 *)&(di->param_dts.scp_support), 0);
	(void)power_dts_read_u32(power_dts_tag(HWLOG_TAG), np, "fcp_support",
		(u32 *)&(di->param_dts.fcp_support), 0);
	(void)power_dts_read_u32(power_dts_tag(HWLOG_TAG), np, "ic_role",
		(u32 *)&(di->ic_role), CHARGE_IC_TYPE_MAIN);
	(void)power_dts_read_u32(power_dts_tag(HWLOG_TAG), np,
		"sense_r_config", &di->sense_r_config, SENSE_R_5_MOHM);
	(void)power_dts_read_u32(power_dts_tag(HWLOG_TAG), np,
		"sense_r_actual", &di->sense_r_actual, SENSE_R_5_MOHM);
	(void)power_dts_read_u32(power_dts_tag(HWLOG_TAG), np, "is_use_platform_battery_voltage",
		(u32 *)&(di->is_use_platform_battery_voltage), 0);
}

static int sm5450_lock_mutex_init(struct sm5450_device_info *di)
{
	if (!di)
		return -ENOMEM;

	mutex_init(&di->scp_detect_lock);
	mutex_init(&di->accp_adapter_reg_lock);

	return 0;
}

static int sm5450_lock_mutex_destroy(struct sm5450_device_info *di)
{
	if (!di)
		return -ENOMEM;

	mutex_destroy(&di->scp_detect_lock);
	mutex_destroy(&di->accp_adapter_reg_lock);

	return 0;
}

static int sm5450_db_value_dump(struct sm5450_device_info *di,
	char *reg_value, int size)
{
	int vbus = 0;
	int ibat = 0;
	int temp = 0;
	int ibus = 0;
	char buff[BUF_LEN] = {0};
	int len = 0;
	int ret;
	u8 reg = 0;

	ret = sm5450_get_vbus_mv(&vbus, di);
	ret += sm5450_get_ibat_ma(&ibat, di);
	ret += sm5450_get_ibus_ma(&ibus, di);
	ret += sm5450_get_device_temp(&temp, di);
	ret += sm5450_read_byte(di, SM5450_CONTROL1_REG, &reg);
	if (ret)
		hwlog_err("%s: error occur when get vbus ibat ibus temp\n", __func__);

	if (((reg & SM5450_CONTROL1_OPMODE_MASK) >> SM5450_CONTROL1_OPMODE_SHIFT) ==
		SM5450_CONTROL1_FBYPASSMODE)
		snprintf(buff, sizeof(buff), "%s", "     LVC    ");
	else if (((reg & SM5450_CONTROL1_OPMODE_MASK) >> SM5450_CONTROL1_OPMODE_SHIFT) ==
		SM5450_CONTROL1_FCHGPUMPMODE)
		snprintf(buff, sizeof(buff), "%s", "     SC     ");
	else
		snprintf(buff, sizeof(buff), "%s", "     BUCK   ");
	len += strlen(buff);
	if (len < size)
		strncat(reg_value, buff, strlen(buff));

	len += snprintf(buff, sizeof(buff), "%-7.2d%-7.2d%-7.2d%-7.2d%-7.2d",
		ibus, vbus, ibat, sm5450_get_vbat_mv(di), temp);
	strncat(reg_value, buff, strlen(buff));

	return len;
}

/* print the register value in charging process */
static int sm5450_dump_reg_value(struct charger_device *dev,
	char *reg_value, int size, void *dev_data)
{
	u8 reg_val = 0;
	int i;
	int ret = 0;
	int len = 0;
	int tmp;
	char buff[BUF_LEN] = {0};
	struct sm5450_device_info *di = (struct sm5450_device_info *)dev_data;

	if (!di) {
		hwlog_err("%s sm5450_device_info is null\n", __func__);
		return -ENOMEM;
	}

	if (di->ic_role == CHARGE_IC_TYPE_AUX) {
		snprintf(buff, sizeof(buff), "   ");
		len += strlen(buff);
		if (len < size)
			strncat(reg_value, buff, strlen(buff));
		memset(buff, 0, sizeof(buff));
	}

	len = sm5450_db_value_dump(di, reg_value, size);

	for (i = 0; i < PAGE0_NUM; i++) {
		tmp = PAGE0_BASE + i;
		if (!((tmp == SM5450_FLAG1_REG) || (tmp == SM5450_FLAG2_REG) ||
			(tmp == SM5450_FLAG3_REG))) {
			ret = ret || sm5450_read_byte(di, tmp, &reg_val);
			snprintf(buff, sizeof(buff), "0x%-7x", reg_val);
			len += strlen(buff);
			if (len < size)
				strncat(reg_value, buff, strlen(buff));
		}
	}
	memset(buff, 0, sizeof(buff));
	for (i = 0; i < PAGE1_NUM; i++) {
		tmp = PAGE1_BASE + i;
		if (!((tmp == SM5450_SCP_ISR1_REG) || (tmp  == SM5450_SCP_ISR2_REG))) {
			ret = ret || sm5450_read_byte(di, tmp, &reg_val);
			snprintf(buff, sizeof(buff), "0x%-7x", reg_val);
			len += strlen(buff);
			if (len < size)
				strncat(reg_value, buff, strlen(buff));
		}
	}

	return 0;
}

/* print the register head in charging process */
static int sm5450_reg_head(struct charger_device *dev, char *reg_head,
	int size, void *dev_data)
{
	int i;
	int tmp;
	int len = 0;
	char buff[BUF_LEN] = {0};
	const char *half_head = "   mode   Ibus   Vbus   Ibat   Vbat   Temp   ";
	const char *half_head1 = "   mode1   Ibus1   Vbus1   Ibat1   Vbat1   Temp1   ";
	struct sm5450_device_info *di = (struct sm5450_device_info *)dev_data;

	if (di->ic_role == CHARGE_IC_TYPE_MAIN) {
		snprintf(reg_head, size, half_head);
		len += strlen(half_head);
	} else {
		snprintf(reg_head, size, half_head1);
		len += strlen(half_head1);
	}

	memset(buff, 0, sizeof(buff));
	for (i = 0; i < PAGE0_NUM; i++) {
		tmp = PAGE0_BASE + i;
		if (!((tmp == SM5450_FLAG1_REG) || (tmp == SM5450_FLAG2_REG) ||
			(tmp == SM5450_FLAG3_REG))) {
			snprintf(buff, sizeof(buff), "R[0x%-2x] ", tmp);
			len += strlen(buff);
			if (len < size)
				strncat(reg_head, buff, strlen(buff));
		}
	}

	memset(buff, 0, sizeof(buff));
	for (i = 0; i < PAGE1_NUM; i++) {
		tmp = PAGE1_BASE + i;
		if (!((tmp == SM5450_SCP_ISR1_REG) || (tmp  == SM5450_SCP_ISR2_REG))) {
			snprintf(buff, sizeof(buff), "R[0x%-2x] ", tmp);
			len += strlen(buff);
			if (len < size)
				strncat(reg_head, buff, strlen(buff));
		}
	}

	return 0;
}

int sm5450_register_head(char *buffer, int size, void *dev_data)
{
	struct sm5450_device_info *di = (struct sm5450_device_info *)dev_data;

	return sm5450_reg_head(NULL, buffer, size, di);
}

int sm5450_dump_reg(char *buffer, int size, void *dev_data)
{
	struct sm5450_device_info *di = (struct sm5450_device_info *)dev_data;

	return sm5450_dump_reg_value(NULL, buffer, size, di);
}

static struct dc_ic_ops sm5450_lvc_ops = {
	.dev_name = "sm5450",
	.ic_init = sm5450_charge_init,
	.ic_exit = sm5450_charge_exit,
	.ic_enable = sm5450_lvc_charge_enable,
	.ic_discharge = sm5450_discharge,
	.is_ic_close = sm5450_is_device_close,
	.get_ic_id = sm5450_get_device_id,
	.config_ic_watchdog = sm5450_config_watchdog_ms,
	.ic_reg_reset_and_init = sm5450_reg_reset_and_init,
};

static struct dc_ic_ops sm5450_sc_ops = {
	.dev_name = "sm5450",
	.ic_init = sm5450_charge_init,
	.ic_exit = sm5450_charge_exit,
	.ic_enable = sm5450_sc_charge_enable,
	.ic_discharge = sm5450_discharge,
	.is_ic_close = sm5450_is_device_close,
	.get_ic_id = sm5450_get_device_id,
	.config_ic_watchdog = sm5450_config_watchdog_ms,
	.ic_reg_reset_and_init = sm5450_reg_reset_and_init,
};

static struct dc_batinfo_ops sm5450_batinfo_ops = {
	.init = sm5450_batinfo_init,
	.exit = sm5450_batinfo_exit,
	.get_bat_btb_voltage = sm5450_get_vbat_mv,
	.get_bat_package_voltage = sm5450_get_vbat_mv,
	.get_vbus_voltage = sm5450_get_vbus_mv,
	.get_bat_current = sm5450_get_ibat_ma,
	.get_ic_ibus = sm5450_get_ibus_ma,
	.get_ic_temp = sm5450_get_device_temp,
};

static struct scp_protocol_ops sm5450_scp_protocol_ops = {
	.chip_name = "sm5450",
	.reg_read = sm5450_scp_reg_read_block,
	.reg_write = sm5450_scp_reg_write_block,
	.reg_multi_read = sm5450_scp_adapter_reg_read_block,
	.detect_adapter = sm5450_scp_detect_adapter,
	.soft_reset_master = sm5450_scp_chip_reset,
	.soft_reset_slave = sm5450_scp_adapter_reset,
	.pre_init = sm5450_pre_init,
};

static struct fcp_protocol_ops sm5450_fcp_protocol_ops = {
	.chip_name = "sm5450",
	.reg_read = sm5450_fcp_reg_read_block,
	.reg_write = sm5450_fcp_reg_write_block,
	.detect_adapter = sm5450_fcp_detect_adapter,
	.soft_reset_master = sm5450_fcp_master_reset,
	.soft_reset_slave = sm5450_fcp_adapter_reset,
	.get_master_status = sm5450_fcp_read_switch_status,
	.stop_charging_config = sm5450_fcp_stop_charge_config,
	.is_accp_charger_type = sm5450_is_fcp_charger_type,
};

static struct power_log_ops sm5450_log_ops = {
	.dev_name = "sm5450",
	.dump_log_head = sm5450_register_head,
	.dump_log_content = sm5450_dump_reg,
};

static struct dc_ic_ops sm5450_aux_lvc_ops = {
	.dev_name = "sm5450_aux",
	.ic_init = sm5450_charge_init,
	.ic_exit = sm5450_charge_exit,
	.ic_enable = sm5450_lvc_charge_enable,
	.ic_discharge = sm5450_discharge,
	.is_ic_close = sm5450_is_device_close,
	.get_ic_id = sm5450_get_device_id,
	.config_ic_watchdog = sm5450_config_watchdog_ms,
	.ic_reg_reset_and_init = sm5450_reg_reset_and_init,
};

static struct dc_ic_ops sm5450_aux_sc_ops = {
	.dev_name = "sm5450_aux",
	.ic_init = sm5450_charge_init,
	.ic_exit = sm5450_charge_exit,
	.ic_enable = sm5450_sc_charge_enable,
	.ic_discharge = sm5450_discharge,
	.is_ic_close = sm5450_is_device_close,
	.get_ic_id = sm5450_get_device_id,
	.config_ic_watchdog = sm5450_config_watchdog_ms,
	.ic_reg_reset_and_init = sm5450_reg_reset_and_init,
};

static struct dc_batinfo_ops sm5450_aux_batinfo_ops = {
	.init = sm5450_batinfo_init,
	.exit = sm5450_batinfo_exit,
	.get_bat_btb_voltage = sm5450_get_vbat_mv,
	.get_bat_package_voltage = sm5450_get_vbat_mv,
	.get_vbus_voltage = sm5450_get_vbus_mv,
	.get_bat_current = sm5450_get_ibat_ma,
	.get_ic_ibus = sm5450_get_ibus_ma,
	.get_ic_temp = sm5450_get_device_temp,
};

static struct power_log_ops sm5450_aux_log_ops = {
	.dev_name = "sm5450_aux",
	.dump_log_head = sm5450_register_head,
	.dump_log_content = sm5450_dump_reg,
};

static void sm5450_init_ops_dev_data(struct sm5450_device_info *di)
{
	if (di->ic_role == CHARGE_IC_TYPE_MAIN) {
		sm5450_lvc_ops.dev_data = (void *)di;
		sm5450_sc_ops.dev_data = (void *)di;
		sm5450_batinfo_ops.dev_data = (void *)di;
		sm5450_log_ops.dev_data = (void *)di;
	} else {
		sm5450_aux_lvc_ops.dev_data = (void *)di;
		sm5450_aux_sc_ops.dev_data = (void *)di;
		sm5450_aux_batinfo_ops.dev_data = (void *)di;
		sm5450_aux_log_ops.dev_data = (void *)di;
	}
}

static int sm5450_ops_register(struct sm5450_device_info *di)
{
	int ret;

	sm5450_init_ops_dev_data(di);

	if (di->ic_role == CHARGE_IC_TYPE_MAIN) {
		mutex_lock(&g_sm5450_log_lock);
		ret = dc_ic_ops_register(LVC_MODE, CHARGE_IC_TYPE_MAIN, &sm5450_lvc_ops);
		ret += dc_ic_ops_register(SC_MODE, CHARGE_IC_TYPE_MAIN, &sm5450_sc_ops);
		ret += dc_batinfo_ops_register(SC_MODE, CHARGE_IC_TYPE_MAIN, &sm5450_batinfo_ops);
		ret += dc_batinfo_ops_register(LVC_MODE, CHARGE_IC_TYPE_MAIN, &sm5450_batinfo_ops);
		if (sm5450_is_support_scp(di) == 0)
			ret += scp_protocol_ops_register(&sm5450_scp_protocol_ops);
		if (sm5450_is_support_fcp(di) == 0)
			ret += fcp_protocol_ops_register(&sm5450_fcp_protocol_ops);

		ret += power_log_ops_register(&sm5450_log_ops);
		mutex_unlock(&g_sm5450_log_lock);
	} else {
		mutex_lock(&g_sm5450_log_lock);
		ret = dc_ic_ops_register(LVC_MODE, CHARGE_IC_TYPE_AUX, &sm5450_aux_lvc_ops);
		ret += dc_ic_ops_register(SC_MODE, CHARGE_IC_TYPE_AUX, &sm5450_aux_sc_ops);
		ret += dc_batinfo_ops_register(SC_MODE, CHARGE_IC_TYPE_AUX, &sm5450_aux_batinfo_ops);
		ret += dc_batinfo_ops_register(LVC_MODE, CHARGE_IC_TYPE_AUX, &sm5450_aux_batinfo_ops);
		if (sm5450_is_support_scp(di) == 0)
			ret += scp_protocol_ops_register(&sm5450_scp_protocol_ops);
		if (sm5450_is_support_fcp(di) == 0)
			ret += fcp_protocol_ops_register(&sm5450_fcp_protocol_ops);

		ret += power_log_ops_register(&sm5450_aux_log_ops);
		mutex_unlock(&g_sm5450_log_lock);
	}

	if (ret) {
		hwlog_err("ops_register fail\n");
		return ret;
	}
	return 0;
}

static int sm5450_probe(struct i2c_client *client,
	const struct i2c_device_id *id)
{
	int ret;
	struct sm5450_device_info *di = NULL;
	struct device_node *np = NULL;

	hwlog_info("probe begin\n");

	if (!client || !client->dev.of_node || !id)
		return -ENODEV;

	di = devm_kzalloc(&client->dev, sizeof(*di), GFP_KERNEL);
	if (!di)
		return -ENOMEM;

	if (sm5450_lock_mutex_init(di)) {
		ret = -EINVAL;
		goto sm5450_fail_0;
	}

	di->dev = &client->dev;
	np = di->dev->of_node;
	di->client = client;
	i2c_set_clientdata(client, di);
	di->chip_already_init = 1;

	di->device_id = sm5450_get_device_id(di);
	if (di->device_id == -1)
		return -1;
	di->rev_id = sm5450_get_revision_id(di);
	if (di->rev_id == -1)
		return -1;

	sm5450_parse_dts(np, di);

	if ((sm5450_is_support_scp(di) == 0) || (sm5450_is_support_fcp(di) == 0))
		g_sm5450_scp_dev = di;

	ret = sm5450_interrupt_clear(di);
	if (ret < 0) {
		hwlog_err("interrupt not cleared\n");
		goto sm5450_fail_0;
	}

	power_gpio_config_interrupt(np, "intr_gpio", "sm5450_gpio_int",
		&(di->gpio_int), &(di->irq_int));
	ret = request_irq(di->irq_int, sm5450_interrupt,
		IRQF_TRIGGER_FALLING, "sm5450_int_irq", di);
	if (ret) {
		hwlog_err("gpio irq request fail\n");
		di->irq_int = -1;
		goto sm5450_fail_1;
	}
	INIT_WORK(&di->irq_work, sm5450_interrupt_work);

	ret = sm5450_ops_register(di);
	if (ret)
		goto sm5450_fail_2;

	ret = sm5450_reg_reset(di);
	if (ret) {
		hwlog_err("sm5450 reg reset fail\n");
		goto sm5450_fail_2;
	}

	ret = sm5450_reg_init(di);
	if (ret)
		goto sm5450_fail_2;

	hwlog_info("probe end\n");
	return 0;

sm5450_fail_2:
	free_irq(di->irq_int, di);
sm5450_fail_1:
	gpio_free(di->gpio_int);
	(void)sm5450_lock_mutex_destroy(di);
sm5450_fail_0:
	di->chip_already_init = 0;
	g_sm5450_scp_dev = NULL;
	devm_kfree(&client->dev, di);

	return ret;
}

static int sm5450_remove(struct i2c_client *client)
{
	struct sm5450_device_info *di = i2c_get_clientdata(client);

	if (!di)
		return -ENODEV;

	sm5450_reg_reset(di);

	if (di->irq_int)
		free_irq(di->irq_int, di);
	if (di->gpio_int)
		gpio_free(di->gpio_int);
	(void)sm5450_lock_mutex_destroy(di);

	return 0;
}

static void sm5450_shutdown(struct i2c_client *client)
{
	struct sm5450_device_info *di = i2c_get_clientdata(client);

	if (!di)
		return;

	sm5450_reg_reset(di);
}

MODULE_DEVICE_TABLE(i2c, sm5450);
static const struct of_device_id sm5450_of_match[] = {
	{
		.compatible = "sm5450",
		.data = NULL,
	},
	{},
};

static const struct i2c_device_id sm5450_i2c_id[] = {
	{ "sm5450", 0 }, {}
};

static struct i2c_driver sm5450_driver = {
	.probe = sm5450_probe,
	.remove = sm5450_remove,
	.shutdown = sm5450_shutdown,
	.id_table = sm5450_i2c_id,
	.driver = {
		.owner = THIS_MODULE,
		.name = "sm5450",
		.of_match_table = of_match_ptr(sm5450_of_match),
	},
};

static int __init sm5450_init(void)
{
	return i2c_add_driver(&sm5450_driver);
}

static void __exit sm5450_exit(void)
{
	i2c_del_driver(&sm5450_driver);
}

module_init(sm5450_init);
module_exit(sm5450_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("sm5450 module driver");
MODULE_AUTHOR("Huawei Technologies Co., Ltd.");
