/*
 * cw2217_fuel_gauge.c
 *
 * coul with cw2217 driver
 *
 * Copyright (c) 2021-2021 Honor Technologies Co., Ltd.
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

#include <log/hw_log.h>
#include <chipset_common/hwpower/power_log.h>
#include <chipset_common/hwpower/power_algorithm.h>
#include <chipset_common/hwpower/coul_interface.h>
#include <chipset_common/hwpower/battery_model_public.h>
#include <chipset_common/hwpower/coul_calibration.h>
#include <chipset_common/hwpower/power_gpio.h>
#include <chipset_common/hwpower/power_dts.h>
#include <chipset_common/hwpower/power_thermalzone.h>
#include "cw2217_fuel_gauge.h"

#define HWLOG_TAG cw2217
HWLOG_REGIST();

static unsigned char config_info[SIZE_BATINFO] = {
	0x46, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0xB0, 0xBC, 0xC2, 0xC6, 0xC5, 0xC5, 0x8C, 0x4E,
	0x26, 0xFF, 0xFF, 0xF3, 0xC9, 0x96, 0x7A, 0x5C,
	0x4D, 0x45, 0x38, 0x7A, 0xB4, 0xDB, 0xEF, 0xCA,
	0xCA, 0xCE, 0xD1, 0xD0, 0xCD, 0xCB, 0xC6, 0xCB,
	0xC6, 0xC6, 0xC8, 0xA3, 0x97, 0x8C, 0x85, 0x7B,
	0x73, 0x76, 0x83, 0x8E, 0xA5, 0x8E, 0x52, 0x4D,
	0x00, 0x00, 0x57, 0x10, 0x00, 0x82, 0xC6, 0x00,
	0x00, 0x00, 0x64, 0x10, 0x91, 0xAE, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0A,
};

struct cw_battery *g_cw_bat;

static int cw_read(
	struct i2c_client *client,
	unsigned char reg,
	unsigned char buf[])
{
	int ret;
	/* sleep 1 ms */
	msleep(1);
	ret = i2c_smbus_read_i2c_block_data( client, reg, 1, buf);
	if (ret < 0) {
		hwlog_err("read reg:0x%x i2c error ret:%d\n", reg, ret);
	}
	return ret;
}

static int cw_write(
	struct i2c_client *client,
	unsigned char reg,
	unsigned char const buf[])
{
	int ret;
	/* sleep 1 ms */
	msleep(1);
	ret = i2c_smbus_write_i2c_block_data( client, reg, 1, &buf[0] );
	if (ret < 0) {
		hwlog_err("write reg:0x%x i2c error ret:%d\n", reg, ret);
	}
	return ret;
}

static int cw_read_word(
	struct i2c_client *client,
	unsigned char reg,
	unsigned char buf[])
{
	int ret;
	unsigned char reg_val[2] = {0, 0};
	unsigned int temp_val_buff = 0;
	unsigned int temp_val_second = 0;

	/* sleep 1 ms */
	msleep(1);
	ret = i2c_smbus_read_i2c_block_data( client, reg, 2, reg_val );
	if (ret < 0) {
		hwlog_err("read word reg:0x%x i2c error ret:%d\n", reg, ret);
	}
	/* 8:refer to left shift 8 bit for high-order */
	temp_val_buff = (reg_val[0] << 8) + reg_val[1];
	/* sleep 4 ms */
	msleep(4);
	ret = i2c_smbus_read_i2c_block_data( client, reg, 2, reg_val );
	if (ret < 0) {
		hwlog_err("read word reg:0x%x i2c error second time ret:%d\n",
			reg, ret);
	}
	/* 8:refer to left shift 8 bit for high-order */
	temp_val_second = (reg_val[0] << 8) + reg_val[1];
	if (temp_val_buff != temp_val_second) {
		/* sleep 4 ms */
		msleep(4);
		ret = i2c_smbus_read_i2c_block_data( client, reg, 2, reg_val );
		if (ret < 0) {
			hwlog_err("read word reg:0x%x i2c error third time ret:%d\n",
				reg, ret);
		}
		/* 8:refer to left shift 8 bit for high-order */
		temp_val_buff = (reg_val[0] << 8) + reg_val[1];
	}
	buf[0] = reg_val[0];
	buf[1] = reg_val[1];
	return ret;
}

static int cw2217_enable(struct cw_battery *cw_bat)
{
	int ret;
	unsigned char reg_val = MODE_DEFAULT;

	ret = cw_write(cw_bat->client, REG_MODE_CONFIG, &reg_val);
	if (ret < 0) {
		hwlog_err("enable MODE_DEFAULT fail\n");
		return ret;
	}
	/* sleep 20 ms */
	msleep(20);

	reg_val = MODE_SLEEP;
	ret = cw_write(cw_bat->client, REG_MODE_CONFIG, &reg_val);
	if (ret < 0) {
		hwlog_err("enable MODE_SLEEP fail\n");
		return ret;
	}

	/* sleep 20 ms */
	msleep(20);
	reg_val = MODE_NORMAL;
	ret = cw_write(cw_bat->client, REG_MODE_CONFIG, &reg_val);
	if (ret < 0) {
		hwlog_err("enable MODE_NORMAL fail\n");
		return ret;
	}
	/* sleep 20 ms */
	msleep(20);
	hwlog_info("cw2217_enable!!!\n");

	return 0;
}

static int cw_get_capacity(struct cw_battery *cw_bat)
{
	int ret;
	unsigned char ui_soc;

	ret = cw_read(cw_bat->client, REG_SOC_INT, &ui_soc);
	if (ret < 0) {
		hwlog_err("read REG_SOC_INT fail\n");
		return -1;
	}
	if (ui_soc >= UI_FULL) {
		hwlog_info("UI_SOC=%d larger than 100\n", ui_soc);
		ui_soc = UI_FULL;
	}

	return ui_soc;
}

static int cw_get_chip_id(struct cw_battery *cw_bat)
{
	int ret;
	unsigned char reg_val = 0;
	int chip_id;
	ret = cw_read(cw_bat->client, REG_CHIP_ID, &reg_val);
	if (ret < 0) {
		hwlog_err("read REG_CHIP_ID fail\n");
		return ret;
	}
	chip_id = reg_val;
	hwlog_info("chip_id=%d\n", chip_id);
	return chip_id;
}

static int cw_get_cycle_count(struct cw_battery *cw_bat)
{
	int ret;
	unsigned char reg_val = 0;
	int cycle;
	ret = cw_read(cw_bat->client, REG_CYCLE, &reg_val);
	if (ret < 0) {
		hwlog_err("read REG_CYCLE fail\n");
		return ret;
	}
	cycle = reg_val;
	hwlog_info("cycle=%d\n", cycle);
	return cycle;
}

static int cw_get_SOH(struct cw_battery *cw_bat)
{
	int ret;
	unsigned char reg_val = 0;
	int soh;
	ret = cw_read(cw_bat->client, REG_SOH, &reg_val);
	if (ret < 0) {
		hwlog_err("read REG_SOH fail\n");
		return ret;
	}
	soh = reg_val;
	hwlog_info("soh=%d\n", soh);
	return soh;
}

static int cw_get_capacity_mAH(struct cw_battery *cw_bat)
{
	int soh;
	int soc;
	int capacity_mAH;
	soh = cw_get_SOH(cw_bat);
	soc = cw_get_capacity(cw_bat);
	capacity_mAH = cw_bat->design_capacity * soh * soc / HUNDRED / HUNDRED;

	return capacity_mAH;
}

long get_complement_code(unsigned short raw_code)
{
	long complement_code;
	int dir;

	if (0 != (raw_code & 0x8000)) {
		dir = -1;
		raw_code =  (~raw_code) + 1;
	} else {
		dir = 1;
	}
	complement_code = (long)raw_code * dir;

	return complement_code;
}

static long cw_get_current(struct cw_battery *cw_bat)
{
	int ret;
	unsigned char reg_val[2] = {0 , 0};
	long curr;
	/* unsigned short must u16 */
	unsigned short current_reg;

	ret = cw_read_word(cw_bat->client, REG_CURRENT_H, reg_val);
	if (ret < 0) {
		hwlog_err("read REG_CURRENT_H fail\n");
		return -1;
	}
	/* 8:refer to left shift 8 bit for high-order */
	current_reg = (reg_val[0] << 8) + reg_val[1];
	curr = get_complement_code(current_reg);
	/*
	 * 160:refer to current calculation coefficient
	 * accuracy of rsense is 0.01, so need to * 100 first
	 */
	curr = curr  * 160 * 100 / cw_bat->rsense / 100;

	return (curr);
}

static int cw_get_voltage(struct cw_battery *cw_bat)
{
	int ret;
	unsigned char reg_val[2] = {0 , 0};
	unsigned int voltage;

	ret = cw_read_word(cw_bat->client, REG_VCELL_H, reg_val);
	if (ret < 0) {
		hwlog_err("read REG_VCELL_H fail\n");
		return -1;
	}
	/* 8:refer to left shift 8 bit for high-order */
	voltage = (reg_val[0] << 8) + reg_val[1];

	/* vol = reg_val * 312.5, 5 * THOUSAND / 16 = 312.5*/
	voltage = voltage  * 5 * THOUSAND / 16;

	return (int)voltage;
}

static int cw_set_ntc_compensation_temp(struct cw_battery *cw_bat, int temp_val, int cur_temp)
{
	int temp_with_compensation = temp_val;
	struct common_comp_data comp_data;

	if (!cw_bat)
		return temp_with_compensation;

	comp_data.refer = abs(cur_temp);
	comp_data.para_size = NTC_PARA_LEVEL;
	comp_data.para = cw_bat->ntc_temp_compensation_para;
	if (cw_bat->ntc_compensation_is == 1)
		temp_with_compensation = power_get_compensation_value(temp_val,
			&comp_data);

	hwlog_debug("temp_with_compensation=%d temp_no_compensation=%d ichg=%d\n",
		temp_with_compensation, temp_val, cur_temp);
	return temp_with_compensation;
}


static int cw_get_temp(struct cw_battery *cw_bat, bool flag)
{
	int ret;
	unsigned char reg_val = 0;
	int temp;
	int bat_curr;

	bat_curr = cw_get_current(cw_bat);
	ret = cw_read(cw_bat->client, REG_TEMP, &reg_val);
	if (ret < 0) {
		hwlog_err("read REG_TEMP fail\n");
		return -1;
	}
	/* reg_val * 10 / 2 - 400 is cw2217 temperature computational formula */
	temp = (int)reg_val * 10 / 2 - 400;
	if (flag)
		return cw_set_ntc_compensation_temp(cw_bat, temp, bat_curr);
	return temp;
}

static void cw_update_data(struct cw_battery *cw_bat)
{
	cw_bat->voltage = cw_get_voltage(cw_bat);
	cw_bat->curr = cw_get_current(cw_bat);
	cw_bat->capacity = cw_get_capacity(cw_bat);
	cw_bat->temp = cw_get_temp(cw_bat, true);
	hwlog_info("voltage=%d current=%d capcity=%d temp=%d\n",
		cw_bat->voltage, cw_bat->curr, cw_bat->capacity, cw_bat->temp);
}

static int cw_init_data(struct cw_battery *cw_bat)
{
	cw_bat->chip_id = cw_get_chip_id(cw_bat);
	cw_bat->voltage = cw_get_voltage(cw_bat);
	cw_bat->curr = cw_get_current(cw_bat);
	cw_bat->capacity = cw_get_capacity(cw_bat);
	cw_bat->temp = cw_get_temp(cw_bat, true);
	hwlog_info("chip_id=%d voltage=%d current=%d capacity=%d temp=%d\n",
		cw_bat->chip_id, cw_bat->voltage, cw_bat->curr,
		cw_bat->capacity, cw_bat->temp);
	return 0;
}

/* CW2217 update profile function, Often called during initialization */
static int cw_update_config_info(struct cw_battery *cw_bat)
{
	int ret;
	unsigned char i;
	unsigned char reg_val = 0;
	unsigned char reg_val_dig = 0;
	unsigned char version = cw_bat->fg_para_version;
	int count = 0;

	ret = cw_write(cw_bat->client, REG_VERSION, &version);
	if (ret < 0) {
		hwlog_info("cw2217 write version fail");
	}

	/* update new battery info */
	for (i = 0; i < SIZE_BATINFO; i++) {
		reg_val = config_info[i];
		ret = cw_write(cw_bat->client, REG_BATINFO + i, &reg_val);
		if (ret < 0)
			return ret;

		hwlog_info("update config write reg[%02X]=%02X\n",
			REG_BATINFO +i, reg_val);
	}

	/* set UPDATE_FLAG */
	ret = cw_read(cw_bat->client, REG_SOC_ALERT, &reg_val);
	if (ret < 0) {
		hwlog_err("read REG_SOC_ALERT fail\n");
		return ret;
	}
	reg_val |= CONFIG_UPDATE_FLG;
	ret = cw_write(cw_bat->client, REG_SOC_ALERT, &reg_val);
	if (ret < 0) {
		hwlog_err("write REG_SOC_ALERT fail\n");
		return ret;
	}
	/* close all interruptes */
	reg_val = 0;
	ret = cw_write(cw_bat->client, REG_GPIO_CONFIG, &reg_val);
	if (ret < 0) {
		hwlog_err("write REG_GPIO_CONFIG fail\n");
		return ret;
	}
	ret = cw2217_enable(cw_bat);
	if (ret < 0)
		return ret;

	while(1) {
		msleep(HUNDRED);
		if ((cw_get_voltage(cw_bat) != 0) &&
			(cw_get_capacity(cw_bat) <= 100)) {
			break;
		}
		count++;
		if (count >= READ_BAT_INFO_RETRY_TIME)
			break;
	}
	if (count >= READ_BAT_INFO_RETRY_TIME) {
		hwlog_err("count is bigger than 25\n");
		return -1;
	}

	for (i = 0; i < READ_COUL_INT_RETRY_TIME; i++) {
		msleep(HUNDRED);
		ret = cw_read(cw_bat->client, REG_SOC_INT, &reg_val);
		ret = cw_read(cw_bat->client, REG_SOC_INT + 1, &reg_val_dig);
		hwlog_info("i=%d soc=%d .soc=%d\n", i, reg_val, reg_val_dig);
		if (ret < 0) {
			hwlog_err("read REG_SOC_INT fail\n");
			return ret;
		}
		else if (reg_val <= HUNDRED)
			break;
		}
	if (i >= READ_COUL_INT_RETRY_TIME)
		return -1;

	return 0;
}

static int cw_init(struct cw_battery *cw_bat)
{
	int ret;
	unsigned char last_version = 0;
	unsigned char reg_val = MODE_NORMAL;
	unsigned char config_flg = 0;

	ret = cw_read(cw_bat->client, REG_MODE_CONFIG, &reg_val);
	if (ret < 0) {
		hwlog_err("read REG_MODE_CONFIG fail\n");
		return ret;
	}

	ret = cw_read(cw_bat->client, REG_SOC_ALERT, &config_flg);
	if (ret < 0) {
		hwlog_err("read REG_SOC_ALERT fail\n");
		return ret;
	}

	ret = cw_read(cw_bat->client, REG_VERSION, &last_version);
	if(ret < 0) {
		hwlog_err("read REG_VERSION fail.\n");
		return ret;
	}
	hwlog_info("cw2217 last_version = %d.\n",last_version);

	if ((reg_val != MODE_NORMAL) || ((config_flg & CONFIG_UPDATE_FLG) == 0x00) || (cw_bat->fg_para_version != last_version)) {
		ret = cw_update_config_info(cw_bat);
		if (ret < 0)
			return ret;
	} else {
		ret = cw_read(cw_bat->client, REG_USER, &reg_val);
		if (ret < 0) {
			hwlog_err("read REG_USER fail\n");
			return ret;
		}
	}
	hwlog_info("cw2217 init success\n");
	return 0;
}

static void cw_bat_work(struct work_struct *work)
{
	struct cw_battery *cw_bat = g_cw_bat;

	cw_update_data(cw_bat);
	queue_delayed_work(cw_bat->cwfg_workqueue, &cw_bat->battery_delay_work,
		msecs_to_jiffies(QUEUE_DELAYED_WORK_TIME));
}

static int cw2217_is_ready(void *dev_data)
{
	if (g_cw_bat)
		return 1;

	return 0;
}

#ifdef CONFIG_HLTHERM_RUNTEST
static int cw2217_is_battery_exist(void *dev_data)
{
	return 0;
}
#else
static int cw2217_is_battery_exist(void *dev_data)
{
	struct cw_battery *cw_bat = (struct cw_battery *)dev_data;

	if (!cw_bat)
		return 0;

	if ((cw_bat->temp <= CW2217_TEMP_ABR_LOW) ||
		(cw_bat->temp >= CW2217_TEMP_ABR_HIGH))
		return 0;

	return 1;
}
#endif /* CONFIG_HLTHERM_RUNTEST */

static int cw2217_read_battery_soc(void *dev_data)
{
	struct cw_battery *cw_bat = (struct cw_battery *)dev_data;

	if (!cw_bat)
		return 0;

	return cw_bat->capacity;
}

static int cw2217_read_battery_vol(void *dev_data)
{
	struct cw_battery *cw_bat = (struct cw_battery *)dev_data;

	if (!cw_bat)
		return 0;

	return cw_bat->voltage / THOUSAND;
}

static int cw2217_read_battery_current(void *dev_data)
{
	struct cw_battery *cw_bat = (struct cw_battery *)dev_data;
	return (int)cw_get_current(cw_bat);
}

static int cw2217_read_battery_avg_current(void *dev_data)
{
	struct cw_battery *cw_bat = (struct cw_battery *)dev_data;
	return (int)cw_get_current(cw_bat);
}

static int cw2217_read_battery_fcc(void *dev_data)
{
	struct cw_battery *cw_bat = (struct cw_battery *)dev_data;
	int soh = 0;
	soh = cw_get_SOH(cw_bat);
	return cw_bat->design_capacity * soh / HUNDRED;
}

static int cw2217_read_battery_cycle(void *dev_data)
{
	struct cw_battery *cw_bat = (struct cw_battery *)dev_data;
	return cw_get_cycle_count(cw_bat);
}

static int cw2217_read_battery_rm(void *dev_data)
{
	struct cw_battery *cw_bat = (struct cw_battery *)dev_data;
	return cw_get_capacity_mAH(cw_bat);
}

static int cw2217_read_battery_temperature(void *dev_data)
{
	struct cw_battery *cw_bat = (struct cw_battery *)dev_data;
	return cw_get_temp(cw_bat, true);
}

static int cw2217_set_battery_low_voltage(int val, void *dev_data)
{
	return 0;
}

static int cw2217_set_last_capacity(int capacity, void *dev_data)
{
	struct cw_battery *cw_bat = (struct cw_battery *)dev_data;
	int ret;
	unsigned char reg_val = (unsigned char)capacity;

	ret = cw_write(cw_bat->client, REG_USER, &reg_val);
	if (ret < 0) {
		hwlog_err("cw2217_set_last_capacity fail\n");
	}
	return 0;
}

static int cw2217_get_last_capacity(void *dev_data)
{
	struct cw_battery *cw_bat = (struct cw_battery *)dev_data;
	int ret;
	unsigned char reg_val = 0;
	int cap ;
	int last_cap;

	cap = cw2217_read_battery_soc(dev_data);
	ret = cw_read(cw_bat->client, REG_USER, &reg_val);
	last_cap = (int)reg_val;
	if (ret < 0) {
		hwlog_err("cw2217_get_last_capacity fail\n");
	}
	hwlog_info("%s last capacity is %d%, cap is %d%", last_cap, cap);

	if ((last_cap <= 0) || (cap <= 0) ||
		(abs(last_cap - cap) >= CW2217_CAPACITY_TH))
		return cap;

	return last_cap;
}

static int cw2217_get_log_head(char *buffer, int size, void *dev_data)
{
	struct cw_battery *cw_bat = (struct cw_battery *)dev_data;

	if (!buffer || !cw_bat)
		return -1;

	snprintf(buffer, size,
		"    Temp    Vbat    Ibat    AIbat    Rm    Soc   ");

	return 0;
}

static int cw2217_dump_log_data(char *buffer, int size, void *dev_data)
{
	struct cw_battery *di = (struct cw_battery *)dev_data;
	struct cw2217_fg_display_data g_dis_data;

	if (!buffer || !di)
		return -1;

	g_dis_data.vbat = cw_get_voltage(di) / 1000; /* mv */
	g_dis_data.ibat = cw_get_current(di);
	g_dis_data.avg_ibat = cw_get_current(di);
	g_dis_data.rm = cw_get_capacity_mAH(di);
	g_dis_data.temp = cw_get_temp(di, false);
	g_dis_data.soc = cw_get_capacity(di);

	snprintf(buffer, size, "%-7d%-7d%-7d%-7d%-7d%-7d   ",
		g_dis_data.temp, g_dis_data.vbat, g_dis_data.ibat,
		g_dis_data.avg_ibat, g_dis_data.rm, g_dis_data.soc);

	return 0;
}

static struct power_log_ops cw2217_fg_ops = {
	.dev_name = "cw2217",
	.dump_log_head = cw2217_get_log_head,
	.dump_log_content = cw2217_dump_log_data,
};

static struct coul_interface_ops cw2217_ops = {
	.type_name = "main",
	.is_coul_ready = cw2217_is_ready,
	.is_battery_exist = cw2217_is_battery_exist,
	.get_battery_capacity = cw2217_read_battery_soc,
	.get_battery_voltage = cw2217_read_battery_vol,
	.get_battery_current = cw2217_read_battery_current,
	.get_battery_avg_current = cw2217_read_battery_avg_current,
	.get_battery_temperature = cw2217_read_battery_temperature,
	.get_battery_fcc = cw2217_read_battery_fcc,
	.get_battery_cycle = cw2217_read_battery_cycle,
	.set_battery_low_voltage = cw2217_set_battery_low_voltage,
	.set_battery_last_capacity = cw2217_set_last_capacity,
	.get_battery_last_capacity = cw2217_get_last_capacity,
	.get_battery_rm = cw2217_read_battery_rm,
};

static int cw2217_get_calibration_curr(int *val, void *data)
{
	*val = cw_get_current(g_cw_bat);
	return 0;
}

static int cw2217_get_calibration_vol(int *val, void *dev_data)
{
	*val = (int)cw_get_voltage(g_cw_bat);
	return 0;
}

static int cw2217_set_current_gain(unsigned int val, void *dev_data)
{
	return 0;
}

static int cw2217_set_voltage_gain(unsigned int val, void *dev_data)
{
	return 0;
}

static int cw2217_enable_cali_mode(int val, void *dev_data)
{
	return 0;
}

static struct coul_cali_ops cw2217_cali_ops = {
	.dev_name = "aux",
	.get_current = cw2217_get_calibration_curr,
	.get_voltage = cw2217_get_calibration_vol,
	.set_current_gain= cw2217_set_current_gain,
	.set_voltage_gain = cw2217_set_voltage_gain,
	.set_cali_mode = cw2217_enable_cali_mode,
};

static int cw2217_parse_fg_para(struct device_node *np, struct cw_battery *cw_bat)
{
	int ret;
	bool choose_default_flag = false;
	struct device_node *dev_node = NULL;
	const char *batt_default_name = NULL;
	const char *batt_name = bat_model_name();

	if (power_dts_read_string(power_dts_tag("FG_CW2217"), np,
		"batt_default_name", &batt_default_name))
		return -EINVAL;

	if (!batt_name || batt_name[0] == 0) {
		batt_name = batt_default_name;
		choose_default_flag = true;
	}

	dev_node = of_find_node_by_name(np, batt_name);
	if (!dev_node && !choose_default_flag)
		dev_node = of_find_node_by_name(np, batt_default_name);
	if (!dev_node) {
		hwlog_err("batt para read failed\n");
		return -EINVAL;
	}

	(void)power_dts_read_u32(power_dts_tag("FG_CW2217"), dev_node,
		"fg_para_version", (u32 *)&cw_bat->fg_para_version, 0xFF);
	hwlog_info("fg_para_version = 0x%x\n", cw_bat->fg_para_version);

	ret = power_dts_read_u8_array(power_dts_tag("FG_CW2217"), dev_node,
		"cw_image_data", config_info, SIZE_BATINFO);
	if (ret) {
		hwlog_err("cw_image_data read failed\n");
		return -EINVAL;
	}

	return 0;
}

static void cw2217_parse_batt_ntc(struct device_node *np, struct cw_battery *cw_bat)
{
	int array_len;
	int i;
	long idata = 0;
	const char *string = NULL;
	int ret;

	if (!np)
		return;
	if (of_property_read_u32(np, "ntc_compensation_is",
		&(cw_bat->ntc_compensation_is))) {
		hwlog_info("get ntc_compensation_is failed\n");
		return;
	}
	array_len = of_property_count_strings(np, "ntc_temp_compensation_para");
	if ((array_len <= 0) || (array_len % NTC_PARA_TOTAL != 0)) {
		hwlog_err("ntc is invaild,please check ntc_temp_para number\n");
		return;
	}
	if (array_len > NTC_PARA_LEVEL * NTC_PARA_TOTAL) {
		array_len = NTC_PARA_LEVEL * NTC_PARA_TOTAL;
		hwlog_err("temp is too long use only front %d paras\n", array_len);
		return;
	}

	for (i = 0; i < array_len; i++) {
		ret = of_property_read_string_index(np,
			"ntc_temp_compensation_para", i, &string);
		if (ret) {
			hwlog_err("get ntc_temp_compensation_para failed\n");
			return;
		}
		/* 10 means decimalism */
		ret = kstrtol(string, 10, &idata);
		if (ret)
			break;

		switch (i % NTC_PARA_TOTAL) {
		case NTC_PARA_ICHG:
			cw_bat->ntc_temp_compensation_para[i / NTC_PARA_TOTAL]
				.refer = idata;
			break;
		case NTC_PARA_VALUE:
			cw_bat->ntc_temp_compensation_para[i / NTC_PARA_TOTAL]
				.comp_value = idata;
			break;
		default:
			hwlog_err("ntc_temp_compensation_para get failed\n");
		}
		hwlog_info("ntc_temp_compensation_para[%d][%d] = %ld\n",
			i / (NTC_PARA_TOTAL), i % (NTC_PARA_TOTAL), idata);
	}
}

static int cw2217_dts_parse(struct device_node *np, struct cw_battery *cw_bat)
{
	int ret;
	ret = of_property_read_u32(np, "design_capacity", &cw_bat->design_capacity);
	if (ret < 0) {
		hwlog_err("get design capacity fail!\n");
		cw_bat->design_capacity = DESIGN_CAPACITY;
	}
	ret = of_property_read_u32(np, "rsense", &cw_bat->rsense);
	if (ret < 0 || cw_bat->rsense == 0) {
		hwlog_err("get rsense fail!\n");
		cw_bat->rsense = 100; /* default is 1 Milliohm, set 100 to support accuracy 0.01 */
	}
	cw2217_parse_batt_ntc(np, cw_bat);
	ret = cw2217_parse_fg_para(np, cw_bat);
	if (ret) {
		hwlog_err("parse_dts: parse fg_para failed\n");
		return ret;
	}
	return 0;
}

static int cw2217_get_raw_data (int adc_channel, long *data, void *dev_data)
{
	struct cw_battery *cw_bat = NULL;
	(void)adc_channel;

	if (!data || !dev_data)
		return -1;

	cw_bat = (struct cw_battery *) dev_data;
	/* divide 10 means covert 0.1 degree to degree */
	*data = cw_get_temp(cw_bat, false) / 10;
	return 0;
}

static struct power_tz_ops cw2217_battery_temp_tz_ops = {
	.get_raw_data = cw2217_get_raw_data,
};
static int cw2217_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	int ret = 0;
	int loop = 0;
	struct cw_battery *cw_bat = NULL;
	struct device_node *np = NULL;

	cw_bat = devm_kzalloc(&client->dev, sizeof(*cw_bat), GFP_KERNEL);
	if (!cw_bat) {
		hwlog_err("cw_bat create fail\n");
			return -ENOMEM;
	}

	i2c_set_clientdata(client, cw_bat);
	cw_bat->client = client;
	cw_bat->dev = &client->dev;
	np = cw_bat->dev->of_node;
	if (!np){
		hwlog_err("dev of node is NULL\n");
		goto cw2217_probe_fial;
	}

	ret = cw2217_dts_parse(np, cw_bat);
	if (ret){
		hwlog_err("dts parse fail\n");
		goto cw2217_probe_fial;
	}

	ret = cw_init(cw_bat);
	while ((loop++ < CW_INIT_RETRY_TIME) && (ret != 0)) {
		/* sleep 200 ms */
		msleep(200);
		ret = cw_init(cw_bat);
	}

	if (ret) {
		hwlog_err("cw2217 init fail\n");
		goto cw2217_probe_fial;
	}
	ret = cw_init_data(cw_bat);
	if (ret) {
		hwlog_err("cw2217 init data fail\n");
		goto cw2217_probe_fial;
	}

	g_cw_bat = cw_bat;
	cw2217_ops.dev_data = (void*)cw_bat;
	coul_interface_ops_register(&cw2217_ops);
	cw2217_fg_ops.dev_data = (void*)cw_bat;
	power_log_ops_register(&cw2217_fg_ops);
	cw2217_cali_ops.dev_data = (void*)cw_bat;
	coul_cali_ops_register(&cw2217_cali_ops);

	cw2217_battery_temp_tz_ops.dev_data = (void *)cw_bat;
	if (power_tz_ops_register(&cw2217_battery_temp_tz_ops, "cw2217"))
		hwlog_err("power_tz_ops_register fail");
	cw_bat->cwfg_workqueue = create_singlethread_workqueue("cwfg_gauge");
	INIT_DELAYED_WORK(&cw_bat->battery_delay_work, cw_bat_work);
	/* start work after 50 ms */
	queue_delayed_work(cw_bat->cwfg_workqueue,
		&cw_bat->battery_delay_work, msecs_to_jiffies(50));

	hwlog_info("cw2217 driver probe success\n");
	return 0;
cw2217_probe_fial:
	devm_kfree(&client->dev, cw_bat);
	return ret;

}

static int cw2217_remove(struct i2c_client *client)
{
	cancel_delayed_work(&g_cw_bat->battery_delay_work);
	destroy_workqueue(g_cw_bat->cwfg_workqueue);
	return 0;
}

#ifdef CONFIG_PM
static int cw_bat_suspend(struct device *dev)
{
	if (!g_cw_bat) {
		return -1;
	}
	cancel_delayed_work(&g_cw_bat->battery_delay_work);
	return 0;
}

#ifdef CONFIG_I2C_OPERATION_IN_COMPLETE
static void cw_bat_complete(struct device *dev)
{
	if (!g_cw_bat)
		return;

	hwlog_info("bat complete enter\n");
	queue_delayed_work(g_cw_bat->cwfg_workqueue,
		&g_cw_bat->battery_delay_work, msecs_to_jiffies(2));
}

static int cw_bat_resume(struct device *dev)
{
	return 0;
}
#else
static int cw_bat_resume(struct device *dev)
{
	if (!g_cw_bat) {
		return -1;
	}

	queue_delayed_work(g_cw_bat->cwfg_workqueue,
		&g_cw_bat->battery_delay_work, msecs_to_jiffies(2));
	return 0;
}
#endif

static const struct dev_pm_ops cw_bat_pm_ops = {
		.suspend  = cw_bat_suspend,
		.resume   = cw_bat_resume,
#ifdef CONFIG_I2C_OPERATION_IN_COMPLETE
		.complete = cw_bat_complete,
#endif
};
#endif

static const struct i2c_device_id cw2217_id_table[] = {
	{CWFG_NAME, 0},
	{}
};

static struct of_device_id cw2217_match_table[] = {
	{
		.compatible = "cellwise,cw2217",
	},
	{},
};

static struct i2c_driver cw2217_driver = {
	.driver = {
		.name = CWFG_NAME,
#ifdef CONFIG_PM
		.pm = &cw_bat_pm_ops,
#endif
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(cw2217_match_table),
	},
	.probe = cw2217_probe,
	.remove = cw2217_remove,
	.id_table = cw2217_id_table,
};

static int __init cw2217_init(void)
{
	i2c_add_driver(&cw2217_driver);
	return 0;
}

static void __exit cw2217_exit(void)
{
	i2c_del_driver(&cw2217_driver);
}

module_init(cw2217_init);
module_exit(cw2217_exit);

MODULE_AUTHOR("Cellwise FAE");
MODULE_DESCRIPTION("CW2217 FGADC Device Driver V1.2");
MODULE_LICENSE("GPL");
