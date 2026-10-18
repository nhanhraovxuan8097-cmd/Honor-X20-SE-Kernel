/*
 * rt9426a_battary.c
 *
 * driver for rt9426a battery fuel gauge
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

#include "rt9426a_battery.h"

#define HWLOG_TAG rt9426a
HWLOG_REGIST();

#define PRECISION_ENHANCE	5

/* Global Variable for RT9426A */
u16 g_PAGE_CHKSUM[14] = {0};

struct rt9426a_chip {
	struct i2c_client *i2c;
	struct device *dev;
	struct rt9426a_platform_data *pdata;
	struct power_supply *fg_psy;
	struct regmap *regmap;
	struct mutex var_lock;
	struct mutex update_lock;
	struct delayed_work update_work;
	int alert_irq;
	int capacity;
	int soc_offset;
	u8 online:1;
	int btemp;
	int bvolt;
	int bcurr;
	u16 ic_ver;
	int design_capacity;
	u16 ocv_checksum_ic;
	u16 ocv_checksum_dtsi;
	bool calib_unlock;
	/* for update ieoc setting by dtsi */
	int icc_sts;
	/* for update ieoc setting by api */
	int ocv_index;
#ifdef CONFIG_HUAWEI_POWER_EMBEDDED_ISOLATION
	int c_gain;
	int v_gain;
	int c_offset;
	bool low_v_smooth_en;
#endif /* CONFIG_HUAWEI_POWER_EMBEDDED_ISOLATION */

};

#ifdef CONFIG_HUAWEI_POWER_EMBEDDED_ISOLATION
static struct rt9426a_chip *g_rt9426a_chip;
static int rt9426a_get_display_data(struct rt9426a_chip *di,
	int index);
static void rt9426a_parse_batt_ntc(struct device_node *np,
	struct rt9426a_platform_data *pdata);

#endif /* CONFIG_HUAWEI_POWER_EMBEDDED_ISOLATION */

static const struct rt9426a_platform_data def_platform_data = {
	.dtsi_version = { 0, 0 },
	.para_version = 0,
	.soc_offset_size = { 2, 1 },
	.offset_interpolation_order = { 2, 2 },
	.battery_type = 4352,
	.temp_source = 0,
	.volt_source = 0,
	.curr_source = 0,
	/* unit:0.01mR 1000x0.01 = 10mR(default) */
	.rs_ic_setting = 1000,
	/* unit:0.01mR 1000x0.01 = 10mR(default) */
	.rs_schematic = 1000,
	/* add for aging cv */
	.fcc = { 2000, 2000, 2000, 2000, 2000 },
	.fc_vth = { 0x78, 0x78, 0x78, 0x78, 0x78 },
	/* add for smooth_soc default: disable */
	.smooth_soc_en = 0,
	/* for update ieoc setting by dtsi unit: mA */
	.icc_threshold = { 0, 0, 0 },
	.ieoc_setting = { 0, 0, 0, 0 },
};

static int rt9426a_block_read(struct i2c_client *i2c, u8 reg, int len,
	void *dst)
{
	struct rt9426a_chip *chip = i2c_get_clientdata(i2c);
	int ret;

	ret = regmap_raw_read(chip->regmap, reg, dst, len);
	if (ret < 0)
		hwlog_err("RT9426A block read 0x%02x fail\n", reg);
	return ret;
}

static int rt9426a_block_write(struct i2c_client *i2c,
	u8 reg, int len, const void *src)
{
	struct rt9426a_chip *chip = i2c_get_clientdata(i2c);
	int ret;

	ret = regmap_raw_write(chip->regmap, reg, src, len);
	if (ret < 0)
		hwlog_err("RT9426A block write 0x%02x fail\n", reg);
	return ret;
}


static int rt9426a_reg_read_word(struct i2c_client *i2c, u8 reg)
{
	u16 data = 0;
	int ret;

	ret = rt9426a_block_read(i2c, reg, 2, &data);
	return (ret < 0) ? ret : (s32)le16_to_cpu(data);
}

static int rt9426a_reg_write_word(struct i2c_client *i2c, u8 reg,
	u16 data)
{
	data = cpu_to_le16(data);
	return rt9426a_block_write(i2c, reg, 2, (uint8_t *)&data);
}

static int __maybe_unused rt9426a_reg_write_word_with_check(
	struct rt9426a_chip *chip, u8 reg, u16 data)
{
	/* sometimes need retry */
	int retry_times = 2;
	int r_data;

	while (retry_times) {
		rt9426a_reg_write_word(chip->i2c, reg, data);
		rt9426a_reg_write_word(chip->i2c, RT9426A_REG_DUMMY, 0x0000);
		mdelay(5);
		r_data = rt9426a_reg_read_word(chip->i2c, reg);
		if (data == r_data) {
			hwlog_debug("Write REG_0x%.2x Successful\n", reg);
			break;
		}
		retry_times--;
		if (retry_times == 0)
			hwlog_err("Write REG_0x%.2x fail\n", reg);
	}
	return r_data;
}

/* caculates page checksum & total checksum */
static int rt9426a_calculate_checksum_crc(struct rt9426a_chip *chip)
{
	u8 array_idx;
	u8 i;
	u8 j;
	u16 page_sum = 0;
	u16 crc_result;

	/* Calculate Page Checksum & Save to Global Array */
	for (array_idx = 0; array_idx < 14; array_idx++) {
		for (i = 0; i < 8; i++) {
			page_sum += chip->pdata->extreg_table[array_idx].data[2 * i] +
				(chip->pdata->extreg_table[array_idx].data[2 * i + 1] << 8);
		}
		g_PAGE_CHKSUM[array_idx] = 0xFFFF - page_sum;
		hwlog_debug("RT9426A Page Checksum: Page=%d, Ckecksum=0x%x\n",
			array_idx, g_PAGE_CHKSUM[array_idx]);
	}

	/* Calculate Extend Register CRC16 & return */
	crc_result = 0xFFFF;
	for (array_idx = 0; array_idx < 14; array_idx++) {
		for (i = 0; i < 16;i++) {
			crc_result = crc_result ^ chip->pdata->extreg_table[array_idx].data[i];

			for (j = 0; j < 8; j++) {
				if(crc_result & 0x01) {
					crc_result = (crc_result >> 1) ^ 0xA001;
				} else
					crc_result = crc_result >> 1;
			}
		}
	}
	hwlog_info("RT9426A Ext Reg CRC16=0x%x\n", crc_result);
	return crc_result;
}

/* get gauge total checksum value */
static int rt9426a_get_checksum(struct rt9426a_chip *chip)
{
	u8 retry_times = 3;
	u8 i;
	u16 regval, checksum_result = 0;

	while (retry_times) {
		/* Send Command to get Total Checksum */
		if(rt9426a_reg_write_word(chip->i2c, RT9426A_REG_CNTL,
			RT9426A_TOTAL_CHKSUM_CMD) >= 0) {
			rt9426a_reg_write_word(chip->i2c,
				RT9426A_REG_DUMMY, 0x0000);
			mdelay(5);
			/* Polling [BUSY] flag */
			for (i = 0; i < 10; i++) {
				regval = rt9426a_reg_read_word(chip->i2c,
					RT9426A_REG_FLAG3);
				if (regval & RT9426A_GAUGE_BUSY_MASK)
					mdelay(1);
				else
					break;
			}
			/* Get Total Checksum */
			checksum_result = rt9426a_reg_read_word(chip->i2c,
				RT9426A_REG_TOTAL_CHKSUM);
			break;
		}
		retry_times--;
		if (retry_times == 0) {
			hwlog_info("RT9426A Sent Total Checksum Command Fail\n");
			return 0xFFFF;
		}
		else
			hwlog_debug("RT9426A Sent Total Checksum Command Retry\n");
	}

	hwlog_debug("Get RT9426A Total Checksum = 0x%x\n", checksum_result);
	return checksum_result;
}

static void rt9426a_read_page_cmd(struct rt9426a_chip *chip,
	uint8_t page)
{
	uint16_t read_page_cmd = RT9426A_READ_PAGE_CMD;

	read_page_cmd += page;
	rt9426a_reg_write_word(chip->i2c, RT9426A_REG_BDCNTL, read_page_cmd);
	rt9426a_reg_write_word(chip->i2c, RT9426A_REG_BDCNTL, read_page_cmd);
	/* delay 5ms */
	mdelay(5);
}

static void rt9426a_write_page_cmd(struct rt9426a_chip *chip,
	uint8_t page)
{
	uint16_t write_page_cmd = RT9426A_WRITE_PAGE_CMD;

	write_page_cmd += page;
	rt9426a_reg_write_word(chip->i2c, RT9426A_REG_BDCNTL, write_page_cmd);
	rt9426a_reg_write_word(chip->i2c, RT9426A_REG_BDCNTL, write_page_cmd);
	mdelay(5);
}

static int rt9426a_unseal_wi_retry(struct rt9426a_chip *chip)
{
	int i;
	int regval;
	/* sometimes need retry */
	int retry_times = 3;
	int ret;

	for (i = 0; i < retry_times; i++) {
		regval = rt9426a_reg_read_word(chip->i2c, RT9426A_REG_FLAG3);
		if ((regval & RT9426A_UNSEAL_MASK) == RT9426A_UNSEAL_STATUS) {
			hwlog_debug("RT9426A Unseal Pass\n");
			ret = RT9426A_UNSEAL_PASS;
			goto out;
		} else {
			/* 2: retry time */
			if (i >= 2) {
				hwlog_err("RT9426A Unseal Fail after 3 retries\n");
				ret = RT9426A_UNSEAL_FAIL;
				goto out;
			} else if (i > 0) {
				/* print error msg instead of delay */
				hwlog_info("RT9426A Unseal Fail Cnt = %d\n",
					i + 1);
			}
			/* 0xffff:lower 2 bytes */
			rt9426a_reg_write_word(chip->i2c, RT9426A_REG_CNTL,
				RT9426A_Unseal_Key & 0xffff);
			/* >>16 higher 2 bytes */
			rt9426a_reg_write_word(chip->i2c, RT9426A_REG_CNTL,
				RT9426A_Unseal_Key >> 16);
			rt9426a_reg_write_word(chip->i2c, RT9426A_REG_DUMMY,
				0x0000);
			/* delay 5ms */
			mdelay(5);
		}
	}
	ret = RT9426A_UNSEAL_FAIL;
out:
	return ret;
}

static void rt9426a_sleep_duty_set(struct rt9426a_chip *chip,
	uint16_t data)
{
	int regval;

	if (rt9426a_unseal_wi_retry(chip) == RT9426A_UNSEAL_PASS) {
		rt9426a_read_page_cmd(chip, RT9426A_PAGE_1);
		regval = rt9426a_reg_read_word(chip->i2c, RT9426A_REG_SWINDOW7);
		/* mask bit [2-0] and set */
		regval = ((regval & 0xfff8) | (data & 0x0007));
		rt9426a_write_page_cmd(chip, RT9426A_PAGE_1);
		rt9426a_reg_write_word(chip->i2c, RT9426A_REG_SWINDOW7, regval);
		/* 0x0000: reset value */
		rt9426a_reg_write_word(chip->i2c, RT9426A_REG_DUMMY, 0x0000);
		/* delay 10ms */
		mdelay(10);
	}
}

static void rt9426a_sleep_duty_read(struct rt9426a_chip *chip)
{
	int regval;

	if (rt9426a_unseal_wi_retry(chip) == RT9426A_UNSEAL_PASS) {
		rt9426a_read_page_cmd(chip, RT9426A_PAGE_1);
		regval = rt9426a_reg_read_word(chip->i2c, RT9426A_REG_SWINDOW7);
		/* 0x0007 set bit[2-0] */
		regval = (regval & 0x0007);
		hwlog_info("Sleep_Dutty = 2^%d sec)\n", regval);
	}
}

static void rt9426a_enter_sleep(struct rt9426a_chip *chip)
{
	rt9426a_reg_write_word(chip->i2c, RT9426A_REG_CNTL,
		RT9426A_ENTR_SLP_CMD);
	/* reset */
	rt9426a_reg_write_word(chip->i2c, RT9426A_REG_DUMMY, 0x0000);
}

static void rt9426a_exit_sleep(struct rt9426a_chip *chip)
{
	rt9426a_reg_write_word(chip->i2c, RT9426A_REG_CNTL,
		RT9426A_EXIT_SLP_CMD);
	/* reset */
	rt9426a_reg_write_word(chip->i2c, RT9426A_REG_DUMMY, 0x0000);
}

/* "data" is input temperature with unit = .1'C */
static void rt9426a_temperature_set(struct rt9426a_chip *chip,
	int data)
{
	hwlog_info("%s:temp=%d .1'C\n", __func__, data);
	/* data + 2732 means to K temp */
	rt9426a_reg_write_word(chip->i2c, RT9426A_REG_TEMP, data + 2732);
	/* reset */
	rt9426a_reg_write_word(chip->i2c, RT9426A_REG_DUMMY, 0x0000);
}

static void rt9426a_reset(struct rt9426a_chip *chip)
{
	/* 0x0041 means reset regs */
	rt9426a_reg_write_word(chip->i2c, RT9426A_REG_CNTL, 0x0041);
	/* delay 1s */
	mdelay(1000);
}

#if 0
static int rt9426a_get_avg_vbat(struct rt9426a_chip *chip)
{
	int regval = 0;

	regval = rt9426a_reg_read_word(chip->i2c, RT9426A_REG_AV);
	if (regval < 0)
		return -EIO;
	return regval;
}
#endif

static int rt9426a_get_volt(struct rt9426a_chip *chip)
{
	if (chip->pdata->volt_source)
		chip->bvolt = rt9426a_reg_read_word(chip->i2c,
			chip->pdata->volt_source);

	return chip->bvolt;
}

static int rt9426a_get_temp(struct rt9426a_chip *chip)
{
	if (chip->pdata->temp_source) {
		chip->btemp = rt9426a_reg_read_word(chip->i2c,
			chip->pdata->temp_source);
		/* to 'C temp */
		chip->btemp -= 2732;
	}

	return chip->btemp;
}

static unsigned int rt9426a_get_cyccnt(struct rt9426a_chip *chip)
{
	int ret;
	unsigned int cyccnt = 0;

	ret = rt9426a_reg_read_word(chip->i2c, RT9426A_REG_CYC);
	if (ret < 0)
		hwlog_err("%s: read cycle count fail\n", __func__);
	else
		cyccnt = ret;

	return cyccnt;
}

/* get average current */
static int rt9426a_get_current(struct rt9426a_chip *chip)
{
	if (chip->pdata->curr_source) {
		chip->bcurr = rt9426a_reg_read_word(chip->i2c,
			chip->pdata->curr_source);
		if (chip->bcurr < 0)
			return -EIO;
		/* 0x7FFF handle negative situation */
		if (chip->bcurr > 0x7FFF) {
			chip->bcurr = 0x10000 - chip->bcurr;
			chip->bcurr = 0 - chip->bcurr;
		}
	}

	return chip->bcurr;
}

/* pseudo sub-routine to get is_adpater_plugged */
static int rt9426a_get_is_adapter_plugged(struct rt9426a_chip *chip)
{
	/* To-Do:
	 * return 1, if charging adapter is plugged.
	 * return 0, if charging adapter is unplugged.
	 */
	return 0;
}
static int rt9426a_fg_get_offset(struct rt9426a_chip *chip, int soc,
	int temp);

/* add for smooth_soc */
static int rt9426a_fg_get_soc(struct rt9426a_chip *chip,
	int to_do_smooth_soc)
{
	int regval;
	int capacity;
	int btemp;

	regval = rt9426a_reg_read_word(chip->i2c, RT9426A_REG_SOC);
	if (regval < 0) {
		hwlog_err("read soc value fail\n");
		return -EIO;
	}
	/* 10 precision */
	capacity = (regval * 10);
	hwlog_info("capacity before offset=%d\n", capacity);
	btemp = rt9426a_get_temp(chip);
	hwlog_info("TEMP=%d\n", btemp);
	chip->soc_offset = rt9426a_fg_get_offset(chip, capacity, btemp);
	hwlog_info("SOC_OFFSET=%d\n", chip->soc_offset);
	capacity += chip->soc_offset;
	hwlog_info("capacity after offset=%d\n", capacity);
	if (capacity > 0)
		/* 10 precision */
		capacity = DIV_ROUND_UP(capacity, 10);
	else
		capacity = 0;
	/* 100: full capacity */
	if (capacity > 100)
		capacity = 100;
	hwlog_info("SYS_SOC=%d\n", capacity);

	/* for smooth_soc */
	if (to_do_smooth_soc == 1) {
		hwlog_info("smooth soc [st, ic]=[%d, %d]\n", chip->capacity,
			capacity);
		hwlog_info("smooth soc ta_sts [%d]\n",
			rt9426a_get_is_adapter_plugged(chip));
		if (abs(chip->capacity - capacity) >= 1) {
			if (capacity > chip->capacity) {
				if (rt9426a_get_is_adapter_plugged(chip) == 1)
					capacity = chip->capacity + 1;
				else
					capacity = chip->capacity;
			} else {
				if (rt9426a_get_current(chip) <= 0)
					capacity = chip->capacity - 1;
				else
					capacity = chip->capacity;
			}
		}
		hwlog_info("SYS_SOC=%d (after smooth)\n", capacity);
	}
	return capacity;
}

static unsigned int rt9426a_get_design_capacity(struct rt9426a_chip *chip)
{
	chip->design_capacity = rt9426a_reg_read_word(chip->i2c, RT9426A_REG_DC);
	if (chip->design_capacity < 0)
		return -EIO;

	return chip->design_capacity;
}

/* checking cycle_cnt & bccomp */
static int rt9426a_set_cyccnt(struct rt9426a_chip *chip,
	unsigned int cyc_new)
{
	int ret;
	unsigned int cyccnt;

	ret = rt9426a_reg_write_word(chip->i2c, RT9426A_REG_CNTL,
		RT9426A_SET_CYCCNT_KEY);
	if (ret < 0) {
		hwlog_err("send keyword to set cycle count fail\n");
	} else {
		/* delay 1ms */
		mdelay(1);
		rt9426a_reg_write_word(chip->i2c, RT9426A_REG_CYC, cyc_new);
		/* reset */
		rt9426a_reg_write_word(chip->i2c, RT9426A_REG_DUMMY, 0x0000);
		/* delay 1ms */
		mdelay(5);
		/* read back check */
		cyccnt = rt9426a_reg_read_word(chip->i2c, RT9426A_REG_CYC);
		if (cyccnt == cyc_new) {
			ret = cyccnt;
			hwlog_err("set cycle count to %d successfully\n",
				cyccnt);
		} else {
			ret = -1;
			hwlog_err("set cycle count failed target=%d now=%d\n",
				cyc_new, cyccnt);
		}
	}

	return ret;
}

static int rt9426a_get_cycle_cnt_from_nvm(struct rt9426a_chip *chip,
	unsigned int *cyccnt_nvm)
{
	/* To-Do: Get backup Cycle Count from NVM and save to "cyc_nvm" */
	return 0;
}

static int rt9426a_get_bccomp(struct rt9426a_chip *chip)
{
	int regval = 0, retry_times = 2;

	while (retry_times-- > 0) {
		regval = rt9426a_reg_read_word(chip->i2c, RT9426A_REG_BCCOMP);
		hwlog_info("BCCOMP now is 0x%04X\n", regval);
		if ((regval > 0xCCD) && (regval < 0x7999))
			return regval;
	}

	return -EINVAL;
}

static int rt9426a_set_bccomp(struct rt9426a_chip *chip,
	unsigned int bccomp_new)
{
	int opcfg4 = 0, opcfg4_new = 0;

	if (rt9426a_unseal_wi_retry(chip) == RT9426A_UNSEAL_FAIL)
		return -EINVAL;
	/* Get present OPCFG4 */
	rt9426a_read_page_cmd(chip, RT9426A_PAGE_1);
	opcfg4 = rt9426a_reg_read_word(chip->i2c, RT9426A_REG_SWINDOW4);
	opcfg4_new = opcfg4 | 0x0040;
	/* Enable SET_BCOMP by set OPCFG4 */
	rt9426a_write_page_cmd(chip, RT9426A_PAGE_1);
	rt9426a_reg_write_word(chip->i2c, RT9426A_REG_SWINDOW4, opcfg4_new);
	/* reset */
	rt9426a_reg_write_word(chip->i2c, RT9426A_REG_DUMMY, 0x0000);
	/* delay 1ms */
	mdelay(1);
	/* Set new BCCOMP */
	rt9426a_write_page_cmd(chip, RT9426A_PAGE_2);
	rt9426a_reg_write_word(chip->i2c, RT9426A_REG_SWINDOW5, bccomp_new);
	/* reset */
	rt9426a_reg_write_word(chip->i2c, RT9426A_REG_DUMMY, 0x0000);
	/* delay 1ms */
	mdelay(1);
	hwlog_info("Set BCCOMP as 0x%04X\n", bccomp_new);
	/* Recover OPCFG4 */
	rt9426a_write_page_cmd(chip, RT9426A_PAGE_1);
	rt9426a_reg_write_word(chip->i2c, RT9426A_REG_SWINDOW4, opcfg4);
	/* reset */
	rt9426a_reg_write_word(chip->i2c, RT9426A_REG_DUMMY, 0x0000);
	/* delay 1ms */
	mdelay(1);
	/* Seal after reading */
	rt9426a_reg_write_word(chip->i2c, RT9426A_REG_CNTL, RT9426A_SEAL_CMD);
	/* reset */
	rt9426a_reg_write_word(chip->i2c, RT9426A_REG_DUMMY, 0x0000);
	/* delay 1ms */
	mdelay(1);

	return 0;
}

static int rt9426a_get_bccomp_from_nvm(struct rt9426a_chip *chip,
	unsigned int *bccomp_nvm)
{
	/* To-Do: Get backup "BCCOMP" from NVM and save to "bccomp_nvm" */
	return 0;
}

static void __maybe_unused rt9426a_check_cycle_cnt_for_fg_ini
	(struct rt9426a_chip *chip)
{
	int cyccnt_fg = 0;
	int cyccnt_nvm = 0;
	int bccomp_nvm = 0x4000;

	/* read cyccnt from fg & nvm */
	cyccnt_fg = rt9426a_get_cyccnt(chip);
	rt9426a_get_cycle_cnt_from_nvm(chip, &cyccnt_nvm);

	if (cyccnt_fg < cyccnt_nvm) {
		/* read bccomp from nvm */
		rt9426a_get_bccomp_from_nvm(chip, &bccomp_nvm);
		/* set cyccnt to fg by backup value*/
		rt9426a_set_cyccnt(chip, cyccnt_nvm);
		/* set bccomp to fg by backup value*/
		rt9426a_set_bccomp(chip, bccomp_nvm);
		hwlog_info("%s:recover cycle count from nvm target=%d now=%d\n",
			__func__, cyccnt_nvm, cyccnt_fg);
		hwlog_info("%s:recover bccomp from nvm target=%d\n",
			__func__, bccomp_nvm);
	} else
		hwlog_info("%s:cycle counts are the same(%d)\n",
			__func__, cyccnt_fg);
}

static unsigned int rt9426a_get_ocv_checksum(struct rt9426a_chip *chip)
{
	int regval = 0;
	regval = rt9426a_unseal_wi_retry(chip);

	if (regval == RT9426A_UNSEAL_FAIL)
		chip->ocv_checksum_ic = 0xFFFF;
	else{
		/* get ocv checksum from ic */
		rt9426a_reg_write_word(chip->i2c, RT9426A_REG_BDCNTL, 0xCA09);
		rt9426a_reg_write_word(chip->i2c, RT9426A_REG_BDCNTL, 0xCA09);
		/* delay 5ms */
		mdelay(5);
		chip->ocv_checksum_ic = rt9426a_reg_read_word(chip->i2c,
			RT9426A_REG_SWINDOW5);
	}
	return chip->ocv_checksum_ic;
}
/* updating ieoc setting by api
 * unit of ieoc_curr: mA
 * unit of ieoc_buff: mA
 */
static int __maybe_unused rt9426a_write_ieoc_setting_api(
	struct rt9426a_chip *chip,
	unsigned int ieoc_curr,
	unsigned int ieoc_buff)
{
	int fc_setting;
	int regval;
	int fc_ith_setting;

	hwlog_info("%s\n" ,__func__);
	regval = rt9426a_reg_read_word(chip->i2c, RT9426A_REG_FLAG1);
	if (regval & BIT(0))
		return 0;

	if (rt9426a_unseal_wi_retry(chip) == RT9426A_UNSEAL_FAIL)
		return -EINVAL;

	/* read fc_setting for bit operation ; 2021-01-18 */
	rt9426a_read_page_cmd(chip, RT9426A_PAGE_5);
	regval = rt9426a_reg_read_word(chip->i2c, RT9426A_REG_SWINDOW3);

	/*
	 * calculate fc_ith by input parameter ; 2021-01-18
	 * fc_ith_setting = ((curr+buff)*rs_schematic)/(rs_ic_setting*4)
	 * e.g. rs_ic_setting = 2.5mR  rs_ic_setting = 1mR
	 * fc_ith_setting = (ieoc_setting * rs_schematic)/(rs_ic_setting*4)
	 *                = (ieoc_setting * 1)/(2.5*4)
	 */
	fc_ith_setting = ((ieoc_curr + ieoc_buff) *
		(int)chip->pdata->rs_schematic);
	fc_ith_setting = DIV_ROUND_UP(fc_ith_setting,
		(int)chip->pdata->rs_ic_setting * 4);

	/* recombine to fc_setting by bit operation ; 2021-01-18 */
	fc_setting = (regval & 0xFF) | ((fc_ith_setting & 0xFF) << 8);

	hwlog_info("fc_setting was 0x%04X is 0x%04X\n" ,regval, fc_setting);
	rt9426a_write_page_cmd(chip, RT9426A_PAGE_5);
	rt9426a_reg_write_word(chip->i2c, RT9426A_REG_SWINDOW3, fc_setting);

	rt9426a_reg_write_word(chip->i2c, RT9426A_REG_CNTL, 0x0020);
	rt9426a_reg_write_word(chip->i2c, RT9426A_REG_DUMMY, 0x0000);
	mdelay(5);

	return 0;
}
/* add for updating ieoc setting by dtsi */
static int rt9426a_check_icc_sts(struct rt9426a_chip *chip, int icc_now)
{
	int i;

	for (i = 0; i < 3; i++) {
		if (chip->pdata->icc_threshold[i] > icc_now)
			break;
	}

	chip->icc_sts = i;
	return 0;
}

static int rt9426a_get_charging_current_setting(struct rt9426a_chip *chip)
{
	/* To-Do: dummy return 500mA as the default setting */
	return RT9426A_CHG_CURR_VAL;
}
/* updating ieoc setting by dtsi */
static int __maybe_unused rt9426a_update_ieoc_setting(struct rt9426a_chip *chip)
{
	int icc_setting;
	int regval;

	hwlog_info( "update_ieoc_setting begin\n");
	regval = rt9426a_reg_read_word(chip->i2c, RT9426A_REG_FLAG1);
	if (regval & BIT(0))
		return 0;

	if (chip->pdata->icc_threshold[0] == 0)
		return 0;

	if (rt9426a_unseal_wi_retry(chip) == RT9426A_UNSEAL_FAIL)
		return -EINVAL;

	icc_setting = rt9426a_get_charging_current_setting(chip);
	hwlog_info("icc_setting=%d\n", icc_setting);
	rt9426a_check_icc_sts(chip, icc_setting);

	rt9426a_read_page_cmd(chip, RT9426A_PAGE_5);
	regval = rt9426a_reg_read_word(chip->i2c, RT9426A_REG_SWINDOW3);

	if (regval != chip->pdata->ieoc_setting[chip->icc_sts]) {
		hwlog_info("icc sts=%d update setting to 0x%04x\n" ,
			 chip->icc_sts, chip->pdata->ieoc_setting[chip->icc_sts]);
		rt9426a_write_page_cmd(chip, RT9426A_PAGE_5);
		rt9426a_reg_write_word(chip->i2c, RT9426A_REG_SWINDOW3,
			chip->pdata->ieoc_setting[chip->icc_sts]);
	}
	/* disable extend access mode */
	rt9426a_reg_write_word(chip->i2c, RT9426A_REG_CNTL, 0x0020);
	/* reset */
	rt9426a_reg_write_word(chip->i2c, RT9426A_REG_DUMMY, 0x0000);
	/* delay 5ms */
	mdelay(5);

	return 0;
}

static int rt9426a_write_ocv_table(struct rt9426a_chip *chip)
{
	int retry_times, i, j, regval, rtn = RT9426A_WRITE_OCV_FAIL;
	const u32 *pval = (u32 *)chip->pdata->ocv_table + chip->ocv_index * 80;

	/* set OCV Table */
	if (*pval == 0x13) {
		retry_times = 3;
		hwlog_info("Write NEW OCV Table\n");
		while (retry_times) {
			for (i = 0; i < 9; i++) {
				rt9426a_reg_write_word(chip->i2c,
					RT9426A_REG_BDCNTL, 0xCB00 + i);
				rt9426a_reg_write_word(chip->i2c,
					RT9426A_REG_BDCNTL, 0xCB00 + i);
				for (j = 0; j < 8; j++) {
					rt9426a_reg_write_word(chip->i2c,
						0x40 + j * 2, *(pval + i * 8 + j));
					mdelay(1);
				}
			}
			rt9426a_reg_write_word(chip->i2c,
				RT9426A_REG_BDCNTL, 0xCB09);
			rt9426a_reg_write_word(chip->i2c,
				RT9426A_REG_BDCNTL, 0xCB09);
			for (i = 0; i < 5; i++) {
				rt9426a_reg_write_word(chip->i2c, 0x40 + i * 2,
					*(pval + 9 * 8 + i));
				mdelay(1);
			}
			rt9426a_reg_write_word(chip->i2c, RT9426A_REG_DUMMY,
				0x0000);
			mdelay(10);
			regval = rt9426a_reg_read_word(chip->i2c,
				RT9426A_REG_FLAG2);
			if (regval & RT9426A_USR_TBL_USED_MASK) {
				hwlog_info("OCV Table Write Successful\n");
				rtn = RT9426A_WRITE_OCV_PASS;
				break;
			}
			retry_times--;
			if (retry_times == 0)
				hwlog_err("Set OCV Table fail\n");
		}
	}
	return rtn;
}

static int rt9426a_apply_pdata(struct rt9426a_chip *);
static void rt9426a_update_info(struct rt9426a_chip *chip)
{
	int regval;
	int ret;
	int retry_times = 3;
	int i;
	struct power_supply *batt_psy;

	hwlog_info("%s\n", __func__);
	mutex_lock(&chip->update_lock);

	/* get battery temp from battery power supply */
	batt_psy = power_supply_get_by_name("battery");
	if (!batt_psy)
		hwlog_err("get batt_psy fail\n");

	if (rt9426a_unseal_wi_retry(chip) == RT9426A_UNSEAL_FAIL)
		goto end_update_info;

	/* check if re-init is necessary */
	for (i = 0 ; i < retry_times ; i++) {
		regval = rt9426a_reg_read_word(chip->i2c, RT9426A_REG_FLAG3);
		if((regval & RT9426A_RI_MASK) && (regval != 0xFFFF)) {
			hwlog_info("Init RT9426A by [RI]\n");
			rt9426a_apply_pdata(chip);
			break;
		}
		else if(regval!=0xFFFF)
			break;
		mdelay(5);
	}

	/* read OPCFG1~7 to check */
	rt9426a_read_page_cmd(chip, RT9426A_PAGE_1);

	hwlog_debug("OPCFG1~5(0x%x 0x%x 0x%x 0x%x 0x%x)\n",
		rt9426a_reg_read_word(chip->i2c, RT9426A_REG_SWINDOW1),
		rt9426a_reg_read_word(chip->i2c, RT9426A_REG_SWINDOW2),
		rt9426a_reg_read_word(chip->i2c, RT9426A_REG_SWINDOW3),
		rt9426a_reg_read_word(chip->i2c, RT9426A_REG_SWINDOW4),
		rt9426a_reg_read_word(chip->i2c, RT9426A_REG_SWINDOW5));

	rt9426a_read_page_cmd(chip, RT9426A_PAGE_10);

	hwlog_debug("OPCFG6~7(0x%x 0x%x)\n",
		rt9426a_reg_read_word(chip->i2c, RT9426A_REG_SWINDOW1),
		rt9426a_reg_read_word(chip->i2c, RT9426A_REG_SWINDOW2));

	ret = rt9426a_reg_read_word(chip->i2c, RT9426A_REG_FLAG2);

	rt9426a_read_page_cmd(chip, RT9426A_PAGE_2);
	regval = rt9426a_reg_read_word(chip->i2c, RT9426A_REG_SWINDOW1);
	regval = (regval & 0x0300) >> 8;
	if (((ret & 0x0800) >> 11) == 1) {
		hwlog_info("OCV table define by User\n");
	} else {
		if (regval == 0)
			hwlog_info("OCV(4200) EDV(3200)\n");
		else if (regval == 1)
			hwlog_info("OCV(4350) EDV(3200)\n");
		else if (regval == 2)
			hwlog_info("OCV(4400) EDV(3200)\n");
		else
			hwlog_info("OCV(4350) EDV(3400)\n");
	}

	regval = rt9426a_reg_read_word(chip->i2c, RT9426A_REG_SWINDOW5);
	hwlog_debug("CSCOMP4(%d)\n", regval);
	regval = rt9426a_reg_read_word(chip->i2c, RT9426A_REG_SWINDOW4);
	hwlog_debug( "CSCOMP5(%d)\n", regval);

	hwlog_debug("DSNCAP(%d) FCC(%d)\n",
		rt9426a_reg_read_word(chip->i2c, RT9426A_REG_DC),
		rt9426a_reg_read_word(chip->i2c, RT9426A_REG_FCC));

	hwlog_debug("VOLT_SOURCE(0x%x) INPUT_VOLT(%d) FG_VBAT(%d) FG_OCV(%d) FG_AV(%d)\n",
		chip->pdata->volt_source, rt9426a_get_volt(chip),
		rt9426a_reg_read_word(chip->i2c, RT9426A_REG_VBAT),
		rt9426a_reg_read_word(chip->i2c, RT9426A_REG_OCV),
		rt9426a_reg_read_word(chip->i2c, RT9426A_REG_AV));
	hwlog_debug("CURR_SOURCE(0x%x) INPUT_CURR(%d) FG_CURR(%d) FG_AI(%d)\n",
		chip->pdata->curr_source, rt9426a_get_current(chip),
		rt9426a_reg_read_word(chip->i2c, RT9426A_REG_CURR),
		rt9426a_reg_read_word(chip->i2c, RT9426A_REG_AI));
	hwlog_debug("TEMP_SOURCE(0x%x) INPUT_TEMP(%d) FG_TEMP(%d)\n",
		chip->pdata->temp_source, rt9426a_get_temp(chip),
		rt9426a_reg_read_word(chip->i2c, RT9426A_REG_TEMP));
	hwlog_debug("FG_FG_INTT(%d) FG_AT(%d)\n",
		rt9426a_reg_read_word(chip->i2c, RT9426A_REG_INTT),
		rt9426a_reg_read_word(chip->i2c, RT9426A_REG_AT));

	regval = rt9426a_reg_read_word(chip->i2c, RT9426A_REG_FLAG1);
	hwlog_debug("FLAG1(0x%x)\n", regval);
	if (((regval & 0x0200) >> 9) == 1)
		hwlog_info("FC=1\n");
	else
		hwlog_info("FC=0\n");

	if (((regval & 0x0004) >> 2) == 1)
		hwlog_info("FD=1\n");
	else
		hwlog_info("FD=0\n");

	regval = rt9426a_reg_read_word(chip->i2c, RT9426A_REG_FLAG2);
	hwlog_debug("FLAG2(0x%x)\n", regval);

	if (((regval & 0xE000) >> 13) == 0)
		hwlog_info("Power_Mode (Active)\n");
	else if (((regval & 0xE000) >> 13) == 1)
		hwlog_info("Power_Mode (FST_RSP_ACT)\n");
	else if (((regval & 0xE000) >> 13) == 2)
		hwlog_info("Power_Mode (Shutdown)\n");
	else
		hwlog_info("Power_Mode (Sleep)\n");

	if (((regval & 0x0800) >> 11) == 1)
		hwlog_info("User_Define_Table (IN USE)\n");
	else
		hwlog_info("User_Define_Table (NOT IN USE)\n");
	if (((regval & 0x0040) >> 6) == 1)
		hwlog_info("Battery_Status (Inserted)\n");
	else
		hwlog_info("Battery_Status (Removed)\n");

	if (((regval & 0x0001)) == 1)
		hwlog_info("RLX=1\n");
	else
		hwlog_info("RLX=0\n");

	regval = rt9426a_reg_read_word(chip->i2c, RT9426A_REG_FLAG3);
	hwlog_debug("FLAG3(0x%x)\n", regval);
	if (((regval & 0x0100) >> 8) == 1)
		hwlog_info("RI=1\n");
	else
		hwlog_info("RI=0\n");

	if (((regval & 0x0001)) == 1)
		hwlog_info("RT9426A (Unseal)\n");
	else
		hwlog_info("RT9426A (Seal)\n");

	hwlog_debug("CYCCNT(%d)\n", rt9426a_get_cyccnt(chip));

	regval = rt9426a_reg_read_word(chip->i2c, RT9426A_REG_SOC);
	hwlog_debug("SOC(%d)\n", regval);

	chip->capacity = rt9426a_fg_get_soc(chip, chip->pdata->smooth_soc_en);

	regval = rt9426a_reg_read_word(chip->i2c, RT9426A_REG_RM);
	hwlog_debug("RM(%d)\n", regval);

	regval = rt9426a_reg_read_word(chip->i2c, RT9426A_REG_SOH);
	hwlog_debug("SOH(%d)\n", regval);

	// rt9426a_update_ieoc_setting(chip);

	if (batt_psy) {
		power_supply_changed(batt_psy);
		power_supply_put(batt_psy);
	}
end_update_info:
	mutex_unlock(&chip->update_lock);
	return;
}

static irqreturn_t rt9426a_irq_handler(int irqno, void *param)
{
	struct rt9426a_chip *chip = (struct rt9426a_chip *)param;
#ifdef CONFIG_HUAWEI_POWER_EMBEDDED_ISOLATION
	u16 irq_val;
#endif /* CONFIG_HUAWEI_POWER_EMBEDDED_ISOLATION */

	hwlog_info("%s\n", __func__);
	hwlog_info("FG_FLAG1(0x%x) FG_FLAG2(0x%x) FG_FLAG3(0x%x)\n",
		rt9426a_reg_read_word(chip->i2c, RT9426A_REG_FLAG1),
		rt9426a_reg_read_word(chip->i2c, RT9426A_REG_FLAG2),
		rt9426a_reg_read_word(chip->i2c, RT9426A_REG_FLAG3));

	rt9426a_update_info(chip);
#ifdef CONFIG_HUAWEI_POWER_EMBEDDED_ISOLATION
	/* 0x02: bit flag for UV_IRQ */
	irq_val = rt9426a_reg_read_word(chip->i2c, RT9426A_REG_IRQ);
	hwlog_info("FG_IRQ(0x%x)\n", irq_val);

	if (irq_val & 0x02)
		power_event_notify(POWER_NT_COUL, POWER_NE_COUL_LOW_VOL, NULL);
#endif /* CONFIG_HUAWEI_POWER_EMBEDDED_ISOLATION */
	return IRQ_HANDLED;
}

static void new_vgcomp_soc_offset_datas(struct device *dev, int type,
	struct rt9426a_platform_data *pdata, int size_x, int size_y, int size_z)
{
	switch (type) {
	case SOC_OFFSET:
		if (pdata->soc_offset.soc_offset_data) {
			devm_kfree(dev, pdata->soc_offset.soc_offset_data);
			pdata->soc_offset.soc_offset_data = NULL;
		}
		if (size_x != 0 && size_y != 0)
			pdata->soc_offset.soc_offset_data = devm_kzalloc(dev,
				size_x * size_y * sizeof(struct data_point),
				GFP_KERNEL);
		if (pdata->soc_offset.soc_offset_data) {
			pdata->soc_offset.soc_voltnr = size_x;
			pdata->soc_offset.tempnr = size_y;

		} else {
			pdata->soc_offset.soc_voltnr = 0;
			pdata->soc_offset.tempnr = 0;
		}
		break;
	default:
		WARN_ON(1);
	}
}

static inline const struct data_point *get_mesh_data(int i, int j, int k,
	const struct data_point *mesh, int xnr, int ynr)
{
	return mesh + k * ynr * xnr + j * xnr + i;
}

static int get_sub_mesh(int state, struct data_point *mesh_buffer,
	struct submask_condition *condition)
{
	int i, j, k = 0;
	int x = condition->x;
	int y = condition->y;
	int z = condition->z;

	for (i = 0; i < condition->xnr; ++i) {
		if (get_mesh_data(i, 0, 0, condition->mesh_src,
					condition->xnr, condition->ynr)->x >= x)
			break;
	}
	for (; i >= 0 && i < condition->xnr; --i) {
		if (get_mesh_data(i, 0, 0, condition->mesh_src,
					condition->xnr, condition->ynr)->x <= x)
			break;
	}

	for (j = 0; j < condition->ynr; ++j) {
		if (get_mesh_data(0, j, 0, condition->mesh_src,
					condition->xnr, condition->ynr)->y >= y)
			break;
	}
	for (; j >= 0 && j < condition->ynr; --j) {
		if (get_mesh_data(0, j, 0, condition->mesh_src,
					condition->xnr, condition->ynr)->y <= y)
			break;
	}

	if (state == FG_COMP) {
		for (k = 0; k < condition->znr; ++k) {
			if (get_mesh_data(0, 0, k, condition->mesh_src,
					condition->xnr, condition->ynr)->z >= z)
				break;
		}
		for (; k >= 0 && k < condition->znr; --k) {
			if (get_mesh_data(0, 0, k, condition->mesh_src,
					condition->xnr, condition->ynr)->z <= z)
				break;
		}
	}

	i -= ((condition->order_x - 1) / 2);
	j -= ((condition->order_y - 1) / 2);
	k -= ((condition->order_z - 1) / 2);

	if (i <= 0)
		i = 0;
	if (j <= 0)
		j = 0;
	if (k <= 0)
		k = 0;
	if ((i + condition->order_x) > condition->xnr)
		i = condition->xnr - condition->order_x;
	if ((j + condition->order_y) > condition->ynr)
		j = condition->ynr - condition->order_y;
	if ((k + condition->order_z) > condition->znr)
		k = condition->znr - condition->order_z;

	if (state == FG_COMP) {
		for (z = 0; z < condition->order_z; ++z) {
			for (y = 0; y < condition->order_y; ++y) {
				for (x = 0; x < condition->order_x; ++x) {
					*(mesh_buffer + z * condition->order_y *
							condition->order_z +
						y * condition->order_x + x)
						= *get_mesh_data(i + x, j + y,
							k + z,
							condition->mesh_src,
							condition->xnr,
							condition->ynr);
				}
			}
		}
	} else {
		for (y = 0; y < condition->order_y; ++y) {
			for (x = 0; x < condition->order_x; ++x) {
				*(mesh_buffer + y * condition->order_x + x)
					= *get_mesh_data(i + x, j + y, 0,
						condition->mesh_src,
						condition->xnr,
						condition->ynr);
			}
		}
	}
	return 0;
}

static int offset_li(int xnr, int ynr, const struct data_point *mesh,
	int x, int y)
{
	long long retval = 0;
	int i, j, k;
	long long wm, wd;
	const struct data_point *cache = NULL;

	for (i = 0; i < xnr; ++i) {
		for (j = 0; j < ynr; ++j) {
			wm = wd = 1;
			cache = get_mesh_data(i, j, 0, mesh, xnr, ynr);
			for (k = 0; k < xnr; ++k) {
				if (i != k) {
					wm *= (x - get_mesh_data(k, j, 0,
							mesh, xnr, ynr)->x);
					wd *= (cache->x - get_mesh_data(k, j, 0,
							mesh, xnr, ynr)->x);
				}
			}
			for (k = 0; k < ynr; ++k) {
				if (j != k) {
					wm *= (y - get_mesh_data(i, k, 0,
							mesh, xnr, ynr)->y);
					wd *= (cache->y - get_mesh_data(i, k, 0,
							mesh, xnr, ynr)->y);
				}
			}
			retval += div64_s64(
				((cache->w * wm) << PRECISION_ENHANCE), wd);
		}
	}
	return (int)((retval + (1 << (PRECISION_ENHANCE - 1))) >> PRECISION_ENHANCE);
}

static int rt9426a_fg_get_offset(struct rt9426a_chip *chip,
	int soc_val, int temp)
{
	const int ip_x = chip->pdata->offset_interpolation_order[0];
	const int ip_y = chip->pdata->offset_interpolation_order[1];

	struct data_point *sub_mesh = NULL;
	int xnr, ynr;
	int offset;
	struct soc_offset_table *offset_table = NULL;
	struct submask_condition condition = {
		.x = soc_val,
		.y = temp,
	};

	sub_mesh = kzalloc(ip_x * ip_y * sizeof(struct data_point), GFP_KERNEL);
	if (!sub_mesh)
		return 0;

	mutex_lock(&chip->var_lock);
	offset_table = &chip->pdata->soc_offset;
	xnr = offset_table->soc_voltnr;
	ynr = offset_table->tempnr;
	if (xnr == 0 || ynr == 0) {
		mutex_unlock(&chip->var_lock);
		kfree(sub_mesh);
		return 0;
	}
	condition.order_x = min(ip_x, xnr);
	condition.order_y = min(ip_y, ynr);
	condition.xnr = xnr;
	condition.ynr = ynr;
	condition.mesh_src = offset_table->soc_offset_data;
	get_sub_mesh(SOC_OFFSET, sub_mesh, &condition);
	offset = offset_li(condition.order_x, condition.order_y, sub_mesh,
		soc_val, temp);
	mutex_unlock(&chip->var_lock);
	kfree(sub_mesh);
	return offset;
}

#ifdef CONFIG_HUAWEI_POWER_EMBEDDED_ISOLATION
static int rt_fg_get_property(struct power_supply *psy,
	enum power_supply_property psp, union power_supply_propval *val)
{
	struct rt9426a_chip *chip = power_supply_get_drvdata(psy);
	int rc = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = chip->online;
		hwlog_info("psp_online=%d\n", val->intval);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		val->intval = rt9426a_get_volt(chip);
		hwlog_info("psp_volt_now=%d\n", val->intval);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN:
		val->intval = chip->pdata->battery_type;
		hwlog_info("psp_volt_max_design=%d\n", val->intval);
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		chip->capacity = rt9426a_get_display_data(chip, RT9426A_DISPLAY_SOC);
		val->intval = chip->capacity;
		hwlog_info("psp_capacity=%d\n", val->intval);
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN:
		val->intval = rt9426a_get_display_data(chip, RT9426A_DISPLAY_DISIGN_FCC);
		hwlog_info("psp_charge_full_design=%d\n", val->intval);
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		val->intval = rt9426a_get_display_data(chip, RT9426A_DISPLAY_IBAT);
		hwlog_info("psp_curr_now=%d\n", val->intval);
		break;
	case POWER_SUPPLY_PROP_TEMP:
		val->intval = rt9426a_get_display_data(chip, RT9426A_DISPLAY_TEMP);
		hwlog_info("psp_tem=%d\n", val->intval);
		break;
	case POWER_SUPPLY_PROP_CYCLE_COUNT:
		val->intval = rt9426a_get_cyccnt(chip);
		hwlog_info("psp_cyccn=%d\n", val->intval);
		break;
	/* aging cv */
	case POWER_SUPPLY_PROP_VOLTAGE_OCV:
		val->intval = chip->ocv_index;
		hwlog_info("ocv index=%d\n", chip->ocv_index);
		break;
	default:
		rc = -EINVAL;
		break;
	}

	return rc;
}
#else
static int rt_fg_get_property(struct power_supply *psy,
	enum power_supply_property psp,
	union power_supply_propval *val)
{
	struct rt9426a_chip *chip = power_supply_get_drvdata(psy);
	int rc = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = chip->online;
		dev_info(chip->dev, "psp_online = %d\n", val->intval);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		val->intval = rt9426a_get_volt(chip);
		dev_info(chip->dev, "psp_volt_now = %d\n", val->intval);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN:
		val->intval = chip->pdata->battery_type;
		dev_info(chip->dev, "psp_volt_max_design = %d\n", val->intval);
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		/* add for smooth_soc
		 * chip->capacity = rt9426a_fg_get_soc(chip);
		 */
		val->intval = chip->capacity;
		dev_info(chip->dev, "psp_capacity = %d\n", val->intval);
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN:
		val->intval = rt9426a_get_design_capacity(chip);
		dev_info(chip->dev, "psp_charge_full_design = %d\n", val->intval);
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		val->intval = rt9426a_get_current(chip);
		dev_info(chip->dev, "psp_curr_now = %d\n", val->intval);
		break;
	case POWER_SUPPLY_PROP_TEMP:
		val->intval = rt9426a_get_temp(chip);
		dev_info(chip->dev, "psp_temp = %d\n", val->intval);
		break;
	case POWER_SUPPLY_PROP_CYCLE_COUNT:
		val->intval = rt9426a_get_cyccnt(chip);
		dev_info(chip->dev, "psp_cyccnt = %d\n", val->intval);
		break;
	/* for aging cv */
	case POWER_SUPPLY_PROP_VOLTAGE_OCV:
		val->intval = chip->ocv_index;
		dev_info(chip->dev, "ocv index = %d\n", chip->ocv_index);
		break;
	default:
		rc = -EINVAL;
		break;
	}

	return rc;
}
#endif /* CONFIG_HUAWEI_POWER_EMBEDDED_ISOLATION */

static int rt_fg_set_property(struct power_supply *psy,
	enum power_supply_property psp, const union power_supply_propval *val)
{
	struct rt9426a_chip *chip = power_supply_get_drvdata(psy);
	int rc = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_TEMP:
		rt9426a_temperature_set(chip, val->intval);
		break;
	case POWER_SUPPLY_PROP_CYCLE_COUNT:
		if (val->intval < 0)
			return -EINVAL;
		rt9426a_set_cyccnt(chip, val->intval);
		break;
	/* add for aging cv; 2021-01-18 */
	case POWER_SUPPLY_PROP_VOLTAGE_OCV:
		if (val->intval < 0 || val->intval > 4)
			return -EINVAL;
		chip->ocv_index = val->intval;
		/* save to RSVD2 after ocv_index assigned ; 2021-02-26 */
		rt9426a_reg_write_word(chip->i2c, RT9426A_REG_RSVD2, chip->ocv_index);
		if (rt9426a_unseal_wi_retry(chip) == RT9426A_UNSEAL_FAIL)
			return -EIO;
		mutex_lock(&chip->update_lock);
		/* write aging ocv table */
		rt9426a_write_ocv_table(chip);
		/* write aging fcc */
		rt9426a_write_page_cmd(chip, RT9426A_PAGE_2);
		rt9426a_reg_write_word(chip->i2c, RT9426A_REG_SWINDOW7,
			chip->pdata->fcc[chip->ocv_index]);
		rt9426a_reg_write_word(chip->i2c, RT9426A_REG_DUMMY, 0x0000);
		mdelay(5);
		/* write aging fc_vth */
		rt9426a_write_page_cmd(chip, RT9426A_PAGE_5);
		rt9426a_reg_write_word(chip->i2c, RT9426A_REG_SWINDOW3,
				(chip->pdata->fc_vth[chip->ocv_index]) |
				(chip->pdata->extreg_table[5].data[5] << 8));
		mutex_unlock(&chip->update_lock);
		/* update ocv checksum */
		chip->ocv_checksum_dtsi = *((u32 *)(chip->pdata->ocv_table) +
			chip->ocv_index * 80 + RT9426A_IDX_OF_OCV_CKSUM);
		break;
	default:
		rc = -EINVAL;
		break;
	}

	return rc;
}

static int rt_fg_property_is_writeable(struct power_supply *psy,
	enum power_supply_property psp)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_TEMP:
	case POWER_SUPPLY_PROP_CYCLE_COUNT:
	/* add for aging cv; 2021-01-18 */
	case POWER_SUPPLY_PROP_VOLTAGE_OCV:
		return true;
	default:
		return false;
	}
}

static enum power_supply_property rt_fg_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_CYCLE_COUNT,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN,
	POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_VOLTAGE_OCV,
};

static struct power_supply_desc fg_psy_desc = {
#ifdef CONFIG_HUAWEI_POWER_EMBEDDED_ISOLATION
	.name = "rt-fuelgauge",
#else
	.name = "battery",
#endif /* CONFIG_HUAWEI_POWER_EMBEDDED_ISOLATION */
	.type = POWER_SUPPLY_TYPE_BATTERY,
	.properties = rt_fg_props,
	.num_properties = ARRAY_SIZE(rt_fg_props),
	.get_property = rt_fg_get_property,
	.set_property = rt_fg_set_property,
	.property_is_writeable = rt_fg_property_is_writeable,
};

static int __maybe_unused rt9426a_get_calibration_para(
	struct rt9426a_chip *chip, u8 *curr_offs, u8 *curr_gain, u8 *volt_gain)
{
	/* To-Do: get calibration parameter(curr_offs/curr_gain/volt_gain)
	 * from platform NVM
	 */
	return 0;
}

static int rt9426a_enter_calibration_mode(struct rt9426a_chip *chip)
{
	int i;
	int regval;
	/* 3:calibration retry times */
	int retry_times = 3;
	int ret;
	int j;
	int tick_old = 0;
	int tick_new = 0;

	for (i = 0; i < retry_times; i++) {
		regval = rt9426a_reg_read_word(chip->i2c, RT9426A_REG_FLAG2);

		if ((regval & RT9426A_CALI_MODE_MASK) == RT9426A_CALI_MODE_MASK) {
			hwlog_info("RT9426 is in Calibration Mode\n");
			ret = RT9426A_CALI_MODE_PASS;
			goto out;
		} else {
			if (rt9426a_unseal_wi_retry(chip) == RT9426A_UNSEAL_PASS){
				/* Check System Tick before entering Calibration */
				rt9426a_reg_write_word(chip->i2c,
					RT9426A_REG_CNTL,
					RT9426A_SYS_TICK_ON_CMD);
				rt9426a_reg_write_word(chip->i2c,
					RT9426A_REG_DUMMY, 0x0000);
				/* delay 5ms */
				mdelay(5);
				tick_old = rt9426a_reg_read_word(chip->i2c,
					RT9426A_REG_ADV);
				for (j = 0; j < 1000; j++) {
					tick_new = rt9426a_reg_read_word(chip->i2c,
						RT9426A_REG_ADV);
					if (tick_old != tick_new) {
						mdelay(300);
						break;
					}
					mdelay(5);
				}
				/* Send Cmd to Enter Calibration */
				rt9426a_reg_write_word(chip->i2c,
					RT9426A_REG_CNTL, RT9426A_CALI_ENTR_CMD);
				rt9426a_reg_write_word(chip->i2c,
					RT9426A_REG_DUMMY, 0x0000);
				/* Disable System Tick */
				rt9426a_reg_write_word(chip->i2c,
					RT9426A_REG_CNTL,
					RT9426A_SYS_TICK_OFF_CMD);
				if (i >= 2) {
					hwlog_err("RT9426 Enter Calibration Mode Fail after 3 retries\n");
					ret = RT9426A_CALI_MODE_FAIL;
					goto out;
				}
				/* delay 5ms */
				mdelay(5);
			}
		}
	}
	ret = RT9426A_CALI_MODE_FAIL;
out:
	return ret;
}

static void rt9426a_exit_calibration_mode(struct rt9426a_chip *chip)
{
	rt9426a_reg_write_word(chip->i2c, RT9426A_REG_CNTL,
		RT9426A_CALI_EXIT_CMD);
	rt9426a_reg_write_word(chip->i2c, RT9426A_REG_DUMMY, 0x0000);
}

static void rt9426a_apply_sense_resistor(struct rt9426a_chip *chip)
{
	int op_config10;
	int rsense;

	rsense = chip->pdata->rs_ic_setting / RT9426A_NEW_RS_UNIT;

	if (rt9426a_unseal_wi_retry(chip) == RT9426A_UNSEAL_PASS){
		/* get op_config10 */
		rt9426a_read_page_cmd(chip, RT9426A_PAGE_10);
		op_config10 = rt9426a_reg_read_word(chip->i2c,
			RT9426A_REG_SWINDOW3);
		/* update rsense to op_config10 */
		op_config10 = (RT9426A_PAGE_10 & 0xFF00) | (rsense & 0xFF);
		/* apply op_config10 */
		rt9426a_write_page_cmd(chip, RT9426A_PAGE_10);
		rt9426a_reg_write_word(chip->i2c, RT9426A_REG_SWINDOW3,
			op_config10);
		/* delay 5ms */
		mdelay(5);
	}
}

static void rt9426a_apply_calibration_para(struct rt9426a_chip *chip,
	u8 curr_offs, u8 curr_gain, u8 volt_gain)
{
	if (rt9426a_unseal_wi_retry(chip) == RT9426A_UNSEAL_PASS){
		rt9426a_write_page_cmd(chip, RT9426A_PAGE_0);
		/* set Current system gain & offset */
		if((curr_gain != 0x00) && (curr_gain != 0xFF) &&
				(curr_offs != 0x00) && (curr_offs != 0xFF)){
			rt9426a_reg_write_word(chip->i2c, RT9426A_REG_SWINDOW1,
				curr_gain | (curr_offs << 8));
		}
		else{
			rt9426a_reg_write_word(chip->i2c, RT9426A_REG_SWINDOW1,
				0x8080);
		}
		/* set Voltage system gain */
		if((volt_gain != 0x00) && (volt_gain != 0xFF)) {
			rt9426a_reg_write_word(chip->i2c, RT9426A_REG_SWINDOW7,
				0x88 | (volt_gain << 8));
		}
		else{
			rt9426a_reg_write_word(chip->i2c, RT9426A_REG_SWINDOW7,
				0x8088);
		}
		/* delay 5ms */
		mdelay(5);
	}
}
static void __maybe_unused rt9426a_assign_calibration_para(
	struct rt9426a_chip *chip, u8 curr_offs, u8 curr_gain, u8 volt_gain)
{
	/* Check and assign Current system gain & offset */
	if((curr_gain != 0x00)&&(curr_gain != 0xFF)&&
		(curr_offs != 0x00)&&(curr_offs != 0xFF)){
		chip->pdata->extreg_table[0].data[0] = curr_gain;
		chip->pdata->extreg_table[0].data[1] = curr_offs;
		hwlog_debug("assign calibrated current parameter before use\n");
	}
	else{
		chip->pdata->extreg_table[0].data[0] = 0x80;
		chip->pdata->extreg_table[0].data[1] = 0x80;
		hwlog_debug("assign default current parameter before use\n");
	}
	/* Check and assign Voltage system gain */
	if((volt_gain != 0x00)&&(volt_gain != 0xFF)){
		chip->pdata->extreg_table[0].data[13] = volt_gain;
		hwlog_debug("assign calibrated voltage parameter before use\n");
	}
	else{
		chip->pdata->extreg_table[0].data[13] = 0x80;
		hwlog_debug("assign default voltage parameter before use\n");
	}

}

static int rt9426a_get_curr_by_conversion(struct rt9426a_chip *chip)
{
	int regval = 0;

	if(rt9426a_enter_calibration_mode(chip) == RT9426A_CALI_MODE_PASS){
		/* Start current convertion */
		rt9426a_reg_write_word(chip->i2c, RT9426A_REG_CNTL,
			RT9426A_CURR_CONVERT_CMD);
		rt9426a_reg_write_word(chip->i2c, RT9426A_REG_DUMMY, 0x0000);
		/* delay 50ms */
		mdelay(50);

		/* Get convert result */
		regval  = rt9426a_reg_read_word(chip->i2c, RT9426A_REG_CURR);
		if (regval < 0)
			return -EIO;
		if (regval > 0x7FFF) {
			regval = 0x10000 - regval;
			regval = 0 - regval;
		}
	}

	/* scaling for the current */
	regval = regval * (int)chip->pdata->rs_ic_setting /
		(int)chip->pdata->rs_schematic;
	hwlog_info("CALIB_CURRENT=%d mA\n",regval);

	return regval;
}

static int rt9426a_get_volt_by_conversion(struct rt9426a_chip *chip)
{
	int regval = 0;
	if(rt9426a_enter_calibration_mode(chip) == RT9426A_CALI_MODE_PASS){
		/* Start voltage convertion */
		rt9426a_reg_write_word(chip->i2c, RT9426A_REG_CNTL,
			RT9426A_VOLT_CONVERT_CMD);
		rt9426a_reg_write_word(chip->i2c, RT9426A_REG_DUMMY, 0x0000);
		/* delay 50ms */
		mdelay(50);

		/* Get voltage result */
		regval  = rt9426a_reg_read_word(chip->i2c, RT9426A_REG_VBAT);
	}
	return regval;
}

/* To Disable power path */
static int rt9426a_request_charging_inhibit(bool needInhibit)
{
	if (needInhibit){
		/* To-Do: Turn OFF charging power path to battery */
	} else {
		/* To-Do: Turn ON charging power path to battery */
	}
	return 0;
}

static int rt9426a_enter_shutdown_mode(struct rt9426a_chip *chip)
{
	int regval;
	int loop;

	for (loop = 0; loop < RT9426A_SHUTDOWN_RETRY_TIMES; loop++) {
		regval = rt9426a_reg_read_word(chip->i2c, RT9426A_REG_FLAG2);
		if(regval & RT9426A_SHDN_MASK)
			break;
		rt9426a_reg_write_word(chip->i2c, RT9426A_REG_CNTL,
			RT9426A_SHDN_ENTR_CMD);
		rt9426a_reg_write_word(chip->i2c, RT9426A_REG_DUMMY, 0x0000);
		/* delay 20ms */
		mdelay(20);
	}
	regval = rt9426a_reg_read_word(chip->i2c, RT9426A_REG_FLAG2);
	if (!(regval & RT9426A_SHDN_MASK)) {
		hwlog_err("Enter Shutdown Fail\n");
		return -1;
	}
	hwlog_info("Enter Shutdown Success\n");
	return 0;
}

static int rt9426a_exit_shutdown_mode(struct rt9426a_chip *chip)
{
	int regval;
	int loop;
	int cmd_cnt = 0;

	for (loop = 0; loop < RT9426A_SHUTDOWN_RETRY_TIMES; loop++) {
		regval = rt9426a_reg_read_word(chip->i2c, RT9426A_REG_FLAG2);
		if(!(regval & RT9426A_SHDN_MASK))
			break;
		rt9426a_reg_write_word(chip->i2c, RT9426A_REG_CNTL,
			RT9426A_SHDN_EXIT_CMD);
		rt9426a_reg_write_word(chip->i2c, RT9426A_REG_DUMMY, 0x0000);
		/* delay 250ms */
		mdelay(250);
		hwlog_info("Send Exit Shutdown Cmd Count=%d\n", ++cmd_cnt);
	}
	regval = rt9426a_reg_read_word(chip->i2c, RT9426A_REG_FLAG2);
	if(regval & RT9426A_SHDN_MASK){
		hwlog_info("RT9426A is in Shutdown\n");
		return -1;
	}
	hwlog_info("RT9426A is not in Shutdown\n");

	if (cmd_cnt == 0)
		goto out;

	/* Power path control check */
	regval = rt9426a_get_current(chip);
	if(regval > 0){
		/* Disable power path */
		rt9426a_request_charging_inhibit(true);
		hwlog_info("RT9426A request to enable charging inhibit\n");
		mdelay(1250);
	}
	/* Send QS Command to get INI SOC */
	rt9426a_reg_write_word(chip->i2c, RT9426A_REG_CNTL, 0x4000);
	rt9426a_reg_write_word(chip->i2c, RT9426A_REG_DUMMY, 0x0000);
	hwlog_info("Send QS after exiting Shutdown\n");
	/* delay 5ms */
	mdelay(5);
	/* Power path recover check */
	if(regval > 0){
		/* To Enable power path */
		rt9426a_request_charging_inhibit(false);
		hwlog_info("RT9426A request to disable charging inhibit\n");
	}
out:
	return 0;
}

static ssize_t rt_fg_show_attrs(struct device *,
	struct device_attribute *, char *);
static ssize_t rt_fg_store_attrs(struct device *,
	struct device_attribute *, const char *,
				 size_t count);

#define RT_FG_ATTR(_name)				\
{							\
	.attr = {.name = #_name, .mode = 0664},		\
	.show = rt_fg_show_attrs,			\
	.store = rt_fg_store_attrs,			\
}

static struct device_attribute rt_fuelgauge_attrs[] = {
	RT_FG_ATTR(fg_temp),
	RT_FG_ATTR(volt),
	RT_FG_ATTR(curr),
	RT_FG_ATTR(update),
	RT_FG_ATTR(ocv_table),
	RT_FG_ATTR(enter_sleep),
	RT_FG_ATTR(exit_sleep),
	RT_FG_ATTR(set_sleep_duty),
	RT_FG_ATTR(DBP0),
	RT_FG_ATTR(DBP1),
	RT_FG_ATTR(DBP2),
	RT_FG_ATTR(DBP3),
	RT_FG_ATTR(DBP4),
	RT_FG_ATTR(DBP5),
	RT_FG_ATTR(DBP6),
	RT_FG_ATTR(DBP7),
	RT_FG_ATTR(DBP8),
	RT_FG_ATTR(DBP9),
	RT_FG_ATTR(WCNTL),
	RT_FG_ATTR(WEXTCNTL),
	RT_FG_ATTR(WSW1),
	RT_FG_ATTR(WSW2),
	RT_FG_ATTR(WSW3),
	RT_FG_ATTR(WSW4),
	RT_FG_ATTR(WSW5),
	RT_FG_ATTR(WSW6),
	RT_FG_ATTR(WSW7),
	RT_FG_ATTR(WSW8),
	RT_FG_ATTR(WTEMP),
	RT_FG_ATTR(UNSEAL),
	RT_FG_ATTR(FG_SET_TEMP),
	RT_FG_ATTR(FG_RESET),
	RT_FG_ATTR(CALIB_SECURE),
	RT_FG_ATTR(CALIB_RSENSE),
	RT_FG_ATTR(CALIB_CURRENT),
	RT_FG_ATTR(CALIB_VOLTAGE),
	RT_FG_ATTR(CALIB_FACTOR),
	RT_FG_ATTR(FORCE_SHUTDOWN),
	RT_FG_ATTR(BCCOMP),
};

enum {
	FG_TEMP = 0,
	FG_VOLT,
	FG_CURR,
	FG_UPDATE,
	OCV_TABLE,
	ENTER_sleep,
	EXIT_sleep,
	SET_sleep_DUTY,
	DBP0,
	DBP1,
	DBP2,
	DBP3,
	DBP4,
	DBP5,
	DBP6,
	DBP7,
	DBP8,
	DBP9,
	WCNTL,
	WEXTCNTL,
	WSW1,
	WSW2,
	WSW3,
	WSW4,
	WSW5,
	WSW6,
	WSW7,
	WSW8,
	WTEMP,
	UNSEAL,
	FG_SET_TEMP,
	FG_RESET,
	CALIB_SECURE,
	CALIB_RSENSE,
	CALIB_CURRENT,
	CALIB_VOLTAGE,
	CALIB_FACTOR,
	FORCE_SHUTDOWN,
	BCCOMP,
};

static ssize_t rt_fg_show_attrs(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct rt9426a_chip *chip = dev_get_drvdata(dev->parent);
	const ptrdiff_t offset = attr - rt_fuelgauge_attrs;
	int i = 0;

	switch (offset) {
	case FG_TEMP:
		i += scnprintf(buf + i, PAGE_SIZE - i, "%d\n", chip->btemp);
		break;
	case FG_VOLT:
		i += scnprintf(buf + i, PAGE_SIZE - i, "%d\n", chip->bvolt);
		break;
	case FG_CURR:
		i += scnprintf(buf + i, PAGE_SIZE - i, "%d\n", chip->bcurr);
		break;
	case CALIB_SECURE:
		i = scnprintf(buf, PAGE_SIZE,
			chip->calib_unlock ? "Unlocked\n" : "Locked\n");
		break;
	case CALIB_CURRENT:
		if (!chip->calib_unlock)
			return -EACCES;
		rt9426a_enter_calibration_mode(chip);
		i = scnprintf(buf, PAGE_SIZE, "%d\n",
			rt9426a_get_curr_by_conversion(chip));
		rt9426a_exit_calibration_mode(chip);
		break;
	case CALIB_VOLTAGE:
		if (!chip->calib_unlock)
			return -EACCES;
		rt9426a_enter_calibration_mode(chip);
		i = scnprintf(buf, PAGE_SIZE, "%d\n",
			rt9426a_get_volt_by_conversion(chip));
		rt9426a_exit_calibration_mode(chip);
		break;
	case BCCOMP:
		i = scnprintf(buf, PAGE_SIZE, "%d\n", rt9426a_get_bccomp(chip));
		break;
	case FG_UPDATE:
	case OCV_TABLE:
	case ENTER_sleep:
	case EXIT_sleep:
	case SET_sleep_DUTY:
	case DBP0:
	case DBP1:
	case DBP2:
	case DBP3:
	case DBP4:
	case DBP5:
	case DBP6:
	case DBP7:
	case DBP8:
	case DBP9:
	case WCNTL:
	case WEXTCNTL:
	case WSW1:
	case WSW2:
	case WSW3:
	case WSW4:
	case WSW5:
	case WSW6:
	case WSW7:
	case WSW8:
	case WTEMP:
	case UNSEAL:
	case FG_SET_TEMP:
	case FG_RESET:
	default:
		i = -EINVAL;
		break;
	}
	return i;
}

static ssize_t rt_fg_store_attrs(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct rt9426a_chip *chip = dev_get_drvdata(dev->parent);
	const ptrdiff_t offset = attr - rt_fuelgauge_attrs;
	int ret = 0;
	int x;
	int y;
	char *org = NULL;
	char *token = NULL;
	char *cur = NULL;
	int temp[8];
	int val;

	switch (offset) {
	case FG_TEMP:
		ret = kstrtoint(buf, 10, &val);
		if (ret < 0) {
			hwlog_err("get FG_TEMP paremters fail\n");
			ret = -EINVAL;
		}
		chip->btemp = val;
		ret = count;
		break;
	case FG_VOLT:
		ret = kstrtoint(buf, 10, &val);
		if (ret < 0) {
			hwlog_err("get FG_VOLT paremters fail\n");
			ret = -EINVAL;
		}
		chip->bvolt = val;
		ret = count;
		break;
	case FG_CURR:
		ret = kstrtoint(buf, 10, &val);
		if (ret < 0) {
			hwlog_err("get FG_CURR paremters fail\n");
			ret = -EINVAL;
		}
		chip->bcurr = val;
		ret = count;
		break;
	case FG_UPDATE:
		if (sscanf(buf, "%x\n", &x) == 1 && x == 1) {
			rt9426a_update_info(chip);
			ret = count;
		} else
			ret = -EINVAL;
		break;
	case OCV_TABLE:
		if (sscanf(buf, "%x\n", &x) == 1 && x == 1) {
			for (x = 0; x < RT9426A_OCV_ROW_SIZE; x++) {
				rt9426a_reg_write_word(chip->i2c,
					RT9426A_REG_BDCNTL, 0xCA00 + x);
				rt9426a_reg_write_word(chip->i2c,
					RT9426A_REG_BDCNTL, 0xCA00 + x);
				for (y = 0; y < RT9426A_OCV_COL_SIZE; y++)
					temp[y] = rt9426a_reg_read_word(chip->i2c,
						RT9426A_REG_SWINDOW1 + y * 2);

				hwlog_debug("fg_ocv_table_%d\n", x);
				hwlog_debug( "<0x%x 0x%x 0x%x 0x%x>\n",
					 temp[0], temp[1], temp[2], temp[3]);
				hwlog_debug("<0x%x 0x%x 0x%x 0x%x>\n",
					 temp[4], temp[5], temp[6], temp[7]);
			}
			ret = count;
		} else
			ret = -EINVAL;
		break;
	case ENTER_sleep:
		if (sscanf(buf, "%x\n", &x) == 1 && x == 1) {
			rt9426a_enter_sleep(chip);
			hwlog_info("ENTER_sleep SLP_STS=%d\n",
				rt9426a_reg_read_word(chip->i2c,
				RT9426A_REG_FLAG2) >> 15);
			ret = count;
		} else
			ret = -EINVAL;
		break;
	case EXIT_sleep:
		if (sscanf(buf, "%x\n", &x) == 1 && x == 1) {
			rt9426a_exit_sleep(chip);
			hwlog_info("EXIT_sleep SLP_STS=%d\n",
				rt9426a_reg_read_word(chip->i2c,
				RT9426A_REG_FLAG2) >> 15);
			ret = count;
		} else
			ret = -EINVAL;
		break;
	case SET_sleep_DUTY:
		ret = kstrtoint(buf, 10, &val);
		if (ret < 0) {
			hwlog_err("SET_sleep_DUTY get paremters fail\n");
			ret = -EINVAL;
		}
		rt9426a_sleep_duty_set(chip, val);
		rt9426a_sleep_duty_read(chip);
		ret = count;
		break;
	case DBP0:
		if (sscanf(buf, "%x\n", &x) == 1 && x == 1) {
			rt9426a_read_page_cmd(chip, RT9426A_PAGE_0);
			for (x = 0; x < 8; x++)
				temp[x] = rt9426a_reg_read_word(chip->i2c,
					RT9426A_REG_SWINDOW1 + x * 2);

			hwlog_debug("fg_data_block_page0");
			hwlog_debug("0x41:<0x%x>\n", temp[0]);
			hwlog_debug("0x43:<0x%x>\n", temp[1]);
			hwlog_debug("0x45:<0x%x>\n", temp[2]);
			hwlog_debug("0x47:<0x%x>\n", temp[3]);
			hwlog_debug("0x49:<0x%x>\n", temp[4]);
			hwlog_debug("0x4B:<0x%x>\n", temp[5]);
			hwlog_debug("0x4D:<0x%x>\n", temp[6]);
			hwlog_debug("0x4F:<0x%x>\n", temp[7]);
			ret = count;
		} else
			ret = -EINVAL;
		break;
	case DBP1:
		if (sscanf(buf, "%x\n", &x) == 1 && x == 1) {
			rt9426a_read_page_cmd(chip, RT9426A_PAGE_1);
			for (x = 0; x < 8; x++)
				temp[x] = rt9426a_reg_read_word(chip->i2c,
					RT9426A_REG_SWINDOW1 + x * 2);

			hwlog_debug("fg_data_block_page1");
			hwlog_debug("0x41:<0x%x>\n", temp[0]);
			hwlog_debug("0x43:<0x%x>\n", temp[1]);
			hwlog_debug("0x45:<0x%x>\n", temp[2]);
			hwlog_debug("0x47:<0x%x>\n", temp[3]);
			hwlog_debug("0x49:<0x%x>\n", temp[4]);
			hwlog_debug("0x4B:<0x%x>\n", temp[5]);
			hwlog_debug("0x4D:<0x%x>\n", temp[6]);
			hwlog_debug("0x4F:<0x%x>\n", temp[7]);
			ret = count;
		} else
			ret = -EINVAL;
		break;
	case DBP2:
		if (sscanf(buf, "%x\n", &x) == 1 && x == 1) {
			rt9426a_read_page_cmd(chip, RT9426A_PAGE_2);
			for (x = 0; x < 8; x++)
				temp[x] = rt9426a_reg_read_word(chip->i2c,
					RT9426A_REG_SWINDOW1 + x * 2);

			hwlog_debug("fg_data_block_page2");
			hwlog_debug("0x41:<0x%x>\n", temp[0]);
			hwlog_debug("0x43:<0x%x>\n", temp[1]);
			hwlog_debug("0x45:<0x%x>\n", temp[2]);
			hwlog_debug("0x47:<0x%x>\n", temp[3]);
			hwlog_debug("0x49:<0x%x>\n", temp[4]);
			hwlog_debug("0x4B:<0x%x>\n", temp[5]);
			hwlog_debug("0x4D:<0x%x>\n", temp[6]);
			hwlog_debug("0x4F:<0x%x>\n", temp[7]);
			ret = count;
		} else
			ret = -EINVAL;
		break;
	case DBP3:
		if (sscanf(buf, "%x\n", &x) == 1 && x == 1) {
			rt9426a_read_page_cmd(chip, RT9426A_PAGE_3);
			for (x = 0; x < RT9426A_BLOCK_PAGE_SIZE; x++)
				temp[x] = rt9426a_reg_read_word(chip->i2c,
					RT9426A_REG_SWINDOW1 + x * 2);

			hwlog_debug("fg_data_block_page3");
			hwlog_debug("0x41:<0x%x>\n", temp[0]);
			hwlog_debug("0x43:<0x%x>\n", temp[1]);
			hwlog_debug("0x45:<0x%x>\n", temp[2]);
			hwlog_debug("0x47:<0x%x>\n", temp[3]);
			hwlog_debug("0x49:<0x%x>\n", temp[4]);
			hwlog_debug("0x4B:<0x%x>\n", temp[5]);
			hwlog_debug("0x4D:<0x%x>\n", temp[6]);
			hwlog_debug("0x4F:<0x%x>\n", temp[7]);
			ret = count;
		} else
			ret = -EINVAL;
		break;
	case DBP4:
		if (sscanf(buf, "%x\n", &x) == 1 && x == 1) {
			rt9426a_read_page_cmd(chip, RT9426A_PAGE_4);
			for (x = 0; x < RT9426A_BLOCK_PAGE_SIZE; x++)
				temp[x] = rt9426a_reg_read_word(chip->i2c,
					RT9426A_REG_SWINDOW1 + x * 2);

			hwlog_debug("fg_data_block_page4");
			hwlog_debug("0x41:<0x%x>\n", temp[0]);
			hwlog_debug("0x43:<0x%x>\n", temp[1]);
			hwlog_debug("0x45:<0x%x>\n", temp[2]);
			hwlog_debug("0x47:<0x%x>\n", temp[3]);
			hwlog_debug("0x49:<0x%x>\n", temp[4]);
			hwlog_debug("0x4B:<0x%x>\n", temp[5]);
			hwlog_debug("0x4D:<0x%x>\n", temp[6]);
			hwlog_debug("0x4F:<0x%x>\n", temp[7]);
			ret = count;
		} else
			ret = -EINVAL;
		break;
	case DBP5:
		if (sscanf(buf, "%x\n", &x) == 1 && x == 1) {
			rt9426a_read_page_cmd(chip, RT9426A_PAGE_5);
			for (x = 0; x < RT9426A_BLOCK_PAGE_SIZE; x++)
				temp[x] = rt9426a_reg_read_word(chip->i2c,
					RT9426A_REG_SWINDOW1 + x * 2);

			hwlog_debug("fg_data_block_page5");
			hwlog_debug("0x41:<0x%x>\n", temp[0]);
			hwlog_debug("0x43:<0x%x>\n", temp[1]);
			hwlog_debug("0x45:<0x%x>\n", temp[2]);
			hwlog_debug("0x47:<0x%x>\n", temp[3]);
			hwlog_debug("0x49:<0x%x>\n", temp[4]);
			hwlog_debug("0x4B:<0x%x>\n", temp[5]);
			hwlog_debug("0x4D:<0x%x>\n", temp[6]);
			hwlog_debug("0x4F:<0x%x>\n", temp[7]);
			ret = count;
		} else
			ret = -EINVAL;
		break;
	case DBP6:
		if (sscanf(buf, "%x\n", &x) == 1 && x == 1) {
			rt9426a_read_page_cmd(chip, RT9426A_PAGE_6);
			for (x = 0; x < RT9426A_BLOCK_PAGE_SIZE; x++)
				temp[x] = rt9426a_reg_read_word(chip->i2c,
					RT9426A_REG_SWINDOW1 + x * 2);

			hwlog_debug("fg_data_block_page6");
			hwlog_debug("0x41:<0x%x>\n", temp[0]);
			hwlog_debug("0x43:<0x%x>\n", temp[1]);
			hwlog_debug("0x45:<0x%x>\n", temp[2]);
			hwlog_debug("0x47:<0x%x>\n", temp[3]);
			hwlog_debug("0x49:<0x%x>\n", temp[4]);
			hwlog_debug("0x4B:<0x%x>\n", temp[5]);
			hwlog_debug("0x4D:<0x%x>\n", temp[6]);
			hwlog_debug("0x4F:<0x%x>\n", temp[7]);
			ret = count;
		} else
			ret = -EINVAL;
		break;
	case DBP7:
		if (sscanf(buf, "%x\n", &x) == 1 && x == 1) {
			rt9426a_read_page_cmd(chip, RT9426A_PAGE_7);
			for (x = 0; x < RT9426A_BLOCK_PAGE_SIZE; x++)
				temp[x] = rt9426a_reg_read_word(chip->i2c,
					RT9426A_REG_SWINDOW1 + x * 2);

			hwlog_debug("fg_data_block_page7");
			hwlog_debug("0x41:<0x%x>\n", temp[0]);
			hwlog_debug("0x43:<0x%x>\n", temp[1]);
			hwlog_debug("0x45:<0x%x>\n", temp[2]);
			hwlog_debug("0x47:<0x%x>\n", temp[3]);
			hwlog_debug("0x49:<0x%x>\n", temp[4]);
			hwlog_debug("0x4B:<0x%x>\n", temp[5]);
			hwlog_debug("0x4D:<0x%x>\n", temp[6]);
			hwlog_debug("0x4F:<0x%x>\n", temp[7]);
			ret = count;
		} else
			ret = -EINVAL;
		break;
	case DBP8:
		if (sscanf(buf, "%x\n", &x) == 1 && x == 1) {
			rt9426a_read_page_cmd(chip, RT9426A_PAGE_8);
			for (x = 0; x < RT9426A_BLOCK_PAGE_SIZE; x++)
				temp[x] = rt9426a_reg_read_word(chip->i2c,
					RT9426A_REG_SWINDOW1 + x * 2);

			hwlog_debug("fg_data_block_page8");
			hwlog_debug("0x41:<0x%x>\n", temp[0]);
			hwlog_debug("0x43:<0x%x>\n", temp[1]);
			hwlog_debug("0x45:<0x%x>\n", temp[2]);
			hwlog_debug("0x47:<0x%x>\n", temp[3]);
			hwlog_debug("0x49:<0x%x>\n", temp[4]);
			hwlog_debug("0x4B:<0x%x>\n", temp[5]);
			hwlog_debug("0x4D:<0x%x>\n", temp[6]);
			hwlog_debug("0x4F:<0x%x>\n", temp[7]);
			ret = count;
		} else
			ret = -EINVAL;
		break;
	case DBP9:
		if (sscanf(buf, "%x\n", &x) == 1 && x == 1) {
			rt9426a_read_page_cmd(chip, RT9426A_PAGE_9);
			mdelay(5);
			for (x = 0; x < RT9426A_BLOCK_PAGE_SIZE; x++)
				temp[x] = rt9426a_reg_read_word(chip->i2c,
					RT9426A_REG_SWINDOW1 + x*2);

			hwlog_debug("fg_data_block_page9");
			hwlog_debug("0x41:<0x%x>\n", temp[0]);
			hwlog_debug("0x43:<0x%x>\n", temp[1]);
			hwlog_debug("0x45:<0x%x>\n", temp[2]);
			hwlog_debug("0x47:<0x%x>\n", temp[3]);
			hwlog_debug("0x49:<0x%x>\n", temp[4]);
			hwlog_debug("0x4B:<0x%x>\n", temp[5]);
			hwlog_debug("0x4D:<0x%x>\n", temp[6]);
			hwlog_debug("0x4F:<0x%x>\n", temp[7]);
			ret = count;
		} else
			ret = -EINVAL;
		break;
	case WCNTL:
		ret = kstrtoint(buf, 10, &val);
		if (ret < 0) {
			hwlog_err("get paremters fail\n");
			ret = -EINVAL;
		}
		rt9426a_reg_write_word(chip->i2c, RT9426A_REG_CNTL, val);
		rt9426a_reg_write_word(chip->i2c, RT9426A_REG_DUMMY, 0x0000);
		ret = count;
		break;
	case WEXTCNTL:
		ret = kstrtoint(buf, 10, &val);
		if (ret < 0) {
			hwlog_err("get paremters fail\n");
			ret = -EINVAL;
		}
		rt9426a_reg_write_word(chip->i2c, RT9426A_REG_BDCNTL, val);
		rt9426a_reg_write_word(chip->i2c, RT9426A_REG_BDCNTL, val);
		ret = count;
		break;
	case WSW1:
		ret = kstrtoint(buf, 10, &val);
		if (ret < 0) {
			hwlog_err("get paremters fail\n");
			ret = -EINVAL;
		}
		rt9426a_reg_write_word(chip->i2c, RT9426A_REG_SWINDOW1, val);
		rt9426a_reg_write_word(chip->i2c, RT9426A_REG_DUMMY, 0x0000);
		ret = count;
		break;
	case WSW2:
		ret = kstrtoint(buf, 10, &val);
		if (ret < 0) {
			hwlog_err("get paremters fail\n");
			ret = -EINVAL;
		}
		rt9426a_reg_write_word(chip->i2c, RT9426A_REG_SWINDOW2, val);
		rt9426a_reg_write_word(chip->i2c, RT9426A_REG_DUMMY, 0x0000);
		ret = count;
		break;
	case WSW3:
		ret = kstrtoint(buf, 10, &val);
		if (ret < 0) {
			hwlog_err("get paremters fail\n");
			ret = -EINVAL;
		}
		rt9426a_reg_write_word(chip->i2c, RT9426A_REG_SWINDOW3, val);
		rt9426a_reg_write_word(chip->i2c, RT9426A_REG_DUMMY, 0x0000);
		ret = count;
		break;
	case WSW4:
		ret = kstrtoint(buf, 10, &val);
		if (ret < 0) {
			hwlog_err("get paremters fail\n");
			ret = -EINVAL;
		}
		rt9426a_reg_write_word(chip->i2c, RT9426A_REG_SWINDOW4, val);
		rt9426a_reg_write_word(chip->i2c, RT9426A_REG_DUMMY, 0x0000);
		ret = count;
		break;
	case WSW5:
		ret = kstrtoint(buf, 10, &val);
		if (ret < 0) {
			hwlog_err("get paremters fail\n");
			ret = -EINVAL;
		}
		rt9426a_reg_write_word(chip->i2c, RT9426A_REG_SWINDOW5, val);
		rt9426a_reg_write_word(chip->i2c, RT9426A_REG_DUMMY, 0x0000);
		ret = count;
		break;
	case WSW6:
		ret = kstrtoint(buf, 10, &val);
		if (ret < 0) {
			hwlog_err("get paremters fail\n");
			ret = -EINVAL;
		}
		rt9426a_reg_write_word(chip->i2c, RT9426A_REG_SWINDOW6, val);
		rt9426a_reg_write_word(chip->i2c, RT9426A_REG_DUMMY, 0x0000);
		ret = count;
		break;
	case WSW7:
		ret = kstrtoint(buf, 10, &val);
		if (ret < 0) {
			hwlog_err("get paremters fail\n");
			ret = -EINVAL;
		}
		rt9426a_reg_write_word(chip->i2c, RT9426A_REG_SWINDOW7, val);
		rt9426a_reg_write_word(chip->i2c, RT9426A_REG_DUMMY, 0x0000);
		ret = count;
		break;
	case WSW8:
		ret = kstrtoint(buf, 10, &val);
		if (ret < 0) {
			hwlog_err("get paremters fail\n");
			ret = -EINVAL;
		}
		rt9426a_reg_write_word(chip->i2c, RT9426A_REG_SWINDOW8, val);
		rt9426a_reg_write_word(chip->i2c, RT9426A_REG_DUMMY, 0x0000);
		ret = count;
		break;
	case WTEMP:
		ret = kstrtoint(buf, 10, &val);
		if (ret < 0) {
			hwlog_err("get paremters fail\n");
			ret = -EINVAL;
		}
		rt9426a_reg_write_word(chip->i2c,
			RT9426A_REG_TEMP, ((val*10)+2732));
		rt9426a_reg_write_word(chip->i2c, RT9426A_REG_DUMMY, 0x0000);
		ret = count;
		break;
	case UNSEAL:
		ret = kstrtoint(buf, 10, &val);
		if (ret < 0) {
			hwlog_err("get paremters fail\n");
			ret = -EINVAL;
		}
		ret = rt9426a_reg_read_word(chip->i2c, RT9426A_REG_FLAG3);
		if ((ret & 0x0001) == 0) {
			/* Unseal RT9426A */
			rt9426a_reg_write_word(chip->i2c, RT9426A_REG_CNTL,
						(RT9426A_Unseal_Key & 0xffff)); /* get low 2 bytes */
			rt9426a_reg_write_word(chip->i2c, RT9426A_REG_CNTL,
						(RT9426A_Unseal_Key >> 16)); /* get low 2 bytes */
			rt9426a_reg_write_word(chip->i2c,
						RT9426A_REG_DUMMY, 0x0000);
			/* delay 10ms */
			mdelay(10);

			ret = rt9426a_reg_read_word(chip->i2c, RT9426A_REG_FLAG3);
			if ((ret & 0x0001) == 0)
				hwlog_info("RT9426A Unseal Fail\n");
			else
				hwlog_debug("RT9426A Unseal Pass\n");
		} else
			hwlog_debug("RT9426A Unseal Pass\n");

		/* Unseal RT9426A */
		ret = count;
		break;
	case FG_SET_TEMP:
		ret = kstrtoint(buf, 10, &val);
		if (ret < 0) {
			hwlog_err("get paremters fail\n");
			ret = -EINVAL;
		}
		rt9426a_temperature_set(chip, val);
		ret = count;
		break;
	case FG_RESET:
		rt9426a_reset(chip);
		ret = count;
		break;
	case CALIB_SECURE:
		if (kstrtoint(buf, 0, &x))
			return -EINVAL;
		chip->calib_unlock = (x == 5526789);
		ret = count;
		break;
	case CALIB_RSENSE:
		if (!chip->calib_unlock)
			return -EACCES;
		/* set rsense according to dtsi */
		rt9426a_apply_sense_resistor(chip);
		rt9426a_apply_calibration_para(chip, 0x80, 0x80, 0x80);
		ret = count;
		break;
	case CALIB_FACTOR:
		if (!chip->calib_unlock)
			return -EACCES;
		org = kstrdup(buf, GFP_KERNEL);
		if (!org)
			return -ENOMEM;
		cur = org;
		x = 0;
		while ((token = strsep(&cur, ",")) != NULL) {
			if (kstrtoint(token, 0, &temp[x]))
				break;
			if (++x >= 3)
				break;
		}
		kfree(org);
		if (x != 3)
			return -EINVAL;
		rt9426a_apply_calibration_para(chip, temp[0], temp[1], temp[2]);
		ret = count;
		break;
	case FORCE_SHUTDOWN:
		if (kstrtoint(buf, 0, &x) || x < 0 || x > 1)
			return -EINVAL;

		ret = x ? rt9426a_enter_shutdown_mode(chip) :
			rt9426a_exit_shutdown_mode(chip);
		if (ret)
			return ret;
		ret = count;
		break;
	case BCCOMP:
		if (kstrtoint(buf, 0, &x) || x < 0xCCD || x > 0x7999)
			return -EINVAL;

		ret = rt9426a_set_bccomp(chip, x);
		if (ret)
			return ret;
		ret = count;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int rt_fg_create_attrs(struct device *dev)
{
	int i;
	int rc;

	for (i = 0; i < ARRAY_SIZE(rt_fuelgauge_attrs); i++) {
		rc = device_create_file(dev, &rt_fuelgauge_attrs[i]);
		if (rc)
			goto create_attrs_failed;
	}
	goto create_attrs_succeed;

create_attrs_failed:
	hwlog_err("%s: failed (%d)\n", rc);
	while (i--)
		device_remove_file(dev, &rt_fuelgauge_attrs[i]);
create_attrs_succeed:
	return rc;
}

static int rt9426a_irq_enable(struct rt9426a_chip *chip, bool enable)
{
	int regval;
	int opcfg2;

	opcfg2 = chip->pdata->extreg_table[1].data[3] * 256 +
		chip->pdata->extreg_table[1].data[2];
	hwlog_info("OPCFG2=0x%04x\n", opcfg2);

	if (rt9426a_unseal_wi_retry(chip) == RT9426A_UNSEAL_PASS) {
		rt9426a_write_page_cmd(chip, RT9426A_PAGE_1);
		rt9426a_reg_write_word(chip->i2c, RT9426A_REG_SWINDOW2,
			enable ? opcfg2 : 0);
		rt9426a_reg_write_word(chip->i2c, RT9426A_REG_DUMMY, 0x0000);
		/* delay 5ms */
		mdelay(5);
		/* if disable, force clear irq status */
		if (!enable) {
			regval = rt9426a_reg_read_word(chip->i2c,
				RT9426A_REG_IRQ);
			hwlog_info("previous irq status 0x%04x\n", regval);
		}
	}

	return 0;
}

static int rt9426a_irq_init(struct rt9426a_chip *chip)
{
	int rc = 0;

	hwlog_info("%s\n", __func__);
	chip->alert_irq = chip->i2c->irq;
	hwlog_info("irq = %d\n", chip->alert_irq);
	rc = devm_request_threaded_irq(chip->dev, chip->alert_irq, NULL,
		rt9426a_irq_handler, IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
		"rt9426a_fg_irq", chip);
	if (rc < 0) {
		hwlog_err("irq register failed chip->alert_irq=%d rc=%d\n",
			chip->alert_irq, rc);
		return rc;
	}
	device_init_wakeup(chip->dev, true);

	return 0;
}

static int rt9426a_irq_deinit(struct rt9426a_chip *chip)
{
	device_init_wakeup(chip->dev, false);
	return 0;
}

static int rt9426a_apply_pdata(struct rt9426a_chip *chip)
{
	int regval;
	int i;
	/* sometimes need retry */
	int retry_times = 30;
	int ret = RT9426A_INIT_PASS;
	int i2c_retry_times;
	int reg_flag2 = 0;
	int reg_flag3 = 0;
	int total_checksum_retry = 0;
	u16 total_checksum_target = 0;
	u16 fg_total_checksum = 0;
	u16 page_checksum = 0;
	u16 page_idx_cmd = 0;
	u16 extend_reg_data = 0;
	u8 page_idx = 0;
	u8 extend_reg_cmd_addr = 0;
	u8 array_idx =  0;
	int volt_now = 0;
	int fd_vth_now = 0;
	int fd_threshold = 0;
	int wt_ocv_result = RT9426A_WRITE_OCV_FAIL;
	const u32 *pval = NULL;

	hwlog_info("%s\n", __func__);
	/* read ocv_index before using it ; 2021-02-26 */
	chip->ocv_index = rt9426a_reg_read_word(chip->i2c, RT9426A_REG_RSVD2);
	if (chip->ocv_index < OCV_INDEX_MIN || chip->ocv_index > OCV_INDEX_MAX) {
		hwlog_err("chip->ocv_index = %d\n", chip->ocv_index);
		chip->ocv_index = 0;
	}
	/* update ocv_checksum_dtsi after ocv_index updated ; 2021-02-26 */
	chip->ocv_checksum_dtsi = *((u32 *)(chip->pdata->ocv_table) +
		chip->ocv_index * 80 + RT9426A_IDX_OF_OCV_CKSUM);
	/* add for aging cv ; udpate fcc before writing ; 2021-01-18 */
	chip->pdata->extreg_table[2].data[12] =
		chip->pdata->fcc[chip->ocv_index] & 0xff;
	chip->pdata->extreg_table[2].data[13] =
		(chip->pdata->fcc[chip->ocv_index] >> 8) & 0xff;
	/* add for aging cv ; udpate fc_vth before writing ; 2021-01-18 */
	chip->pdata->extreg_table[5].data[4]=
		chip->pdata->fc_vth[chip->ocv_index];

	for (i = 0 ; i < retry_times ; i++) {
		/* Check [RDY]@Reg-Flag2 & [RI]@Reg-Flag3 */
		reg_flag2 = rt9426a_reg_read_word(chip->i2c, RT9426A_REG_FLAG2);
		reg_flag3 = rt9426a_reg_read_word(chip->i2c, RT9426A_REG_FLAG3);
		if (((reg_flag2&RT9426A_RDY_MASK) && (reg_flag2!=0xFFFF))||
			((reg_flag3&RT9426A_RI_MASK) && (reg_flag3!=0xFFFF))) {
			/* Exit Shutdown Mode before any action */
			/* rt9426a_exit_shutdown_mode(chip); */
			/* Check [RI]@Reg-Flag3 & fg total checksum */
			regval = rt9426a_get_checksum(chip);
			/* get ocv checksum from fg ic before applying ocv table */
			rt9426a_get_ocv_checksum(chip);
			if (reg_flag3&RT9426A_RI_MASK) {
				hwlog_info("Init RT9426A by [RI]\n");
				break;
			} else if (regval!=(rt9426a_calculate_checksum_crc(chip))){
				hwlog_info("Force Init RT9426A by total checksum\n");
				break;
			} else if (chip->ocv_checksum_ic != chip->ocv_checksum_dtsi){
				hwlog_info("Force Init RT9426A by OCV checksum\n");
				break;
			} else{
				hwlog_info("No need to Init RT9426A\n");
				ret = RT9426A_INIT_BYPASS;
				break;
			}
		} else if (i == retry_times-1) {
			hwlog_info("RT9426A Check [RDY] fail.\n");
		}
		mdelay(5);
	}
	/* Bypass to apply RT9426A parameter */
	if(ret < RT9426A_INIT_PASS)
		goto BYPASS_EXT_REG;

	if (rt9426a_unseal_wi_retry(chip) == RT9426A_UNSEAL_FAIL) {
		ret = RT9426A_INIT_UNSEAL_ERR;
		goto END_INIT;
	}

	/* NOTICE!! Please follow below setting sequence & order
	 * write ocv table only when checksum matched for aging cv
	 */
	{
		pval = (u32 *)(chip->pdata->ocv_table) +
	                  chip->ocv_index * 80 + RT9426A_IDX_OF_OCV_CKSUM;
		if(*pval == chip->ocv_checksum_dtsi)
			wt_ocv_result = rt9426a_write_ocv_table(chip);
	}
	/* Read Reg IRQ to reinital Alert pin state */
	rt9426a_reg_read_word(chip->i2c, RT9426A_REG_IRQ);

	/* get calibration parameter */
	// rt9426a_get_calibration_para(chip, &target_curr_offs,
	//	&target_curr_gain, &target_volt_gain);
	/* assign calibration parameter */
	// rt9426a_assign_calibration_para(chip, target_curr_offs,
	//	target_curr_gain, target_volt_gain);

TOTAL_CHECKSUM_CHECK:

	/* Calculate Checksum & CRC */
	total_checksum_target = rt9426a_calculate_checksum_crc(chip);

	/* Get Total Checksum from RT9426A */
	fg_total_checksum = rt9426a_get_checksum(chip);

	/* Compare Total Checksum with Target */
	hwlog_info("Total Checksum: Target = 0x%x, Result = 0x%x\n",
		 total_checksum_target, fg_total_checksum);

	if (fg_total_checksum == total_checksum_target) {
		ret = RT9426A_INIT_BYPASS;
		goto BYPASS_EXT_REG;
	}
	else if (total_checksum_retry > 5) {
		hwlog_info("Write RT9426A Extend Register Fail\n");
		ret = RT9426A_INIT_CKSUM_ERR;
		retry_times = 0;
		goto SET_SEAL_CMD;
	}
	total_checksum_retry++;

	/* Set Ext Register */
	for (page_idx = 0; page_idx < 16; page_idx++) {
		if((page_idx != 4) && (page_idx != 9)) {
			/* retry count for i2c fail only */
			i2c_retry_times = 0;
PAGE_CHECKSUM1:
			/* Send Write Page Command */
			page_idx_cmd = RT9426A_WPAGE_CMD | page_idx;
			if (rt9426a_reg_write_word(chip->i2c, RT9426A_REG_BDCNTL,
				page_idx_cmd) >= 0) {
				rt9426a_reg_write_word(chip->i2c,
					RT9426A_REG_BDCNTL, page_idx_cmd);
				retry_times = 0;
PAGE_CHECKSUM2:
				mdelay(5);
				/* Get Page Checksum from RT9426A */
				page_checksum = rt9426a_reg_read_word(chip->i2c,
					RT9426A_REG_SWINDOW9);
				if (page_idx < 4)
					array_idx = page_idx;
				else if ((page_idx > 4) && (page_idx < 9))
					array_idx = page_idx - 1;
				else if (page_idx > 9)
					array_idx = page_idx - 2;

				/* resend write page cmd again if failed */
				if (retry_times > 0) {
					rt9426a_reg_write_word(chip->i2c,
						RT9426A_REG_BDCNTL, page_idx_cmd);
					rt9426a_reg_write_word(chip->i2c,
						RT9426A_REG_BDCNTL, page_idx_cmd);
					mdelay(1);
				}

				if ((page_checksum != g_PAGE_CHKSUM[array_idx]) ||
					(total_checksum_retry >  3)) {
					/* Update Setting to Extend Page */
					for (i = 0; i < 8; i++){
						extend_reg_cmd_addr = 0x40 + (2 * i);
						extend_reg_data = chip->pdata->extreg_table[array_idx].data[2 * i] +
							(chip->pdata->extreg_table[array_idx].data[2 * i + 1] << 8);

						rt9426a_reg_write_word(chip->i2c,
							extend_reg_cmd_addr,
							extend_reg_data);
					}
					rt9426a_reg_write_word(chip->i2c,
						RT9426A_REG_DUMMY, 0x0000);

					retry_times++;
					if (retry_times < 5) {
						hwlog_info("Page Checksum:%d\n",
							array_idx);
						goto PAGE_CHECKSUM2;
					}
				}
			} else {
				/* prepare retry for i2c fail */
				i2c_retry_times++;
				if(i2c_retry_times<3)
					goto PAGE_CHECKSUM1;
			}
		}
	}
	goto TOTAL_CHECKSUM_CHECK;

BYPASS_EXT_REG:
	if ((chip->ocv_checksum_ic != chip->ocv_checksum_dtsi)&&
		(wt_ocv_result == RT9426A_WRITE_OCV_PASS)) {
		retry_times = 30;
		for (i = 0 ; i < retry_times ; i++) {
			regval = rt9426a_reg_read_word(chip->i2c,
				RT9426A_REG_FLAG2);
			if ((reg_flag2 & RT9426A_RDY_MASK) &&
				(reg_flag2 != 0xFFFF)) {
				mdelay(200);
				if(rt9426a_get_current(chip)>0){
					volt_now = rt9426a_reg_read_word(chip->i2c,
						RT9426A_REG_VBAT);
					fd_vth_now = chip->pdata->extreg_table[RT9426A_FD_TBL_IDX].data[RT9426A_FD_DATA_IDX];
					fd_threshold = RT9426A_FD_BASE + 5 * (fd_vth_now);
					hwlog_info("RT9426A VBAT=%d\n",
						volt_now);
					hwlog_info("RT9426A FD_VTH=%d\n",
						fd_vth_now);
					hwlog_info("RT9426A FD Threshold=%d\n",
						fd_threshold);

					if(volt_now > fd_threshold){
						/* disable battery charging path before QS command */
						rt9426a_request_charging_inhibit(true);
						hwlog_info("Enable Charging Inhibit and delay 1250ms\n");
						mdelay(1250);
					}
				}
				break;
			}
			mdelay(10);
		}
		rt9426a_reg_write_word(chip->i2c, RT9426A_REG_CNTL, 0x4000);
		rt9426a_reg_write_word(chip->i2c, RT9426A_REG_DUMMY, 0x0000);
		mdelay(5);
		hwlog_info("OCV checksum are different, QS is done.\n");

		/* Power path recover check */
		if(volt_now > fd_threshold){
			/* enable battery charging path after QS command */
			rt9426a_request_charging_inhibit(false);
			hwlog_info("Disable Charging Inhibit\n");
		}
	}
	else
		hwlog_info("OCV checksum are the same, bypass QS.\n");

	/* clear RI, set 0 to RI bits */
	regval = rt9426a_reg_read_word(chip->i2c, RT9426A_REG_FLAG3);
	regval = (regval & ~RT9426A_RI_MASK);
	rt9426a_reg_write_word(chip->i2c, RT9426A_REG_FLAG3, regval);
	rt9426a_reg_write_word(chip->i2c, RT9426A_REG_DUMMY, 0x0000);
	regval = rt9426a_reg_read_word(chip->i2c, RT9426A_REG_FLAG3);
	if (((regval & RT9426A_RI_MASK) >> 8) == 0)
		hwlog_info("RT9426A RI=0\n");
	else
		hwlog_info("RT9426A RI=1\n");

	/* Seal RT9426A */
	retry_times = 0;
SET_SEAL_CMD:
	rt9426a_reg_write_word(chip->i2c, RT9426A_REG_CNTL, RT9426A_SEAL_CMD);
	rt9426a_reg_write_word(chip->i2c, RT9426A_REG_DUMMY, 0x0000);
	mdelay(1);
	regval = rt9426a_reg_read_word(chip->i2c, RT9426A_REG_FLAG3);
	if (regval & RT9426A_UNSEAL_STATUS) {
		retry_times++;
		if (retry_times < 3) {
			hwlog_info("RT9426A Seal Retry-%d\n",retry_times);
			goto SET_SEAL_CMD;
		} else {
			hwlog_info("RT9426A Seal Fail\n");
			ret = RT9426A_INIT_SEAL_ERR;
		}
	} else {
		hwlog_info("RT9426A Seal Pass\n");
		ret = RT9426A_INIT_PASS;
	}
END_INIT:
	/* check cyccnt & bccomp */
	// rt9426a_check_cycle_cnt_for_fg_ini(chip);
	/* get initial soc for driver for smooth soc */
	chip->capacity = rt9426a_fg_get_soc(chip, 0);

	if (ret == RT9426A_INIT_PASS) {
		rt9426a_reg_write_word(chip->i2c, RT9426A_REG_RSVD,
			chip->pdata->para_version);
		rt9426a_reg_write_word(chip->i2c, RT9426A_REG_DUMMY, 0x0000);
		mdelay(1);
		hwlog_info("RT9426A Initial Successful\n");
		chip->online = 1;
	} else
		hwlog_info("RT9426A Initial Fail\n");

	return ret;
}

struct dt_offset_params {
	int data[RT9426A_DT_OFFSET_PARA_SIZE];
};

struct dt_extreg_params {
	int edata[RT9426A_DT_EXTREG_PARA_SIZE];
};

static int rt_parse_dt(struct device *dev,
	struct rt9426a_platform_data *pdata)
{
#ifdef CONFIG_OF
	struct device_node *np = dev->of_node;
	/* 1:for boundary protection */
	int sizes[RT9426A_SOC_OFFSET_SIZE + 1] = { 0 };
	int j;
	int ret;
	int i;
	struct dt_offset_params *offset_params;
	const char *bat_name = "rt-fuelguage";
	char prop_name[64] = {0};

#ifdef CONFIG_HUAWEI_POWER_EMBEDDED_ISOLATION
	const char *battery_name = NULL;
	const char *batt_model_name = NULL;
	struct device_node *child_node = NULL;
	struct device_node *default_node = NULL;

	batt_model_name = bat_model_name();
	for_each_child_of_node(np, child_node) {
		if (power_dts_read_string(power_dts_tag(HWLOG_TAG),
			child_node, "batt_name", &battery_name)) {
			hwlog_info("childnode without batt_name property");
			continue;
		}
		if (!battery_name)
			continue;
		if (!default_node)
			default_node = child_node;
		hwlog_info("search battery data, battery_name: %s\n",
			battery_name);
		if (!batt_model_name || !strcmp(battery_name, batt_model_name))
			break;
	}

	if (!child_node) {
		if (default_node) {
			dev_info(dev, "cannt match childnode, use first\n");
			child_node = default_node;
		} else {
			dev_info(dev, "cannt find any childnode, use father\n");
			child_node = np;
		}
	}

	ret = of_property_read_string(np, "rt,bat_name",
		(char const **)&bat_name);
	if (ret == 0)
		pdata->bat_name = kasprintf(GFP_KERNEL, "%s", bat_name);

	ret = of_property_read_u32_array(child_node, "rt,dtsi_version",
		pdata->dtsi_version, RT9426A_DTSI_VER_SIZE);
	if (ret < 0)
		pdata->dtsi_version[0] = pdata->dtsi_version[1] = 0;

	ret = of_property_read_u32(child_node, "rt,para_version",
		&pdata->para_version);
	if (ret < 0)
		pdata->para_version = 0;

	ret = of_property_read_u32(child_node, "rt,battery_type",
		&pdata->battery_type);
	if (ret < 0) {
		hwlog_err("uset default battery_type 4350mV, EDV=3200mV\n");
		pdata->battery_type = 4352;
	}

	ret = of_property_read_u32(child_node, "rt,volt_source",
		&pdata->volt_source);
	if (ret < 0)
		pdata->volt_source = RT9426A_REG_AV;

	if (pdata->volt_source == RT9426A_VOLT_SOURCE_NONE) {
		pdata->volt_source = 0;
	} else if (pdata->volt_source == RT9426A_VOLT_SOURCE_VBAT) {
		pdata->volt_source = RT9426A_REG_VBAT;
	} else if (pdata->volt_source == RT9426A_VOLT_SOURCE_OCV) {
		pdata->volt_source = RT9426A_REG_OCV;
	} else if (pdata->volt_source == RT9426A_VOLT_SOURCE_AV) {
		pdata->volt_source = RT9426A_REG_AV;
	} else {
		hwlog_err("pdata->volt_source is out of range, use 3\n");
		pdata->volt_source = RT9426A_REG_AV;
	}

	ret = of_property_read_u32(child_node, "rt,temp_source",
		&pdata->temp_source);
	if (ret < 0)
		pdata->temp_source = 0;

	if (pdata->temp_source == RT9426A_TEMP_SOURCE_NONE) {
		pdata->temp_source = 0;
	} else if (pdata->temp_source == RT9426A_TEMP_SOURCE_TEMP) {
		pdata->temp_source = RT9426A_REG_TEMP;
	} else if (pdata->temp_source == RT9426A_TEMP_SOURCE_INIT) {
		pdata->temp_source = RT9426A_REG_INTT;
	} else if (pdata->temp_source == RT9426A_TEMP_SOURCE_AT) {
		pdata->temp_source = RT9426A_REG_AT;
	} else {
		hwlog_err("pdata->temp_source is out of range, use 0\n");
		pdata->temp_source = 0;
	}

	ret = of_property_read_u32(child_node, "rt,curr_source",
		&pdata->curr_source);
	if (ret < 0)
		pdata->curr_source = 0;

	if (pdata->curr_source == RT9426A_CURR_SOURCE_NONE) {
		pdata->curr_source = 0;
	} else if (pdata->curr_source == RT9426A_CURR_SOURCE_CURR) {
		pdata->curr_source = RT9426A_REG_CURR;
	} else if (pdata->curr_source == RT9426A_CURR_SOURCE_AI) {
		pdata->curr_source = RT9426A_REG_AI;
	} else {
		hwlog_err("pdata->curr_source is out of range, use 2\n");
		pdata->curr_source = RT9426A_REG_AI;
	}

	ret = of_property_read_u32_array(child_node,
		"rt,offset_interpolation_order",
		pdata->offset_interpolation_order,
		RT9426A_OFFSET_INTERPLO_SIZE);
	if (ret < 0)
		pdata->offset_interpolation_order[0] =
			pdata->offset_interpolation_order[1] = 2;

	sizes[0] = sizes[1] = 0;
	ret = of_property_read_u32_array(child_node, "rt,soc_offset_size",
		sizes, RT9426A_SOC_OFFSET_SIZE);
	if (ret < 0)
		hwlog_err("Can't get prop soc_offset_size(%d)\n", ret);

	new_vgcomp_soc_offset_datas(dev, SOC_OFFSET, pdata, sizes[0],
		sizes[1], 0);
	if (pdata->soc_offset.soc_offset_data) {
		offset_params = devm_kzalloc(dev, sizes[0] * sizes[1] *
			sizeof(struct dt_offset_params), GFP_KERNEL);
		if (!offset_params)
			return -1;

		of_property_read_u32_array(child_node, "rt,soc_offset_data",
			(u32 *)offset_params, sizes[0] * sizes[1] *
			(RT9426A_SOC_OFFSET_SIZE + 1));
		for (j = 0; j < sizes[0] * sizes[1]; j++) {
			pdata->soc_offset.soc_offset_data[j].x =
				offset_params[j].data[0];
			pdata->soc_offset.soc_offset_data[j].y =
				offset_params[j].data[1];
			pdata->soc_offset.soc_offset_data[j].offset =
				offset_params[j].data[2];
		}
		devm_kfree(dev, offset_params);
	}
	/*  Read Ext. Reg Table for RT9426A  */
	ret = of_property_read_u8_array(child_node, "rt,fg_extreg_table",
		(u8 *)pdata->extreg_table, 224);
	if (ret < 0) {
		hwlog_err("no ocv table property\n");
		for (j = 0; j < 15; j++)
			for (i = 0; i < 16; i++)
				pdata->extreg_table[j].data[i] = 0;
	}

	/* parse fcc array by 5 element */
	ret = of_property_read_u32_array(child_node, "rt,fcc", pdata->fcc, 5);
	if (ret < 0) {
		hwlog_err("no FCC property, use defaut 2000\n");
		for (i = 0; i < 5; i++)
			pdata->fcc[i] = RT9426A_DESIGN_FCC_VAL;
	}

	/* parse fc_vth array by 5 element */
	ret = of_property_read_u32_array(child_node, "rt,fg_fc_vth",
		pdata->fc_vth, 5);
	if (ret < 0) {
		hwlog_err("no fc_vth property, use default 4200mV\n");
		for (i = 0; i < 5; i++)
			pdata->fc_vth[i] = RT9426A_FC_VTH_DEFAULT_VAL;
	}
	/* for smooth_soc */
	of_property_read_u32(child_node, "rt,smooth_soc_en",
		&pdata->smooth_soc_en);
	hwlog_debug("smooth_soc_en = %d\n", pdata->smooth_soc_en);

	of_property_read_u32(child_node, "rt,rs_ic_setting",
		&pdata->rs_ic_setting);
	of_property_read_u32(child_node, "rt,rs_schematic",
		&pdata->rs_schematic);
	hwlog_debug("rs_ic_setting = %d\n", pdata->rs_ic_setting);
	hwlog_debug("rs_schematic = %d\n", pdata->rs_schematic);

	/* for update ieoc setting */
	ret = of_property_read_u32_array(child_node, "rt,icc_threshold",
		pdata->icc_threshold, RT9426A_ICC_THRESHOLD_SIZE);
	if (ret < 0) {
		hwlog_err("no icc threshold property reset to 0\n");
		for (i = 0; i < RT9426A_ICC_THRESHOLD_SIZE; i++)
			pdata->icc_threshold[i] = 0;
	}

	ret = of_property_read_u32_array(child_node, "rt,ieoc_setting",
		pdata->ieoc_setting, 4);
	if (ret < 0) {
		hwlog_err("no ieoc setting property, reset to 0\n");
		for (i = 0; i < 4; i++)
			pdata->ieoc_setting[i] = 0;
	}

	/* parse ocv_table array by 80x5 element */
	for (i = 0; i < 5; i++) {
		snprintf(prop_name, 64, "rt,fg_ocv_table%d", i);
		ret = of_property_read_u32_array(child_node, prop_name,
			(u32 *)pdata->ocv_table + i * 80, 80);
		if (ret < 0)
			memset32((u32 *)pdata->ocv_table + i * 80, 0, 80);
	}

	(void)power_dts_read_u32(power_dts_tag(HWLOG_TAG), np,
		"rt9426a_config_ver",
		&pdata->rt9426a_config_ver, RT9426A_DRIVER_VER);

	rt9426a_parse_batt_ntc(np, pdata);
	pdata->force_use_aux_cali_para =
		of_property_read_bool(np, "force_use_aux_cali_para");

#else

	ret = of_property_read_u32_array(np, "rt,dtsi_version",
		pdata->dtsi_version, 2);
	if (ret < 0)
		pdata->dtsi_version[0] = pdata->dtsi_version[1] = 0;

	ret = of_property_read_u32(np, "rt,para_version", &pdata->para_version);
	if (ret < 0)
		pdata->para_version = 0;

	ret = of_property_read_string(np, "rt,bat_name",
		(char const **)&bat_name);
	if (ret == 0)
		pdata->bat_name = kasprintf(GFP_KERNEL, "%s", bat_name);

	ret = of_property_read_u32_array(np, "rt,offset_interpolation_order",
		pdata->offset_interpolation_order, 2);
	if (ret < 0)
		pdata->offset_interpolation_order[0] =
			pdata->offset_interpolation_order[1] = 2;

	sizes[0] = sizes[1] = 0;
	ret = of_property_read_u32_array(np, "rt,soc_offset_size", sizes, 2);
	if (ret < 0)
		hwlog_info("Can't get prop soc_offset_size(%d)\n", ret);

	new_vgcomp_soc_offset_datas(dev, SOC_OFFSET, pdata, sizes[0],
		sizes[1], 0);
	if (pdata->soc_offset.soc_offset_data) {
		offset_params = devm_kzalloc(dev,
			sizes[0] * sizes[1] * sizeof(struct dt_offset_params),
			GFP_KERNEL);

		of_property_read_u32_array(np, "rt,soc_offset_data",
			(u32 *)offset_params, sizes[0] * sizes[1] * 3);
		for (j = 0; j < sizes[0] * sizes[1]; j++) {
			pdata->soc_offset.soc_offset_data[j].x =
				offset_params[j].data[0];
			pdata->soc_offset.soc_offset_data[j].y =
				offset_params[j].data[1];
			pdata->soc_offset.soc_offset_data[j].offset =
				offset_params[j].data[2];
		}
		devm_kfree(dev, offset_params);
	}

	ret = of_property_read_u32(np, "rt,battery_type", &pdata->battery_type);
	if (ret < 0) {
		hwlog_info("uset default battery_type 4350mV, EDV=3200mV\n");
		pdata->battery_type = 4352;
	}

	ret = of_property_read_u32(np, "rt,volt_source", &pdata->volt_source);
	if (ret < 0)
		pdata->volt_source = RT9426A_REG_AV;

	if (pdata->volt_source == 0)
		pdata->volt_source = 0;
	else if (pdata->volt_source == 1)
		pdata->volt_source = RT9426A_REG_VBAT;
	else if (pdata->volt_source == 2)
		pdata->volt_source = RT9426A_REG_OCV;
	else if (pdata->volt_source == 3)
		pdata->volt_source = RT9426A_REG_AV;
	else {
		hwlog_info("pdata->volt_source is out of range, use 3\n");
		pdata->volt_source = RT9426A_REG_AV;
	}

	ret = of_property_read_u32(np, "rt,temp_source", &pdata->temp_source);
	if (ret < 0)
		pdata->temp_source = 0;

	if (pdata->temp_source == 0)
		pdata->temp_source = 0;
	else if (pdata->temp_source == 1)
		pdata->temp_source = RT9426A_REG_TEMP;
	else if (pdata->temp_source == 2)
		pdata->temp_source = RT9426A_REG_INTT;
	else if (pdata->temp_source == 3)
		pdata->temp_source = RT9426A_REG_AT;
	else {
		hwlog_info("pdata->temp_source is out of range, use 0\n");
		pdata->temp_source = 0;
	}
	ret = of_property_read_u32(np, "rt,curr_source", &pdata->curr_source);
	if (ret < 0)
		pdata->curr_source = 0;
	if (pdata->curr_source == 0)
		pdata->curr_source = 0;
	else if (pdata->curr_source == 1)
		pdata->curr_source = RT9426A_REG_CURR;
	else if (pdata->curr_source == 2)
		pdata->curr_source = RT9426A_REG_AI;
	else {
		hwlog_info("pdata->curr_source is out of range, use 2\n");
		pdata->curr_source = RT9426A_REG_AI;
	}
	/*  Read Ext. Reg Table for RT9426A  */
	ret = of_property_read_u8_array(np, "rt,fg_extreg_table",
		(u8 *)pdata->extreg_table, 224);
	if (ret < 0) {
		hwlog_err("no ocv table property\n");
		for (j = 0; j < 15; j++)
			for (i = 0; i < 16; i++)
				pdata->extreg_table[j].data[i] = 0;
	}

	of_property_read_u32(np, "rt,rs_ic_setting",&pdata->rs_ic_setting);
	of_property_read_u32(np, "rt,rs_schematic",&pdata->rs_schematic);

	hwlog_info("rs_ic_setting = %d\n", pdata->rs_ic_setting);
	hwlog_info("rs_schematic = %d\n", pdata->rs_schematic);

	/* parse fcc array by 5 element */
	ret = of_property_read_u32_array(np, "rt,fcc", pdata->fcc, 5);
	if (ret < 0) {
		hwlog_err("no FCC property, use defaut 2000\n");
		for (i = 0; i < 5; i++)
			pdata->fcc[i] = 2000;
	}
	/* parse fc_vth array by 5 element */
	ret = of_property_read_u32_array(np, "rt,fg_fc_vth", pdata->fc_vth, 5);
	if (ret < 0) {
		hwlog_err("no fc_vth property, use default 4200mV\n");
		for (i = 0; i < 5; i++)
			pdata->fc_vth[i] = 0x0078;
	}
	/* parse ocv_table array by 80x5 element */
	for (i = 0; i < 5; i++) {
		snprintf(prop_name, 64, "rt,fg_ocv_table%d", i);
		ret = of_property_read_u32_array(np, prop_name,
			(u32 *)pdata->ocv_table + i * 80, 80);
		if (ret < 0)
			memset32((u32 *)pdata->ocv_table + i * 80, 0, 80);
	}
	/* for smooth_soc */
	of_property_read_u32(np, "rt,smooth_soc_en",&pdata->smooth_soc_en);
	hwlog_info("smooth_soc_en = %d\n", pdata->smooth_soc_en);

	/* for update ieoc setting by dtsi */
	ret = of_property_read_u32_array(np, "rt,icc_threshold",
		pdata->icc_threshold, 3);
	if (ret < 0) {
		hwlog_err("no icc threshold property reset to 0\n");
		for (i = 0; i < 3; i++)
			pdata->icc_threshold[i] = 0;
	}

	ret = of_property_read_u32_array(np, "rt,ieoc_setting",
		pdata->ieoc_setting, 4);
	if (ret < 0) {
		hwlog_err("no ieoc setting property, reset to 0\n");
		for (i = 0; i < 4; i++)
			pdata->ieoc_setting[i] = 0;
	}
#endif /* CONFIG_HUAWEI_POWER_EMBEDDED_ISOLATION */
#endif /* CONFIG_OF */
	return 0;
}

static int rt9426a_i2c_chipid_check(struct rt9426a_chip *chip)
{
	unsigned int val = 0;
	int ret;

	ret = regmap_read(chip->regmap, RT9426A_REG_DEVICE_ID, &val);
	if (ret < 0)
		return ret;

	if (val != RT9426A_DEVICE_ID) {
		hwlog_err("dev_id not match\n");
		return -ENODEV;
	}

	chip->ic_ver = val;
	return 0;
}

#ifdef CONFIG_HUAWEI_POWER_EMBEDDED_ISOLATION
static void rt9426a_parse_batt_ntc(struct device_node *np,
	struct rt9426a_platform_data *pdata)
{
	int array_len;
	int i;
	long idata = 0;
	const char *string = NULL;
	int ret;

	if (!np)
		return;
	if (of_property_read_u32(np, "ntc_compensation_is",
		&(pdata->ntc_compensation_is))) {
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
		hwlog_err("temp is too long use only front %d paras\n",
			array_len);
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
			pdata->ntc_temp_compensation_para[i / NTC_PARA_TOTAL]
				.refer = idata;
			break;
		case NTC_PARA_VALUE:
			pdata->ntc_temp_compensation_para[i / NTC_PARA_TOTAL]
				.comp_value = idata;
			break;
		default:
			hwlog_err("ntc_temp_compensation_para get failed\n");
		}
		hwlog_info("ntc_temp_compensation_para[%d][%d] = %ld\n",
			i / (NTC_PARA_TOTAL), i % (NTC_PARA_TOTAL), idata);
	}
}

static unsigned int rt9426a_get_rm(struct rt9426a_chip *chip)
{
	return rt9426a_reg_read_word(chip->i2c, RT9426A_REG_RM);
}

static int rt9426a_is_ready(void *dev_data)
{
	if (!g_rt9426a_chip)
		return 0;

	return 1;
}

static int rt9426a_set_ntc_compensation_temp(
	struct rt9426a_platform_data *pdata, int temp_val, int cur_temp)
{
	int temp_with_compensation = temp_val;
	struct common_comp_data comp_data;

	if (!pdata)
		return temp_with_compensation;

	comp_data.refer = abs(cur_temp);
	comp_data.para_size = NTC_PARA_LEVEL;
	comp_data.para = pdata->ntc_temp_compensation_para;
	if ((pdata->ntc_compensation_is == 1) &&
		(temp_val >= COMPENSATION_THRESHOLD))
		temp_with_compensation = power_get_compensation_value(temp_val,
			&comp_data);

	hwlog_info("temp_with_compensat=%d temp_no_compensat=%d ichg=%d\n",
		temp_with_compensation, temp_val, cur_temp);
	return temp_with_compensation;
}

static int rt9426a_read_battery_temperature(void *dev_data)
{
	int temp;
	int bat_curr;

	if (!g_rt9426a_chip)
		return 0;

	temp = rt9426a_get_display_data(g_rt9426a_chip,
		RT9426A_DISPLAY_TEMP);
	bat_curr = rt9426a_get_display_data(g_rt9426a_chip,
		RT9426A_DISPLAY_IBAT);

	return rt9426a_set_ntc_compensation_temp(g_rt9426a_chip->pdata,
		temp, bat_curr);
}

#ifdef CONFIG_HLTHERM_RUNTEST
static int rt9426a_is_battery_exist(void *dev_data)
{
	return 0;
}
#else
static int rt9426a_is_battery_exist(void *dev_data)
{
	int temp;

	temp = rt9426a_read_battery_temperature(dev_data);
	if ((temp <= RT9426A_TEMP_ABR_LOW) ||
		(temp >= RT9426A_TEMP_ABR_HIGH))
		return 0;

	return 1;
}
#endif /* CONFIG_HLTHERM_RUNTEST */

static void set_low_volt_smooth(struct rt9426a_chip *chip)
{
	int regval;

	regval = rt9426a_unseal_wi_retry(chip);
	if (regval == RT9426A_UNSEAL_FAIL) {
		hwlog_err("Unseal failed. Unable to do lv smooth\n");
	} else {
		rt9426a_write_page_cmd(chip, RT9426A_PAGE_5);
		/* low voltage smooth, enable: 0x00FF, disable: 0x0032 */
		if (chip->low_v_smooth_en)
			rt9426a_reg_write_word(chip->i2c, RT9426A_REG_SWINDOW8,
				0x00FF);
		else
			rt9426a_reg_write_word(chip->i2c, RT9426A_REG_SWINDOW8,
				0x0032);
	}
}

static void check_for_lv_smooth(struct rt9426a_chip *chip)
{
	int vbat = rt9426a_reg_read_word(chip->i2c, RT9426A_REG_VBAT);
	int temp = rt9426a_get_temp(chip);
	int soc = rt9426a_fg_get_soc(chip, chip->pdata->smooth_soc_en);
	int curr = rt9426a_get_current(chip);

	if ((temp >= LV_SMOOTH_T_MIN) && (temp <= LV_SMOOTH_T_MAX)) {
		if (curr <= LV_SMOOTH_I_TH) {
			/* dsg current <= -1000mA, disable lv_smooth */
			if (chip->low_v_smooth_en) {
				chip->low_v_smooth_en = false;
				set_low_volt_smooth(chip);
			}
		} else {
			/* dsg current>-1000mA keep check vbat&soc condition */
			if ((vbat <= LV_SMOOTH_V_TH) &&
				(soc > LV_SMOOTH_S_TH)) {
				/* vbat&soc condition meet enable lv_smooth */
				if (!chip->low_v_smooth_en) {
					chip->low_v_smooth_en = true;
					set_low_volt_smooth(chip);
				}
			} else {
				if (chip->low_v_smooth_en) {
					chip->low_v_smooth_en = false;
					set_low_volt_smooth(chip);
				}
			}
		}
	} else {
		/* temperature condition don't meet, disable lv_smooth */
		if (chip->low_v_smooth_en) {
			chip->low_v_smooth_en = false;
			set_low_volt_smooth(chip);
		}
	}
}

static int rt9426a_get_display_data(struct rt9426a_chip *di, int index)
{
	int val = 0;

	switch (index) {
	case RT9426A_DISPLAY_TEMP:
		val = rt9426a_get_temp(di);
		break;
	case RT9426A_DISPLAY_VBAT:
		val = rt9426a_get_volt(di);
		break;
	case RT9426A_DISPLAY_IBAT:
		val = rt9426a_get_current(di);
		if (di->pdata->rs_schematic)
			val = ((s64)val *  di->pdata->rs_ic_setting) /
				di->pdata->rs_schematic;
		break;
	case RT9426A_DISPLAY_AVG_IBAT:
		val = rt9426a_get_current(di);
			if (di->pdata->rs_schematic)
				val = ((s64)val *  di->pdata->rs_ic_setting) /
					di->pdata->rs_schematic;
		break;
	case RT9426A_DISPLAY_RM:
		val = rt9426a_get_rm(di);
		if (di->pdata->rs_schematic)
			val = ((s64)val *  di->pdata->rs_ic_setting) /
				di->pdata->rs_schematic;
		break;
	case RT9426A_DISPLAY_SOC:
		val = rt9426a_fg_get_soc(di, di->pdata->smooth_soc_en);
		check_for_lv_smooth(di);
		break;
	case RT9426A_DISPLAY_DISIGN_FCC:
		val = rt9426a_get_design_capacity(di);
		if (di->pdata->rs_schematic)
			val = ((s64)val *  di->pdata->rs_ic_setting) /
				di->pdata->rs_schematic;
		break;
	case RT9426A_DISPLAY_FCC:
		val = rt9426a_reg_read_word(di->i2c, RT9426A_REG_FCC);
		if (di->pdata->rs_schematic)
			val = ((s64)val * di->pdata->rs_ic_setting) /
				di->pdata->rs_schematic;
		break;
	default:
		break;
	}

	return val;
}

static int rt9426a_get_log_head(char *buffer, int size, void *dev_data)
{
	struct rt9426a_chip *di = (struct rt9426a_chip *)dev_data;

	if (!buffer || !di)
		return -1;

	snprintf(buffer, size,
		"    Temp   Vbat   Ibat   AIbat   Rm   Soc   ");

	return 0;
}

static void rt9426a_dump_register(struct rt9426a_chip *di)
{
	int val;

	val = rt9426a_reg_read_word(di->i2c, RT9426A_REG_RSVD);
	if (val >= 0)
		hwlog_debug("RSVD = 0x%x\n", val);

	val = rt9426a_reg_read_word(di->i2c, RT9426A_REG_FLAG1);
	if (val >= 0)
		hwlog_debug("flag1 = 0x%x\n", val);

	val = rt9426a_reg_read_word(di->i2c, RT9426A_REG_FLAG2);
	if (val >= 0)
		hwlog_debug("flag2 = 0x%x\n", val);

	val = rt9426a_reg_read_word(di->i2c, RT9426A_REG_FLAG3);
	if (val >= 0)
		hwlog_debug("flag3 = 0x%x\n", val);

	val = rt9426a_reg_read_word(di->i2c, RT9426A_REG_DC);
	if (val >= 0)
		hwlog_debug("DC = %d\n", val);

	val = rt9426a_reg_read_word(di->i2c, RT9426A_REG_FCC);
	if (val >= 0)
		hwlog_debug("FCC = %d\n", val);
}

static int rt9426a_dump_log_data(char *buffer, int size, void *dev_data)
{
	struct rt9426a_chip *di = (struct rt9426a_chip *)dev_data;
	struct rt9426a_display_data g_dis_data;

	if (!buffer || !di)
		return -1;

	g_dis_data.vbat = rt9426a_get_display_data(di, RT9426A_DISPLAY_VBAT);
	g_dis_data.ibat = rt9426a_get_display_data(di, RT9426A_DISPLAY_IBAT);
	g_dis_data.avg_ibat = rt9426a_get_display_data(di, RT9426A_DISPLAY_AVG_IBAT);
	g_dis_data.rm = rt9426a_get_display_data(di, RT9426A_DISPLAY_RM);
	g_dis_data.temp = rt9426a_get_display_data(di, RT9426A_DISPLAY_TEMP);
	g_dis_data.soc = rt9426a_get_display_data(di, RT9426A_DISPLAY_SOC);

	snprintf(buffer, size, "%-7d%-7d%-7d%-7d%-7d%-7d   ",
		g_dis_data.temp, g_dis_data.vbat, g_dis_data.ibat,
		g_dis_data.avg_ibat, g_dis_data.rm, g_dis_data.soc);

	return 0;
}

static struct power_log_ops rt9426a_fg_ops = {
	.dev_name = "rt9426a",
	.dump_log_head = rt9426a_get_log_head,
	.dump_log_content = rt9426a_dump_log_data,
};


static int rt9426a_read_battery_soc(void *dev_data)
{
	if (!g_rt9426a_chip)
		return 0;

	return rt9426a_get_display_data(g_rt9426a_chip,
		RT9426A_DISPLAY_SOC);
}

static int rt9426a_read_battery_vol(void *dev_data)
{
	if (!g_rt9426a_chip)
		return 0;

	return rt9426a_get_display_data(g_rt9426a_chip,
		RT9426A_DISPLAY_VBAT);
}

static int rt9426a_read_battery_current(void *dev_data)
{
	if (!g_rt9426a_chip)
		return 0;

	return rt9426a_get_display_data(g_rt9426a_chip,
		RT9426A_DISPLAY_IBAT);
}

static int rt9426a_read_battery_avg_current(void *dev_data)
{
	if (!g_rt9426a_chip)
		return 0;

	return rt9426a_get_display_data(g_rt9426a_chip,
		RT9426A_DISPLAY_AVG_IBAT);
}

static int rt9426a_read_battery_fcc(void *dev_data)
{
	if (!g_rt9426a_chip)
		return 0;

	return rt9426a_get_display_data(g_rt9426a_chip,
		RT9426A_DISPLAY_FCC);
}

static int rt9426a_read_battery_cycle(void *dev_data)
{
	if (!g_rt9426a_chip)
		return 0;

	return rt9426a_get_cyccnt(g_rt9426a_chip);
}

static int rt9426a_read_battery_rm(void *dev_data)
{
	if (!g_rt9426a_chip)
		return 0;

	return rt9426a_get_display_data(g_rt9426a_chip, RT9426A_DISPLAY_RM);
}

static int rt9426a_set_battery_low_voltage(int val, void *dev_data)
{
	int uv_set;
	int reg_val;
	u16 wr_val;

	if (!g_rt9426a_chip)
		return -1;

	/* 2400: val = 2400mV + 10mV* uv_set */
	uv_set = (val - 2400) / 10;
	if (uv_set < 0)
		return -1;

	rt9426a_read_page_cmd(g_rt9426a_chip, RT9426A_PAGE_3);
	reg_val = rt9426a_reg_read_word(g_rt9426a_chip->i2c,
		RT9426A_REG_SWINDOW5);
	if (reg_val < 0)
		return -1;
	wr_val = ((unsigned int)uv_set << RT9426A_BYTE_BITS) &
		RT9426A_HIGH_BYTE_MASK;
	wr_val |= (unsigned int)reg_val & RT9426A_LOW_BYTE_MASK;

	hwlog_info( "uv_set=0x%x, reg_val=0x%x, wr_val=0x%x\n",
		uv_set, reg_val, wr_val);
	rt9426a_write_page_cmd(g_rt9426a_chip, RT9426A_PAGE_3);
	return rt9426a_reg_write_word(g_rt9426a_chip->i2c,
		RT9426A_REG_SWINDOW5, wr_val);
}

static int rt9426a_set_last_capacity(int capacity, void *dev_data)
{
	if ((capacity > RT9426A_FULL_CAPCACITY) || (capacity < 0) ||
		!g_rt9426a_chip)
		return 0;

	return rt9426a_reg_write_word(g_rt9426a_chip->i2c,
		RT9426A_EXTEND_REG, capacity);
}

static int rt9426a_get_last_capacity(void *dev_data)
{
	int last_cap = 0;
	int cap;

	if (!g_rt9426a_chip)
		return last_cap;

	last_cap = rt9426a_reg_read_word(g_rt9426a_chip->i2c,
		RT9426A_EXTEND_REG);
	cap = rt9426a_read_battery_soc(dev_data);

	hwlog_info( "%s read cap=%d, last_cap=%d\n", __func__, cap, last_cap);

	if ((last_cap <= 0) || (cap <= 0))
		return cap;

	if (abs(last_cap - cap) >= RT9426A_CAPACITY_TH)
		return cap;

	/* reset last capacity */
	rt9426a_reg_write_word(g_rt9426a_chip->i2c, RT9426A_EXTEND_REG, 0);

	return last_cap;
}

static struct coul_interface_ops rt9426a_ops = {
	.type_name = "main",
	.is_coul_ready = rt9426a_is_ready,
	.is_battery_exist = rt9426a_is_battery_exist,
	.get_battery_capacity = rt9426a_read_battery_soc,
	.get_battery_voltage = rt9426a_read_battery_vol,
	.get_battery_current = rt9426a_read_battery_current,
	.get_battery_avg_current = rt9426a_read_battery_avg_current,
	.get_battery_temperature = rt9426a_read_battery_temperature,
	.get_battery_fcc = rt9426a_read_battery_fcc,
	.get_battery_cycle = rt9426a_read_battery_cycle,
	.set_battery_low_voltage = rt9426a_set_battery_low_voltage,
	.set_battery_last_capacity = rt9426a_set_last_capacity,
	.get_battery_last_capacity = rt9426a_get_last_capacity,
	.get_battery_rm = rt9426a_read_battery_rm,
};

static int rt9426a_get_calibration_curr(int *val, void *dev_data)
{
	if (!val || !g_rt9426a_chip) {
		hwlog_err("invalid val or g_rt9426_chip\n");
		return -1;
	}

	*val = rt9426a_get_curr_by_conversion(g_rt9426a_chip);
	hwlog_debug("cali cur %d\n", *val);
	return 0;
}

static int rt9426a_get_calibration_vol(int *val, void *dev_data)
{
	if (!val || !g_rt9426a_chip) {
		hwlog_err("invalid val or g_rt9426_chip\n");
		return -1;
	}

	*val = rt9426a_get_volt_by_conversion(g_rt9426a_chip);
	*val *= 1000; /* mv to uv */
	hwlog_debug("cali vol %d\n", *val);
	return 0;
}

static int rt9426a_set_current_gain(unsigned int val, void *dev_data)
{
	if (!g_rt9426a_chip) {
		hwlog_err("invalid g_rt9426_chip\n");
		return -1;
	}

	val = RT9426A_GAIN_DEFAULT_VAL + (((s64)(val) * RT9426A_GAIN_BASE_VAL) /
		RT9426A_COUL_DEFAULT_VAL - RT9426A_GAIN_BASE_VAL);

	g_rt9426a_chip->c_gain = val;
	rt9426a_apply_calibration_para(g_rt9426a_chip, RT9426A_GAIN_DEFAULT_VAL,
		g_rt9426a_chip->c_gain, g_rt9426a_chip->v_gain);
	hwlog_debug("cur gain %d\n", val);
	return 0;
}

static int rt9426a_set_current_offset(int val, void *dev_data)
{
	if (!g_rt9426a_chip || !g_rt9426a_chip->pdata->rs_ic_setting) {
		hwlog_err("invalid g_rt9426a_chip\n");
		return -1;
	}

	val = RT9426A_GAIN_DEFAULT_VAL +
		((s64)(val) * g_rt9426a_chip->pdata->rs_schematic /
		g_rt9426a_chip->pdata->rs_ic_setting) / RT9426A_COUL_OFFSET_VAL;

	g_rt9426a_chip->c_offset = val;
	rt9426a_apply_calibration_para(g_rt9426a_chip, g_rt9426a_chip->c_offset,
		g_rt9426a_chip->c_gain, g_rt9426a_chip->v_gain);
	hwlog_info("cur offset %d\n", val);
	return 0;

}

static int rt9426a_set_voltage_gain(unsigned int val, void *dev_data)
{
	if (!g_rt9426a_chip) {
		hwlog_err("invalid g_rt9426_chip\n");
		return -1;
	}

	val = RT9426A_GAIN_DEFAULT_VAL + (((s64)(val) * RT9426A_GAIN_BASE_VAL) /
		RT9426A_COUL_DEFAULT_VAL - RT9426A_GAIN_BASE_VAL);

	g_rt9426a_chip->v_gain = val;
	rt9426a_apply_calibration_para(g_rt9426a_chip, RT9426A_GAIN_DEFAULT_VAL,
		g_rt9426a_chip->c_gain, g_rt9426a_chip->v_gain);
	hwlog_debug("voltage gain %d\n", val);
	return 0;
}

static int rt9426a_enable_cali_mode(int enable, void *dev_data)
{
	if (!g_rt9426a_chip)
		return -1;

	if (enable)
		rt9426a_enter_calibration_mode(g_rt9426a_chip);
	else
		rt9426a_exit_calibration_mode(g_rt9426a_chip);
	return 0;
}

static struct coul_cali_ops rt9426a_cali_ops = {
	.dev_name = "aux",
	.get_current = rt9426a_get_calibration_curr,
	.get_voltage = rt9426a_get_calibration_vol,
	.set_current_gain = rt9426a_set_current_gain,
	.set_current_offset = rt9426a_set_current_offset,
	.set_voltage_gain = rt9426a_set_voltage_gain,
	.set_cali_mode = rt9426a_enable_cali_mode,
};

static int rt9426a_calibration_para_invalid(int c_gain, int v_gain, int c_offset)
{
	return ((c_gain < RT9426A_TBATICAL_MIN_A) ||
		(c_gain > RT9426A_TBATICAL_MAX_A) ||
		(v_gain < RT9426A_TBATCAL_MIN_A) ||
		(v_gain > RT9426A_TBATCAL_MAX_A) ||
		(c_offset < RT9426A_TBATICAL_MIN_B) ||
		(c_offset > RT9426A_TBATICAL_MAX_B));
}

static int rt9426a_get_data_from_int(int val)
{
	return RT9426A_GAIN_DEFAULT_VAL + (((s64)(val) * RT9426A_GAIN_BASE_VAL) /
		RT9426A_COUL_DEFAULT_VAL - RT9426A_GAIN_BASE_VAL);
}

static int rt9426a_get_offset_from_int(int val)
{
	if (!g_rt9426a_chip->pdata->rs_ic_setting) {
		hwlog_err("%s %d \n", __func__, g_rt9426a_chip->pdata->rs_ic_setting);
		return RT9426A_GAIN_DEFAULT_VAL;
	}

	return RT9426A_GAIN_DEFAULT_VAL +
		((s64)(val) * g_rt9426a_chip->pdata->rs_schematic /
		g_rt9426a_chip->pdata->rs_ic_setting) / RT9426A_COUL_OFFSET_VAL;
}

static void rt9426a_init_calibration_para(struct rt9426a_chip *chip)
{
	int c_a = 0;
	int v_a = 0;
	int c_offset = 0;

	chip->c_gain = RT9426A_GAIN_DEFAULT_VAL;
	chip->v_gain = RT9426A_GAIN_DEFAULT_VAL;
	chip->c_offset = RT9426A_GAIN_DEFAULT_VAL;

	coul_cali_get_para(COUL_CALI_MODE_AUX, COUL_CALI_PARA_CUR_A, &c_a);
	coul_cali_get_para(COUL_CALI_MODE_AUX, COUL_CALI_PARA_VOL_A, &v_a);
	coul_cali_get_para(COUL_CALI_MODE_AUX, COUL_CALI_PARA_CUR_B, &c_offset);

	if (chip->pdata->force_use_aux_cali_para && (c_a != 0) && (v_a != 0)) {
		hwlog_info("force_use_aux_cali_para\n");

		if (c_a < RT9426A_TBATICAL_MIN_A)
			c_a = RT9426A_TBATICAL_MIN_A;
		else if (c_a > RT9426A_TBATICAL_MAX_A)
			c_a = RT9426A_TBATICAL_MAX_A;

		if (c_offset < RT9426A_TBATICAL_MIN_B || c_offset > RT9426A_TBATICAL_MAX_B)
			c_offset = 0;
		if (v_a < RT9426A_TBATCAL_MIN_A)
			v_a = RT9426A_TBATCAL_MIN_A;
		else if (v_a > RT9426A_TBATCAL_MAX_A)
			v_a = RT9426A_TBATCAL_MAX_A;
	}

	if (rt9426a_calibration_para_invalid(c_a, v_a, c_offset)) {
		coul_cali_get_para(COUL_CALI_MODE_MAIN, COUL_CALI_PARA_CUR_A, &c_a);
		coul_cali_get_para(COUL_CALI_MODE_MAIN, COUL_CALI_PARA_VOL_A, &v_a);
		coul_cali_get_para(COUL_CALI_MODE_MAIN, COUL_CALI_PARA_CUR_B, &c_offset);
		if (rt9426a_calibration_para_invalid(c_a, v_a, c_offset))
			goto update;
	}

	chip->c_gain = rt9426a_get_data_from_int(c_a);
	chip->v_gain = rt9426a_get_data_from_int(v_a);
	chip->c_offset = rt9426a_get_offset_from_int(c_offset);

	hwlog_info("c_gain %d, v_gain %d, c_offset %d\n", chip->c_gain, chip->v_gain, chip->c_offset);
update:
	rt9426a_apply_calibration_para(chip, chip->c_offset,
		chip->c_gain, chip->v_gain);
}

#endif /* CONFIG_HUAWEI_POWER_EMBEDDED_ISOLATION */

static bool rt9426a_is_writeable_reg(struct device *dev,
	unsigned int reg)
{
	return (reg % 2) ? false : true;
}

static bool rt9426a_is_readable_reg(struct device *dev,
	unsigned int reg)
{
	return (reg % 2) ? false : true;
}

static const struct regmap_config rt9426a_regmap_config = {
	.reg_bits = 8,
	.val_bits = 16,
	.writeable_reg = rt9426a_is_writeable_reg,
	.readable_reg = rt9426a_is_readable_reg,
	.max_register = RT9426A_REG_TOTAL_CHKSUM,
	.val_format_endian = REGMAP_ENDIAN_LITTLE,
};

static void fg_update_work_func(struct work_struct *work)
{
	struct rt9426a_chip *chip = container_of(work, struct rt9426a_chip,
		update_work.work);

	hwlog_info("%s++\n", __func__);
	rt9426a_update_info(chip);
	queue_delayed_work(system_power_efficient_wq,
		&chip->update_work, 20 * HZ);
	hwlog_info("%s--\n", __func__);
}

static int rt9426a_i2c_probe(struct i2c_client *i2c)
{
	struct rt9426a_platform_data *pdata = i2c->dev.platform_data;
	struct rt9426a_chip *chip;
	struct power_supply_config psy_config = {};
	bool use_dt = i2c->dev.of_node;
	int ret;

	/* alloc memory */
	chip = devm_kzalloc(&i2c->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;
	if (use_dt) {
		pdata = devm_kzalloc(&i2c->dev, sizeof(*pdata), GFP_KERNEL);
		if (!pdata)
			return -ENOMEM;
		memcpy(pdata, &def_platform_data, sizeof(*pdata));
		rt_parse_dt(&i2c->dev, pdata);
		chip->pdata = pdata;
		i2c->dev.platform_data = pdata;
	} else {
		if (!pdata) {
			hwlog_err("no platdata specified\n");
			return -EINVAL;
		}
	}

	chip->i2c = i2c;
	chip->dev = &i2c->dev;
	chip->alert_irq = -1;
	chip->btemp = RT9426A_BAT_TEMP_VAL;
	chip->bvolt = RT9426A_BAT_VOLT_VAL;
	chip->bcurr = RT9426A_BAT_CURR_VAL;
	chip->design_capacity = RT9426A_DESIGN_CAP_VAL;
	chip->ocv_checksum_ic = 0;
	/* for aging cv */
	chip->ocv_checksum_dtsi = *((u32 *)chip->pdata->ocv_table +
	                          chip->ocv_index * 80 + RT9426A_IDX_OF_OCV_CKSUM);
#ifdef CONFIG_HUAWEI_POWER_EMBEDDED_ISOLATION
	g_rt9426a_chip = chip;
#endif /* CONFIG_HUAWEI_POWER_EMBEDDED_ISOLATION */
	mutex_init(&chip->var_lock);
	mutex_init(&chip->update_lock);
	INIT_DELAYED_WORK(&chip->update_work, fg_update_work_func);
	i2c_set_clientdata(i2c, chip);

	/* rt regmap init */
	chip->regmap = devm_regmap_init_i2c(i2c, &rt9426a_regmap_config);
	if (IS_ERR(chip->regmap)) {
		hwlog_err("regmap init fail\n");
		return PTR_ERR(chip->regmap);
	}

	/* check chip id first */
	ret = rt9426a_i2c_chipid_check(chip);
	if (ret < 0) {
		hwlog_err("chip id check fail\n");
		return ret;
	}

	/* apply platform data */
	ret = rt9426a_apply_pdata(chip);
	if (ret < 0) {
		hwlog_info("apply pdata fail\n");
		return ret;
	}

	/* fg psy register */
	psy_config.of_node = i2c->dev.of_node;
	psy_config.drv_data = chip;
	if (pdata->bat_name)
		fg_psy_desc.name = pdata->bat_name;
	chip->fg_psy = devm_power_supply_register(&i2c->dev,
		&fg_psy_desc, &psy_config);
	if (IS_ERR(chip->fg_psy)) {
		hwlog_info("register batt psy fail\n");
		return PTR_ERR(chip->fg_psy);
	}

	rt_fg_create_attrs(&chip->fg_psy->dev);

	/* mask irq before irq register */
	ret = rt9426a_irq_enable(chip, false);
	if (ret < 0) {
		hwlog_info("scirq mask fail\n");
		return ret;
	}

	ret = rt9426a_irq_init(chip);
	if (ret < 0) {
		hwlog_info("irq init fail\n");
		return ret;
	}

	ret = rt9426a_irq_enable(chip, true);
	if (ret < 0) {
		hwlog_info("scirq mask fail\n");
		return ret;
	}

#ifdef CONFIG_HUAWEI_POWER_EMBEDDED_ISOLATION
	rt9426a_fg_ops.dev_data = (void *)chip;
	power_log_ops_register(&rt9426a_fg_ops);

	coul_interface_ops_register(&rt9426a_ops);
	rt9426a_init_calibration_para(chip);
	coul_cali_ops_register(&rt9426a_cali_ops);
	rt9426a_dump_register(chip);
#endif /* CONFIG_HUAWEI_POWER_EMBEDDED_ISOLATION */

	hwlog_info("chip ver=0x%04x\n", chip->ic_ver);
	queue_delayed_work(system_power_efficient_wq,
		&chip->update_work, 5 * HZ);
	return 0;
}

static int rt9426a_i2c_remove(struct i2c_client *i2c)
{
	struct rt9426a_chip *chip = i2c_get_clientdata(i2c);

	hwlog_info("%s\n", __func__);
	rt9426a_irq_enable(chip, false);
	rt9426a_irq_deinit(chip);
	mutex_destroy(&chip->update_lock);
	mutex_destroy(&chip->var_lock);

	return 0;
}

static int rt9426a_i2c_suspend(struct device *dev)
{
	struct rt9426a_chip *chip = dev_get_drvdata(dev);

	hwlog_info("%s\n", __func__);
	if (device_may_wakeup(dev))
		enable_irq_wake(chip->alert_irq);

	return 0;
}

static int rt9426a_i2c_resume(struct device *dev)
{
	struct rt9426a_chip *chip = dev_get_drvdata(dev);

	hwlog_info("%s\n", __func__);
	if (device_may_wakeup(dev))
		disable_irq_wake(chip->alert_irq);

	return 0;
}

static SIMPLE_DEV_PM_OPS(rt9426a_pm_ops, rt9426a_i2c_suspend,
	rt9426a_i2c_resume);

static const struct of_device_id rt_match_table[] = {
	{ .compatible = "richtek,rt9426a", },
	{},
};
MODULE_DEVICE_TABLE(of, rt_match_table);

static struct i2c_driver rt9426a_i2c_driver = {
	.driver = {
		.name = "rt9426a",
		.of_match_table = of_match_ptr(rt_match_table),
		.pm = &rt9426a_pm_ops,
	},
	.probe_new = rt9426a_i2c_probe,
	.remove = rt9426a_i2c_remove,
};
module_i2c_driver(rt9426a_i2c_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Honor Technologies Co., Ltd.");
MODULE_DESCRIPTION("RT9426A Fuelgauge Driver");
MODULE_VERSION("1.0.1_G");
