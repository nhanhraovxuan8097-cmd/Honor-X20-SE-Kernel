/*
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

#include "aw32280.h"
#include <huawei_platform/power/common_module/power_platform.h>
#include <chipset_common/hwpower/power_i2c.h>
#include <chipset_common/hwpower/power_gpio.h>
#include <chipset_common/hwpower/power_log.h>
#include <chipset_common/hwpower/power_thermalzone.h>
#include <chipset_common/hwpower/power_algorithm.h>
#include <chipset_common/hwpower/power_delay.h>
#include <chipset_common/hwpower/power_printk.h>
#include <linux/delay.h>

#define HWLOG_TAG aw32280
HWLOG_REGIST();

static int aw32280_config_ac_ovp_threshold_mv(struct aw32280_device_info *di);

static int aw32280_write_byte(struct aw32280_device_info *di, u16 reg, u8 value)
{
	if (!di || (di->chip_already_init == 0)) {
		hwlog_err("chip not init\n");
		return -EIO;
	}

	return power_i2c_u16_write_byte(di->client, reg, value);
}

static int aw32280_read_byte(struct aw32280_device_info *di, u16 reg, u8 *value)
{
	if (!di || (di->chip_already_init == 0)) {
		hwlog_err("chip not init\n");
		return -EIO;
	}

	return power_i2c_u16_read_byte(di->client, reg, value);
}

static ssize_t aw32280_show_attrs(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct aw32280_device_info *di = NULL;
	ssize_t len = 0;
	int ret = 0;
	u8 val = 0;
	u16 i = 0;

	if (!client)
		return 0;

	di = i2c_get_clientdata(client);
	if (!di)
		return -1;

	for (i = 0; i < AW32280_REG_MAX; i++) {
		if (!(aw32280_reg_access[i] & REG_RD_ACCESS))
			continue;
		ret = aw32280_read_byte(di, i, &val);
		if (ret) {
			pr_err("error: read register fail\n");
			return len;
		}
		len += snprintf(buf + len, PAGE_SIZE - len,
			"reg[%04X]=0x%02X\n", i, val);
	}
	return len;
}

static ssize_t aw32280_store_attrs(struct device *dev,
	struct device_attribute *attr,
	const char *buf, size_t count)
{
	unsigned int databuf[2] = {0};
	struct i2c_client *client = to_i2c_client(dev);
	struct aw32280_device_info *di = NULL;
	int ret = 0;

	if (!client)
		return 0;

	di = i2c_get_clientdata(client);
	if (!di)
		return -1;

	if (2 == sscanf(buf, "%x %x", &databuf[0], &databuf[1])) {
		ret = aw32280_write_byte(di, (u16)databuf[0], (u8)databuf[1]);
		if (ret)
			pr_err("error: write register fail\n");
	}
	return count;
}

#define aw32280_attr(_name) \
{ \
	.attr = { .name = #_name, .mode = 0664 }, \
	.show = aw32280_show_attrs, \
	.store = aw32280_store_attrs, \
}

static struct device_attribute aw32280_attrs[] = {
	aw32280_attr(registers),
};

static int aw32280_create_attrs(struct device *dev)
{
	int i;
	int rc = 0;

	for (i = 0; i < ARRAY_SIZE(aw32280_attrs); i++) {
		rc = device_create_file(dev, &aw32280_attrs[i]);
		if (rc)
			goto create_attrs_failed;
	}
	goto create_attrs_succeed;

create_attrs_failed:
	while (i--)
		device_remove_file(dev, &aw32280_attrs[i]);
create_attrs_succeed:
	return rc;
}

static int aw32280_remove_attrs(struct device *dev)
{
	int i;
	int rc = 0;

	for (i = 0; i < ARRAY_SIZE(aw32280_attrs); i++) {
		device_remove_file(dev, &aw32280_attrs[i]);
	}
	return rc;
}

static int aw32280_read_word(struct aw32280_device_info *di, u16 reg, s16 *value)
{
	u16 data = 0;

	if (!di || (di->chip_already_init == 0)) {
		hwlog_err("chip not init\n");
		return -EIO;
	}

	if (power_i2c_u16_read_word(di->client, reg, &data, false))
		return -1;

	*value = (s16)data;
	return 0;
}

static int aw32280_write_mask(struct aw32280_device_info *di,
	u16 reg, u8 mask, u8 shift, u8 value)
{
	int ret;
	u8 val = 0;

	ret = aw32280_read_byte(di, reg, &val);
	if (ret < 0)
		return ret;

	val &= ~mask;
	val |= ((value << shift) & mask);

	return aw32280_write_byte(di, reg, val);
}

static void aw32280_dump_register(struct aw32280_device_info *di)
{
	int i;
	int ret;
	u8 val = 0;

	if (!di)
		return;

	for (i = 0; i < AW32280_REG_MAX; i++) {
		if (!(aw32280_reg_access[i] & REG_RD_ACCESS))
			continue;
		ret = aw32280_read_byte(di, i, &val);
		if (ret) {
			pr_err("error: read register fail\n");
			return;
		}
		hwlog_info("aw32280_dump_register [%x]=0x%x\n", i, val);
	}
}

static int aw32280_reg_reset(struct aw32280_device_info *di)
{
	int ret;
	u8 reg = 0;

	ret = aw32280_write_byte(di, AW32280_SOFT_RST_CTRL_REG,
		AW32280_SOFT_RESET_VALUE);
	ret |= aw32280_write_byte(di, AW32280_SOFT_RST_CTRL_REG,
		AW32280_SOFT_RESET_DONE);
	if (ret) {
		pr_err("error: reg_reset write fail!\n");
		return -1;
	}
	ret = aw32280_read_byte(di, AW32280_SOFT_RST_CTRL_REG, &reg);
	if (ret) {
		pr_err("error: reg_reset read fail!\n");
		return -1;
	}
	pr_info("reg_reset [%04X]=0x%02X\n",
		AW32280_SOFT_RST_CTRL_REG, reg);

	return 0;
}

static int aw32280_fault_clear(struct aw32280_device_info *di)
{
	u8 irq_flag = 0;
	u8 irq_flag_0 = 0;
	u8 irq_flag_1 = 0;
	u8 irq_flag_2 = 0;
	u8 irq_flag_3 = 0;
	u8 irq_flag_4 = 0;
	u8 irq_flag_5 = 0;
	int ret;

	ret = aw32280_write_byte(di, AW32280_IRQ_FLAG_REG, 0xff);
	ret |= aw32280_write_byte(di, AW32280_IRQ_FLAG_0_REG, 0xff);
	ret |= aw32280_write_byte(di, AW32280_IRQ_FLAG_1_REG, 0xff);
	ret |= aw32280_write_byte(di, AW32280_IRQ_FLAG_2_REG, 0xff);
	ret |= aw32280_write_byte(di, AW32280_IRQ_FLAG_3_REG, 0xff);
	ret |= aw32280_write_byte(di, AW32280_IRQ_FLAG_4_REG, 0xff);
	ret |= aw32280_write_byte(di, AW32280_IRQ_FLAG_5_REG, 0xff);
	if (ret)
		hwlog_err("irq_work read fail\n");

	ret = aw32280_read_byte(di, AW32280_IRQ_FLAG_REG, &irq_flag);
	ret |= aw32280_read_byte(di, AW32280_IRQ_FLAG_0_REG, &irq_flag_0);
	ret |= aw32280_read_byte(di, AW32280_IRQ_FLAG_1_REG, &irq_flag_1);
	ret |= aw32280_read_byte(di, AW32280_IRQ_FLAG_2_REG, &irq_flag_2);
	ret |= aw32280_read_byte(di, AW32280_IRQ_FLAG_3_REG, &irq_flag_3);
	ret |= aw32280_read_byte(di, AW32280_IRQ_FLAG_4_REG, &irq_flag_4);
	ret |= aw32280_read_byte(di, AW32280_IRQ_FLAG_5_REG, &irq_flag_5);
	if (ret)
		hwlog_err("irq_work read fail\n");

	hwlog_info("aw32280_fault_clear irq_flag [%x]=0x%x\n", AW32280_IRQ_FLAG_REG, irq_flag);
	hwlog_info("aw32280_fault_clear irq_flag_0 [%x]=0x%x\n", AW32280_IRQ_FLAG_0_REG, irq_flag_0);
	hwlog_info("aw32280_fault_clear irq_flag_1 [%x]=0x%x\n", AW32280_IRQ_FLAG_1_REG, irq_flag_1);
	hwlog_info("aw32280_fault_clear irq_flag_2 [%x]=0x%x\n", AW32280_IRQ_FLAG_2_REG, irq_flag_2);
	hwlog_info("aw32280_fault_clear irq_flag_3 [%x]=0x%x\n", AW32280_IRQ_FLAG_3_REG, irq_flag_3);
	hwlog_info("aw32280_fault_clear irq_flag_4 [%x]=0x%x\n", AW32280_IRQ_FLAG_4_REG, irq_flag_4);
	hwlog_info("aw32280_fault_clear irq_flag_5 [%x]=0x%x\n", AW32280_IRQ_FLAG_5_REG, irq_flag_5);

	return 0;
}

static int aw32280_ic_set_sc_mode(int mode, void *dev_data)
{
	int ret;
	u8 reg = 0;
	u8 value = mode & AW32280_SC_SC_MODE_MASK;
	struct aw32280_device_info *di = (struct aw32280_device_info *)dev_data;

	if (!di)
		return -1;

	ret = aw32280_write_mask(di, AW32280_SC_SC_MODE_REG,
		AW32280_SC_SC_MODE_MASK, AW32280_SC_SC_MODE_SHIFT,
		value);
	if (ret)
		return -1;

	ret = aw32280_read_byte(di, AW32280_SC_SC_MODE_REG, &reg);
	if (ret)
		return -1;

	hwlog_info("aw32280_ic_set_sc_mode [%x]=0x%x role %d\n", AW32280_SC_SC_MODE_REG, reg, di->ic_role);

	/* if sc mode is setted, need to set ac ovp vaule */
	aw32280_config_ac_ovp_threshold_mv(di);
	return 0;
}

#if 0
static int aw32280_get_charge_sc_mode(void *dev_data)
{
	int ret;
	u8 reg = 0;
	int value;
	struct aw32280_device_info *di = (struct aw32280_device_info *)dev_data;

	if (!di)
		return -1;

	ret = aw32280_read_byte(di, AW32280_SC_SC_MODE_REG, &reg);
	if (ret)
		return -1;

	hwlog_info("aw32280_get_charge_sc_mode [%x]=0x%x role %d\n", AW32280_SC_SC_MODE_REG, reg, di->ic_role);

	value = (int)(reg & AW32280_SC_SC_MODE_MASK);
	return value;
}
#endif

static int aw32280_ic_set_ovp_gate(int ovp_gate, void *dev_data)
{
	return 0;
}

static int aw32280_charge_enable(int enable, void *dev_data)
{
	int ret;
	u8 reg = 0;
	u8 value = enable ? 0x1 : 0x0;
	struct aw32280_device_info *di = (struct aw32280_device_info *)dev_data;

	gpio_set_value(di->gpio_en, enable);

	if (!di)
		return -1;

	ret = aw32280_write_mask(di, AW32280_SC_SC_EN_REG,
		AW32280_SC_SC_EN_MASK, AW32280_SC_SC_EN_SHIFT,
		value);
	if (ret)
		return -1;

	ret = aw32280_read_byte(di, AW32280_SC_SC_EN_REG, &reg);
	if (ret)
		return -1;

	hwlog_info("charge_enable [%x]=0x%x\n", AW32280_SC_SC_EN_REG, reg);

	return 0;
}

static int aw32280_adc_enable(int enable, void *dev_data)
{
	int ret;
	u8 reg = 0;
	u8 value = enable ? 0x1 : 0x0;
	struct aw32280_device_info *di = (struct aw32280_device_info *)dev_data;

	if (!di)
		return -1;

	ret = aw32280_write_mask(di, AW32280_HKADC_EN_REG,
		AW32280_SC_HKADC_EN_MASK, AW32280_SC_HKADC_EN_SHIFT,
		value);
	if (ret)
		return -1;

	ret = aw32280_read_byte(di, AW32280_HKADC_EN_REG, &reg);
	if (ret)
		return -1;

	hwlog_info("adc_enable [%x]=0x%x\n", AW32280_HKADC_EN_REG, reg);
	return 0;
}

static bool aw32280_is_adc_disabled(struct aw32280_device_info *di)
{
	u8 reg = 0;
	int ret;

	ret = aw32280_read_byte(di, AW32280_HKADC_EN_REG, &reg);
	if (ret || !(reg & AW32280_SC_HKADC_EN_MASK)) {
		hwlog_info("AW32280_HKADC_EN_REG [%x]=0x%x\n", AW32280_HKADC_EN_REG, reg);
		return true;
	}

	return false;
}

static int aw32280_discharge(int enable, void *dev_data)
{
	return 0;
}

static int aw32280_is_device_close(void *dev_data)
{
	u8 reg = 0;
	int ret;
	struct aw32280_device_info *di = (struct aw32280_device_info *)dev_data;

	if (!di)
		return 1;

	ret = aw32280_read_byte(di, AW32280_SC_SC_EN_REG, &reg);
	if (ret)
		return 1;

	if (reg & AW32280_SC_SC_EN_MASK)
		return 0;

	return 1;
}

static int aw32280_get_device_id(void *dev_data)
{
	u8 part_info = 0;
	int ret;
	struct aw32280_device_info *di = (struct aw32280_device_info *)dev_data;

	if (!di)
		return -1;

	if (di->get_id_time == AW32280_USED)
		return di->device_id;

	di->get_id_time = AW32280_USED;
	ret = aw32280_read_byte(di, AW32280_CHIP_ID_5_REG, &part_info);
	if (ret) {
		di->get_id_time = AW32280_NOT_USED;
		hwlog_err("get_device_id read fail\n");
		return -1;
	}
	hwlog_info("get_device_id [%x]=0x%x\n",
		AW32280_CHIP_ID_5_REG, part_info);

	part_info = part_info & AW32280_DEVICE_ID_MASK;
	switch (part_info) {
	case AW32280_DEVICE_ID_AW32280:
	case AW32280_DEVICE_ID_AW32280_NEW:
		di->device_id = SWITCHCAP_AW32280;
		break;
	default:
		di->device_id = -1;
		hwlog_err("device id not match\n");
		break;
	}

	return di->device_id;
}

static int aw32280_hkadc_sample_completed(void *dev_data)
{
	int ret;
	int count = 0;
	u8 data_valid = 0;

	struct aw32280_device_info *di = (struct aw32280_device_info *)dev_data;

	if (!di)
		return -1;

	ret = aw32280_write_mask(di, AW32280_HKADC_START_REG,
		AW32280_HKADC_START_MASK, AW32280_HKADC_START_SHIFT,
		AW32280_HKADC_START_CONVERSION);
	if (ret)
		return -1;

	while (count < AW32280_HKADC_SAMPLE_TIMES) {
		ret = aw32280_read_byte(di, AW32280_HKADC_DATA_VALID_REG, &data_valid);
		if (ret == 0 &&
			(data_valid & AW32280_HKADC_DATA_VALID_MASK) == AW32280_HKADC_DATA_VALID) {
			return AW32280_HKADC_SAMPLE_COMPLETED;
		} else {
			power_usleep(150);
		}
	}

	hwlog_err("data_valid=0x%x\n", data_valid);

	return AW32280_HKADC_SAMPLE_NOT_COMPLETED;
}

static int aw32280_get_vbat_mv(void *dev_data)
{
	s16 data = 0;
	int ret;
	int vbat;
	struct aw32280_device_info *di = (struct aw32280_device_info *)dev_data;

	if (!di)
		return -1;

	if (aw32280_is_adc_disabled(di) || (!aw32280_hkadc_sample_completed(di)))
		return 0;

	ret = aw32280_read_word(di, AW32280_VBAT1_ADC_L_REG, &data);
	if (ret)
		return -1;

	hwlog_info("vbat = %d mv\n", data);

	vbat = (int)(data);

	return vbat;
}

static int aw32280_get_ibat_ma(int *ibat, void *dev_data)
{
	int ret;
	s16 data = 0;
	struct aw32280_device_info *di = (struct aw32280_device_info *)dev_data;

	if (!ibat || !di)
		return -1;

	if (aw32280_is_adc_disabled(di) || (!aw32280_hkadc_sample_completed(di))) {
		*ibat = 0;
		return 0;
	}

	ret = aw32280_read_word(di, AW32280_IBAT1_ADC_L_REG, &data);
	if (ret)
		return -1;

	*ibat = (int)((data) * AW32208_IBAT_ADC_STEP / AW32208_IBAT_ADC_SCALE);
	hwlog_info("ibat = %d mA\n", *ibat);

	return 0;
}

static int aw32280_get_ibus_ma(int *ibus, void *dev_data)
{
	s16 data = 0;
	int ret;
	struct aw32280_device_info *di = (struct aw32280_device_info *)dev_data;

	if (!di || !ibus)
		return -1;

	if (aw32280_is_adc_disabled(di) || (!aw32280_hkadc_sample_completed(di))) {
		*ibus = 0;
		return 0;
	}

	ret = aw32280_read_word(di, AW32280_IBUS_ADC_L_REG, &data);
	if (ret)
		return -1;

	hwlog_info("ibus = %d ma\n", data);

	*ibus = (int)(data);

	return 0;
}

static int aw32280_get_vbus_mv(int *vbus, void *dev_data)
{
	int ret;
	s16 data = 0;
	struct aw32280_device_info *di = (struct aw32280_device_info *)dev_data;

	if (!di || !vbus)
		return -1;

		if (aw32280_is_adc_disabled(di) || (!aw32280_hkadc_sample_completed(di))) {
		*vbus = 0;
		return 0;
	}

	ret = aw32280_read_word(di, AW32280_VBUS_ADC_L_REG, &data);
	if (ret)
		return -1;

	hwlog_info("vbus = %d mv\n", data);

	*vbus = (int)(data);

	return 0;
}

static int aw32280_get_raw_data(int adc_channel, long *data, void *dev_data)
{
	int adc_value;
	struct aw32280_device_info *di = (struct aw32280_device_info *)dev_data;
	struct adc_comp_data comp_data = { 0 };

	if (!di || !data)
		return -1;

	adc_value = power_platform_get_adc_sample(adc_channel);
	if (adc_value < 0)
		return -1;

	comp_data.adc_accuracy = di->adc_accuracy;
	comp_data.adc_v_ref = di->adc_v_ref;
	comp_data.v_pullup = di->v_pullup;
	comp_data.r_pullup = di->r_pullup;
	comp_data.r_comp = di->r_comp;

	*data = (long)power_get_adc_compensation_value(adc_value, &comp_data);
	if (*data < 0)
		return -1;

	return 0;
}

static int aw32280_is_tsbat_disabled(void *dev_data)
{
	return 0;
}

static int aw32280_get_device_temp(int *temp, void *dev_data)
{
	s16 data;
	int ret;

	struct aw32280_device_info *di = (struct aw32280_device_info *)dev_data;

	if (!temp || !di)
		return -1;

	if (aw32280_is_adc_disabled(di) || (!aw32280_hkadc_sample_completed(di))) {
		*temp = 0;
		return 0;
	}

	ret = aw32280_read_word(di, AW32280_TDIE_ADC_L_REG, &data);
	if (ret)
		return -1;

	/* calculate method */
	*temp = (int)((130851800 - data * 25 * 10000000 / 4096)
			/ 423918 / 10);
	hwlog_info("aw32280_get_device_temp=%d\n", *temp);

	return 0;
}

static int aw32280_get_vusb_mv(int *vusb, void *dev_data)
{
	int ret;
	s16 data = 0;
	struct aw32280_device_info *di = (struct aw32280_device_info *)dev_data;

	if (!vusb || !di)
		return -1;

	if (aw32280_is_adc_disabled(di) || (!aw32280_hkadc_sample_completed(di))) {
		*vusb = 0;
		return 0;
	}

	ret = aw32280_read_word(di, AW32280_VUSB_ADC_L_REG, &data);
	if (ret)
		return -1;

	hwlog_info("vusb = %d mv\n", data);

	*vusb = (int)(data);

	return 0;
}

static int aw32280_get_vout_mv(int *vout, void *dev_data)
{
	int ret;
	s16 data = 0;
	struct aw32280_device_info *di = (struct aw32280_device_info *)dev_data;

	if (!vout || !di)
		return -1;

	if (aw32280_is_adc_disabled(di) || (!aw32280_hkadc_sample_completed(di))) {
		*vout = 0;
		return 0;
	}

	ret = aw32280_read_word(di, AW32280_VOUT1_ADC_L_REG, &data);
	if (ret)
		return -1;

	hwlog_info("vout = %d mv\n", data);

	*vout = (int)(data);

	return 0;
}

static int aw32280_get_register_head(char *buffer, int size, void *dev_data)
{
	struct aw32280_device_info *di = (struct aw32280_device_info *)dev_data;

	if (!buffer || !di)
		return -1;

	if (di->ic_role == CHARGE_IC_TYPE_MAIN)
		snprintf(buffer, size,
			"   Ibus   Vbus   Ibat   Vusb   Vout   Vbat   Temp");
	else
		snprintf(buffer, size,
			"   Ibus1  Vbus1  Ibat1  Vusb1  Vout1  Vbat1  Temp1");

	return 0;
}

static int aw32280_value_dump(char *buffer, int size, void *dev_data)
{
	int ibus = 0;
	int vbus = 0;
	int ibat = 0;
	int vusb = 0;
	int vout = 0;
	int temp = 0;
	struct aw32280_device_info *di = (struct aw32280_device_info *)dev_data;

	if (!buffer || !di)
		return -1;

	aw32280_get_ibus_ma(&ibus, dev_data);
	aw32280_get_vbus_mv(&vbus, dev_data);
	aw32280_get_ibat_ma(&ibat, dev_data);
	aw32280_get_vusb_mv(&vusb, dev_data);
	aw32280_get_vout_mv(&vout, dev_data);
	aw32280_get_device_temp(&temp, dev_data);

	if (di->ic_role == CHARGE_IC_TYPE_MAIN)
		snprintf(buffer, size,
			"     %-7d%-7d%-7d%-7d%-7d%-7d%-7d",
			ibus, vbus, ibat, vusb, vout,
			aw32280_get_vbat_mv(dev_data), temp);
	else
		snprintf(buffer, size,
			"%-7d%-7d%-7d%-7d%-7d%-7d%-7d  ",
			ibus, vbus, ibat, vusb, vout,
			aw32280_get_vbat_mv(dev_data), temp);

	return 0;
}

static int aw32280_kick_watchdog(void *dev_data)
{
	int ret;
	struct aw32280_device_info *di = (struct aw32280_device_info *)dev_data;

	ret = aw32280_write_mask(di, AW32280_WDT_SOFT_RST_REG,
		AW32280_WD_RST_N_MASK, AW32280_WD_RST_N_SHIFT,
		AW32280_WD_RST);
	if (ret)
		return -1;

	return ret;
}

static int aw32280_config_watchdog_ms(int time, void *dev_data)
{
	u8 val;
	u8 reg;
	int ret;
	struct aw32280_device_info *di = (struct aw32280_device_info *)dev_data;

	if (!di)
		return -1;

	if (time >= AW32280_SC_WATCHDOG_TIMER_5000MS)
		val = AW32280_SC_WATCHDOG_SET_5000MS;
	else if (time >= AW32280_SC_WATCHDOG_TIMER_2000MS)
		val = AW32280_SC_WATCHDOG_SET_2000MS;
	else if (time >= AW32280_SC_WATCHDOG_TIMER_1000MS)
		val = AW32280_SC_WATCHDOG_SET_1000MS;
	else
		val = AW32280_SC_WATCHDOG_SET_500MS;

	ret = aw32280_write_mask(di, AW32280_WDT_CTRL_REG,
		AW32280_SC_WATCHDOG_TIMER_MASK, AW32280_SC_WATCHDOG_TIMER_SHIFT,
		val);
	if (ret)
		return -1;

	ret = aw32280_read_byte(di, AW32280_WDT_CTRL_REG, &reg);
	if (ret)
		return -1;

	hwlog_info("config_watchdog_ms [%x]=0x%x\n", AW32280_WDT_CTRL_REG, reg);

	return 0;
}

static int aw32280_config_vbat_ovp_threshold_mv(struct aw32280_device_info *di,
	int ovp_threshold)
{
	u8 value;
	int ret;

	if (ovp_threshold < AW32280_DA_FWD_VBAT_OVP_BASE)
		ovp_threshold = AW32280_DA_FWD_VBAT_OVP_BASE;

	if (ovp_threshold > AW32280_DA_FWD_VBAT_OVP_MAX)
		ovp_threshold = AW32280_DA_FWD_VBAT_OVP_MAX;

	value = (u8)((ovp_threshold - AW32280_DA_FWD_VBAT_OVP_BASE) /
		AW32280_DA_FWD_VBAT_OVP_STEP);
	ret = aw32280_write_mask(di, AW32280_DA_FWD_VBAT1_OVP_SEL_REG,
		AW32280_DA_FWD_VBAT_OVP_MASK, AW32280_DA_FWD_VBAT_OVP_SHIFT,
		value);
	if (ret)
		return -1;

	hwlog_info("config_vbat_ovp_threshold_mv [%x]=0x%x\n",
		AW32280_DA_FWD_VBAT1_OVP_SEL_REG, value);

	return 0;
}

static int aw32280_config_vout_ovp_threshold_mv(struct aw32280_device_info *di,
	int ovp_threshold)
{
	u8 value;
	int ret;

	if (ovp_threshold < AW32280_DA_SC_VOUT_OVP_SEL_4600MV)
		ovp_threshold = AW32280_DA_SC_VOUT_OVP_SEL_4600MV;

	if (ovp_threshold > AW32280_DA_SC_VOUT_OVP_SEL_5200MV)
		ovp_threshold = AW32280_DA_SC_VOUT_OVP_SEL_5200MV;

	value = (u8)((ovp_threshold - AW32280_DA_SC_VOUT_OVP_SEL_4600MV) /
		AW32280_DA_SC_VOUT_OVP_SEL_STEP);

	ret = aw32280_write_mask(di, AW32280_DA_SC_VOUT_OVP_SEL_REG,
		AW32280_DA_SC_VOUT_OVP_SEL_MASK, AW32280_DA_SC_VOUT_OVP_SEL_SHIFT,
		value);
	if (ret)
		return -1;

	hwlog_info("aw32280_config_vout_ovp_threshold_mv [%x]=0x%x\n",
		AW32280_DA_SC_VOUT_OVP_SEL_REG, value);

	return 0;
}

static int aw32280_config_ibat_ocp_threshold_ma(struct aw32280_device_info *di,
	u8 value)
{
	int ret;

	ret = aw32280_write_mask(di, AW32280_SC_PRO_TOP_CFG_REG_4_REG,
		AW32280_DA_FWD_IBAT1_OCP_SEL_MASK, AW32280_DA_FWD_IBAT1_OCP_SEL_SHIFT,
		value);
	if (ret)
		return -1;

	hwlog_info("config_ibat_ocp_threshold_ma [%x]=0x%x\n",
		AW32280_SC_PRO_TOP_CFG_REG_4_REG, value);

	return 0;
}

static int aw32280_config_ac_ovp_threshold_mv(struct aw32280_device_info *di)
{
	/* no register to set vusb*/
	int ret;

	ret = aw32280_write_byte(di, AW32280_USB_OVP_CFG_REG_2_REG, AW32280_USB_OVP_CFG_REG_2_REG_INIT);
/*
	sc_mode = aw32280_get_charge_sc_mode(di);

	if (sc_mode == AW32280_SC_SC_MODE_F41SC) {
		ret = aw32280_write_mask(di, AW32280_USB_OVP_CFG_REG_2_REG,
			AW32280_USB_OVP_CFG_REG_2_MASK, AW32280_USB_OVP_CFG_REG_2_SHIFT,
			AW32280_USB_OVP_F41SC);
	}
	else if (sc_mode == AW32280_SC_SC_MODE_F21SC) {
		ret = aw32280_write_mask(di, AW32280_USB_OVP_CFG_REG_2_REG,
			AW32280_USB_OVP_CFG_REG_2_MASK, AW32280_USB_OVP_CFG_REG_2_SHIFT,
			AW32280_USB_OVP_F21SC);
	} else {
		ret = aw32280_write_mask(di, AW32280_USB_OVP_CFG_REG_2_REG,
			AW32280_USB_OVP_CFG_REG_2_MASK, AW32280_USB_OVP_CFG_REG_2_SHIFT,
			AW32280_USB_OVP_FBPS);
	}*/
	return 0;
}

#if 0
static int aw32280_config_psw_ovp_threshold_mv(struct aw32280_device_info *di)
{
	/* no register to set vusb*/
	int ret;
	int sc_mode = 0;

	sc_mode = aw32280_get_charge_sc_mode(di);

	if (sc_mode == AW32280_SC_SC_MODE_F41SC) {
		ret = aw32280_write_mask(di, AW32280_PSW_OVP_CFG_REG_2_REG,
			AW32280_PSW_OVP_CFG_REG_2_MASK, AW32280_PSW_OVP_CFG_REG_2_SHIFT,
			AW32280_PSW_OVP_F41SC);
	} else if (sc_mode == AW32280_SC_SC_MODE_F21SC) {
		ret = aw32280_write_mask(di, AW32280_PSW_OVP_CFG_REG_2_REG,
			AW32280_PSW_OVP_CFG_REG_2_MASK, AW32280_PSW_OVP_CFG_REG_2_SHIFT,
			AW32280_PSW_OVP_F21SC);
	} else {
		ret = aw32280_write_mask(di, AW32280_PSW_OVP_CFG_REG_2_REG,
			AW32280_PSW_OVP_CFG_REG_2_MASK, AW32280_PSW_OVP_CFG_REG_2_SHIFT,
			AW32280_PSW_OVP_FBPS);
	}
	if (ret)
		hwlog_err("aw32280_config_psw_ovp_threshold_mv fail\n");
	return 0;
}
#endif

/* vbus ovp need to be setted after sc mode setted */
static int aw32280_config_vbus_ovp_threshold(struct aw32280_device_info *di,
	u8 value)
{
	int ret = 0;

	ret |= aw32280_write_mask(di, AW32280_DA_SC_VBUS_OVP_F41SC_SEL_REG,
		AW32280_DA_SC_VBUS_OVP_F41SC_SEL_MASK, AW32280_DA_SC_VBUS_OVP_F41SC_SEL_SHIFT,
		value);

	ret |= aw32280_write_mask(di, AW32280_DA_SC_VBUS_OVP_F21SC_SEL_REG,
		AW32280_DA_SC_VBUS_OVP_F21SC_SEL_MASK, AW32280_DA_SC_VBUS_OVP_F21SC_SEL_SHIFT,
		value);

	ret |= aw32280_write_mask(di, AW32280_DA_SC_VBUS_OVP_FBPS_SEL_REG,
		AW32280_DA_SC_VBUS_OVP_FBPS_SEL_MASK, AW32280_DA_SC_VBUS_OVP_FBPS_SEL_SHIFT,
		value);
	if (ret)
		hwlog_err("aw32280_config_vbus_ovp_threshold fail\n");

	return 0;
}

#if 0
static int aw32280_config_ibus_ocp_threshold_ma(struct aw32280_device_info *di,
	int ocp_threshold)
{
	return 0;
}

static int aw32280_config_switching_frequency(struct aw32280_device_info *di,
	int data)
{
	/* change switch frquency if affect others devices */
	return 0;
}
#endif

static int aw32280_config_ibat_sns_res(struct aw32280_device_info *di, int data)
{
	int res_config;

	if (data == SENSE_R_1_MOHM)
		res_config = AW32280_DA_IBAT_SRE_SEL_1MOHM;
	else
		res_config = AW32280_DA_IBAT_SRE_SEL_2MOHM;

	return aw32280_write_mask(di, AW32280_SC_PRO_TOP_CFG_REG_6_REG,
		AW32280_DA_IBAT_SRE_SEL_MASK, AW32280_DA_IBAT_SRE_SEL_SHIFT,
		res_config);
}

static int aw32280_reg_init(struct aw32280_device_info *di)
{
	int ret;

	/* config control registers */
	ret = aw32280_write_byte(di, AW32280_SC_OVP_EN_REG,
		AW32280_SC_OVP_EN_REG_INIT);
	ret = aw32280_write_byte(di, AW32280_SC_OVP_MODE_REG,
		AW32280_SC_OVP_MODE_REG_INIT);
	ret = aw32280_write_byte(di, AW32280_SC_PSW_EN_REG,
		AW32280_SC_PSW_EN_REG_INIT);
	ret = aw32280_write_byte(di, AW32280_SC_PSW_MODE_REG,
		AW32280_SC_PSW_MODE_REG_INIT);
	/* to enable watchdong later */
	ret = aw32280_write_byte(di, AW32280_SC_WDT_EN_REG,
		AW32280_SC_WDT_EN_REG_INIT);

	/* config hkadc registers */
	ret = aw32280_write_byte(di, AW32280_HKADC_EN_REG,
		AW32280_HKADC_EN_REG_INIT);
	/* To do: sign for channels in sequence loop */
	ret = aw32280_write_byte(di, AW32280_HKADC_SEQ_CH_H_REG,
		0xFF);
	ret = aw32280_write_byte(di, AW32280_HKADC_SEQ_CH_L_REG,
		0xFF);
	/* enable signal for OTP */
	ret = aw32280_write_byte(di, AW32280_OTP_EN_REG,
		AW32280_OTP_EN_REG_INIT);
	/* bat res place low side */
	ret = aw32280_write_byte(di, AW32280_DA_IBAT1_RES_PLACE_SEL_REG,
		AW32280_DA_IBAT1_RES_PLACE_SEL_REG_INIT);

	/* config irq registers */
	// MASK IRQ, need to check which irqs need to be mask
	ret = aw32280_write_byte(di, AW32280_IRQ_MASK_REG,
		AW32280_IRQ_MASK_REG_INIT);
	ret = aw32280_write_byte(di, AW32280_IRQ_MASK_0_REG,
		AW32280_IRQ_MASK_0_REG_INIT);
	ret = aw32280_write_byte(di, AW32280_IRQ_MASK_1_REG,
		AW32280_IRQ_MASK_1_REG_INIT);
	ret = aw32280_write_byte(di, AW32280_IRQ_MASK_2_REG,
		AW32280_IRQ_MASK_2_REG_INIT);
	ret = aw32280_write_byte(di, AW32280_IRQ_MASK_3_REG,
		AW32280_IRQ_MASK_3_REG_INIT);
	ret = aw32280_write_byte(di, AW32280_IRQ_MASK_4_REG,
		AW32280_IRQ_MASK_4_REG_INIT);
	ret = aw32280_write_byte(di, AW32280_IRQ_MASK_5_REG,
		AW32280_IRQ_MASK_5_REG_INIT);

	// REG_ANA Register init, need to double check again
	ret = aw32280_write_byte(di, AW32280_REF_TOP_CFG_REG_0_REG,
		AW32280_REF_TOP_CFG_REG_0_REG_INIT);
	/* To do: whther to enable Vdrop OVP */
	ret = aw32280_write_byte(di, AW32280_USB_OVP_CFG_REG_0_REG,
		AW32280_USB_OVP_CFG_REG_0_REG_INIT);
	ret = aw32280_write_byte(di, AW32280_PSW_OVP_CFG_REG_0_REG,
		AW32280_PSW_OVP_CFG_REG_0_REG_INIT);
	ret = aw32280_write_byte(di, AW32280_SC_PRO_TOP_CFG_REG_0_REG,
		AW32280_SC_PRO_TOP_CFG_REG_0_REG_INIT);
	ret = aw32280_write_byte(di, AW32280_SC_PRO_TOP_CFG_REG_1_REG,
		AW32280_SC_PRO_TOP_CFG_REG_1_REG_INIT);
	ret = aw32280_write_byte(di, AW32280_SC_PRO_TOP_CFG_REG_2_REG,
		AW32280_SC_PRO_TOP_CFG_REG_2_REG_INIT);
	ret = aw32280_write_byte(di, AW32280_SC_PRO_TOP_CFG_REG_3_REG,
		AW32280_SC_PRO_TOP_CFG_REG_3_REG_INIT);
	ret = aw32280_write_byte(di, AW32280_SC_DET_TOP_CFG_REG_0_REG,
		AW32280_SC_DET_TOP_CFG_REG_0_REG_INIT);
	ret = aw32280_write_byte(di, AW32280_SC_DET_TOP_CFG_REG_1_REG,
		AW32280_SC_DET_TOP_CFG_REG_1_REG_INIT);
	ret = aw32280_write_byte(di, AW32280_SYS_LOGIC_CFG_REG_1_REG,
		AW32280_SYS_LOGIC_CFG_REG_1_REG_INIT);
	ret = aw32280_write_byte(di, AW32280_SC_TOP_CFG_REG_6_REG,
		AW32280_SC_TOP_CFG_REG_6_REG_INIT); // SC standalone or parallel select
	ret = aw32280_write_byte(di, AW32280_SC_PRO_TOP_CFG_REG_8_REG,
		AW32280_SC_PRO_TOP_CFG_REG_8_REG_INIT);
	ret = aw32280_write_byte(di, AW32280_SC_PRO_TOP_CFG_REG_9_REG,
		AW32280_SC_PRO_TOP_CFG_REG_9_REG_INIT);
	ret = aw32280_write_byte(di, AW32280_SC_PRO_TOP_CFG_REG_12_REG,
		AW32280_SC_PRO_TOP_CFG_REG_12_REG_INIT);
	ret = aw32280_write_byte(di, AW32280_SC_DET_TOP_CFG_REG_2_REG,
		AW32280_SC_DET_TOP_CFG_REG_2_REG_INIT); // VBUS, VUSB, VPSW resistor divider ratio select.
	ret = aw32280_write_byte(di, AW32280_SC_DET_TOP_CFG_REG_4_REG,
		AW32280_SC_DET_TOP_CFG_REG_4_REG_INIT);
	ret = aw32280_write_byte(di, AW32280_SC_DET_TOP_CFG_REG_5_REG,
		AW32280_SC_DET_TOP_CFG_REG_5_REG_INIT);

	ret += aw32280_config_vout_ovp_threshold_mv(di,
		AW32280_VOUT_OVP_THRESHOLD_INIT);
	ret += aw32280_config_vbat_ovp_threshold_mv(di,
		AW32280_VBAT_OVP_THRESHOLD_INIT);
	ret += aw32280_config_ibat_ocp_threshold_ma(di,
		AW32280_DA_FWD_IBAT1_OCP_SEL_13000MA);
	// the register to set vusb and psw
	ret += aw32280_config_ac_ovp_threshold_mv(di);
	ret += aw32280_config_vbus_ovp_threshold(di,
		AW32280_VBUS_OVP_THRESHOLD_INIT);
	ret += aw32280_config_ibat_sns_res(di, di->sense_r_config);

	return ret;
}

static int aw32280_charge_init(void *dev_data)
{
	struct aw32280_device_info *di = (struct aw32280_device_info *)dev_data;

	if (!di) {
		hwlog_err("di is null\n");
		return -1;
	}

	if (aw32280_reg_init(di))
		return -1;

	di->device_id = aw32280_get_device_id(dev_data);
	if (di->device_id == -1)
		return -1;

	hwlog_info("device id is %d\n", di->device_id);

	di->init_finish_flag = AW32280_INIT_FINISH;
	return 0;
}

static int aw32280_charge_exit(void *dev_data)
{
	int ret;
	struct aw32280_device_info *di = (struct aw32280_device_info *)dev_data;

	if (!di) {
		hwlog_err("di is null\n");
		return -1;
	}

	ret = aw32280_charge_enable(AW32280_SWITCHCAP_DISABLE, dev_data);

	di->init_finish_flag = AW32280_NOT_INIT;
	di->int_notify_enable_flag = AW32280_DISABLE_INT_NOTIFY;

	power_usleep(DT_USLEEP_10MS);

	return ret;
}

static int aw32280_batinfo_exit(void *dev_data)
{
	return 0;
}

static int aw32280_batinfo_init(void *dev_data)
{
	return 0;
}

static void aw32280_fault_event_notify(unsigned long event, void *data)
{
	struct atomic_notifier_head *fault_event_notifier_list = NULL;

	hsc_get_fault_notifier(&fault_event_notifier_list);
	atomic_notifier_call_chain(fault_event_notifier_list, event, data);
}

static void aw32280_fault_handle(struct aw32280_device_info *di,
	struct nty_data *data)
{
	int val = 0;
	// u8 irq_flag = data->event1;
	u8 irq_flag_0 = data->event2;
	// u8 irq_flag_1 = data->event3;
	u8 irq_flag_2 = data->event4;
	u8 irq_flag_3 = data->event5;
	u8 irq_flag_4 = data->event6;
	u8 irq_flag_5 = data->event7;

	if (irq_flag_0 & AW32280_IRQ_OVP_VUSB_OVP_MASK) {
		hwlog_info("USB/AC OVP happened\n");
		aw32280_fault_event_notify(DC_FAULT_AC_OVP, data);
	} else if (irq_flag_4 & AW32280_IRQ_FWD_VBAT1_OVP_MASK) {
		val = aw32280_get_vbat_mv(di);
		hwlog_info("BAT OVP happened, vbat=%d mv\n", val);
		if (val >= AW32280_VBAT_OVP_THRESHOLD_INIT)
			aw32280_fault_event_notify(DC_FAULT_VBAT_OVP, data);
	} else if (irq_flag_3 & AW32280_IRQ_FWD_IBAT1_OCP_MASK) {
		aw32280_get_ibat_ma(&val, di);
		hwlog_info("BAT OCP happened, ibat=%d ma\n", val);
		//if (val >= AW32280_IBAT_OCP_THRESHOLD_INIT)
			aw32280_fault_event_notify(DC_FAULT_IBAT_OCP, data);
	} else if (irq_flag_2 & AW32280_IRQ_SC_VBUS_OVP_MASK) {
		aw32280_get_vbus_mv(&val, di);
		hwlog_info("BUS OVP happened, vbus=%d mv\n", val);
		if (val >= AW32280_VBUS_OVP_THRESHOLD_INIT)
			aw32280_fault_event_notify(DC_FAULT_VBUS_OVP, data);
	} else if (irq_flag_4 & AW32280_IRQ_SC_OCP3_MASK) {
		aw32280_get_ibus_ma(&val, di);
		hwlog_info("BUS OCP happened, ibus=%d ma\n", val);
		//if (val >= AW32280_IBUS_OCP_THRESHOLD_INIT)
			aw32280_fault_event_notify(DC_FAULT_IBUS_OCP, data);
	} else if (irq_flag_3 & AW32280_IRQ_TBAT1_OTP_MASK) {
		hwlog_info("BAT TEMP OTP happened\n");
		aw32280_fault_event_notify(DC_FAULT_TSBAT_OTP, data);
	} else if (irq_flag_5 & AW32280_IRQ_TDIE_OTP_MASK) {
		hwlog_info("DIE TEMP OTP happened\n");
		aw32280_fault_event_notify(DC_FAULT_TDIE_OTP, data);
	} else if (irq_flag_5 & AW32280_IRQ_TDIE_OTP_ALM_MASK) {
		hwlog_info("DIE TEMP OTP ALM happened\n");
	} else {
		hwlog_info("other interrupts happened\n");
	}
}

static void aw32280_interrupt_work(struct work_struct *work)
{
	struct aw32280_device_info *di = NULL;
	struct nty_data *data = NULL;
	// u8 reg = 0;
	u8 irq_flag = 0;
	u8 irq_flag_0 = 0;
	u8 irq_flag_1 = 0;
	u8 irq_flag_2 = 0;
	u8 irq_flag_3 = 0;
	u8 irq_flag_4 = 0;
	u8 irq_flag_5 = 0;
	int ret;
	int gpio_val;

	if (!work)
		return;

	di = container_of(work, struct aw32280_device_info, irq_work);
	if (!di || !di->client) {
		hwlog_err("di is null\n");
		return;
	}

	data = &(di->nty_data);

	ret = aw32280_read_byte(di, AW32280_IRQ_FLAG_REG, &irq_flag);
	ret |= aw32280_read_byte(di, AW32280_IRQ_FLAG_0_REG, &irq_flag_0);
	ret |= aw32280_read_byte(di, AW32280_IRQ_FLAG_1_REG, &irq_flag_1);
	ret |= aw32280_read_byte(di, AW32280_IRQ_FLAG_2_REG, &irq_flag_2);
	ret |= aw32280_read_byte(di, AW32280_IRQ_FLAG_3_REG, &irq_flag_3);
	ret |= aw32280_read_byte(di, AW32280_IRQ_FLAG_4_REG, &irq_flag_4);
	ret |= aw32280_read_byte(di, AW32280_IRQ_FLAG_5_REG, &irq_flag_5);
	if (ret)
		hwlog_err("irq_work read fail\n");

	gpio_val = gpio_get_value(di->gpio_int);
	hwlog_info("check gpio_int gpio_val=%d ,role = %d\n", gpio_val, di->ic_role);

	hwlog_info("irq_flag [%x]=0x%x\n", AW32280_IRQ_FLAG_REG, irq_flag);
	hwlog_info("irq_flag_0 [%x]=0x%x\n", AW32280_IRQ_FLAG_0_REG, irq_flag_0);
	hwlog_info("irq_flag_1 [%x]=0x%x\n", AW32280_IRQ_FLAG_1_REG, irq_flag_1);
	hwlog_info("irq_flag_2 [%x]=0x%x\n", AW32280_IRQ_FLAG_2_REG, irq_flag_2);
	hwlog_info("irq_flag_3 [%x]=0x%x\n", AW32280_IRQ_FLAG_3_REG, irq_flag_3);
	hwlog_info("irq_flag_4 [%x]=0x%x\n", AW32280_IRQ_FLAG_4_REG, irq_flag_4);
	hwlog_info("irq_flag_5 [%x]=0x%x\n", AW32280_IRQ_FLAG_5_REG, irq_flag_5);

	data->event1 = irq_flag;
	data->event2 = irq_flag_0;
	data->event3 = irq_flag_1;
	data->event4 = irq_flag_2;
	data->event5 = irq_flag_3;
	data->event6 = irq_flag_4;
	data->event7 = irq_flag_5;
	data->addr = di->client->addr;

	if (di->int_notify_enable_flag == AW32280_ENABLE_INT_NOTIFY) {
		aw32280_fault_handle(di, data);
		aw32280_dump_register(di);
	}

	/* clear irq */
	ret = aw32280_write_byte(di, AW32280_IRQ_FLAG_REG, 0xff);
	ret |= aw32280_write_byte(di, AW32280_IRQ_FLAG_0_REG, 0xff);
	ret |= aw32280_write_byte(di, AW32280_IRQ_FLAG_1_REG, 0xff);
	ret |= aw32280_write_byte(di, AW32280_IRQ_FLAG_2_REG, 0xff);
	ret |= aw32280_write_byte(di, AW32280_IRQ_FLAG_3_REG, 0xff);
	ret |= aw32280_write_byte(di, AW32280_IRQ_FLAG_4_REG, 0xff);
	ret |= aw32280_write_byte(di, AW32280_IRQ_FLAG_5_REG, 0xff);
	if (ret)
		hwlog_err("irq_work clear irq fail\n");

	/* check interrupt pin stat */
	gpio_val = gpio_get_value(di->gpio_int);
	hwlog_info("double check gpio_int gpio_val=%d\n", gpio_val);

	enable_irq(di->irq_int);
}

static irqreturn_t aw32280_interrupt(int irq, void *_di)
{
	struct aw32280_device_info *di = _di;

	if (!di) {
		hwlog_err("di is null\n");
		return IRQ_HANDLED;
	}

	if (di->chip_already_init == 0)
		hwlog_err("chip not init\n");

	if (di->init_finish_flag == AW32280_INIT_FINISH)
		di->int_notify_enable_flag = AW32280_ENABLE_INT_NOTIFY;

	hwlog_info("int happened\n");
	disable_irq_nosync(di->irq_int);
	schedule_work(&di->irq_work);
	return IRQ_HANDLED;
}

static int aw32280_irq_init(struct aw32280_device_info *di,
	struct device_node *np)
{
	int ret;

	ret = power_gpio_config_interrupt(np,
		"gpio_int", "aw32280_gpio_int", &di->gpio_int, &di->irq_int);
	if (ret)
		return ret;

	ret = request_irq(di->irq_int, aw32280_interrupt,
		IRQF_TRIGGER_LOW, "aw32280_int_irq", di);
	if (ret) {
		hwlog_err("gpio irq request fail\n");
		di->irq_int = -1;
		gpio_free(di->gpio_int);
		return ret;
	}

	enable_irq_wake(di->irq_int);
	INIT_WORK(&di->irq_work, aw32280_interrupt_work);
	return 0;
}

static int aw32280_reg_reset_and_init(struct aw32280_device_info *di)
{
	int ret;

	ret = aw32280_reg_reset(di);
	if (ret) {
		hwlog_err("reg reset fail\n");
		return ret;
	}

	ret = aw32280_fault_clear(di);
	if (ret) {
		hwlog_err("fault clear fail\n");
		return ret;
	}

	ret = aw32280_reg_init(di);
	if (ret) {
		hwlog_err("reg init fail\n");
		return ret;
	}

	return 0;
}

static int aw32280_gpio_init(struct aw32280_device_info *di,
	struct device_node *np)
{
	if (power_gpio_config_output(np, "gpio_tm_eco", "aw32280_tm_eco",
		&di->gpio_tm_eco, 0)) {
		hwlog_err("gpio_tm_eco init fail\n");
		goto gpio_tm_eco_fail;
	}

	/* gpio_rst_n trigger chip reset */
	if (power_gpio_config_output(np, "gpio_rst", "aw32280_rst_n",
		&di->gpio_rst_n, AW32280_CHG_RST_N)) {
		hwlog_err("gpio_rst init fail\n");
		goto gpio_rst_n_fail;
	}

	power_usleep(DT_USLEEP_10MS); /* sleep 10ms */

	gpio_set_value(di->gpio_rst_n, 1);

	/* gpio_en */
	if (power_gpio_config_output(np, "gpio_en", "aw32280_en",
		&di->gpio_en, AW32280_CHG_ENABLE)) {
		hwlog_err("gpio_en init fail\n");
		goto gpio_en_fail;
	}

	power_usleep(DT_USLEEP_10MS); /* sleep 10ms */

	return 0;

gpio_en_fail:
	gpio_free(di->gpio_rst_n);
gpio_rst_n_fail:
	gpio_free(di->gpio_tm_eco);
gpio_tm_eco_fail:
	return -EINVAL;
}

static void aw32280_parse_dts(struct device_node *np,
	struct aw32280_device_info *di)
{
	(void)power_dts_read_u32(power_dts_tag(HWLOG_TAG), np,
		"switching_frequency", &di->switching_frequency,
		AW32280_SC_FREQ_SEL_CONFIG_600KHZ);
	(void)power_dts_read_u32(power_dts_tag(HWLOG_TAG), np,
		"ic_role", &di->ic_role, CHARGE_IC_TYPE_MAIN);
	(void)power_dts_read_u32(power_dts_tag(HWLOG_TAG), np,
		"sense_r_config", &di->sense_r_config, SENSE_R_1_MOHM);
	(void)power_dts_read_u32(power_dts_tag(HWLOG_TAG), np,
		"sense_r_actual", &di->sense_r_actual, SENSE_R_1_MOHM);
}

static struct dc_ic_ops aw32280_sysinfo_ops = {
	.dev_name = "aw32280",
	.ic_init = aw32280_charge_init,
	.ic_exit = aw32280_charge_exit,
	.ic_enable = aw32280_charge_enable,
	.ic_adc_enable = aw32280_adc_enable,
	.ic_discharge = aw32280_discharge,
	.is_ic_close = aw32280_is_device_close,
	.kick_ic_watchdog = aw32280_kick_watchdog,
	.get_ic_id = aw32280_get_device_id,
	.config_ic_watchdog = aw32280_config_watchdog_ms,
	.get_ic_status = aw32280_is_tsbat_disabled,
	.ic_set_sc_mode = aw32280_ic_set_sc_mode,
	.ic_set_ovp_gate = aw32280_ic_set_ovp_gate,
};

static struct dc_batinfo_ops aw32280_batinfo_ops = {
	.init = aw32280_batinfo_init,
	.exit = aw32280_batinfo_exit,
	.get_bat_btb_voltage = aw32280_get_vbat_mv,
	.get_bat_package_voltage = aw32280_get_vbat_mv,
	.get_vbus_voltage = aw32280_get_vbus_mv,
	.get_bat_current = aw32280_get_ibat_ma,
	.get_ic_ibus = aw32280_get_ibus_ma,
	.get_ic_temp = aw32280_get_device_temp,
	.get_ic_vusb = aw32280_get_vusb_mv,
	.get_ic_vout = aw32280_get_vout_mv,
};

static struct power_tz_ops aw32280_temp_sensing_ops = {
	.get_raw_data = aw32280_get_raw_data,
};

static struct power_log_ops aw32280_log_ops = {
	.dev_name = "aw32280",
	.dump_log_head = aw32280_get_register_head,
	.dump_log_content = aw32280_value_dump,
};

static struct dc_ic_ops aw32280_aux_sysinfo_ops = {
	.dev_name = "aw32280_aux",
	.ic_init = aw32280_charge_init,
	.ic_exit = aw32280_charge_exit,
	.ic_enable = aw32280_charge_enable,
	.ic_adc_enable = aw32280_adc_enable,
	.ic_discharge = aw32280_discharge,
	.is_ic_close = aw32280_is_device_close,
	.get_ic_id = aw32280_get_device_id,
	.kick_ic_watchdog = aw32280_kick_watchdog,
	.config_ic_watchdog = aw32280_config_watchdog_ms,
	.get_ic_status = aw32280_is_tsbat_disabled,
	.ic_set_sc_mode = aw32280_ic_set_sc_mode,
	.ic_set_ovp_gate = aw32280_ic_set_ovp_gate,
};

static struct dc_batinfo_ops aw32280_aux_batinfo_ops = {
	.init = aw32280_batinfo_init,
	.exit = aw32280_batinfo_exit,
	.get_bat_btb_voltage = aw32280_get_vbat_mv,
	.get_bat_package_voltage = aw32280_get_vbat_mv,
	.get_vbus_voltage = aw32280_get_vbus_mv,
	.get_bat_current = aw32280_get_ibat_ma,
	.get_ic_ibus = aw32280_get_ibus_ma,
	.get_ic_temp = aw32280_get_device_temp,
	.get_ic_vusb = aw32280_get_vusb_mv,
	.get_ic_vout = aw32280_get_vout_mv,
};

static struct power_tz_ops aw32280_aux_temp_sensing_ops = {
	.get_raw_data = aw32280_get_raw_data,
};

static struct power_log_ops aw32280_aux_log_ops = {
	.dev_name = "aw32280_aux",
	.dump_log_head = aw32280_get_register_head,
	.dump_log_content = aw32280_value_dump,
};

static void aw32280_init_ops_dev_data(struct aw32280_device_info *di)
{
	if (di->ic_role == CHARGE_IC_TYPE_MAIN) {
		aw32280_sysinfo_ops.dev_data = (void *)di;
		aw32280_batinfo_ops.dev_data = (void *)di;
		aw32280_temp_sensing_ops.dev_data = (void *)di;
		aw32280_log_ops.dev_data = (void *)di;
	} else {
		aw32280_aux_sysinfo_ops.dev_data = (void *)di;
		aw32280_aux_batinfo_ops.dev_data = (void *)di;
		aw32280_aux_temp_sensing_ops.dev_data = (void *)di;
		aw32280_aux_log_ops.dev_data = (void *)di;
	}
}

static int aw32280_ops_register(struct aw32280_device_info *di)
{
	int ret;

	aw32280_init_ops_dev_data(di);

	if (di->ic_role == CHARGE_IC_TYPE_MAIN) {
		ret = dc_ic_ops_register(HSC_MODE, di->ic_role,
			&aw32280_sysinfo_ops);
		ret |= dc_batinfo_ops_register(HSC_MODE, di->ic_role,
			&aw32280_batinfo_ops);
	} else {
		ret = dc_ic_ops_register(HSC_MODE, di->ic_role,
			&aw32280_aux_sysinfo_ops);
		ret |= dc_batinfo_ops_register(HSC_MODE, di->ic_role,
			&aw32280_aux_batinfo_ops);
	}
	if (ret) {
		hwlog_err("sysinfo ops register fail\n");
		return ret;
	}

	if (di->ic_role == CHARGE_IC_TYPE_MAIN) {
		ret = power_tz_ops_register(&aw32280_temp_sensing_ops,
			"aw32280");
		ret |= power_log_ops_register(&aw32280_log_ops);
	} else {
		ret = power_tz_ops_register(&aw32280_aux_temp_sensing_ops,
			"aw32280_aux");
		ret |= power_log_ops_register(&aw32280_aux_log_ops);
	}
	if (ret)
		hwlog_err("thermalzone or power log ops register fail\n");

	return 0;
}

void aw32280_hkadc_start_cfg_work(struct work_struct *work)
{
	struct aw32280_device_info *di =
		container_of(work, struct aw32280_device_info, hkadc_start_cfg_work.work);
	int ret;

	ret = aw32280_write_mask(di, AW32280_HKADC_CTRL1_REG,
		AW32280_HKADC_SEQ_LOOP_MASK, AW32280_HKADC_SEQ_LOOP_SHIFT,
		AW32280_HKADC_SEQ_LOOP_EN);

	ret |= aw32280_adc_enable(AW32280_SC_HKADC_EN, di);

	ret |= aw32280_write_byte(di, AW32280_HKADC_SEQ_CH_H_REG, 0x14);

	ret |= aw32280_write_byte(di, AW32280_HKADC_SEQ_CH_L_REG, 0xAF);

	ret |= aw32280_write_mask(di, AW32280_HKADC_RD_SEQ_REG,
		AW32280_HKADC_RD_SEQ_MASK, AW32280_HKADC_RD_SEQ_SHIFT, AW32280_HKADC_RD_SEQ_REQ);

	if(!aw32280_hkadc_sample_completed(di))
		return;
}

static int aw32280_probe(struct i2c_client *client,
	const struct i2c_device_id *id)
{
	int ret;
	struct aw32280_device_info *di = NULL;
	struct device_node *np = NULL;

	hwlog_info("aw32280_probe");

	if (!client || !client->dev.of_node || !id)
		return -ENODEV;

	di = devm_kzalloc(&client->dev, sizeof(*di), GFP_KERNEL);
	if (!di)
		return -ENOMEM;

	di->dev = &client->dev;
	np = di->dev->of_node;
	di->client = client;
	i2c_set_clientdata(client, di);

	di->chip_already_init = 1;

	/* do chip reset, or regiters read fail */
	ret = aw32280_gpio_init(di, np);
	if (ret)
		goto aw32280_fail_0;

	/* disable chip suspend, or register's val is 0 */
	ret = aw32280_write_byte(di, AW32280_ECO_MODE_REG, 0x1);

	ret |= aw32280_get_device_id(di);
	if (ret < 0)
		goto aw32280_fail_1;

	aw32280_parse_dts(np, di);

	ret = aw32280_reg_reset_and_init(di);
	if (ret)
		goto aw32280_fail_1;

	ret = aw32280_irq_init(di, np);
	if (ret)
		goto aw32280_fail_1;

	ret = aw32280_create_attrs(di->dev);
	if (ret)
		goto aw32280_fail_2;

	ret = aw32280_ops_register(di);
	if (ret)
		goto aw32280_fail_3;

	/* use adc loop to calculate ibus,ibat,etc.
	 * we use oneshot mode now, not schedule the work
	 */
	INIT_DELAYED_WORK(&di->hkadc_start_cfg_work, aw32280_hkadc_start_cfg_work);

	return 0;

aw32280_fail_3:
	aw32280_remove_attrs(di->dev);
aw32280_fail_2:
	free_irq(di->irq_int, di);
	gpio_free(di->gpio_int);
aw32280_fail_1:
	gpio_free(di->gpio_rst_n);
	gpio_free(di->gpio_en);
	gpio_free(di->gpio_tm_eco);
aw32280_fail_0:
	di->chip_already_init = 0;
	devm_kfree(&client->dev, di);

	return ret;
}

static int aw32280_remove(struct i2c_client *client)
{
	struct aw32280_device_info *di = i2c_get_clientdata(client);

	if (!di)
		return -ENODEV;

	if (di->irq_int)
		free_irq(di->irq_int, di);

	if (di->gpio_int)
		gpio_free(di->gpio_int);

	return 0;
}

static void aw32280_shutdown(struct i2c_client *client)
{
	struct aw32280_device_info *di = i2c_get_clientdata(client);

	if (!di)
		return;

	/* soft reset, and check adc si disnable */
	aw32280_reg_reset(di);
}

#ifdef CONFIG_PM
static int aw32280_i2c_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct aw32280_device_info *di = NULL;

	if (!client)
		return 0;

	di = i2c_get_clientdata(client);
	if (di)
		aw32280_adc_enable(0, (void *)di);

	return 0;
}
#ifdef CONFIG_I2C_OPERATION_IN_COMPLETE
static void aw32280_i2c_complete(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct aw32280_device_info *di = NULL;

	if (!client)
		return;

	hwlog_info("sc complete enter\n");
	di = i2c_get_clientdata(client);
	if (di)
		aw32280_adc_enable(1, (void *)di);
}

static int aw32280_i2c_resume(struct device *dev)
{
	return 0;
}
#else
static int aw32280_i2c_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct aw32280_device_info *di = NULL;

	if (!client)
		return 0;

	di = i2c_get_clientdata(client);
	if (di)
		aw32280_adc_enable(1, (void *)di);

	return 0;
}
#endif
static const struct dev_pm_ops aw32280_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(aw32280_i2c_suspend, aw32280_i2c_resume)
#ifdef CONFIG_I2C_OPERATION_IN_COMPLETE
	.complete = aw32280_i2c_complete,
#endif
};
#define AW32280_PM_OPS (&aw32280_pm_ops)
#else
#define AW32280_PM_OPS (NULL)
#endif /* CONFIG_PM */

MODULE_DEVICE_TABLE(i2c, aw32280);
static const struct of_device_id aw32280_of_match[] = {
	{
		.compatible = "aw32280",
		.data = NULL,
	},
	{},
};

static const struct i2c_device_id aw32280_i2c_id[] = {
	{ "aw32280", 0 },
	{}
};

static struct i2c_driver aw32280_driver = {
	.probe = aw32280_probe,
	.remove = aw32280_remove,
	.shutdown = aw32280_shutdown,
	.id_table = aw32280_i2c_id,
	.driver = {
		.owner = THIS_MODULE,
		.name = "aw32280",
		.of_match_table = of_match_ptr(aw32280_of_match),
		.pm = AW32280_PM_OPS,
	},
};

static int __init aw32280_init(void)
{
	return i2c_add_driver(&aw32280_driver);
}

static void __exit aw32280_exit(void)
{
	i2c_del_driver(&aw32280_driver);
}

module_init(aw32280_init);
module_exit(aw32280_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("aw32280 module driver");
MODULE_AUTHOR("Honor Technologies Co., Ltd.");
