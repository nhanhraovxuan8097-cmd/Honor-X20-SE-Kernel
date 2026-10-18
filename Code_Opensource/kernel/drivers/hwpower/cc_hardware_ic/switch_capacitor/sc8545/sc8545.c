/*
 * sc8545.c
 *
 * sc8545 driver for direct charge and scp protocol
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

#include "sc8545.h"
#include <chipset_common/hwpower/power_i2c.h>
#include <chipset_common/hwpower/power_log.h>
#include <chipset_common/hwpower/power_gpio.h>
#include <chipset_common/hwpower/power_delay.h>
#include <chipset_common/hwpower/power_printk.h>

#define HWLOG_TAG sc8545
HWLOG_REGIST();

static bool adp_plugout;
static struct sc8545_device_info *g_sc8545_scp_dev;

static int sc8545_write_byte(struct sc8545_device_info *di,
	u8 reg, u8 value)
{
	if (!di || (di->chip_already_init == 0)) {
		hwlog_err("chip not init\n");
		return -ENODEV;
	}

	return power_i2c_u8_write_byte(di->client, reg, value);
}

static int sc8545_read_byte(struct sc8545_device_info *di,
	u8 reg, u8 *value)
{
	if (!di || (di->chip_already_init == 0)) {
		hwlog_err("chip not init\n");
		return -ENODEV;
	}

	return power_i2c_u8_read_byte(di->client, reg, value);
}

static int sc8545_read_word(struct sc8545_device_info *di,
	u8 reg, s16 *value)
{
	u16 data = 0;

	if (!di || (di->chip_already_init == 0)) {
		hwlog_err("chip not init\n");
		return -ENODEV;
	}

	if (power_i2c_u8_read_word(di->client, reg, &data, true))
		return -EIO;

	*value = (s16)data;
	return 0;
}

static int sc8545_write_mask(struct sc8545_device_info *di,
	u8 reg, u8 mask, u8 shift, u8 value)
{
	int ret;
	u8 val = 0;

	ret = sc8545_read_byte(di, reg, &val);
	if (ret < 0)
		return ret;

	val &= ~mask;
	val |= ((value << shift) & mask);

	return sc8545_write_byte(di, reg, val);
}

static void sc8545_dump_register(void *dev_data)
{
	int i;
	u8 flag = 0;
	struct sc8545_device_info *di = (struct sc8545_device_info *)dev_data;

	if (!di)
		return;

	for (i = SC8545_VBATREG_REG; i <= SC8545_PMID2OUT_OVP_UVP_REG; i++) {
		sc8545_read_byte(di, i, &flag);
		hwlog_info("reg[0x%x]=0x%x\n", i, flag);
	}
}

static int sc8545_discharge(int enable, void *dev_data)
{
	int ret;
	u8 reg = 0;
	u8 value = enable ? 0x1 : 0x0;
	struct sc8545_device_info *di = (struct sc8545_device_info *)dev_data;

	if (!di)
		return -ENODEV;

	ret = sc8545_write_mask(di, SC8545_CTRL2_REG,
		SC8545_VBUS_PD_EN_MASK, SC8545_VBUS_PD_EN_SHIFT, value);
	ret += sc8545_read_byte(di, SC8545_CTRL2_REG, &reg);
	if (ret) {
		hwlog_err("discharge fail\n");
		return -EIO;
	}

	if (enable) {
		/* delay 50ms to disable vbus_pd manually */
		(void)power_msleep(DT_MSLEEP_50MS, 0, NULL);
		(void)sc8545_write_mask(di, SC8545_CTRL2_REG,
			SC8545_VBUS_PD_EN_MASK, SC8545_VBUS_PD_EN_SHIFT, 0);
	}

	hwlog_info("discharge [%x]=0x%x\n", SC8545_CTRL2_REG, reg);
	return 0;
}

static int sc8545_is_device_close(void *dev_data)
{
	u8 reg = 0;
	int ret;
	struct sc8545_device_info *di = (struct sc8545_device_info *)dev_data;

	if (!di)
		return 1;

	ret = sc8545_read_byte(di, SC8545_CONVERTER_STATE_REG, &reg);
	if (ret)
		return 1;

	if (reg & SC8545_CP_SWITCHING_STAT_MASK)
		return 0;

	return 1;
}

static int sc8545_get_device_id(void *dev_data)
{
	u8 dev_id = 0;
	int ret;
	static bool first_rd;
	struct sc8545_device_info *di = (struct sc8545_device_info *)dev_data;

	if (!di)
		return SC8545_DEVICE_ID_GET_FAIL;

	if (first_rd)
		return di->device_id;

	first_rd = true;
	ret = sc8545_read_byte(di, SC8545_DEVICE_ID_REG, &dev_id);
	if (ret) {
		first_rd = false;
		hwlog_err("get_device_id read fail\n");
		return SC8545_DEVICE_ID_GET_FAIL;
	}
	hwlog_info("get_device_id [%x]=0x%x\n", SC8545_DEVICE_ID_REG, dev_id);

	if (dev_id == SC8545_DEVICE_ID_VALUE)
		di->device_id = SWITCHCAP_SC8545;
	else
		di->device_id = SC8545_DEVICE_ID_GET_FAIL;

	hwlog_info("device_id: 0x%x\n", di->device_id);

	return di->device_id;
}

static int sc8545_get_vbat_mv(void *dev_data)
{
	s16 data = 0;
	int ret;
	int vbat;
	struct sc8545_device_info *di = (struct sc8545_device_info *)dev_data;

	if (!di)
		return -ENODEV;

	ret = sc8545_read_word(di, SC8545_VBAT_ADC1_REG, &data);
	if (ret)
		return -EIO;

	/* VBAT ADC LSB: 1.25mV */
	vbat = (int)data * 125 / 100;

	hwlog_info("VBAT_ADC=0x%x, vbat=%d\n", data, vbat);
	return vbat;
}

static int sc8545_get_ibat_ma(int *ibat, void *dev_data)
{
	int ret;
	s16 data = 0;
	struct sc8545_device_info *di = (struct sc8545_device_info *)dev_data;

	if (!di || !ibat)
		return -EINVAL;

	ret = sc8545_read_word(di, SC8545_IBAT_ADC1_REG, &data);
	if (ret)
		return -EIO;

	/* IBAT ADC LSB: 3.125mA */
	*ibat = ((int)data * 3125 / 1000) * di->sense_r_config;
	*ibat /= di->sense_r_actual;

	hwlog_info("IBAT_ADC=0x%x, ibat=%d\n", data, *ibat);
	return 0;
}

static int sc8545_get_ibus_ma(int *ibus, void *dev_data)
{
	s16 data = 0;
	int ret;
	struct sc8545_device_info *di = (struct sc8545_device_info *)dev_data;

	if (!di || !ibus)
		return -EINVAL;

	ret = sc8545_read_word(di, SC8545_IBUS_ADC1_REG, &data);
	if (ret)
		return -EIO;

	/* IBUS ADC LSB: 1.875mA */
	*ibus = (int)data * 1875 / 1000;

	hwlog_info("IBUS_ADC=0x%x, ibus=%d\n", data, *ibus);
	return 0;
}

static int sc8545_get_vbus_mv(int *vbus, void *dev_data)
{
	int ret;
	s16 data = 0;
	struct sc8545_device_info *di = (struct sc8545_device_info *)dev_data;

	if (!di || !vbus)
		return -EINVAL;

	ret = sc8545_read_word(di, SC8545_VBUS_ADC1_REG, &data);
	if (ret)
		return -EIO;

	/* VBUS ADC LSB: 3.75mV */
	*vbus = (int)data * 375 / 100;

	hwlog_info("VBUS_ADC=0x%x, vbus=%d\n", data, *vbus);
	return 0;
}

static int sc8545_get_vusb_mv(int *vusb, void *dev_data)
{
	int ret;
	s16 data = 0;
	struct sc8545_device_info *di = (struct sc8545_device_info *)dev_data;

	if (!di || !vusb)
		return -EINVAL;

	ret = sc8545_read_word(di, SC8545_VAC_ADC1_REG, &data);
	if (ret)
		return -EIO;

	/* VUSB ADC LSB: 5mV */
	*vusb = (int)data * 5;

	hwlog_info("VUSB_ADC=0x%x, vusb=%d\n", data, *vusb);
	return 0;
}

static int sc8545_get_device_temp(int *temp, void *dev_data)
{
	s16 data = 0;
	int ret;
	struct sc8545_device_info *di = (struct sc8545_device_info *)dev_data;

	if (!di || !temp)
		return -EINVAL;

	ret = sc8545_read_word(di, SC8545_TDIE_ADC1_REG, &data);
	if (ret)
		return -EIO;

	/* TDIE_ADC LSB: 0.5C */
	*temp = (int)data * 5 / 10;

	hwlog_info("TDIE_ADC=0x%x, temp=%d\n", data, *temp);
	return 0;
}

static int sc8545_config_watchdog_ms(int time, void *dev_data)
{
	u8 val;
	int ret;
	u8 reg = 0;
	struct sc8545_device_info *di = (struct sc8545_device_info *)dev_data;

	if (!di)
		return -ENODEV;

	if (time >= SC8545_WTD_CONFIG_TIMING_30000MS)
		val = SC8545_WTD_SET_30000MS;
	else if (time >= SC8545_WTD_CONFIG_TIMING_5000MS)
		val = SC8545_WTD_SET_5000MS;
	else if (time >= SC8545_WTD_CONFIG_TIMING_1000MS)
		val = SC8545_WTD_SET_1000MS;
	else if (time >= SC8545_WTD_CONFIG_TIMING_500MS)
		val = SC8545_WTD_SET_500MS;
	else if (time >= SC8545_WTD_CONFIG_TIMING_200MS)
		val = SC8545_WTD_SET_200MS;
	else
		val = SC8545_WTD_CONFIG_TIMING_DIS;

	ret = sc8545_write_mask(di, SC8545_CTRL3_REG, SC8545_WTD_TIMEOUT_MASK,
		SC8545_WTD_TIMEOUT_SHIFT, val);
	ret += sc8545_read_byte(di, SC8545_CTRL3_REG, &reg);
	if (ret)
		return -EIO;

	hwlog_info("config_watchdog_ms [%x]=0x%x\n", SC8545_CTRL3_REG, reg);
	return 0;
}

static int sc8545_config_vbat_ovp_th_mv(void *dev_data, int ovp_th)
{
	u8 value;
	int ret;
	struct sc8545_device_info *di = (struct sc8545_device_info *)dev_data;

	if (!di)
		return -ENODEV;

	if (ovp_th < SC8545_VBAT_OVP_BASE)
		ovp_th = SC8545_VBAT_OVP_BASE;

	if (ovp_th > SC8545_VBAT_OVP_MAX)
		ovp_th = SC8545_VBAT_OVP_MAX;

	value = (u8)((ovp_th - SC8545_VBAT_OVP_BASE) / SC8545_VBAT_OVP_STEP);
	ret = sc8545_write_mask(di, SC8545_VBAT_OVP_REG, SC8545_VBAT_OVP_MASK,
		SC8545_VBAT_OVP_SHIFT, value);
	ret += sc8545_read_byte(di, SC8545_VBAT_OVP_REG, &value);
	if (ret)
		return -EIO;

	hwlog_info("config_vbat_ovp_threshold_mv [%x]=0x%x\n",
		SC8545_VBAT_OVP_REG, value);
	return 0;
}

static int sc8545_config_vbat_regulation(void *dev_data, int vbat_regulation)
{
	u8 value;
	int ret;
	struct sc8545_device_info *di = (struct sc8545_device_info *)dev_data;

	if (!di)
		return -ENODEV;

	if (vbat_regulation < SC8545_VBATREG_BELOW_OVP_BASE)
		vbat_regulation = SC8545_VBATREG_BELOW_OVP_BASE;

	if (vbat_regulation > SC8545_VBATREG_BELOW_OVP_MAX)
		vbat_regulation = SC8545_VBATREG_BELOW_OVP_MAX;

	value = (u8)((vbat_regulation - SC8545_VBATREG_BELOW_OVP_BASE) /
		SC8545_VBATREG_BELOW_OVP_STEP);
	ret = sc8545_write_mask(di, SC8545_VBATREG_REG,
		SC8545_VBATREG_BELOW_OVP_MASK,
		SC8545_VBATREG_BELOW_OVP_SHIFT, value);
	ret += sc8545_write_mask(di, SC8545_VBATREG_REG, SC8545_VBATREG_EN_MASK,
		SC8545_VBATREG_EN_SHIFT, 1);
	ret += sc8545_read_byte(di, SC8545_VBATREG_REG, &value);
	if (ret)
		return -EIO;

	hwlog_info("config_vbat_reg_threshold_mv [%x]=0x%x\n",
		SC8545_IBATREG_REG, value);
	return 0;
}

static int sc8545_config_ibat_ocp_th_ma(void *dev_data, int ocp_th)
{
	u8 value;
	int ret;
	struct sc8545_device_info *di = (struct sc8545_device_info *)dev_data;

	if (!di)
		return -ENODEV;

	if (ocp_th < SC8545_IBAT_OCP_BASE)
		ocp_th = SC8545_IBAT_OCP_BASE;

	if (ocp_th > SC8545_IBAT_OCP_MAX)
		ocp_th = SC8545_IBAT_OCP_MAX;

	value = (u8)((ocp_th - SC8545_IBAT_OCP_BASE) / SC8545_IBAT_OCP_STEP);
	ret = sc8545_write_mask(di, SC8545_IBAT_OCP_REG, SC8545_IBAT_OCP_MASK,
		SC8545_IBAT_OCP_SHIFT, value);
	ret += sc8545_read_byte(di, SC8545_IBAT_OCP_REG, &value);
	if (ret)
		return -EIO;

	hwlog_info("config_ibat_ocp_threshold_ma [%x]=0x%x\n",
		SC8545_IBAT_OCP_REG, value);
	return 0;
}

static int sc8545_config_ibat_regulation(void *dev_data, int ibat_regulation)
{
	u8 value;
	int ret;
	struct sc8545_device_info *di = (struct sc8545_device_info *)dev_data;

	if (!di)
		return -ENODEV;

	if (ibat_regulation < SC8545_IBATREG_BELOW_OCP_BASE)
		ibat_regulation = SC8545_IBATREG_BELOW_OCP_BASE;

	if (ibat_regulation > SC8545_IBATREG_BELOW_OCP_MAX)
		ibat_regulation = SC8545_IBATREG_BELOW_OCP_MAX;

	value = (u8)((ibat_regulation - SC8545_IBATREG_BELOW_OCP_BASE) /
		SC8545_IBATREG_BELOW_OCP_STEP);
	ret = sc8545_write_mask(di, SC8545_IBATREG_REG,
		SC8545_IBATREG_BELOW_OCP_MASK,
		SC8545_IBATREG_BELOW_OCP_SHIFT, value);
	ret += sc8545_write_mask(di, SC8545_IBATREG_REG, SC8545_IBATREG_EN_MASK,
		SC8545_IBATREG_EN_SHIFT, 1);
	ret += sc8545_read_byte(di, SC8545_IBATREG_REG, &value);
	if (ret)
		return -EIO;

	hwlog_info("config_ibat_reg_regulation_mv [%x]=0x%x\n",
		SC8545_IBATREG_REG, value);
	return 0;
}

static int sc8545_config_vac_ovp_th_mv(void *dev_data, int ovp_th)
{
	u8 value;
	int ret;
	struct sc8545_device_info *di = (struct sc8545_device_info *)dev_data;

	if (!di)
		return -ENODEV;

	if (ovp_th < SC8545_VAC_OVP_BASE)
		ovp_th = SC8545_VAC_OVP_DEFAULT;

	if (ovp_th > SC8545_VAC_OVP_MAX)
		ovp_th = SC8545_VAC_OVP_MAX;

	value = (u8)((ovp_th - SC8545_VAC_OVP_BASE) / SC8545_VAC_OVP_STEP);
	ret = sc8545_write_mask(di, SC8545_VAC_OVP_REG, SC8545_VAC_OVP_MASK,
		SC8545_VAC_OVP_SHIFT, value);
	ret += sc8545_read_byte(di, SC8545_VAC_OVP_REG, &value);
	if (ret)
		return -EIO;

	hwlog_info("config_ac_ovp_threshold_mv [%x]=0x%x\n",
		SC8545_VAC_OVP_REG, value);
	return 0;
}

static int sc8545_config_vbus_ovp_th_mv(void *dev_data, int ovp_th)
{
	u8 value;
	int ret;
	struct sc8545_device_info *di = (struct sc8545_device_info *)dev_data;

	if (!di)
		return -ENODEV;

	if (ovp_th < SC8545_VBUS_OVP_BASE)
		ovp_th = SC8545_VBUS_OVP_BASE;

	if (ovp_th > SC8545_VBUS_OVP_MAX)
		ovp_th = SC8545_VBUS_OVP_MAX;

	value = (u8)((ovp_th - SC8545_VBUS_OVP_BASE) / SC8545_VBUS_OVP_STEP);
	ret = sc8545_write_mask(di, SC8545_VBUS_OVP_REG, SC8545_VBUS_OVP_MASK,
		SC8545_VBUS_OVP_SHIFT, value);
	ret += sc8545_read_byte(di, SC8545_VBUS_OVP_REG, &value);
	if (ret)
		return -EIO;

	hwlog_info("config_vbus_ovp_threshole_mv [%x]=0x%x\n",
		SC8545_VBUS_OVP_REG, value);
	return 0;
}

static int sc8545_config_ibus_ocp_th_ma(void *dev_data, int ocp_th)
{
	u8 value;
	int ret;
	struct sc8545_device_info *di = (struct sc8545_device_info *)dev_data;

	if (!di)
		return -ENODEV;

	if (ocp_th < SC8545_IBUS_OCP_BASE)
		ocp_th = SC8545_IBUS_OCP_BASE;

	if (ocp_th > SC8545_IBUS_OCP_MAX)
		ocp_th = SC8545_IBUS_OCP_MAX;

	value = (u8)((ocp_th - SC8545_IBUS_OCP_BASE) / SC8545_IBUS_OCP_STEP);
	ret = sc8545_write_mask(di, SC8545_IBUS_OCP_UCP_REG,
		SC8545_IBUS_OCP_MASK, SC8545_IBUS_OCP_SHIFT, value);
	ret += sc8545_read_byte(di, SC8545_IBUS_OCP_UCP_REG, &value);
	if (ret)
		return -EIO;

	hwlog_info("config_ibus_ocp_threshold_ma [%x]=0x%x\n",
		SC8545_IBUS_OCP_UCP_REG, value);
	return 0;
}

static int sc8545_config_switching_frequency(int data, void *dev_data)
{
	int freq;
	int freq_shift;
	int ret;
	struct sc8545_device_info *di = (struct sc8545_device_info *)dev_data;

	if (!di)
		return -ENODEV;

	switch (data) {
	case SC8545_SW_FREQ_450KHZ:
		freq = SC8545_FSW_SET_SW_FREQ_500KHZ;
		freq_shift = SC8545_SW_FREQ_SHIFT_M_P10;
		break;
	case SC8545_SW_FREQ_500KHZ:
		freq = SC8545_FSW_SET_SW_FREQ_500KHZ;
		freq_shift = SC8545_SW_FREQ_SHIFT_NORMAL;
		break;
	case SC8545_SW_FREQ_550KHZ:
		freq = SC8545_FSW_SET_SW_FREQ_500KHZ;
		freq_shift = SC8545_SW_FREQ_SHIFT_P_P10;
		break;
	case SC8545_SW_FREQ_675KHZ:
		freq = SC8545_FSW_SET_SW_FREQ_750KHZ;
		freq_shift = SC8545_SW_FREQ_SHIFT_M_P10;
		break;
	case SC8545_SW_FREQ_750KHZ:
		freq = SC8545_FSW_SET_SW_FREQ_750KHZ;
		freq_shift = SC8545_SW_FREQ_SHIFT_NORMAL;
		break;
	case SC8545_SW_FREQ_825KHZ:
		freq = SC8545_FSW_SET_SW_FREQ_750KHZ;
		freq_shift = SC8545_SW_FREQ_SHIFT_P_P10;
		break;
	default:
		freq = SC8545_FSW_SET_SW_FREQ_500KHZ;
		freq_shift = SC8545_SW_FREQ_SHIFT_NORMAL;
		break;
	}

	ret = sc8545_write_mask(di, SC8545_CTRL1_REG, SC8545_SW_FREQ_MASK,
		SC8545_SW_FREQ_SHIFT, freq);
	ret += sc8545_write_mask(di, SC8545_CTRL1_REG, SC8545_FREQ_SHIFT_MASK,
		SC8545_FREQ_SHIFT_SHIFT, freq_shift);
	if (ret)
		return -EIO;

	hwlog_info("config_switching_frequency [%x]=0x%x\n",
		SC8545_CTRL1_REG, freq);
	hwlog_info("config_adjustable_switching_frequency [%x]=0x%x\n",
		SC8545_CTRL1_REG, freq_shift);

	return 0;
}

static int sc8545_congfig_ibat_sns_res(void *dev_data)
{
	int ret;
	u8 value;
	struct sc8545_device_info *di = (struct sc8545_device_info *)dev_data;

	if (!di)
		return -ENODEV;

	if (di->sense_r_config == SENSE_R_5_MOHM)
		value = SC8545_IBAT_SNS_RES_5MOHM;
	else
		value = SC8545_IBAT_SNS_RES_2MOHM;

	ret = sc8545_write_mask(di, SC8545_CTRL2_REG,
		SC8545_SET_IBAT_SNS_RES_MASK,
		SC8545_SET_IBAT_SNS_RES_SHIFT, value);
	if (ret) {
		hwlog_err("congfig_ibat_sns_res fail\n");
		return -EIO;
	}

	hwlog_info("congfig_ibat_sns_res=%d\n", di->sense_r_config);
	return 0;
}

static int sc8545_config_pmic2out_ovp_th_mv(void *dev_data, int ovp_th)
{
	u8 value;
	int ret;
	struct sc8545_device_info *di = (struct sc8545_device_info *)dev_data;

	if (!di)
		return -ENODEV;

	if (ovp_th < SC8545_PMID2OUT_OVP_BASE)
		ovp_th = SC8545_PMID2OUT_OVP_BASE;

	if (ovp_th > SC8545_PMID2OUT_OVP_MAX)
		ovp_th = SC8545_PMID2OUT_OVP_MAX;

	value = (u8)((ovp_th - SC8545_PMID2OUT_OVP_BASE) / SC8545_PMID2OUT_OVP_STEP);
	ret = sc8545_write_mask(di, SC8545_PMID2OUT_OVP_UVP_REG, SC8545_PMID2OUT_OVP_MASK,
			SC8545_PMID2OUT_OVP_SHIFT, value);
	ret += sc8545_read_byte(di, SC8545_PMID2OUT_OVP_UVP_REG, &value);
	if (ret)
		return -EIO;

	hwlog_info("config PMID2OUT_OVP_UVP [%x]=0x%x\n",
			SC8545_PMID2OUT_OVP_UVP_REG, value);
	return 0;
}

static int sc8545_threshold_reg_init(void *dev_data, u8 mode)
{
	int ret;
	int ocp_th;
	struct sc8545_device_info *di = (struct sc8545_device_info *)dev_data;

	if (!di)
		return -ENODEV;

	if (mode == SC8545_CHG_MODE_BYPASS)
		ocp_th = SC8545_LVC_IBUS_OCP_TH_INIT;
	else if (mode == SC8545_CHG_MODE_CHGPUMP)
		ocp_th = SC8545_SC_IBUS_OCP_TH_INIT;
	else
		ocp_th = SC8545_IBUS_OCP_TH_INIT;

	ret = sc8545_config_vac_ovp_th_mv(di, SC8545_VAC_OVP_TH_INIT);
	ret += sc8545_config_vbus_ovp_th_mv(di, SC8545_VBUS_OVP_TH_INIT);
	ret += sc8545_config_ibus_ocp_th_ma(di, ocp_th);
	ret += sc8545_config_vbat_ovp_th_mv(di, SC8545_VBAT_OVP_TH_INIT);
	ret += sc8545_config_ibat_ocp_th_ma(di, SC8545_IBAT_OCP_TH_INIT);
	ret += sc8545_config_ibat_regulation(di, SC8545_IBAT_REGULATION_TH_INIT);
	ret += sc8545_config_vbat_regulation(di, SC8545_VBAT_REGULATION_TH_INIT);
	ret += sc8545_config_pmic2out_ovp_th_mv(di, SC8545_PMID2OUT_OVP_TH_INIT);
	if (ret) {
		hwlog_err("protect threshold init fail\n");
		return -EIO;
	}

	return 0;
}

static int sc8545_lvc_charge_enable(int enable, void *dev_data)
{
	int ret;
	u8 ctrl1_reg = 0;
	u8 ctrl3_reg = 0;
	u8 mode = SC8545_CHG_MODE_BYPASS;
	u8 chg_en = enable ? SC8545_CHG_CTRL_ENABLE : SC8545_CHG_CTRL_DISABLE;
	struct sc8545_device_info *di = (struct sc8545_device_info *)dev_data;

	if (!di)
		return -ENODEV;

	ret = sc8545_threshold_reg_init(di, mode);
	ret += sc8545_write_mask(di, SC8545_CTRL1_REG, SC8545_CHG_CTRL_MASK,
		SC8545_CHG_CTRL_SHIFT, SC8545_CHG_CTRL_DISABLE);
	ret += sc8545_write_mask(di, SC8545_CTRL3_REG, SC8545_CHG_MODE_MASK,
		SC8545_CHG_MODE_SHIFT, mode);
	ret += sc8545_write_mask(di, SC8545_CTRL1_REG, SC8545_CHG_CTRL_MASK,
		SC8545_CHG_CTRL_SHIFT, chg_en);
	ret += sc8545_read_byte(di, SC8545_CTRL1_REG, &ctrl1_reg);
	ret += sc8545_read_byte(di, SC8545_CTRL3_REG, &ctrl3_reg);
	if (ret)
		return -EIO;

	hwlog_info("ic_role=%d,charge_enable[%x]=0x%x,[%x]=0x%x\n",
		di->ic_role, SC8545_CTRL1_REG, ctrl1_reg,
		SC8545_CTRL3_REG, ctrl3_reg);
	return 0;
}

static int sc8545_sc_charge_enable(int enable, void *dev_data)
{
	int ret;
	u8 ctrl1_reg = 0;
	u8 mode = SC8545_CHG_MODE_CHGPUMP;
	u8 chg_en = enable ? SC8545_CHG_CTRL_ENABLE : SC8545_CHG_CTRL_DISABLE;
	struct sc8545_device_info *di = (struct sc8545_device_info *)dev_data;

	if (!di)
		return -ENODEV;

	ret = sc8545_threshold_reg_init(di, mode);
	ret += sc8545_write_mask(di, SC8545_CTRL1_REG, SC8545_CHG_CTRL_MASK,
		SC8545_CHG_CTRL_SHIFT, SC8545_CHG_CTRL_DISABLE);
	ret += sc8545_write_mask(di, SC8545_CTRL3_REG, SC8545_CHG_MODE_MASK,
		SC8545_CHG_MODE_SHIFT, mode);
	ret += sc8545_write_mask(di, SC8545_CTRL1_REG, SC8545_CHG_CTRL_MASK,
		SC8545_CHG_CTRL_SHIFT, chg_en);
	ret += sc8545_read_byte(di, SC8545_CTRL1_REG, &ctrl1_reg);
	if (ret)
		return -EIO;

	hwlog_info("ic_role = %d, charge_enable [%x]=0x%x\n",
		di->ic_role, SC8545_CTRL1_REG, ctrl1_reg);
	return 0;
}

static int sc8545_reg_reset(void *dev_data)
{
	int ret;
	u8 ctrl1_reg = 0;
	struct sc8545_device_info *di = (struct sc8545_device_info *)dev_data;

	if (!di)
		return -ENODEV;

	ret = sc8545_write_mask(di, SC8545_CTRL1_REG,
		SC8545_REG_RST_MASK, SC8545_REG_RST_SHIFT,
		SC8545_REG_RST_ENABLE);
	if (ret)
		return -EIO;

	power_usleep(DT_USLEEP_1MS);
	ret = sc8545_read_byte(di, SC8545_CTRL1_REG, &ctrl1_reg);
	ret += sc8545_config_vac_ovp_th_mv(di, SC8545_VAC_OVP_TH_INIT);
	if (ret)
		return -EIO;

	hwlog_info("reg_reset [%x]=0x%x\n", SC8545_CTRL1_REG, ctrl1_reg);
	return 0;
}

static int sc8545_chip_init(void *dev_data)
{
	return 0;
}

static int sc8545_reg_init(void *dev_data)
{
	int ret;
	u8 mode = SC8545_CHG_MODE_BUCK;
	struct sc8545_device_info *di = (struct sc8545_device_info *)dev_data;

	if (!di)
		return -ENODEV;

	ret = sc8545_config_watchdog_ms(SC8545_WTD_CONFIG_TIMING_5000MS, di);
	ret += sc8545_threshold_reg_init(di, mode);
	ret += sc8545_config_switching_frequency(di->switching_frequency, di);
	ret += sc8545_congfig_ibat_sns_res(di);
	ret += sc8545_write_byte(di, SC8545_INT_MASK_REG,
		SC8545_INT_MASK_REG_INIT);
	ret += sc8545_write_mask(di, SC8545_ADCCTRL_REG,
		SC8545_ADC_EN_MASK, SC8545_ADC_EN_SHIFT, 1);
	ret += sc8545_write_mask(di, SC8545_ADCCTRL_REG,
		SC8545_ADC_RATE_MASK, SC8545_ADC_RATE_SHIFT, 0);
	ret += sc8545_write_mask(di, SC8545_DPDM_CTRL2_REG,
		SC8545_DP_BUFF_EN_MASK, SC8545_DP_BUFF_EN_SHIFT, 1);
	ret += sc8545_write_byte(di, SC8545_SCP_FLAG_MASK_REG,
		SC8545_SCP_FLAG_MASK_REG_INIT);
	ret += sc8545_write_mask(di, SC8545_SCP_CTRL_REG,
		SC8545_DM_3P3_EN_MASK, SC8545_DM_3P3_EN_SHIFT, 0);
	ret += sc8545_write_mask(di, SC8545_IBUS_OCP_UCP_REG,
		SC8545_IBUS_UCP_DEGLITCH_MASK, SC8545_IBUS_UCP_DEGLITCH_SHIFT, 1);
	if (ret) {
		hwlog_err("reg_init fail\n");
		return -EIO;
	}

	return 0;
}

static int sc8545_charge_init(void *dev_data)
{
	struct sc8545_device_info *di = (struct sc8545_device_info *)dev_data;

	if (!di)
		return -ENODEV;

	di->device_id = sc8545_get_device_id((void *)di);
	if (di->device_id == SC8545_DEVICE_ID_GET_FAIL)
		return -EINVAL;

	if (sc8545_reg_init(di))
		return -EINVAL;

	hwlog_info("switchcap sc8545 device id is %d\n", di->device_id);

	di->init_finish_flag = true;
	return 0;
}

static int sc8545_reg_reset_and_init(void *dev_data)
{
	int ret;
	struct sc8545_device_info *di = (struct sc8545_device_info *)dev_data;

	if (!di)
		return -ENODEV;

	ret = sc8545_reg_reset(di);
	ret += sc8545_reg_init(di);
	if (ret)
		return -EINVAL;

	hwlog_info("%s finish, ret = %d\n", __func__, ret);
	return 0;
}

static int sc8545_charge_exit(void *dev_data)
{
	int ret;
	struct sc8545_device_info *di = (struct sc8545_device_info *)dev_data;

	if (!di)
		return -ENODEV;

	ret = sc8545_sc_charge_enable(SC8545_SWITCHCAP_DISABLE, di);
	di->fcp_support = false;
	di->init_finish_flag = false;
	di->int_notify_enable_flag = false;

	return ret;
}

static int sc8545_batinfo_exit(void *dev_data)
{
	return 0;
}

static int sc8545_batinfo_init(void *dev_data)
{
	struct sc8545_device_info *di = (struct sc8545_device_info *)dev_data;

	if (!di)
		return -ENODEV;

	if (sc8545_chip_init(di)) {
		hwlog_err("batinfo init fail\n");
		return -EINVAL;
	}

	return 0;
}

static bool sc8545_scp_check_data(void *dev_data)
{
	int ret;
	int i;
	u8 scp_stat = 0;
	u8 fifo_stat = 0;
	u8 data_num;
	struct sc8545_device_info *di = (struct sc8545_device_info *)dev_data;

	ret = sc8545_read_byte(di, SC8545_SCP_STAT_REG, &scp_stat);
	ret += sc8545_read_byte(di, SC8545_SCP_FIFO_STAT_REG, &fifo_stat);
	if (ret)
		return false;

	data_num = fifo_stat & SC8545_RX_FIFO_CNT_STAT_MASK;
	for (i = 0 ; i < data_num; i++) {
		(void)sc8545_read_byte(di, SC8545_SCP_RX_DATA_REG,
			&di->scp_data[i]);
		hwlog_info("read scp_data=0x%x\n", di->scp_data[i]);
	}
	hwlog_info("scp_stat=0x%x,fifo_stat=0x%x,rx_num=%d\n",
		scp_stat, fifo_stat, data_num);
	hwlog_info("scp_op_num=%d,scp_op=%d\n", di->scp_op_num, di->scp_op);

	/* first scp data should be ack(0x08 or 0x88) */
	if (((di->scp_data[0] & 0x0F) == SC8545_SCP_ACK) &&
		(scp_stat == SC8545_SCP_NO_ERR) &&
		(((di->scp_op == SC8545_SCP_WRITE_OP) &&
		(data_num == SC8545_SCP_ACK_AND_CRC_LEN)) ||
		((di->scp_op == SC8545_SCP_READ_OP) &&
		(data_num == di->scp_op_num + SC8545_SCP_ACK_AND_CRC_LEN))))
		return true;

	return false;
}

static int sc8545_scp_cmd_transfer_check(void *dev_data)
{
	int cnt = 0;

	struct sc8545_device_info *di = (struct sc8545_device_info *)dev_data;

	if (!di)
		return -ENODEV;

	do {
		if (adp_plugout) {
			hwlog_err("transfer check fail, adp plugout\n");
			return -EINVAL;
		}
		(void)power_msleep(DT_MSLEEP_50MS, 0, NULL);
		if (di->scp_trans_done) {
			if (sc8545_scp_check_data(di)) {
				hwlog_info("scp_trans success\n");
				return 0;
			}
			hwlog_info("scp_trans_done, but data err\n");
			return -EINVAL;
		}
		cnt++;
	} while (cnt < SC8545_SCP_ACK_RETRY_TIME);

	hwlog_info("scp adapter trans time out\n");
	return -EINVAL;
}

static int sc8545_scp_adapter_reg_read(u8 *val, u8 reg)
{
	int ret;
	int i;
	struct sc8545_device_info *di = g_sc8545_scp_dev;

	if (!di)
		return -ENODEV;

	mutex_lock(&di->accp_adapter_reg_lock);
	hwlog_info("%s, CMD=0x%x, REG=0x%x\n", __func__,
		SC8545_CHG_SCP_CMD_SBRRD, reg);

	/* clear scp data */
	memset(di->scp_data, 0, sizeof(di->scp_data));

	di->scp_op = SC8545_SCP_READ_OP;
	di->scp_op_num = SC8545_SCP_SBRWR_NUM;
	di->scp_trans_done = false;
	for (i = 0; i < SC8545_SCP_RETRY_TIME; i++) {
		/* clear tx/rx fifo */
		ret = sc8545_write_mask(di, SC8545_SCP_CTRL_REG,
			SC8545_CLR_TX_FIFO_MASK, SC8545_CLR_TX_FIFO_SHIFT, 1);
		ret += sc8545_write_mask(di, SC8545_SCP_CTRL_REG,
			SC8545_CLR_RX_FIFO_MASK, SC8545_CLR_RX_FIFO_SHIFT, 1);
		/* write data */
		ret += sc8545_write_byte(di, SC8545_SCP_TX_DATA_REG,
			SC8545_CHG_SCP_CMD_SBRRD);
		ret += sc8545_write_byte(di, SC8545_SCP_TX_DATA_REG, reg);
		/* start trans */
		ret += sc8545_write_mask(di, SC8545_SCP_CTRL_REG,
			SC8545_SND_START_TRANS_MASK,
			SC8545_SND_START_TRANS_SHIFT, 1);
		if (ret) {
			mutex_unlock(&di->accp_adapter_reg_lock);
			return -EIO;
		}
		/* check cmd transfer success or fail */
		if (sc8545_scp_cmd_transfer_check(di) == 0) {
			memcpy(val, &di->scp_data[1], di->scp_op_num);
			break;
		}
	}
	if (i >= SC8545_SCP_RETRY_TIME) {
		hwlog_err("ack error,retry %d times\n", i);
		ret = -EINVAL;
	}

	mutex_unlock(&di->accp_adapter_reg_lock);
	return ret;
}

static int sc8545_scp_adapter_multi_reg_read(u8 reg, u8 *val, u8 num)
{
	int ret;
	int i;
	struct sc8545_device_info *di = g_sc8545_scp_dev;

	if (!di)
		return -ENODEV;

	mutex_lock(&di->accp_adapter_reg_lock);
	hwlog_info("%s,CMD=0x%x,REG=0x%x,NUM=%d\n",
		__func__, SC8545_CHG_SCP_CMD_MBRRD, reg, num);

	/* clear scp data */
	memset(di->scp_data, 0, sizeof(di->scp_data));

	di->scp_op = SC8545_SCP_READ_OP;
	di->scp_op_num = num;
	di->scp_trans_done = false;
	for (i = 0; i < SC8545_SCP_RETRY_TIME; i++) {
		/* clear tx/rx fifo */
		ret = sc8545_write_mask(di, SC8545_SCP_CTRL_REG,
			SC8545_CLR_TX_FIFO_MASK, SC8545_CLR_TX_FIFO_SHIFT, 1);
		ret += sc8545_write_mask(di, SC8545_SCP_CTRL_REG,
			SC8545_CLR_RX_FIFO_MASK, SC8545_CLR_RX_FIFO_SHIFT, 1);
		/* write cmd, reg, num */
		ret += sc8545_write_byte(di, SC8545_SCP_TX_DATA_REG,
			SC8545_CHG_SCP_CMD_MBRRD);
		ret += sc8545_write_byte(di, SC8545_SCP_TX_DATA_REG, reg);
		ret += sc8545_write_byte(di, SC8545_SCP_TX_DATA_REG, num);
		/* start trans */
		ret += sc8545_write_mask(di, SC8545_SCP_CTRL_REG,
			SC8545_SND_START_TRANS_MASK,
			SC8545_SND_START_TRANS_SHIFT, 1);
		if (ret) {
			mutex_unlock(&di->accp_adapter_reg_lock);
			return -EIO;
		}
		/* check cmd transfer success or fail, ignore ack data */
		if (sc8545_scp_cmd_transfer_check(di) == 0) {
			memcpy(val, &di->scp_data[1], SC8545_MULTI_READ_LEN);
			break;
		}
	}
	if (i >= SC8545_SCP_RETRY_TIME) {
		hwlog_err("ack error,retry %d times\n", i);
		ret = -EINVAL;
	}

	mutex_unlock(&di->accp_adapter_reg_lock);
	return ret;
}

static int sc8545_scp_adapter_reg_read_block(u8 reg, u8 *val, u8 num,
	void *dev_data)
{
	int ret;
	int i, j;
	u8 data[SC8545_MULTI_READ_LEN] = { 0 };
	u8 data_len = (num < SC8545_MULTI_READ_LEN) ? num : SC8545_MULTI_READ_LEN;
	struct sc8545_device_info *di = g_sc8545_scp_dev;

	if (!di || !val)
		return -EINVAL;

	di->scp_error_flag = SC8545_SCP_NO_ERR;

	if ((reg == SCP_PROTOCOL_POWER_CURVE_BASE0) ||
		(reg == SCP_PROTOCOL_POWER_CURVE_BASE1)) {
		data_len = 1;
	}

	for (i = 0; i < num; i += data_len) {
		if (data_len > 1) {
			ret = sc8545_scp_adapter_multi_reg_read(reg + i, data, data_len);
		} else {
			ret = sc8545_scp_adapter_reg_read(data, reg + i);
		}
		if (ret) {
			hwlog_err("scp read failed, reg=0x%x\n", reg + i);
			return -EINVAL;
		}
		for (j = 0; j < data_len; j++) {
			val[i + j] = data[j];
		}
	}

	return 0;
}

static int sc8545_scp_adapter_reg_write(u8 val, u8 reg)
{
	int ret = 0;
	int i;
	struct sc8545_device_info *di = g_sc8545_scp_dev;

	if (!di)
		return -ENODEV;

	mutex_lock(&di->accp_adapter_reg_lock);
	hwlog_info("%s,CMD=0x%x,REG=0x%x,VAL=0x%x\n",
		__func__, SC8545_CHG_SCP_CMD_SBRWR, reg, val);

	/* clear scp data */
	memset(di->scp_data, 0, sizeof(di->scp_data));

	di->scp_op = SC8545_SCP_WRITE_OP;
	di->scp_op_num = SC8545_SCP_SBRWR_NUM;
	di->scp_trans_done = false;
	for (i = 0; i < SC8545_SCP_RETRY_TIME; i++) {
		/* clear tx/rx fifo */
		sc8545_write_mask(di, SC8545_SCP_CTRL_REG,
			SC8545_CLR_TX_FIFO_MASK, SC8545_CLR_TX_FIFO_SHIFT, 1);
		sc8545_write_mask(di, SC8545_SCP_CTRL_REG,
			SC8545_CLR_RX_FIFO_MASK, SC8545_CLR_RX_FIFO_SHIFT, 1);
		/* write data */
		ret += sc8545_write_byte(di, SC8545_SCP_TX_DATA_REG,
			SC8545_CHG_SCP_CMD_SBRWR);
		ret += sc8545_write_byte(di, SC8545_SCP_TX_DATA_REG, reg);
		ret += sc8545_write_byte(di, SC8545_SCP_TX_DATA_REG, val);
		/* start trans */
		ret += sc8545_write_mask(di, SC8545_SCP_CTRL_REG,
			SC8545_SND_START_TRANS_MASK,
			SC8545_SND_START_TRANS_SHIFT, 1);
		if (ret) {
			mutex_unlock(&di->accp_adapter_reg_lock);
			return -EIO;
		}
		/* check cmd transfer success or fail */
		if (sc8545_scp_cmd_transfer_check(di) == 0)
			break;

	}
	if (i >= SC8545_SCP_RETRY_TIME) {
		hwlog_err("ack error,retry %d times\n", i);
		ret = -EINVAL;
	}

	mutex_unlock(&di->accp_adapter_reg_lock);
	return ret;
}

static int sc8545_fcp_master_reset(void *dev_data)
{
	int ret;
	struct sc8545_device_info *di = (struct sc8545_device_info *)dev_data;

	if (!di)
		return -ENODEV;

	ret = sc8545_write_mask(di, SC8545_SCP_CTRL_REG,
		SC8545_SCP_SOFT_RST_MASK, SC8545_SCP_SOFT_RST_SHIFT, 1);
	power_usleep(DT_USLEEP_10MS);

	return ret;
}

static int sc8545_fcp_adapter_reset(void *dev_data)
{
	int ret;
	struct sc8545_device_info *di = (struct sc8545_device_info *)dev_data;

	if (!di)
		return -ENODEV;

	ret = sc8545_write_mask(di, SC8545_SCP_CTRL_REG,
		SC8545_SND_RST_TRANS_MASK, SC8545_SND_RST_TRANS_SHIFT, 1);
	power_usleep(DT_USLEEP_20MS);
	return ret;
}

static int sc8545_fcp_read_switch_status(void *dev_data)
{
	return 0;
}

static int sc8545_is_fcp_charger_type(void *dev_data)
{
	struct sc8545_device_info *di = (struct sc8545_device_info *)dev_data;

	if (!di)
		return 0;

	if (di->dts_fcp_support == 0) {
		hwlog_err("%s:NOT SUPPORT FCP\n", __func__);
		return 0;
	}

	if (di->fcp_support)
		return 1;

	return 0;
}

static void sc8545_fcp_adapter_detect_reset(struct sc8545_device_info *di)
{
	int ret;

	ret = sc8545_fcp_adapter_reset(di);
	ret = sc8545_write_mask(di, SC8545_DPDM_CTRL1_REG,
		SC8545_DPDM_EN_MASK, SC8545_DPDM_EN_SHIFT, FALSE);
	ret += sc8545_write_mask(di, SC8545_DPDM_CTRL2_REG,
		SC8545_DP_BUFF_EN_MASK, SC8545_DP_BUFF_EN_SHIFT, FALSE);
	ret += sc8545_write_mask(di, SC8545_SCP_CTRL_REG, SC8545_SCP_EN_MASK,
		SC8545_SCP_EN_SHIFT, FALSE);
	if (ret)
		hwlog_err("fcp_adapter_reset fail\n");
}

static int sc8545_fcp_adapter_detect_enable(struct sc8545_device_info *di)
{
	int ret;

	ret = sc8545_write_mask(di, SC8545_SCP_CTRL_REG, SC8545_SCP_EN_MASK,
		SC8545_SCP_EN_SHIFT, TRUE);
	ret += sc8545_write_mask(di, SC8545_DPDM_CTRL1_REG, SC8545_DPDM_EN_MASK,
		SC8545_DPDM_EN_SHIFT, TRUE);
	ret += sc8545_write_mask(di, SC8545_DPDM_CTRL2_REG,
		SC8545_DP_BUFF_EN_MASK, SC8545_DP_BUFF_EN_SHIFT, TRUE);
	ret += sc8545_write_byte(di, SC8545_SCP_FLAG_MASK_REG,
		SC8545_SCP_FLAG_MASK_REG_INIT);
	if (ret) {
		hwlog_err("%s fail\n", __func__);
		return -EIO;
	}

	return 0;
}

static int sc8545_fcp_adapter_detct_dpdm_stat(struct sc8545_device_info *di)
{
	int cnt;
	int ret;
	u8 stat = 0;

	for (cnt = 0; cnt < SC8545_CHG_SCP_DETECT_MAX_CNT; cnt++) {
		if (adp_plugout) {
			hwlog_err("dpdm stat detect fail, adp plugout\n");
			return -EINVAL;
		}
		ret = sc8545_read_byte(di, SC8545_DPDM_STAT_REG, &stat);
		hwlog_info("scp_dpdm_stat=0x%x\n", stat);
		if (ret) {
			hwlog_err("read dpdm_stat_reg fail\n");
			continue;
		}

		(void)power_msleep(DT_MSLEEP_100MS, 0, NULL);
		/* 0: DM voltage < 0.325v */
		if ((stat & SC8545_VDM_RD_MASK) >> SC8545_VDM_RD_SHIFT == 0)
			break;
	}
	if (cnt == SC8545_CHG_SCP_DETECT_MAX_CNT) {
		hwlog_err("CHG_SCP_ADAPTER_DETECT_OTHER\n");
		return -EINVAL;
	}

	return 0;
}

static int sc8545_fcp_adapter_detect_ping_stat(struct sc8545_device_info *di)
{
	int cnt;
	int ret;
	u8 scp_stat = 0;

	for (cnt = 0; cnt < SC8545_CHG_SCP_PING_DETECT_MAX_CNT; cnt++) {
		if (adp_plugout) {
			hwlog_err("adapter detect fail, adp plugout\n");
			return -EINVAL;
		}
		/* wait 82ms for every 5-ping */
		if ((cnt % 5) == 0)
			power_msleep(82, 0, NULL);

		ret = sc8545_write_mask(di, SC8545_SCP_CTRL_REG,
			SC8545_SND_START_TRANS_MASK,
			SC8545_SND_START_TRANS_SHIFT, TRUE);
		if (ret)
			return -EIO;

		/* wait 10ms for every ping */
		power_usleep(DT_USLEEP_10MS);

		sc8545_read_byte(di, SC8545_SCP_STAT_REG, &scp_stat);
		hwlog_info("scp ping detect,scp_stat:0x%x\n", scp_stat);
		if ((scp_stat & SC8545_NO_FIRST_SLAVE_PING_STAT_MASK) == 0) {
			hwlog_info("scp adapter detect ok\n");
			di->fcp_support = true;
			break;
		}
	}
	if (cnt == SC8545_CHG_SCP_PING_DETECT_MAX_CNT) {
		hwlog_err("CHG_SCP_ADAPTER_DETECT_OTHER\n");
		return -EINVAL;
	}

	return 0;
}

static bool sc8545_is_vbus_ok(struct sc8545_device_info *di)
{
	int ret;
	u8 irqStat;

	ret = sc8545_read_byte(di, SC8545_INT_STAT_REG, &irqStat);
	if (irqStat & SC8545_ADAPTER_INSERT_STAT_MASK) {
		return true;
	}

	return false;
}

static int sc8545_fcp_adapter_detect(struct sc8545_device_info *di)
{
	int ret;

	if ((adp_plugout == true) && sc8545_is_vbus_ok(di)) {
		adp_plugout = false;
	}

	mutex_lock(&di->scp_detect_lock);
	di->init_finish_flag = true;

	if (di->fcp_support) {
		mutex_unlock(&di->scp_detect_lock);
		return ADAPTER_DETECT_SUCC;
	}

	/* transmit Vdp_src_BC1.2 signal */
	ret = sc8545_fcp_adapter_detect_enable(di);
	if (ret) {
		sc8545_fcp_adapter_detect_reset(di);
		mutex_unlock(&di->scp_detect_lock);
		return ADAPTER_DETECT_OTHER;
	}

	/* Waiting for hvdcp */
	(void)power_msleep(DT_MSLEEP_1S, 0, NULL);

	/* detect dpdm stat */
	ret = sc8545_fcp_adapter_detct_dpdm_stat(di);
	if (ret) {
		sc8545_fcp_adapter_detect_reset(di);
		mutex_unlock(&di->scp_detect_lock);
		return ADAPTER_DETECT_OTHER;
	}

	/* detect ping stat */
	ret = sc8545_fcp_adapter_detect_ping_stat(di);
	if (ret) {
		sc8545_fcp_adapter_detect_reset(di);
		mutex_unlock(&di->scp_detect_lock);
		return ADAPTER_DETECT_OTHER;
	}

	mutex_unlock(&di->scp_detect_lock);
	return ADAPTER_DETECT_SUCC;
}

static int sc8545_fcp_stop_charge_config(void *dev_data)
{
	int ret;
	struct sc8545_device_info *di = (struct sc8545_device_info *)dev_data;

	if (!di)
		return -ENODEV;

	ret = sc8545_fcp_master_reset(di);
	ret += sc8545_write_mask(di, SC8545_SCP_CTRL_REG, SC8545_SCP_EN_MASK,
		SC8545_SCP_EN_SHIFT, 0);
	ret += sc8545_write_mask(di, SC8545_DPDM_CTRL1_REG, SC8545_DPDM_EN_MASK,
		SC8545_DPDM_EN_SHIFT, 0);
	if (ret)
		return -EINVAL;

	di->fcp_support = false;

	hwlog_info("sc8545_fcp_master_reset");
	return ret;
}

static int scp_adapter_reg_read(u8 *val, u8 reg)
{
	int ret;
	struct sc8545_device_info *di = g_sc8545_scp_dev;

	if (!di)
		return -ENODEV;

	if (di->scp_error_flag) {
		hwlog_err("scp timeout happened, do not read reg=0x%x\n", reg);
		return -EINVAL;
	}

	ret = sc8545_scp_adapter_reg_read(val, reg);
	if (ret) {
		hwlog_err("error reg=0x%x\n", reg);
		if (reg != SCP_PROTOCOL_ADP_TYPE0)
			di->scp_error_flag = SC8545_SCP_IS_ERR;

		return -EINVAL;
	}

	return 0;
}

static int scp_adapter_reg_write(u8 val, u8 reg)
{
	int ret;
	struct sc8545_device_info *di = g_sc8545_scp_dev;

	if (!di)
		return -ENODEV;

	if (di->scp_error_flag) {
		hwlog_err("scp timeout happened, do not write reg=0x%x\n", reg);
		return -EINVAL;
	}

	ret = sc8545_scp_adapter_reg_write(val, reg);
	if (ret) {
		hwlog_err("error reg=0x%x\n", reg);
		di->scp_error_flag = SC8545_SCP_IS_ERR;
		return -EINVAL;
	}

	return 0;
}

static int sc8545_self_check(void *dev_data)
{
	return 0;
}

static int sc8545_scp_chip_reset(void *dev_data)
{
	int ret;

	ret = sc8545_fcp_master_reset(dev_data);
	if (ret) {
		hwlog_err("sc8545_fcp_master_reset fail\n");
		return -EINVAL;
	}

	return 0;
}

static int sc8545_scp_reg_read_block(int reg, int *val, int num,
	void *dev_data)
{
	int ret;
	int i;
	u8 data = 0;
	struct sc8545_device_info *di = g_sc8545_scp_dev;

	if (!di || !val)
		return -EINVAL;

	di->scp_error_flag = SC8545_SCP_NO_ERR;

	for (i = 0; i < num; i++) {
		ret = scp_adapter_reg_read(&data, reg + i);
		if (ret) {
			hwlog_err("scp read failed, reg=0x%x\n", reg + i);
			return -EINVAL;
		}
		val[i] = data;
	}

	return 0;
}

static int sc8545_scp_reg_write_block(int reg, const int *val, int num,
	void *dev_data)
{
	int ret;
	int i;
	struct sc8545_device_info *di = g_sc8545_scp_dev;

	if (!di || !val)
		return -EINVAL;

	di->scp_error_flag = SC8545_SCP_NO_ERR;

	for (i = 0; i < num; i++) {
		ret = scp_adapter_reg_write(val[i], reg + i);
		if (ret) {
			hwlog_err("scp write failed, reg=0x%x\n", reg + i);
			return -EINVAL;
		}
	}

	return 0;
}

static int sc8545_scp_detect_adapter(void *dev_data)
{
	struct sc8545_device_info *di = (struct sc8545_device_info *)dev_data;

	if (!di)
		return -ENODEV;

	return sc8545_fcp_adapter_detect(di);
}

int sc8545_fcp_reg_read_block(int reg, int *val, int num, void *dev_data)
{
	int ret, i;
	u8 data = 0;
	struct sc8545_device_info *di = g_sc8545_scp_dev;

	if (!di || !val)
		return -EINVAL;

	di->scp_error_flag = SC8545_SCP_NO_ERR;

	for (i = 0; i < num; i++) {
		ret = scp_adapter_reg_read(&data, reg + i);
		if (ret) {
			hwlog_err("fcp read failed, reg=0x%x\n", reg + i);
			return -EINVAL;
		}
		val[i] = data;
	}
	return 0;
}

static int sc8545_fcp_reg_write_block(int reg, const int *val, int num,
	void *dev_data)
{
	int ret, i;
	struct sc8545_device_info *di = g_sc8545_scp_dev;

	if (!di || !val)
		return -EINVAL;

	di->scp_error_flag = SC8545_SCP_NO_ERR;

	for (i = 0; i < num; i++) {
		ret = scp_adapter_reg_write(val[i], reg + i);
		if (ret) {
			hwlog_err("fcp write failed, reg=0x%x\n", reg + i);
			return -EINVAL;
		}
	}

	return 0;
}

static int sc8545_fcp_detect_adapter(void *dev_data)
{
	struct sc8545_device_info *di = (struct sc8545_device_info *)dev_data;

	if (!di)
		return -ENODEV;

	return sc8545_fcp_adapter_detect(di);
}

static int sc8545_pre_init(void *dev_data)
{
	int ret;
	struct sc8545_device_info *di = (struct sc8545_device_info *)dev_data;

	if (!di)
		return -ENODEV;

	ret = sc8545_self_check(di);
	if (ret) {
		hwlog_err("sc8545_self_check fail\n");
		return ret;
	}

	return ret;
}

static int sc8545_scp_adapter_reset(void *dev_data)
{
	struct sc8545_device_info *di = (struct sc8545_device_info *)dev_data;

	if (!di)
		return -ENODEV;

	return sc8545_fcp_adapter_reset(di);
}

static void sc8545_fault_event_notify(unsigned long event, void *data)
{
	struct atomic_notifier_head *fault_event_notifier_list = NULL;

	sc_get_fault_notifier(&fault_event_notifier_list);
	atomic_notifier_call_chain(fault_event_notifier_list, event, data);
}

static void sc8545_interrupt_handle(struct sc8545_device_info *di,
	struct nty_data *data)
{
	int val = 0;
	u8 flag = data->event1;
	u8 flag1 = data->event2;
	u8 scp_flag = data->event3;

	hwlog_info("%s", __func__);

	if (flag1 & SC8545_VAC_OVP_FLAG_MASK) {
		hwlog_info("AC OVP happened\n");
		sc8545_fault_event_notify(DC_FAULT_AC_OVP, data);
	} else if (flag & SC8545_VBAT_OVP_FLAG_MASK) {
		val = sc8545_get_vbat_mv(di);
		hwlog_info("BAT OVP happened, vbat=%d\n", val);
		if (val >= SC8545_VBAT_OVP_TH_INIT)
			sc8545_fault_event_notify(DC_FAULT_VBAT_OVP, data);
	} else if (flag & SC8545_IBAT_OCP_FLAG_MASK) {
		sc8545_get_ibat_ma(&val, di);
		hwlog_info("BAT OCP happened,ibat=%d\n", val);
		if (val >= SC8545_IBAT_OCP_TH_INIT)
			sc8545_fault_event_notify(DC_FAULT_IBAT_OCP, data);
	} else if (flag & SC8545_VBUS_OVP_FLAG_MASK) {
		sc8545_get_vbus_mv(&val, di);
		hwlog_info("BUS OVP happened,vbus=%d\n", val);
		if (val >= SC8545_VBUS_OVP_TH_INIT)
			sc8545_fault_event_notify(DC_FAULT_VBUS_OVP, data);
	} else if (flag & SC8545_IBUS_OCP_FLAG_MASK) {
		sc8545_get_ibus_ma(&val, di);
		hwlog_info("BUS OCP happened,ibus=%d\n", val);
		if (val >= SC8545_IBUS_OCP_TH_INIT)
			sc8545_fault_event_notify(DC_FAULT_IBUS_OCP, data);
	} else if (flag & SC8545_VOUT_OVP_FLAG_MASK) {
		hwlog_info("VOUT OVP happened\n");
	}

	if (scp_flag & SC8545_TRANS_DONE_FLAG_MASK)
		di->scp_trans_done = true;
}

static void sc8545_interrupt_work(struct work_struct *work)
{
	int ret;
	u8 flag[6] = { 0 };
	struct sc8545_device_info *di = NULL;
	struct nty_data *data = NULL;

	if (!work)
		return;

	di = container_of(work, struct sc8545_device_info, irq_work);
	if (!di || !di->client) {
		hwlog_err("di is null\n");
		return;
	}

	ret = sc8545_read_byte(di, SC8545_INT_FLAG_REG, &flag[0]);
	ret += sc8545_read_byte(di, SC8545_VAC_OVP_REG, &flag[1]);
	ret += sc8545_read_byte(di, SC8545_VDROP_OVP_REG, &flag[2]);
	ret += sc8545_read_byte(di, SC8545_CONVERTER_STATE_REG, &flag[3]);
	ret += sc8545_read_byte(di, SC8545_CTRL3_REG, &flag[4]);
	ret += sc8545_read_byte(di, SC8545_SCP_FLAG_MASK_REG, &flag[5]);
	if (ret)
		hwlog_err("SCP irq_work read fail\n");

	data = &(di->notify_data);
	data->event1 = flag[0];
	data->event2 = flag[1];
	data->event3 = flag[5];
	data->addr = di->client->addr;

	if (di->int_notify_enable_flag) {
		sc8545_interrupt_handle(di, data);
		sc8545_dump_register(di);
	}

	hwlog_info("FLAG0 [0x%x]=0x%x, FLAG1 [0x%x]=0x%x, FLAG2 [0x%x]=0x%x\n",
		SC8545_INT_FLAG_REG, flag[0], SC8545_VAC_OVP_REG, flag[1],
		SC8545_VDROP_OVP_REG, flag[2]);
	hwlog_info("FLAG3 [0x%x]=0x%x, FLAG4 [0x%x]=0x%x, FLAG5 [0x%x]=0x%x\n",
		SC8545_CONVERTER_STATE_REG, flag[3], SC8545_CTRL3_REG, flag[4],
		SC8545_SCP_FLAG_MASK_REG, flag[5]);

	if (flag[0] & SC8545_ADAPTER_INSERT_FLAG_MASK) {
		hwlog_info("adapter plug in\n");
		adp_plugout = false;
	}
	if (flag[0] & SC8545_IBUS_UCP_FALL_FLAG_MASK) {
		hwlog_info("adapter plug out\n");
		adp_plugout = true;
	}

	enable_irq(di->irq_int);
}

static irqreturn_t sc8545_interrupt(int irq, void *_di)
{
	struct sc8545_device_info *di = _di;

	if (!di)
		return IRQ_HANDLED;

	if (di->init_finish_flag)
		di->int_notify_enable_flag = true;

	hwlog_info("sc8545 int happened\n");

	disable_irq_nosync(di->irq_int);
	schedule_work(&di->irq_work);

	return IRQ_HANDLED;
}

static void sc8545_parse_dts(struct device_node *np,
	struct sc8545_device_info *di)
{
	(void)power_dts_read_u32(power_dts_tag(HWLOG_TAG), np,
		"switching_frequency", &di->switching_frequency,
		SC8545_SW_FREQ_550KHZ);
	(void)power_dts_read_u32(power_dts_tag(HWLOG_TAG), np, "scp_support",
		(u32 *)&(di->dts_scp_support), 0);
	(void)power_dts_read_u32(power_dts_tag(HWLOG_TAG), np, "fcp_support",
		(u32 *)&(di->dts_fcp_support), 0);
	(void)power_dts_read_u32(power_dts_tag(HWLOG_TAG), np, "ic_role",
		(u32 *)&(di->ic_role), CHARGE_IC_TYPE_MAIN);
	(void)power_dts_read_u32(power_dts_tag(HWLOG_TAG), np,
		"sense_r_config", &di->sense_r_config, SENSE_R_5_MOHM);
	(void)power_dts_read_u32(power_dts_tag(HWLOG_TAG), np,
		"sense_r_actual", &di->sense_r_actual, SENSE_R_5_MOHM);
}

static void sc8545_lock_mutex_init(struct sc8545_device_info *di)
{
	mutex_init(&di->scp_detect_lock);
	mutex_init(&di->accp_adapter_reg_lock);
}

static void sc8545_lock_mutex_destroy(struct sc8545_device_info *di)
{
	mutex_destroy(&di->scp_detect_lock);
	mutex_destroy(&di->accp_adapter_reg_lock);
}

/* print the register head in charging process */
static int sc8545_register_head(char *buffer, int size, void *dev_data)
{
	struct sc8545_device_info *di = (struct sc8545_device_info *)dev_data;

	if (!di)
		return -ENODEV;

	if (di->ic_role == CHARGE_IC_TYPE_MAIN)
		snprintf(buffer, size,
			"   mode   Ibus   Vbus   Vusb   Ibat   Vbat   Temp");
	else
		snprintf(buffer, size,
			"   mode1  Ibus1  Vbus1  Vusb1  Ibat1  Vbat1  Temp1");

	return 0;
}

/* print the register value in charging process */
static int sc8545_dump_reg(char *buffer, int size, void *dev_data)
{
	int vbus = 0;
	int ibat = 0;
	int temp = 0;
	int ibus = 0;
	int vusb = 0;
	char buff[SC8545_BUF_LEN] = { 0 };
	u8 reg = 0;
	struct sc8545_device_info *di = (struct sc8545_device_info *)dev_data;

	if (!di)
		return -ENODEV;

	(void)sc8545_get_vbus_mv(&vbus, di);
	(void)sc8545_get_ibat_ma(&ibat, di);
	(void)sc8545_get_ibus_ma(&ibus, di);
	(void)sc8545_get_device_temp(&temp, di);
	(void)sc8545_get_vusb_mv(&vusb, di);
	(void)sc8545_read_byte(di, SC8545_CTRL3_REG, &reg);

	if (sc8545_is_device_close(di))
		snprintf(buff, sizeof(buff), "%s", "OFF    ");
	else if (((reg & SC8545_CHG_MODE_MASK) >> SC8545_CHG_MODE_SHIFT) ==
		SC8545_CHG_MODE_BYPASS)
		snprintf(buff, sizeof(buff), "%s", "LVC    ");
	else if (((reg & SC8545_CHG_MODE_MASK) >> SC8545_CHG_MODE_SHIFT) ==
		SC8545_CHG_MODE_CHGPUMP)
		snprintf(buff, sizeof(buff), "%s", "SC     ");

	strncat(buffer, buff, strlen(buff));
	snprintf(buff, sizeof(buff), "%-7d%-7d%-7d%-7d%-7d%-7d",
		ibus, vbus, vusb, ibat, sc8545_get_vbat_mv(di), temp);
	strncat(buffer, buff, strlen(buff));

	return 0;
}

static struct dc_ic_ops g_sc8545_lvc_ops = {
	.dev_name = "sc8545",
	.ic_init = sc8545_charge_init,
	.ic_exit = sc8545_charge_exit,
	.ic_enable = sc8545_lvc_charge_enable,
	.ic_discharge = sc8545_discharge,
	.is_ic_close = sc8545_is_device_close,
	.get_ic_id = sc8545_get_device_id,
	.config_ic_watchdog = sc8545_config_watchdog_ms,
	.ic_reg_reset_and_init = sc8545_reg_reset_and_init,
};

static struct dc_ic_ops g_sc8545_sc_ops = {
	.dev_name = "sc8545",
	.ic_init = sc8545_charge_init,
	.ic_exit = sc8545_charge_exit,
	.ic_enable = sc8545_sc_charge_enable,
	.ic_discharge = sc8545_discharge,
	.is_ic_close = sc8545_is_device_close,
	.get_ic_id = sc8545_get_device_id,
	.config_ic_watchdog = sc8545_config_watchdog_ms,
	.ic_reg_reset_and_init = sc8545_reg_reset_and_init,
};

static struct dc_batinfo_ops g_sc8545_batinfo_ops = {
	.init = sc8545_batinfo_init,
	.exit = sc8545_batinfo_exit,
	.get_bat_btb_voltage = sc8545_get_vbat_mv,
	.get_bat_package_voltage = sc8545_get_vbat_mv,
	.get_vbus_voltage = sc8545_get_vbus_mv,
	.get_bat_current = sc8545_get_ibat_ma,
	.get_ic_ibus = sc8545_get_ibus_ma,
	.get_ic_temp = sc8545_get_device_temp,
};

static struct scp_protocol_ops g_sc8545_scp_protocol_ops = {
	.chip_name = "sc8545",
	.reg_read = sc8545_scp_reg_read_block,
	.reg_write = sc8545_scp_reg_write_block,
	.reg_multi_read = sc8545_scp_adapter_reg_read_block,
	.detect_adapter = sc8545_scp_detect_adapter,
	.soft_reset_master = sc8545_scp_chip_reset,
	.soft_reset_slave = sc8545_scp_adapter_reset,
	.pre_init = sc8545_pre_init,
};

static struct fcp_protocol_ops g_sc8545_fcp_protocol_ops = {
	.chip_name = "sc8545",
	.reg_read = sc8545_fcp_reg_read_block,
	.reg_write = sc8545_fcp_reg_write_block,
	.detect_adapter = sc8545_fcp_detect_adapter,
	.soft_reset_master = sc8545_fcp_master_reset,
	.soft_reset_slave = sc8545_fcp_adapter_reset,
	.get_master_status = sc8545_fcp_read_switch_status,
	.stop_charging_config = sc8545_fcp_stop_charge_config,
	.is_accp_charger_type = sc8545_is_fcp_charger_type,
};

static struct power_log_ops g_sc8545_log_ops = {
	.dev_name = "sc8545",
	.dump_log_head = sc8545_register_head,
	.dump_log_content = sc8545_dump_reg,
};

static struct dc_ic_ops g_sc8545_aux_lvc_ops = {
	.dev_name = "sc8545_aux",
	.ic_init = sc8545_charge_init,
	.ic_exit = sc8545_charge_exit,
	.ic_enable = sc8545_lvc_charge_enable,
	.ic_discharge = sc8545_discharge,
	.is_ic_close = sc8545_is_device_close,
	.get_ic_id = sc8545_get_device_id,
	.config_ic_watchdog = sc8545_config_watchdog_ms,
	.ic_reg_reset_and_init = sc8545_reg_reset_and_init,
};

static struct dc_ic_ops g_sc8545_aux_sc_ops = {
	.dev_name = "sc8545_aux",
	.ic_init = sc8545_charge_init,
	.ic_exit = sc8545_charge_exit,
	.ic_enable = sc8545_sc_charge_enable,
	.ic_discharge = sc8545_discharge,
	.is_ic_close = sc8545_is_device_close,
	.get_ic_id = sc8545_get_device_id,
	.config_ic_watchdog = sc8545_config_watchdog_ms,
	.ic_reg_reset_and_init = sc8545_reg_reset_and_init,
};

static struct dc_batinfo_ops g_sc8545_aux_batinfo_ops = {
	.init = sc8545_batinfo_init,
	.exit = sc8545_batinfo_exit,
	.get_bat_btb_voltage = sc8545_get_vbat_mv,
	.get_bat_package_voltage = sc8545_get_vbat_mv,
	.get_vbus_voltage = sc8545_get_vbus_mv,
	.get_bat_current = sc8545_get_ibat_ma,
	.get_ic_ibus = sc8545_get_ibus_ma,
	.get_ic_temp = sc8545_get_device_temp,
};

static struct power_log_ops g_sc8545_aux_log_ops = {
	.dev_name = "sc8545_aux",
	.dump_log_head = sc8545_register_head,
	.dump_log_content = sc8545_dump_reg,
};

static void sc8545_init_ops_dev_data(struct sc8545_device_info *di)
{
	if (di->ic_role == CHARGE_IC_TYPE_MAIN) {
		g_sc8545_lvc_ops.dev_data = (void *)di;
		g_sc8545_sc_ops.dev_data = (void *)di;
		g_sc8545_batinfo_ops.dev_data = (void *)di;
		g_sc8545_log_ops.dev_data = (void *)di;
	} else {
		g_sc8545_aux_lvc_ops.dev_data = (void *)di;
		g_sc8545_aux_sc_ops.dev_data = (void *)di;
		g_sc8545_aux_batinfo_ops.dev_data = (void *)di;
		g_sc8545_aux_log_ops.dev_data = (void *)di;
	}
	g_sc8545_scp_protocol_ops.dev_data = (void *)di;
	g_sc8545_fcp_protocol_ops.dev_data = (void *)di;
}

static int sc8545_protocol_ops_register(struct sc8545_device_info *di)
{
	int ret;

	if (di->dts_scp_support) {
		ret = scp_protocol_ops_register(&g_sc8545_scp_protocol_ops);
		if (ret)
			return -EINVAL;
	}
	if (di->dts_fcp_support) {
		ret = fcp_protocol_ops_register(&g_sc8545_fcp_protocol_ops);
		if (ret)
			return -EINVAL;
	}

	return 0;
}

static int sc8545_ops_register(struct sc8545_device_info *di)
{
	int ret;

	sc8545_init_ops_dev_data(di);

	if (di->ic_role == CHARGE_IC_TYPE_MAIN) {
		ret = dc_ic_ops_register(LVC_MODE, di->ic_role,
			&g_sc8545_lvc_ops);
		ret += dc_ic_ops_register(SC_MODE, di->ic_role,
			&g_sc8545_sc_ops);
		ret += dc_batinfo_ops_register(SC_MODE, di->ic_role,
			&g_sc8545_batinfo_ops);
		ret += dc_batinfo_ops_register(LVC_MODE, di->ic_role,
			&g_sc8545_batinfo_ops);
		ret += power_log_ops_register(&g_sc8545_log_ops);
		ret += sc8545_protocol_ops_register(di);
	} else {
		ret = dc_ic_ops_register(LVC_MODE, di->ic_role,
			&g_sc8545_aux_lvc_ops);
		ret += dc_ic_ops_register(SC_MODE, di->ic_role,
			&g_sc8545_aux_sc_ops);
		ret += dc_batinfo_ops_register(SC_MODE, di->ic_role,
			&g_sc8545_aux_batinfo_ops);
		ret += dc_batinfo_ops_register(LVC_MODE, di->ic_role,
			&g_sc8545_aux_batinfo_ops);
		ret += power_log_ops_register(&g_sc8545_aux_log_ops);
		ret += sc8545_protocol_ops_register(di);
	}
	if (ret) {
		hwlog_err("ops_register fail\n");
		return ret;
	}

	return 0;
}

static int sc8545_irq_init(struct sc8545_device_info *di,
	struct device_node *np)
{
	int ret;

	ret = power_gpio_config_interrupt(np,
		"intr_gpio", "sc8545_gpio_int", &di->gpio_int, &di->irq_int);
	if (ret)
		return ret;

	ret = request_irq(di->irq_int, sc8545_interrupt,
		IRQF_TRIGGER_FALLING, "sc8545_int_irq", di);
	if (ret) {
		hwlog_err("gpio irq request fail\n");
		di->irq_int = -1;
		gpio_free(di->gpio_int);
		return ret;
	}

	enable_irq_wake(di->irq_int);
	INIT_WORK(&di->irq_work, sc8545_interrupt_work);
	return 0;
}

static int sc8545_probe(struct i2c_client *client,
	const struct i2c_device_id *id)
{
	int ret;
	struct sc8545_device_info *di = NULL;
	struct device_node *np = NULL;

	hwlog_info("probe begin\n");

	if (!client || !client->dev.of_node || !id)
		return -ENODEV;

	di = devm_kzalloc(&client->dev, sizeof(*di), GFP_KERNEL);
	if (!di)
		return -ENOMEM;

	g_sc8545_scp_dev = di;
	di->dev = &client->dev;
	np = di->dev->of_node;
	di->client = client;
	i2c_set_clientdata(client, di);
	di->chip_already_init = 1;

	ret = sc8545_get_device_id(di);
	if (ret == SC8545_DEVICE_ID_GET_FAIL)
		goto sc8545_fail_0;

	sc8545_parse_dts(np, di);
	sc8545_lock_mutex_init(di);

	ret = sc8545_irq_init(di, np);
	if (ret)
		goto sc8545_fail_1;

	ret = sc8545_ops_register(di);
	if (ret)
		goto sc8545_fail_2;

	ret = sc8545_reg_reset(di);
	if (ret)
		goto sc8545_fail_2;

	ret = sc8545_reg_init(di);
	if (ret)
		goto sc8545_fail_2;

	hwlog_info("probe end\n");
	return 0;

sc8545_fail_2:
	free_irq(di->irq_int, di);
	gpio_free(di->gpio_int);
sc8545_fail_1:
	sc8545_lock_mutex_destroy(di);
sc8545_fail_0:
	di->chip_already_init = 0;
	g_sc8545_scp_dev = NULL;
	devm_kfree(&client->dev, di);

	return ret;
}

static int sc8545_remove(struct i2c_client *client)
{
	struct sc8545_device_info *di = i2c_get_clientdata(client);

	if (!di)
		return -ENODEV;

	sc8545_reg_reset(di);

	if (di->irq_int)
		free_irq(di->irq_int, di);
	if (di->gpio_int)
		gpio_free(di->gpio_int);
	sc8545_lock_mutex_destroy(di);

	return 0;
}

static void sc8545_shutdown(struct i2c_client *client)
{
	struct sc8545_device_info *di = i2c_get_clientdata(client);

	if (!di)
		return;

	sc8545_reg_reset(di);
}

MODULE_DEVICE_TABLE(i2c, sc8545);
static const struct of_device_id sc8545_of_match[] = {
	{
		.compatible = "sm5450",
		.data = NULL,
	},
	{},
};

static const struct i2c_device_id sc8545_i2c_id[] = {
	{ "sm5450", 0 }, {}
};

static struct i2c_driver sc8545_driver = {
	.probe = sc8545_probe,
	.remove = sc8545_remove,
	.shutdown = sc8545_shutdown,
	.id_table = sc8545_i2c_id,
	.driver = {
		.owner = THIS_MODULE,
		.name = "sc8545",
		.of_match_table = of_match_ptr(sc8545_of_match),
	},
};

static int __init sc8545_init(void)
{
	return i2c_add_driver(&sc8545_driver);
}

static void __exit sc8545_exit(void)
{
	i2c_del_driver(&sc8545_driver);
}

module_init(sc8545_init);
module_exit(sc8545_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("sc8545 module driver");
MODULE_AUTHOR("Huawei Technologies Co., Ltd.");
