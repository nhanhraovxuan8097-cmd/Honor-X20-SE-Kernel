/*
 * fingerprint.c
 *
 * fingerprint driver
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
#include "fingerprint.h"
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_gpio.h>
#include <linux/of_platform.h>
#include <linux/regulator/consumer.h>
#include <linux/spi/spi.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/platform_device.h>
#include <linux/arm-smccc.h>
#include <securec.h>
#if defined(CONFIG_FB)
#include <linux/notifier.h>
#include <linux/fb.h>
#endif
#include <linux/platform_data/spi-mt65xx.h>
#include <huawei_platform/touchscreen_interface/touchscreen_interface.h>
#define OPTICAL   1
#define COMMON    0
#define NORMAL_TEMPERATURE 0
#define INIT_STATE        0
#define FINGER_DOWN_STATE 1
#define FINGER_UP_STATE   2
#define HOVER_DOWN_STATE  3
#define HOVER_UP_STATE    4
#define FP_STATUS_OPEN  1
#define FP_STATUS_CLOSE 0
#define HBM_WAIT_TIMEOUT (50 * HZ / 1000)
#define FINGER_UP_WAIT_TIMEOUT (500 * HZ / 1000)
#define DECIMAL               10
#define RESET_DELAY_TIME_10MS 10
#define BITS_PER_WORD         8
#define FINGERPRINT_BUFF_SIZE 1024
#define BTB_DET_IRQ_DEBOUNCE  100
#define BTB_DET_IRQ_MAX_CNT   5

static unsigned int g_snr_flag;
static struct fp_data *g_fp_data;
static bool g_close_tp_irq;
static bool g_wait_finger_up_flag;
static int g_tp_state;
static bool g_btb_irq_flag;
static uint32_t g_btb_irq_count;

static int ud_fingerprint_irq_notify(struct tp_to_udfp_data *tp_data);
void mt_spi_disable_master_clk(struct spi_device *spidev);
void mt_spi_enable_master_clk(struct spi_device *spidev);
struct ud_fp_ops g_ud_fp_ops = {
	.fp_irq_notify = ud_fingerprint_irq_notify,
};

#if defined(CONFIG_HUAWEI_DSM)
#include <dsm/dsm_pub.h>
static struct dsm_dev g_dsm_fingerprint = {
	.name = "dsm_fingerprint",
	.device_name = "fpc",
	.ic_name = "NNN",
	.module_name = "NNN",
	.fops = NULL,
	.buff_size = FINGERPRINT_BUFF_SIZE,
};

static struct dsm_client *g_fingerprint_dclient;
#endif

enum spi_clk_state {
	SPI_CLK_DISABLE = 0,
	SPI_CLK_ENABLE,
};

static void fingerprint_gpio_output_dts(struct fp_data *fingerprint,
	int pin, int level)
{
	if (!fingerprint) {
		pr_err("%s, failed, the pointer is null\n", __func__);
		return;
	}

	switch (pin) {
	case FINGERPRINT_RST_PIN:
		if (level)
			pinctrl_select_state(fingerprint->pinctrl,
				fingerprint->fp_rst_high);
		else
			pinctrl_select_state(fingerprint->pinctrl,
				fingerprint->fp_rst_low);
		break;
	case FINGERPRINT_SPI_CS_PIN:
		if (level)
			pinctrl_select_state(
				fingerprint->pinctrl, fingerprint->fp_cs_high);
		else
			pinctrl_select_state(
				fingerprint->pinctrl, fingerprint->fp_cs_low);
		break;
	case FINGERPRINT_SPI_MO_PIN:
		if (level)
			pinctrl_select_state(
				fingerprint->pinctrl, fingerprint->fp_mo_high);
		else
			pinctrl_select_state(
				fingerprint->pinctrl, fingerprint->fp_mo_low);
		break;
	case FINGERPRINT_SPI_CK_PIN:
		if (level)
			pinctrl_select_state(
				fingerprint->pinctrl, fingerprint->fp_ck_high);
		else
			pinctrl_select_state(
				fingerprint->pinctrl, fingerprint->fp_ck_low);
		break;
	case FINGERPRINT_SPI_MI_PIN:
		if (level)
			pinctrl_select_state(
				fingerprint->pinctrl, fingerprint->fp_mi_high);
		else
			pinctrl_select_state(
				fingerprint->pinctrl, fingerprint->fp_mi_low);
		break;
	case FINGERPRINT_POWER_EN_PIN:
		if (level)
			pinctrl_select_state(fingerprint->pinctrl,
				fingerprint->fp_power_high);
		else
			pinctrl_select_state(fingerprint->pinctrl,
				fingerprint->fp_power_low);
		break;
	default:
		break;
	}
}

static ssize_t result_show(struct device *device,
	struct device_attribute *attribute, char *buffer)
{
	struct fp_data *fingerprint = dev_get_drvdata(device);

	if (!fingerprint) {
		pr_err("%s, failed, the pointer is null\n", __func__);
		return -EINVAL;
	}

	return scnprintf(buffer,
		PAGE_SIZE, "%d\n", fingerprint->autotest_input);
}

static ssize_t result_store(struct device *device,
	struct device_attribute *attribute, const char *buffer, size_t count)
{
	struct fp_data *fingerprint = dev_get_drvdata(device);
	unsigned long input_data = 0;
	int err;

	if (!fingerprint) {
		pr_err("%s, failed, the pointer is null\n", __func__);
		return -EINVAL;
	}

	err = kstrtoul(buffer, DECIMAL, &input_data);
	if (err) {
		pr_err("buffer covert fail err = %d\n", err);
		return -EINVAL;
	}
	fingerprint->autotest_input = (unsigned int)input_data;
	sysfs_notify(&(fingerprint->pf_dev->dev.kobj), NULL, "result");
	return (ssize_t)count;
}

static DEVICE_ATTR(result, 0600, result_show, result_store);

/*
 * sysf node to check the interrupt status of the sensor, the interrupt
 * handler should perform sysf_notify to allow userland to poll the node
 */
static ssize_t irq_get(struct device *device,
	struct device_attribute *attribute, char *buffer)
{
	int irq;
	struct fp_data *fingerprint = dev_get_drvdata(device);

	if (!fingerprint) {
		pr_err("%s, failed, the pointer is null\n", __func__);
		return -EINVAL;
	}

	if (fingerprint->use_tp_irq == USE_TP_IRQ) {
		irq = g_fp_data->tp_event;
		pr_info("%s: USE_TP_IRQ, tp_event = %d\n",
			__func__, irq);
		return (ssize_t)scnprintf(buffer, PAGE_SIZE, "%d\n", irq);
	}
	irq = __gpio_get_value(fingerprint->irq_gpio);
	pr_info("[fpc] %s : %d\n", __func__, irq);

	return scnprintf(buffer, PAGE_SIZE, "%d\n", irq);
}

/*
 * writing to the irq node will just drop a printk message
 * and return success, used for latency measurement
 */
static ssize_t irq_ack(struct device *device,
	struct device_attribute *attribute, const char *buffer, size_t count)
{
	struct tp_to_udfp_data data = {0};
	int err;
	long value = 0;

	err = kstrtol(buffer, DECIMAL, &value);
	if (err < 0) {
		pr_err("%s: strict_strtol failed err %d!\n",
			__func__, err);
		return -EINVAL;
	}

	data.udfp_event = (int)value;
	pr_info("%s: udfp_event: %d\n", __func__, data.udfp_event);

	(void)ud_fingerprint_irq_notify(&data);
	return (ssize_t)count;
}

static DEVICE_ATTR(irq, 0600, irq_get, irq_ack);

static ssize_t read_image_flag_show(struct device *device,
	struct device_attribute *attribute, char *buffer)
{
	struct fp_data *fingerprint = dev_get_drvdata(device);

	if (!fingerprint) {
		pr_err("%s, failed, the pointer is null\n", __func__);
		return -EINVAL;
	}
	return scnprintf(buffer, PAGE_SIZE, "%u",
		(unsigned int)fingerprint->read_image_flag);
}

static ssize_t read_image_flag_store(struct device *device,
	struct device_attribute *attribute, const char *buffer, size_t count)
{
	struct fp_data *fingerprint = dev_get_drvdata(device);
	unsigned long image_flag = 0;
	int err;

	if (!fingerprint) {
		pr_err("%s, failed, the pointer is null\n", __func__);
		return -EINVAL;
	}

	err = kstrtoul(buffer, DECIMAL, &image_flag);
	if (err) {
		pr_err("%s, buffer convert fail\n", __func__);
		return -EINVAL;
	}

	if (image_flag == 0)
		fingerprint->read_image_flag = false;
	else
		fingerprint->read_image_flag = true;
	return (ssize_t)count;
}

static DEVICE_ATTR(read_image_flag, 0600, read_image_flag_show,
	read_image_flag_store);

static ssize_t snr_show(struct device *device,
	struct device_attribute *attribute, char *buffer)
{
	struct fp_data *fingerprint = dev_get_drvdata(device);

	if (!fingerprint) {
		pr_err("%s failed, the pointer is null\n", __func__);
		return -EINVAL;
	}

	return scnprintf(buffer, PAGE_SIZE, "%d", fingerprint->snr_stat);
}

static ssize_t snr_store(struct device *device,
	struct device_attribute *attribute, const char *buffer, size_t count)
{
	struct fp_data *fingerprint = dev_get_drvdata(device);
	unsigned long stat = 0;
	int err;

	if (!fingerprint) {
		pr_err("%s failed, the pointer is null\n", __func__);
		return -EINVAL;
	}

	err = kstrtoul(buffer, DECIMAL, &stat);
	if (err) {
		pr_err("%s, buffer convert fail\n", __func__);
		return -EINVAL;
	}
	fingerprint->snr_stat = (unsigned int)stat;
	if (fingerprint->snr_stat)
		g_snr_flag = 1;
	else
		g_snr_flag = 0;

	pr_info("%s g_snr_flag = %u\n", __func__, g_snr_flag);
	return (ssize_t)count;
}

static DEVICE_ATTR(snr, 0660, snr_show, snr_store);

static ssize_t nav_show(struct device *device,
	struct device_attribute *attribute, char *buffer)
{
	struct fp_data *fingerprint = dev_get_drvdata(device);

	if (!fingerprint) {
		pr_err("%s failed, the pointer is null\n", __func__);
		return -EINVAL;
	}
	return scnprintf(buffer, PAGE_SIZE, "%d", fingerprint->nav_stat);
}

static ssize_t nav_store(struct device *device,
	struct device_attribute *attribute, const char *buffer, size_t count)
{
	struct fp_data *fingerprint = dev_get_drvdata(device);
	unsigned long stat = 0;
	int err;

	if (!fingerprint) {
		pr_err("%s failed, the pointer is null\n", __func__);
		return -EINVAL;
	}

	err = kstrtoul(buffer, DECIMAL, &stat);
	if (err) {
		pr_err("%s, buffer convert fail\n", __func__);
		return -EINVAL;
	}
	fingerprint->nav_stat = (unsigned int)stat;
	return (ssize_t)count;
}

static DEVICE_ATTR(nav, 0660, nav_show, nav_store);

static ssize_t fingerprint_chip_info_show(struct device *device,
	struct device_attribute *attribute, char *buf)
{
	char sensor_id[FP_MAX_SENSOR_ID_LEN] = {0};
	struct fp_data *fingerprint = NULL;
	int ret;

	if (device == NULL || buf == NULL) {
		pr_err("%s failed, device or buf pointer is null\n", __func__);
		return -EINVAL;
	}

	fingerprint = dev_get_drvdata(device);
	if (!fingerprint) {
		pr_err("%s failed,the parameters is null\n", __func__);
		return -EINVAL;
	}
	ret = sprintf_s(sensor_id, FP_MAX_SENSOR_ID_LEN,
		"%x", fingerprint->sensor_id);
	if (ret < 0) {
		pr_err("%s, sprintf_s sensor_id fail\n", __func__);
		return -EINVAL;
	}

	return scnprintf(buf, PAGE_SIZE, "%s\n", sensor_id);
}

static DEVICE_ATTR(fingerprint_chip_info, 0444,
	fingerprint_chip_info_show, NULL);

static ssize_t module_id_show(struct device *device,
	struct device_attribute *attribute, char *buf)
{
	struct fp_data *fingerprint = dev_get_drvdata(device);

	if (!fingerprint) {
		pr_err("%s failed,the parameters is null\n", __func__);
		return -EINVAL;
	}
	return scnprintf(buf, PAGE_SIZE, "%s", fingerprint->module_id);
}

static ssize_t module_id_store(struct device *device,
	struct device_attribute *attribute, const char *buffer, size_t count)
{
	errno_t ret;
	struct fp_data *fingerprint = NULL;

	if (device == NULL || buffer == NULL) {
		pr_err("%s failed, device or buf pointer is null\n", __func__);
		return -EINVAL;
	}

	fingerprint = dev_get_drvdata(device);
	if (!fingerprint) {
		pr_err("%s failed,the parameters is null\n", __func__);
		return -EINVAL;
	}
	ret = strncpy_s(fingerprint->module_id, sizeof(fingerprint->module_id),
		buffer, sizeof(fingerprint->module_id) - 1);
	if (ret != EOK) {
		pr_err("%s module id store strncpy fail!!\n", __func__);
		return -EINVAL;
	}
	fingerprint->module_id[sizeof(fingerprint->module_id) - 1] = '\0';
	return (ssize_t)count;
}

static DEVICE_ATTR(module_id, 0444, module_id_show, module_id_store);

static ssize_t module_id_ud_show(struct device *device,
	struct device_attribute *attribute, char *buffer)
{
	struct fp_data *fp = dev_get_drvdata(device);

	if (!fp) {
		pr_err("%s, failed, the pointer is null!\n", __func__);
		return -EINVAL;
	}
	return scnprintf(buffer, PAGE_SIZE, "%s", fp->module_id_ud);
}

static ssize_t module_id_ud_store(struct device *device,
	struct device_attribute *attribute, const char *buffer, size_t count)
{
	struct fp_data *fp = dev_get_drvdata(device);
	int module_id_len = sizeof(fp->module_id_ud);
	errno_t ret;

	if (!fp) {
		pr_err("%s, fp is null!\n", __func__);
		return -EINVAL;
	}
	ret = strncpy_s(fp->module_id_ud, module_id_len, buffer,
		module_id_len - 1);
	if (ret != EOK) {
		pr_err("%s, strncpy_s failed! ret:%d\n", __func__, ret);
		return -EFAULT;
	}
	fp->module_id_ud[module_id_len - 1] = '\0';
	return (ssize_t)count;
}

static DEVICE_ATTR(module_id_ud, 0660, module_id_ud_show, module_id_ud_store);

static ssize_t chip_info_show(struct device *device,
	unsigned int sensor_type, char *chip_info)
{
	unsigned int sensor_id;
	int ret;
	struct fp_data *fp = NULL;

	fp = dev_get_drvdata(device);
	if (!fp) {
		pr_err("%s failed,the parameters is null\n", __func__);
		return -EINVAL;
	}

	if (sensor_type == COMMON) {
		sensor_id = fp->sensor_id;
	} else if (sensor_type == OPTICAL) {
		sensor_id = fp->sensor_id_ud;
	} else {
		pr_err("%s: sensor_type = %u, not found\n", __func__,
			sensor_type);
		return -EFAULT;
	}

	ret = sprintf_s(chip_info, FP_MAX_SENSOR_ID_LEN, "%x\n", sensor_id);
	if (ret < 0) {
		pr_err("%s: chip_info snprintf_s failed ret = %d\n",
			__func__, ret);
		return -EFAULT;
	}

	return ret;
}

static ssize_t ud_fingerprint_chip_info_show(struct device *device,
	struct device_attribute *attribute, char *buf)
{
	pr_info("%s\n", __func__);
	return chip_info_show(device, OPTICAL, buf);
}

static DEVICE_ATTR(ud_fingerprint_chip_info, 0440,
	ud_fingerprint_chip_info_show, NULL);

static ssize_t low_temperature_show(struct device *device,
	struct device_attribute *attribute, char *buffer)
{
	return scnprintf(buffer, PAGE_SIZE, "%d", NORMAL_TEMPERATURE);
}

static ssize_t low_temperature_store(struct device *device,
	struct device_attribute *attribute, const char *buffer, size_t count)
{
	return (ssize_t)count;
}

static DEVICE_ATTR(low_temperature, 0660, low_temperature_show,
	low_temperature_store);

static struct attribute *attributes[] = {
	&dev_attr_irq.attr,
	&dev_attr_fingerprint_chip_info.attr,
	&dev_attr_result.attr,
	&dev_attr_read_image_flag.attr,
	&dev_attr_snr.attr,
	&dev_attr_nav.attr,
	&dev_attr_module_id.attr,
	&dev_attr_module_id_ud.attr,
	&dev_attr_ud_fingerprint_chip_info.attr,
	&dev_attr_low_temperature.attr,
	NULL
};

static const struct attribute_group attribute_group = {
	.attrs = attributes,
};

/* tp event state machine */
static void fingerdown_event_function(int *cur_state, struct fp_data *fp)
{
	if (*cur_state != INIT_STATE && *cur_state != HOVER_DOWN_STATE &&
		*cur_state != HOVER_UP_STATE)
		return;
	*cur_state = FINGER_DOWN_STATE;
	sysfs_notify(&fp->pf_dev->dev.kobj, NULL, dev_attr_irq.attr.name);
}

static void fingerup_event_function(int *cur_state, struct fp_data *fp)
{
	if (*cur_state != FINGER_DOWN_STATE)
		return;

	*cur_state = FINGER_UP_STATE;
	mutex_lock(&fp->mutex_lock_irq_switch);
	if (g_wait_finger_up_flag) {
		wake_up(&fp->wait_finger_up_queue);
		g_wait_finger_up_flag = false;
	}
	g_close_tp_irq = true;
	mutex_unlock(&fp->mutex_lock_irq_switch);
}

static int fp_get_finger_status(void __user *argp)
{
	int ret = copy_to_user(argp, &g_tp_state, sizeof(g_tp_state));

	if (ret) {
		pr_err("copy_to_user failed, ret=%d\n", ret);
		return -EFAULT;
	}

	return FP_RETURN_SUCCESS;
}

static int ud_fingerprint_handle_tp_msg(int udfp_event)
{
	struct fp_data *fp = g_fp_data;

	if (!fp) {
		pr_err("%s fp is NULL!\n", __func__);
		return -EFAULT;
	}

	fp->tp_event = udfp_event;
	switch (udfp_event) {
	case TP_EVENT_FINGER_DOWN:
		fingerdown_event_function(&g_tp_state, fp);
		break;
	case TP_EVENT_FINGER_UP:
		fingerup_event_function(&g_tp_state, fp);
		break;
	case TP_EVENT_HOVER_DOWN:
		if (g_tp_state == INIT_STATE || g_tp_state == HOVER_UP_STATE)
			g_tp_state = HOVER_DOWN_STATE;
		break;
	case TP_EVENT_HOVER_UP:
		if (g_tp_state == HOVER_DOWN_STATE)
			g_tp_state = HOVER_UP_STATE;
		break;
	default:
		break;
	}
	return FP_RETURN_SUCCESS;
}

static int ud_fingerprint_irq_notify(struct tp_to_udfp_data *tp_data)
{
	if (!tp_data) {
		pr_err("%s tp_data is NULL!\n", __func__);
		return -EINVAL;
	}

	pr_info(" %s tp_event = %d, g_tp_state = %d\n", __func__,
		tp_data->udfp_event, g_tp_state);
	if (g_close_tp_irq) {
		pr_err("%s tp irq status is close, not handle tp event\n",
			__func__);
		return FP_RETURN_SUCCESS;
	}
	return ud_fingerprint_handle_tp_msg(tp_data->udfp_event);
}

static irqreturn_t fingerprint_irq_handler(int irq, void *handle)
{
	struct fp_data *fingerprint = (struct fp_data *)handle;

	if (!fingerprint) {
		pr_err("%s fingerprint is NULL!\n", __func__);
		return IRQ_NONE;
	}
	smp_rmb(); /* irq handle */
	if (fingerprint->wakeup_enabled)
		__pm_wakeup_event(&fingerprint->ttw_wl, FPC_TTW_HOLD_TIME);
	sysfs_notify(&fingerprint->pf_dev->dev.kobj, NULL,
		dev_attr_irq.attr.name);
	return IRQ_HANDLED;
}

static ssize_t fingerprint_get_navigation_adjust_value(const struct device *dev,
	struct fp_data *fp_data)
{
	struct device_node *np = NULL;
	unsigned int adjust1 = NAVIGATION_ADJUST_NOREVERSE;
	unsigned int adjust2 = NAVIGATION_ADJUST_NOTURN;
	ssize_t ret;

	np = of_find_compatible_node(NULL, NULL, "mediatek,mtk_finger");
	ret = of_property_read_u32(np, "fingerprint,navigation_adjust1",
		&adjust1);
	if (ret) {
		pr_err("%s ,no navigation_adjust1", __func__);
		return -EINVAL;
	}

	if ((adjust1 != NAVIGATION_ADJUST_NOREVERSE) &&
		(adjust1 != NAVIGATION_ADJUST_REVERSE)) {
		adjust1 = NAVIGATION_ADJUST_NOREVERSE;
		pr_err("%s navigation_adjust1 set err only support 0 and 1\n",
			__func__);
	}
	ret = of_property_read_u32(np, "fingerprint,navigation_adjust2",
		&adjust2);
	if (ret) {
		pr_err("%s ,no navigation_adjust2", __func__);
		return -EINVAL;
	}

	if ((adjust2 != NAVIGATION_ADJUST_NOTURN) &&
		(adjust2 != NAVIGATION_ADJUST_TURN90) &&
		(adjust2 != NAVIGATION_ADJUST_TURN180) &&
		(adjust2 != NAVIGATION_ADJUST_TURN270)) {
		adjust2 = NAVIGATION_ADJUST_NOTURN;
		pr_err("%s navigation_adjust2 set err only support 0 90 180 and 270\n",
			__func__);
	}
	fp_data->navigation_adjust1 = (int)adjust1;
	fp_data->navigation_adjust2 = (int)adjust2;
	pr_info("%s get navigation_adjust1 = %d,navigation_adjust2 = %d\n",
		__func__, fp_data->navigation_adjust1,
		fp_data->navigation_adjust2);
	return SUCCESS;
}

static int fp_get_pinctrl_and_rst_data_from_dts(struct fp_data *fingerprint)
{
	int ret = -1;

	fingerprint->pinctrl = devm_pinctrl_get(&fingerprint->spi->dev);
	if (IS_ERR(fingerprint->pinctrl)) {
		ret = PTR_ERR(fingerprint->pinctrl);
		pr_err("fpsensor Cannot find fp pinctrl1\n");
		return ret;
	}

	fingerprint->fp_rst_low = pinctrl_lookup_state(
		fingerprint->pinctrl, "fingerprint_rst_low");
	if (IS_ERR(fingerprint->fp_rst_low)) {
		ret = PTR_ERR(fingerprint->fp_rst_low);
		pr_err("Cannot find fp pinctrl fp_rst_low!\n");
		return ret;
	}

	fingerprint->fp_rst_high = pinctrl_lookup_state(
		fingerprint->pinctrl, "fingerprint_rst_high");
	if (IS_ERR(fingerprint->fp_rst_high)) {
		ret = PTR_ERR(fingerprint->fp_rst_high);
		pr_err("Cannot find fp pinctrl fp_rst_high!\n");
		return ret;
	}
	pr_info("fingerprint find fp pinctrl fp_rst_high!\n");

	return SUCCESS;
}

static int fp_get_power_en_gpio(struct fp_data *fingerprint)
{
	int ret = -1;

	if (!fingerprint || !fingerprint->pinctrl) {
		pr_err("%s fingerprint or fingerprint->pinctrl is NULL!\n",
			__func__);
		return ret;
	}

	fingerprint->fp_power_low = pinctrl_lookup_state(
		fingerprint->pinctrl, "fingerprint_power_low");
	if (IS_ERR(fingerprint->fp_power_low)) {
		ret = PTR_ERR(fingerprint->fp_power_low);
		pr_err("Cannot find fp pinctrl fp_power_low ret =%d!\n", ret);
		return ret;
	}

	fingerprint->fp_power_high = pinctrl_lookup_state(
		fingerprint->pinctrl, "fingerprint_power_high");
	if (IS_ERR(fingerprint->fp_power_high)) {
		ret = PTR_ERR(fingerprint->fp_power_high);
		pr_err("Cannot find fp pinctrl fp_power_high ret =%d!\n", ret);
		return ret;
	}
	fingerprint_gpio_output_dts(fingerprint,
		FINGERPRINT_POWER_EN_PIN, FP_GPIO_HIGH_LEVEL);

	return SUCCESS;
}

static int fp_get_eint_and_cs_data_from_dts(struct fp_data *fingerprint)
{
	int ret;

	fingerprint->eint_as_int = pinctrl_lookup_state(
		fingerprint->pinctrl, "fingerprint_eint_as_int");
	if (IS_ERR(fingerprint->eint_as_int)) {
		ret = PTR_ERR(fingerprint->eint_as_int);
		pr_err("Cannot find fp pinctrl eint_as_int!\n");
	}

	pr_info("fingerprint find fp pinctrl eint_as_int!\n");
	fingerprint->fp_cs_low = pinctrl_lookup_state(
		fingerprint->pinctrl, "fingerprint_spi_cs_low");
	if (IS_ERR(fingerprint->fp_cs_low)) {
		ret = PTR_ERR(fingerprint->fp_cs_low);
		pr_err("Cannot find fp pinctrl fp_cs_low!\n");
		return ret;
	}

	pr_info("fingerprint find fp pinctrl fp_cs_low!\n");
	fingerprint->fp_cs_high = pinctrl_lookup_state(
		fingerprint->pinctrl, "fingerprint_spi_cs_high");
	if (IS_ERR(fingerprint->fp_cs_high)) {
		ret = PTR_ERR(fingerprint->fp_cs_high);
		pr_err("Cannot find fp pinctrl fp_cs_high!\n");
		return ret;
	}
	pr_info("fingerprint find fp pinctrl fp_cs_high!\n");

	return SUCCESS;
}

static int fp_get_miso_and_mosi_data_from_dts(struct fp_data *fingerprint)
{
	int ret = -1;

	fingerprint->fp_mo_high = pinctrl_lookup_state(
		fingerprint->pinctrl, "fingerprint_spi_mosi_high");
	if (IS_ERR(fingerprint->fp_mo_high)) {
		ret = PTR_ERR(fingerprint->fp_mo_high);
		pr_err("Cannot find fp pinctrl fp_mo_high!\n");
		return ret;
	}
	pr_info("fingerprint  find fp pinctrl fp_mo_high!\n");

	fingerprint->fp_mo_low = pinctrl_lookup_state(
		fingerprint->pinctrl, "fingerprint_spi_mosi_low");
	if (IS_ERR(fingerprint->fp_mo_low)) {
		ret = PTR_ERR(fingerprint->fp_mo_low);
		pr_err("Cannot find fp pinctrl fp_mo_low!\n");
		return ret;
	}
	pr_info("fingerprint find fp pinctrl fp_mo_low!\n");

	fingerprint->fp_mi_high = pinctrl_lookup_state(
		fingerprint->pinctrl, "fingerprint_spi_miso_high");
	if (IS_ERR(fingerprint->fp_mi_high)) {
		ret = PTR_ERR(fingerprint->fp_mi_high);
		pr_err("Cannot find fp pinctrl fp_mi_high!\n");
		return ret;
	}
	pr_info("fingerprint find fp pinctrl fp_mi_high!\n");

	fingerprint->fp_mi_low = pinctrl_lookup_state(
		fingerprint->pinctrl, "fingerprint_spi_miso_low");
	if (IS_ERR(fingerprint->fp_mi_low)) {
		ret = PTR_ERR(fingerprint->fp_mi_low);
		pr_err("Cannot find fp pinctrl fp_mi_low!\n");
		return ret;
	}
	pr_info("fingerprint find fp pinctrl fp_mi_low!\n");

	return SUCCESS;
}

static int fp_get_clk_data_from_dts(struct fp_data *fingerprint)
{
	int ret;

	fingerprint->fp_ck_high = pinctrl_lookup_state(
		fingerprint->pinctrl, "fingerprint_spi_mclk_high");
	if (IS_ERR(fingerprint->fp_ck_high)) {
		ret = PTR_ERR(fingerprint->fp_ck_high);
		pr_err("Cannot find fp pinctrl fp_ck_high!\n");
		return ret;
	}
	pr_info("fingerprint find fp pinctrl fp_ck_high!\n");

	fingerprint->fp_ck_low = pinctrl_lookup_state(
		fingerprint->pinctrl, "fingerprint_spi_mclk_low");
	if (IS_ERR(fingerprint->fp_ck_low)) {
		ret = PTR_ERR(fingerprint->fp_ck_low);
		pr_err("Cannot find fp pinctrl fp_ck_low!\n");
		return ret;
	}
	pr_info("fingerprint find fp pinctrl fp_ck_low!\n");

	return SUCCESS;
}

static void fp_btb_disconnect_dmd_report(int gpio, const char *err_msg)
{
#ifdef CONFIG_HUAWEI_DSM
	if (!dsm_client_ocuppy(g_fingerprint_dclient)) {
		dsm_client_record(g_fingerprint_dclient, "%s %d",
			err_msg, gpio);
		dsm_client_notify(g_fingerprint_dclient,
			DSM_FINGERPRINT_BTB_DISCONNECT_ERROR_NO);
	}
#endif
}

static void fingerprint_get_btb_dts_data(struct device_node *np,
	struct fp_data *fingerprint)
{
	int ret;

	fingerprint->btb_det_gpio = of_get_named_gpio(np,
		"fingerprint,btb_det_gpio", 0);
	if (fingerprint->btb_det_gpio < 0) {
		pr_err("%s read fingerprint,btb_det_gpio failed: %d\n",
			__func__, fingerprint->btb_det_gpio);
		return;
	}

	fingerprint->fp_btb_det = pinctrl_lookup_state(fingerprint->pinctrl,
		"fingerprint_fp_btb_det");
	if (IS_ERR(fingerprint->fp_btb_det)) {
		ret = PTR_ERR(fingerprint->fp_btb_det);
		pr_err("fingerprint Cannot find pinctrl fp_btb_det %d\n", ret);
		return;
	}
	pr_info("fingerprint find fp pinctrl fp_btb_det!\n");

	ret = pinctrl_select_state(fingerprint->pinctrl,
		fingerprint->fp_btb_det);
	if (ret)
		pr_err("pinctrl_select_state fp_btb_det failed %d\n", ret);

	if (gpio_get_value(fingerprint->btb_det_gpio) == FP_GPIO_HIGH_LEVEL)
		fp_btb_disconnect_dmd_report(fingerprint->btb_det_gpio,
			"irq report fingerprint btb disconnect, btb_det_gpio");
}

static int fingerprint_get_dts_data(struct device *dev,
	struct fp_data *fingerprint)
{
	struct device_node *np = NULL;
	struct platform_device *pdev = NULL;
	int ret = -1;

	np = of_find_compatible_node(NULL, NULL, "mediatek,mtk_finger");
	if (!np) {
		pr_err("%s finger node null", __func__);
		return ret;
	}

	ret = of_property_read_u32(np, "fingerprint,use_tp_irq",
			&fingerprint->use_tp_irq);
	if (ret) {
		fingerprint->use_tp_irq = USE_SELF_IRQ;
		pr_err("%s: failed to get use_tp_irq\n", __func__);
	}

	ret = of_property_read_u32(np, "fingerprint,product_id",
		&fingerprint->product_id);
	if (ret)
		pr_err("%s: failed to get product_id\n", __func__);
	pr_info("%s get product_id = %u\n",
			__func__, fingerprint->product_id);

	ret = of_property_read_u32(np,
		"fingerprint,custom_timing_scheme",
		&fingerprint->custom_timing_scheme);
	if (ret) {
		fingerprint->custom_timing_scheme = 0;
		pr_err("%s:failed get custom_timing_scheme\n",
			__func__);
	}

	ret = of_property_read_u32(np, "fingerprint,poweroff_scheme",
		&fingerprint->poweroff_scheme);
	if (ret)
		pr_err("the property name poweroff_scheme is null\n");
	pr_info("fingerprint->poweroff_scheme = %d\n",
			fingerprint->poweroff_scheme);

	pdev = of_find_device_by_node(np);
	if (IS_ERR(pdev)) {
		pr_err("platform device is null\n");
		return PTR_ERR(pdev);
	}

	ret = fp_get_pinctrl_and_rst_data_from_dts(fingerprint);
	if (ret != SUCCESS)
		return ret;

	ret = fp_get_eint_and_cs_data_from_dts(fingerprint);
	if (ret != SUCCESS)
		return ret;

	ret = fp_get_miso_and_mosi_data_from_dts(fingerprint);
	if (ret != SUCCESS)
		return ret;

	ret = fp_get_clk_data_from_dts(fingerprint);
	if (ret != SUCCESS)
		return ret;

	fingerprint_get_btb_dts_data(np, fingerprint);
	fingerprint_gpio_output_dts(fingerprint,
		FINGERPRINT_SPI_MO_PIN, FP_GPIO_LOW_LEVEL);
	fingerprint_gpio_output_dts(fingerprint,
		FINGERPRINT_SPI_MI_PIN, FP_GPIO_LOW_LEVEL);
	fingerprint_gpio_output_dts(fingerprint,
		FINGERPRINT_SPI_CK_PIN, FP_GPIO_LOW_LEVEL);
	fingerprint_gpio_output_dts(fingerprint,
		FINGERPRINT_SPI_CS_PIN, FP_GPIO_LOW_LEVEL);
	fingerprint_gpio_output_dts(fingerprint,
		FINGERPRINT_RST_PIN, FP_GPIO_LOW_LEVEL);

	return SUCCESS;
}

static void fingerprint_custom_timing_scheme_one(struct fp_data *fingerprint)
{
	usleep_range(1000, 1100); /* delay 1ms */
	fingerprint_gpio_output_dts(fingerprint, FINGERPRINT_RST_PIN,
		FP_GPIO_HIGH_LEVEL);
	fingerprint_gpio_output_dts(fingerprint, FINGERPRINT_SPI_CS_PIN,
		FP_GPIO_HIGH_LEVEL);
}

static void fingerprint_custom_timing_scheme_two(struct fp_data *fingerprint)
{
	msleep(10); /* delay 10ms */
	fingerprint_gpio_output_dts(fingerprint, FINGERPRINT_RST_PIN,
		FP_GPIO_HIGH_LEVEL);
	fingerprint_gpio_output_dts(fingerprint, FINGERPRINT_SPI_CS_PIN,
		FP_GPIO_HIGH_LEVEL);
}
/*
 * some device need spacial timing scheme3:
 * first power on the sensor that open loadswtich(gpio), then delay 10ms
 * power on cs, and then delay 600us, power on rst
 */
static void fingerprint_custom_timing_scheme_three(struct fp_data *fingerprint)
{
	msleep(10); /* delay 10ms */
	fingerprint_gpio_output_dts(fingerprint, FINGERPRINT_SPI_CS_PIN,
		FP_GPIO_HIGH_LEVEL);
	udelay(600); /* delay 600us */
	fingerprint_gpio_output_dts(fingerprint, FINGERPRINT_RST_PIN,
		FP_GPIO_HIGH_LEVEL);
}

static void fingerprint_custom_timing_scheme_four(struct fp_data *fingerprint)
{
	fingerprint_gpio_output_dts(fingerprint, FINGERPRINT_RST_PIN,
		FP_GPIO_HIGH_LEVEL);
	fingerprint_gpio_output_dts(fingerprint, FINGERPRINT_SPI_CS_PIN,
		FP_GPIO_HIGH_LEVEL);
}

static void fingerprint_custom_timing(struct fp_data *fingerprint)
{
	pr_info("%s: timing scheme: %d\n", __func__,
		fingerprint->custom_timing_scheme);
	if (fingerprint->custom_timing_scheme == 0) {
		fingerprint_gpio_output_dts(fingerprint,
			FINGERPRINT_RST_PIN, FP_GPIO_HIGH_LEVEL);
		fingerprint_gpio_output_dts(fingerprint,
			FINGERPRINT_SPI_CS_PIN, FP_GPIO_HIGH_LEVEL);
		return;
	}

	switch (fingerprint->custom_timing_scheme) {
	case FP_CUSTOM_TIMING_SCHEME_ONE:
		fingerprint_custom_timing_scheme_one(fingerprint);
		break;
	case FP_CUSTOM_TIMING_SCHEME_TWO:
		fingerprint_custom_timing_scheme_two(fingerprint);
		break;
	case FP_CUSTOM_TIMING_SCHEME_THREE:
		fingerprint_custom_timing_scheme_three(fingerprint);
		break;
	case FP_CUSTOM_TIMING_SCHEME_FOUR:
		fingerprint_custom_timing_scheme_four(fingerprint);
		break;
	default:
		pr_err("%s timing_scheme config error %d\n",
			__func__, fingerprint->custom_timing_scheme);
		break;
	}
}

static int fingerprint_irq_init(struct fp_data *fingerprint)
{
	struct device_node *node = NULL;

	node = of_find_compatible_node(NULL, NULL, "mediatek,mtk_finger");
	if (!node) {
		pr_err("%s,node is null", __func__);
		return -EINVAL;
	}

	fingerprint->irq_gpio = of_get_named_gpio(node,
		"fingerprint_eint_gpio", 0);
	if (fingerprint->irq_gpio < 0) {
		pr_err("%s read fingerprint,eint_gpio fail\n", __func__);
		return -EINVAL;
	}
	fingerprint->irq = irq_of_parse_and_map(node, 0);
		/* get irq number */
	if (fingerprint->irq < 0) {
		pr_err("fingerprint irq_of_parse_and_map fail!!\n");
		return -EINVAL;
	}
	pr_info("fingerprint->irq= %d, fingerprint>irq_gpio = %d\n",
		fingerprint->irq, fingerprint->irq_gpio);
	return SUCCESS;
}

static irqreturn_t fingerprint_btb_det_irq_handler(
	int irq, void *handle)
{
	struct fp_data *dev = (struct fp_data *)handle;

	disable_irq_nosync(irq);
	pr_info("%s: Interrupt occured\n", __func__);
	schedule_delayed_work(&dev->dwork,
		msecs_to_jiffies(BTB_DET_IRQ_DEBOUNCE));
	g_btb_irq_count++;

	return IRQ_HANDLED;
}

static void fingerprint_btb_det_work_func(struct work_struct *work)
{
	struct fp_data *fingerprint =
		container_of(work, struct fp_data, dwork.work);
	int gpio_value = gpio_get_value(fingerprint->btb_det_gpio);

	pr_info("btb_det_gpio value: %d", gpio_value);
	if (gpio_value == FP_GPIO_HIGH_LEVEL)
		fp_btb_disconnect_dmd_report(fingerprint->btb_det_gpio,
			"irq report fingerprint btb disconnect, btb_det_gpio");

	if (g_btb_irq_count < BTB_DET_IRQ_MAX_CNT)
		enable_irq(fingerprint->btb_det_irq);
}

static int fingerprint_btb_det_irq_init(struct fp_data *fingerprint)
{
	int ret;

	if (fingerprint->btb_det_gpio < 0) {
		pr_err("%s read fingerprint,btb_det_gpio failed\n", __func__);
		return -EINVAL;
	}

	ret = gpio_request(fingerprint->btb_det_gpio,
		"fingerprint,btb_det_gpio");
	if (ret) {
		pr_err("%s gpio_request failed %d\n", __func__, ret);
		return ret;
	}

	/* request irq */
	fingerprint->btb_det_irq = gpio_to_irq(fingerprint->btb_det_gpio);
	if (fingerprint->btb_det_irq < 0) {
		pr_err("%s gpio_to_irq fail, %d\n", __func__,
			fingerprint->btb_det_irq);
		gpio_free(fingerprint->btb_det_gpio);
		return -EINVAL;
	}
	pr_info("%s btb_det_irq = %d, btb_det_gpio = %d\n", __func__,
		fingerprint->btb_det_irq, fingerprint->btb_det_gpio);

	INIT_DELAYED_WORK(&fingerprint->dwork, fingerprint_btb_det_work_func);

	ret = request_threaded_irq(fingerprint->btb_det_irq, NULL,
		fingerprint_btb_det_irq_handler,
		IRQF_TRIGGER_RISING | IRQF_ONESHOT, "fingerprint", fingerprint);
	if (ret) {
		cancel_delayed_work_sync(&fingerprint->dwork);
		pr_err("failed to request btb_det_irq %d\n",
			fingerprint->btb_det_irq);
		gpio_free(fingerprint->btb_det_gpio);
		return ret;
	}
	enable_irq_wake(fingerprint->btb_det_irq);
	g_btb_irq_flag = true; /* enable remove func */

	return ret;
}

static void fingerprint_btb_det_remove(struct fp_data *fingerprint)
{
	if (g_btb_irq_flag) {
		cancel_delayed_work_sync(&fingerprint->dwork);
		free_irq(fingerprint->btb_det_irq, fingerprint);
		gpio_free(fingerprint->btb_det_gpio);
	}
}

static int fingerprint_key_remap_reverse(int key)
{
	switch (key) {
	case EVENT_LEFT:
		key = EVENT_RIGHT;
		break;
	case EVENT_RIGHT:
		key = EVENT_LEFT;
		break;
	default:
		break;
	}
	return key;
}

static int fingerprint_key_remap_turn90(int key)
{
	switch (key) {
	case EVENT_LEFT:
		key = EVENT_UP;
		break;
	case EVENT_RIGHT:
		key = EVENT_DOWN;
		break;
	case EVENT_UP:
		key = EVENT_RIGHT;
		break;
	case EVENT_DOWN:
		key = EVENT_LEFT;
		break;
	default:
		break;
	}

	return key;
}

static int fingerprint_key_remap_turn180(int key)
{
	switch (key) {
	case EVENT_LEFT:
		key = EVENT_RIGHT;
		break;
	case EVENT_RIGHT:
		key = EVENT_LEFT;
		break;
	case EVENT_UP:
		key = EVENT_DOWN;
		break;
	case EVENT_DOWN:
		key = EVENT_UP;
		break;
	default:
		break;
	}
	return key;
}

static int fingerprint_key_remap_turn270(int key)
{
	switch (key) {
	case EVENT_LEFT:
		key = EVENT_DOWN;
		break;
	case EVENT_RIGHT:
		key = EVENT_UP;
		break;
	case EVENT_UP:
		key = EVENT_LEFT;
		break;
	case EVENT_DOWN:
		key = EVENT_RIGHT;
		break;
	default:
		break;
	}
	return key;
}

static int fingerprint_key_remap(const struct fp_data *fingerprint, int key)
{
	if ((key != EVENT_RIGHT) && (key != EVENT_LEFT) &&
		(key != EVENT_UP) && (key != EVENT_DOWN))
		return key;
	if ((fingerprint->navigation_adjust1) == NAVIGATION_ADJUST_REVERSE)
		key = fingerprint_key_remap_reverse(key);
	switch (fingerprint->navigation_adjust2) {
	case NAVIGATION_ADJUST_TURN90:
		key = fingerprint_key_remap_turn90(key);
		break;
	case NAVIGATION_ADJUST_TURN180:
		key = fingerprint_key_remap_turn180(key);
		break;
	case NAVIGATION_ADJUST_TURN270:
		key = fingerprint_key_remap_turn270(key);
		break;
	default:
		break;
	}
	return key;
}

static void fingerprint_input_report(struct fp_data *fingerprint, int key)
{
	key = fingerprint_key_remap(fingerprint, key);
	input_report_key(fingerprint->input_dev, key, 1);
	input_sync(fingerprint->input_dev);
	input_report_key(fingerprint->input_dev, key, 0);
	input_sync(fingerprint->input_dev);
}

static int fingerprint_open(struct inode *inode, struct file *file)
{
	struct fp_data *fingerprint = NULL;

	if (!inode || !file)
		return -EINVAL;

	fingerprint = container_of(inode->i_cdev, struct fp_data, cdev);
	file->private_data = fingerprint;
	return SUCCESS;
}

static int fingerprint_get_irq_status(struct fp_data *fingerprint)
{
	int status;

	status = __gpio_get_value(fingerprint->irq_gpio);
	return status;
}

static int fingerprint_spi_clk_switch(struct fp_data *fingerprint,
	enum spi_clk_state ctrl)
{
	mutex_lock(&(fingerprint->mutex_lock_clk));
	if (ctrl == SPI_CLK_DISABLE) {
		if (fingerprint->spi_clk_counter > 0)
			fingerprint->spi_clk_counter--;
		if (fingerprint->spi_clk_counter == 0)
			mt_spi_disable_master_clk(fingerprint->spi);
		else
			pr_err("the disable clk is not match, the spi_clk_counter = %d\n",
				fingerprint->spi_clk_counter);
	} else {
		if (fingerprint->spi_clk_counter == 0)
			mt_spi_enable_master_clk(fingerprint->spi);
		else
			pr_err("the enable clk is not match, the spi_clk_counter = %d\n",
				fingerprint->spi_clk_counter);
		(fingerprint->spi_clk_counter)++;
	}
	mutex_unlock(&(fingerprint->mutex_lock_clk));
	return SUCCESS;
}

static void fingerprint_poweron_open_ldo(struct fp_data *fingerprint)
{
	int ret;
	int max_cnt = 100; /* max times that try to open the power */

	if (!fingerprint->avdd || IS_ERR(fingerprint->avdd)) {
		pr_err("%s:can't get fp_avdd regulator\n", __func__);
		return;
	}

	do {
		pr_info("%s regulator flag:%d\n",
			__func__, regulator_is_enabled(fingerprint->avdd));
		if (!regulator_is_enabled(fingerprint->avdd)) {
			ret = regulator_enable(fingerprint->avdd);
			if (ret != 0)
				pr_err("%s:regulator_enable fail, ret:%d\n",
					__func__, ret);
		}

		/* break the process when the ldo regulator is open */
		if (regulator_is_enabled(fingerprint->avdd)) {
			pr_err("regulator is open and break\n");
			break;
		}
		max_cnt--;
	} while (max_cnt > 0);
}

static void fingerprint_poweron(struct fp_data *fingerprint)
{
	pr_info("%s start\n", __func__);
	switch (fingerprint->poweroff_scheme) {
	case FP_POWEROFF_SCHEME_ONE:
		break;
	case FP_POWEROFF_SCHEME_TWO:
		fingerprint_poweron_open_ldo(fingerprint);
		break;
	default:
		break;
	}
}

static void fingerprint_poweroff_close_ldo(struct fp_data *fingerprint)
{
	int ret;
	int max_cnt = 100; /* max times that try to close the power */

	if (!fingerprint->avdd || IS_ERR(fingerprint->avdd)) {
		pr_err("%s:can't get fp_avdd regulator\n", __func__);
		return;
	}
	/*
	 * the power may be shared with other modules,
	 * so now close the power maybe more than one times
	 */
	do {
		pr_info("%s regulator flag:%d\n",
			__func__, regulator_is_enabled(fingerprint->avdd));
		if (regulator_is_enabled(fingerprint->avdd)) {
			ret = regulator_disable(fingerprint->avdd);
			if (ret != 0)
				pr_err("%s:regulator_disable fail, ret:%d\n",
					__func__, ret);
		}

		/* break the process when the ldo regulator is close */
		if (!regulator_is_enabled(fingerprint->avdd)) {
			pr_err("regulator is close and break\n");
			break;
		}
		max_cnt--;
	} while (max_cnt > 0);
}

static void fingerprint_poweroff(struct fp_data *fingerprint)
{
	pr_info("%s enter\n", __func__);
	switch (fingerprint->poweroff_scheme) {
	case FP_POWEROFF_SCHEME_ONE:
		break;
	case FP_POWEROFF_SCHEME_TWO:
		fingerprint_poweroff_close_ldo(fingerprint);
		break;
	default:
		break;
	}
}

static void fingerprint_poweroff_pd_charge(struct fp_data *fingerprint)
{
	pr_info("%s enter\n", __func__);
	switch (fingerprint->poweroff_scheme) {
	case FP_POWEROFF_SCHEME_ONE:
	case FP_POWEROFF_SCHEME_TWO:
		fingerprint_poweroff_close_ldo(fingerprint);
		break;
	default:
		break;
	}
}

static int fingerprint_extern_ldo_proc(struct device *dev,
	struct fp_data *fingerprint)
{
	int ret;

	pr_info("%s enter\n", __func__);
	fingerprint->avdd = devm_regulator_get(dev, "fp_vdd");
	if (IS_ERR(fingerprint->avdd)) {
		ret = PTR_ERR(fingerprint->avdd);
		pr_err("can't get fp_vdd regulator: %d\n", ret);
		return -EINVAL;
	}
	ret = regulator_enable(fingerprint->avdd);
	if (ret)
		pr_err("can't enable fp_vdd: %d\n", ret);

	fingerprint->vdd = devm_regulator_get(dev, "fp_vddio");
	if (IS_ERR(fingerprint->vdd)) {
		ret = PTR_ERR(fingerprint->vdd);
		pr_err("can't get fp_vddio regulator: %d\n", ret);
		return -EINVAL;
	}
	ret = regulator_enable(fingerprint->vdd);
	if (ret)
		pr_err("can't enable fp_vddio: %d\n", ret);

	if (get_pd_charge_flag())
		fingerprint_poweroff_pd_charge(fingerprint);

	return ret;
}

static void fp_ioc_enable_irq(struct fp_data *fp)
{
	pr_err("fingerprint_ioctl FP_IOC_CMD_ENABLE_IRQ\n");
	if (fp->use_tp_irq == USE_TP_IRQ) {
		g_wait_finger_up_flag = false;
		fp->tp_event = TP_EVENT_FINGER_UP;
		g_tp_state = INIT_STATE;
		mutex_lock(&fp->mutex_lock_irq_switch);
		g_close_tp_irq = false;
		mutex_unlock(&fp->mutex_lock_irq_switch);
		return;
	}
	if (fp->irq_num == 0) {
		enable_irq(fp->irq);
		fp->irq_num = 1;
	}
}

static void fp_ioc_disable_irq(struct fp_data *fp)
{
	pr_err("fingerprint_ioctl FP_IOC_CMD_DISABLE_IRQ\n");
	if (fp->use_tp_irq == USE_TP_IRQ)
		return;
	if (fp->irq_num == 1) {
		disable_irq(fp->irq);
		fp->irq_num = 0;
	}
}

static int fp_ioc_send_sensorid_ud(struct fp_data *fp, const void __user *argp)
{
	unsigned int sensor_id = 0;

	if (copy_from_user(&sensor_id, argp, sizeof(sensor_id))) {
		pr_err("%s copy_from_user failed\n", __func__);
		return -EFAULT;
	}

	fp->sensor_id_ud = sensor_id;
	return FP_RETURN_SUCCESS;
}

void ud_fp_on_hbm_completed(void)
{
	struct fp_data *fp = g_fp_data;

	if (!fp) {
		pr_err("%s fp is null\n", __func__);
		return;
	}

	pr_info("%s notify\n", __func__);
	fp->hbm_status = HBM_ON;
	wake_up(&fp->hbm_queue);
}

void fp_set_lcd_charge_time(int time)
{
	struct fp_data *fp = g_fp_data;

	if (!fp) {
		pr_err("%s fp is null\n", __func__);
		return;
	}

	fp->fingerprint_bigdata.lcd_charge_time = time;
}

void fp_set_lcd_light_on_time(int time)
{
	struct fp_data *fp = g_fp_data;

	if (!fp) {
		pr_err("%s fp is null\n", __func__);
		return;
	}

	fp->fingerprint_bigdata.lcd_on_time = time;
}

void fp_set_cpu_wake_up_time(int time)
{
	struct fp_data *fp = g_fp_data;

	if (!fp) {
		pr_err("%s fp is null\n", __func__);
		return;
	}

	fp->fingerprint_bigdata.cpu_wakeup_time = time;
}

static int fp_ioc_check_hbm_status(struct fp_data *fp)
{
	int ret = FP_RETURN_SUCCESS;

	if (fp->hbm_status == HBM_ON) {
		pr_err("%s ok\n", __func__);
		return ret;
	}
	if (wait_event_timeout(fp->hbm_queue,
		fp->hbm_status == HBM_ON, HBM_WAIT_TIMEOUT) <= 0)
		ret = -EFAULT;

	return ret;
}

static int fp_ioc_get_bigdata(struct fp_data *fp, void __user *argp)
{
	int ret;

	ret = copy_to_user(argp, &fp->fingerprint_bigdata,
		sizeof(fp->fingerprint_bigdata));
	if (ret) {
		pr_err("%s copy_to_user failed, ret=%d\n", __func__, ret);
		return -EFAULT;
	}

	return FP_RETURN_SUCCESS;
}

/* active te single when using fingeprint , fix fingeprint blink question. */
static int fp_ioc_update_te(void)
{
	return FP_RETURN_SUCCESS;
}

static int fp_config_wait_fp_up(void)
{
	int ret = FP_RETURN_SUCCESS;

	if (g_fp_data->tp_event == TP_EVENT_FINGER_UP) {
		pr_err("finger is already up\n");
		return ret;
	}

	g_wait_finger_up_flag = true;
	/* the function times out and is not an exception */
	if (wait_event_timeout(g_fp_data->wait_finger_up_queue,
		g_fp_data->tp_event == TP_EVENT_FINGER_UP,
			FINGER_UP_WAIT_TIMEOUT) <= 0)
		return -EFAULT;
	return ret;
}

static int fingerprint_ioctl_proc_first(struct fp_data *fingerprint,
	unsigned int cmd, unsigned long arg, bool *is_match)
{
	int key;
	int status;
	unsigned int sensor_id;
	void __user *argp = (void __user *)(uintptr_t)arg;
	int error = 0;

	switch (cmd) {
	case FP_IOC_CMD_SEND_UEVENT:
		if (copy_from_user(&key, argp, sizeof(key))) {
			pr_err("copy_from_user failed");
			return -EFAULT;
		}
		fingerprint_input_report(fingerprint, key);
		break;
	case FP_IOC_CMD_GET_IRQ_STATUS:
		status = fingerprint_get_irq_status(fingerprint);
		if (copy_to_user(argp, &status, sizeof(status))) {
			pr_err("copy_to_user failed");
			return -EFAULT;
		}
		break;
	case FP_IOC_CMD_SET_WAKELOCK_STATUS:
		if (copy_from_user(&key, argp, sizeof(key))) {
			pr_err("copy_from_user failed");
			return -EFAULT;
		}
		fingerprint->wakeup_enabled = (key == 1) ? true : false;
		break;
	case FP_IOC_CMD_SEND_SENSORID:
		if (copy_from_user(&sensor_id, argp, sizeof(sensor_id))) {
			pr_err("copy_from_user failed\n");
			return -EFAULT;
		}
		fingerprint->sensor_id = sensor_id;
		pr_err("sensor_id = %x\n", sensor_id);
		break;
	case FP_IOC_CMD_SEND_SENSORID_UD:
		error = fp_ioc_send_sensorid_ud(fingerprint, argp);
		break;
	case FP_IOC_CMD_CHECK_HBM_STATUS:
		error = fp_ioc_check_hbm_status(fingerprint);
		break;
	case FP_IOC_CMD_RESET_HBM_STATUS:
		fingerprint->hbm_status = HBM_NONE;
		break;
	case FP_IOC_CMD_GET_BIGDATA:
		error = fp_ioc_get_bigdata(fingerprint, argp);
		break;
	case FP_IOC_CMD_NOTIFY_DISPLAY_FP_DOWN_UD:
		error = fp_ioc_update_te();
		break;
	case FP_IOC_CMD_WAIT_FINGER_UP:
		error = fp_config_wait_fp_up();
		break;
	case FP_IOC_CMD_IDENTIFY_EXIT:
		pr_info("%s: FP_IOC_CMD_IDENTIFY_EXIT\n", __func__);
		g_fp_data->tp_event = TP_EVENT_FINGER_UP;
		break;
	case FP_IOC_CMD_GET_FINGER_STATUS:
		error = fp_get_finger_status(argp);
		break;
	default:
		*is_match = false;
		break;
	}

	return error;
}

static int fingerprint_ioctl_proc_second(struct fp_data *fingerprint,
	unsigned int cmd)
{
	switch (cmd) {
	case FP_IOC_CMD_ENABLE_IRQ:
		fp_ioc_enable_irq(fingerprint);
		break;
	case FP_IOC_CMD_DISABLE_IRQ:
		fp_ioc_disable_irq(fingerprint);
		break;
	case FP_IOC_CMD_SET_IPC_WAKELOCKS:
		pr_err("MTK do not support the CMD FP_IOC_CMD_SET_IPC_WAKELOCKS\n");
		break;
	case FP_IOC_CMD_SET_POWEROFF:
		pr_info("%s FP_IOC_CMD_SET_POWEROFF\n", __func__);
		fingerprint_poweroff(fingerprint);
		break;
	case FP_IOC_CMD_SET_POWERON:
		pr_info("%s FP_IOC_CMD_SET_POWERON\n", __func__);
		fingerprint_poweron(fingerprint);
		break;
	case FP_IOC_CMD_ENABLE_SPI_CLK:
		fingerprint_spi_clk_switch(fingerprint, SPI_CLK_ENABLE);
		break;
	case FP_IOC_CMD_DISABLE_SPI_CLK:
		fingerprint_spi_clk_switch(fingerprint, SPI_CLK_DISABLE);
		break;
	default:
		pr_err("error = -EFAULT\n");
		return -EFAULT;
	}

	return 0;
}

static long fingerprint_base_ioctl(struct file *file,
	unsigned int cmd, unsigned long arg)
{
	int ret;
	struct fp_data *fingerprint = NULL;
	bool is_match = true;

	fingerprint = (struct fp_data *)file->private_data;
	if (_IOC_TYPE(cmd) != FP_IOC_MAGIC) {
		pr_err("%s: FP_IOC_MAGIC fail, TYPE = %d\n",
			__func__, _IOC_TYPE(cmd));
		return -ENOTTY;
	}

	ret = fingerprint_ioctl_proc_first(fingerprint, cmd, arg, &is_match);
	if (is_match)
		return ret;

	ret = fingerprint_ioctl_proc_second(fingerprint, cmd);
	return ret;
}

static long fingerprint_ioctl(struct file *file, unsigned int cmd,
	unsigned long arg)
{
	return fingerprint_base_ioctl(file, cmd, arg);
}

static long fingerprint_compat_ioctl(struct file *file, unsigned int cmd,
	unsigned long arg)
{
	return fingerprint_ioctl(file, cmd, arg);
}

static int fingerprint_release(struct inode *inode, struct file *file)
{
	pr_info("Enter!\n");
	return SUCCESS;
}

static ssize_t fpsensor_read(struct file *filp,
	char __user *buf, size_t count, loff_t *f_pos)
{
	pr_err("kp Not support read operation in TEE version\n");
	return -EFAULT;
}

static const struct file_operations fingerprint_fops = {
	.owner = THIS_MODULE,
	.open = fingerprint_open,
	.release = fingerprint_release,
	.unlocked_ioctl = fingerprint_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = fingerprint_compat_ioctl,
#endif
	.read = fpsensor_read,
};

#if defined(CONFIG_FB)
static int fb_notifier_callback(struct notifier_block *self,
	unsigned long event, void *data)
{
	struct fb_event *evdata = data;
	int *blank = NULL;
	struct fp_data *fingerprint = container_of(self, struct fp_data,
		fb_notify);
	if (evdata && evdata->data && fingerprint && event == FB_EVENT_BLANK) {
		blank = evdata->data;
		if (*blank == FB_BLANK_UNBLANK)
			atomic_set(&fingerprint->state, FP_LCD_UNBLANK);
		else if (*blank == FB_BLANK_POWERDOWN)
			atomic_set(&(fingerprint->state), FP_LCD_POWEROFF);
	}
	return SUCCESS;
}
#endif

static int fingerprint_probe_init(struct fp_data *fingerprint,
	struct device *dev, struct spi_device *spi)
{
	int error;

#if defined(CONFIG_HUAWEI_DSM)
	if (!g_fingerprint_dclient) {
		pr_err("g_fingerprint_dclient error\n");
		g_fingerprint_dclient = dsm_register_client(&g_dsm_fingerprint);
	}
#endif
	pr_err("fingerprint driver for Android P\n");
	fingerprint->dev = dev;
	dev_set_drvdata(dev, fingerprint);
	fingerprint->spi = spi;
	fingerprint->spi->mode = SPI_MODE_0;
	fingerprint->spi->bits_per_word = BITS_PER_WORD;
	fingerprint->spi->chip_select = 0;
	error = fingerprint_get_dts_data(&(spi->dev), fingerprint);
	if (error) {
		pr_err("fingerprint get dts data fail, error = %d\n",
			error);
		return -ENOMEM;
	}

	error = fp_get_power_en_gpio(fingerprint);
	if (error != SUCCESS)
		pr_err("fingerprint enable gpio fail, error = %d\n", error);

	error = fingerprint_extern_ldo_proc(&spi->dev, fingerprint);
	if (error < 0)
		pr_err("fingerprint_poweron regulator failed\n");

#if defined(CONFIG_FB)
	fingerprint->fb_notify.notifier_call = NULL;
#endif

	atomic_set(&fingerprint->state, FP_UNINIT);
	error = fingerprint_irq_init(fingerprint);
	if (error) {
		pr_err("fingerprint irq init failed, error = %d\n", error);
	}

	error = fingerprint_btb_det_irq_init(fingerprint);
	if (error)
		pr_err("fp_btb_detect_irq_init failed: %d\n", error);

	error = fingerprint_get_navigation_adjust_value(&spi->dev, fingerprint);
	if (error) {
		pr_err("fingerprint get navigation adjust value\n");
		error = -ENOMEM;
	}

	return 0;
}

static void fp_input_event_register(struct fp_data *fingerprint)
{
	input_set_capability(fingerprint->input_dev, EV_KEY, EVENT_UP);
	input_set_capability(fingerprint->input_dev, EV_KEY, EVENT_DOWN);
	input_set_capability(fingerprint->input_dev, EV_KEY, EVENT_LEFT);
	input_set_capability(fingerprint->input_dev, EV_KEY, EVENT_RIGHT);
	input_set_capability(fingerprint->input_dev, EV_KEY, EVENT_CLICK);
	input_set_capability(fingerprint->input_dev, EV_KEY, EVENT_HOLD);
	input_set_capability(fingerprint->input_dev, EV_KEY, EVENT_DCLICK);
	set_bit(EV_KEY, fingerprint->input_dev->evbit);
	set_bit(EVENT_UP, fingerprint->input_dev->evbit);
	set_bit(EVENT_DOWN, fingerprint->input_dev->evbit);
	set_bit(EVENT_LEFT, fingerprint->input_dev->evbit);
	set_bit(EVENT_RIGHT, fingerprint->input_dev->evbit);
	set_bit(EVENT_CLICK, fingerprint->input_dev->evbit);
	set_bit(EVENT_HOLD, fingerprint->input_dev->evbit);
	set_bit(EVENT_DCLICK, fingerprint->input_dev->evbit);
}

static int fingerprint_probe_first(struct fp_data *fingerprint,
	struct device *dev)
{
	int error;

	fingerprint->class = class_create(THIS_MODULE, FP_CLASS_NAME);
	error = alloc_chrdev_region(&(fingerprint->devno), 0, 1, FP_DEV_NAME);
	if (error) {
		pr_err("alloc_chrdev_region failed, error = %d\n", error);
		return -ENOMEM;
	}
	pr_err("%s fingerprint = devno %d\n", __func__, fingerprint->devno);
	fingerprint->device = device_create(fingerprint->class, NULL,
		fingerprint->devno, NULL, "%s", FP_DEV_NAME);
	cdev_init(&(fingerprint->cdev), &fingerprint_fops);
	fingerprint->cdev.owner = THIS_MODULE;
	error = cdev_add(&(fingerprint->cdev), fingerprint->devno, 1);
	if (error) {
		pr_err("cdev_add failed, error = %d\n", error);
		return -ENOMEM;
	}
	fingerprint->input_dev = devm_input_allocate_device(dev);
	if (fingerprint->input_dev == NULL) {
		error = -ENOMEM;
		pr_err("devm_input_allocate_device failed, error = %d\n",
			error);
		return -ENOMEM;
	}
	fingerprint->input_dev->name = "fingerprint";
	/* Also register the key for wake up */
	fp_input_event_register(fingerprint);
	error = input_register_device(fingerprint->input_dev);
	if (error) {
		pr_err("input_register_device failed, error = %d\n", error);
		return -ENOMEM;
	}

	return 0;
}

static void write_conf_to_tee(struct spi_device *spi,
	struct fp_data *fingerprint)
{
	struct arm_smccc_res res;
	uint32_t x1;
	uint32_t x2;
	uint16_t product_id = (uint16_t)fingerprint->product_id;
	uint8_t sensor_type = 0;
	uint8_t spi_bus = (uint8_t)spi->controller->bus_num;

	pr_info("%s: spi_bus = %u\n", __func__, spi_bus);
	x1 = sensor_type;
	x1 = x1 | (spi_bus << OFFSET_8);
	x1 = x1 | (FP_CHECK_NUM << OFFSET_16);

	x2 = product_id;
	x2 = x2 | (FP_CHECK_NUM << OFFSET_16);

	// x1,x2,0,0 --> x1,x2,x3,x4
	arm_smccc_1_1_smc(MTK_SIP_KERNEL_FP_CONF_TO_TEE_ADDR_AARCH64,
		x1, x2, 0, 0, &res);
	pr_info("%s: x1:0x%x x2:0x%x\n", __func__, x1, x2);
}

static int fingerprint_probe_second(struct fp_data *fingerprint)
{
	int error;

	fingerprint->pf_dev = platform_device_alloc(FP_DEV_NAME, -1);
	if (!fingerprint->pf_dev) {
		error = -ENOMEM;
		pr_err("platform_device_alloc failed, error = %d\n", error);
		return -ENOMEM;
	}

	error = platform_device_add(fingerprint->pf_dev);
	if (error) {
		pr_err("platform_device_add failed, error = %d\n", error);
		platform_device_del(fingerprint->pf_dev);
		return -ENOMEM;
	}

	dev_set_drvdata(&(fingerprint->pf_dev->dev), fingerprint);
	error = sysfs_create_group(&(fingerprint->pf_dev->dev).kobj,
		&attribute_group);
	if (error) {
		pr_err("sysfs_create_group failed, error = %d\n", error);
		return -ENOMEM;
	}

	wakeup_source_init(&(fingerprint->ttw_wl), "fpc_ttw_wl");
	init_waitqueue_head(&fingerprint->hbm_queue);
	init_waitqueue_head(&fingerprint->wait_finger_up_queue);
	mutex_init(&(fingerprint->lock));
	mutex_init(&(fingerprint->mutex_lock_clk));
	mutex_init(&fingerprint->mutex_lock_irq_switch);

	if (fingerprint->use_tp_irq == USE_TP_IRQ) {
		fp_ops_register(&g_ud_fp_ops);
		pr_err("%s use tp irq,register ud_fp_ops\n", __func__);
		goto use_tp_irq_tag;
	}

	error = request_threaded_irq(fingerprint->irq, NULL,
		fingerprint_irq_handler,
		(IRQF_TRIGGER_RISING | IRQF_ONESHOT),
		"fingerprint", fingerprint);
	if (error) {
		pr_err("%s failed to request irq %d\n", __func__, fingerprint->irq);
		return -ENOMEM;
	}
	fingerprint->irq_num = 1;

	/* Request that the interrupt should be wakeable */
	enable_irq_wake(fingerprint->irq);
	fingerprint->wakeup_enabled = true;
	fingerprint->snr_stat = 0;
	mt_spi_enable_master_clk(fingerprint->spi);

use_tp_irq_tag:
#if defined(CONFIG_FB)
	if (!fingerprint->fb_notify.notifier_call) {
		fingerprint->fb_notify.notifier_call = fb_notifier_callback;
		fb_register_client(&(fingerprint->fb_notify));
	}
#endif
	write_conf_to_tee(fingerprint->spi, fingerprint);
	fingerprint->nav_stat = 0;
	fingerprint->sensor_id = 0;
	fingerprint->hbm_status = HBM_NONE;
	fingerprint->fingerprint_bigdata.lcd_charge_time = 60; /* unit ms */
	fingerprint->fingerprint_bigdata.lcd_on_time = 50; /* unit ms */
	fingerprint->fingerprint_bigdata.cpu_wakeup_time = 80; /* unit ms */

	g_fp_data = fingerprint;
	fingerprint_custom_timing(fingerprint);

	return 0;
}

static int fingerprint_probe(struct spi_device *spi)
{
	struct device *dev = NULL;
	struct fp_data *fingerprint = NULL;
	int error;

	if (!spi) {
		pr_err("spi is null\n");
		return -EIO;
	}
	pr_info("%s: enter\n", __func__);
	dev = &(spi->dev);
	if (!dev) {
		pr_err("%s: FINGER dev is null\n", __func__);
		goto exit;
	}

	fingerprint = devm_kzalloc(dev, sizeof(*fingerprint), GFP_KERNEL);
	if (!fingerprint) {
		error = -ENOMEM;
		goto exit;
	}

	error = fingerprint_probe_init(fingerprint, dev, spi);
	if (error)
		goto exit;

	error = fingerprint_probe_first(fingerprint, dev);
	if (error)
		goto exit;

	error = fingerprint_probe_second(fingerprint);
	if (error)
		goto exit;

	return error;
exit:
	pr_info("%s failed!\n", __func__);
#if defined(CONFIG_HUAWEI_DSM)
	if (error && (!dsm_client_ocuppy(g_fingerprint_dclient))) {
		dsm_client_record(g_fingerprint_dclient,
			"%s failed, error = %d\n", __func__, error);
		dsm_client_notify(g_fingerprint_dclient,
			DSM_FINGERPRINT_PROBE_FAIL_ERROR_NO);
	}
#endif
	return error;
}

static int fingerprint_remove(struct spi_device *spi)
{
	struct fp_data *fingerprint = NULL;

	if (!spi) {
		pr_err("spi is null\n");
		return -EINVAL;
	}

	fingerprint = dev_get_drvdata(&(spi->dev));
	sysfs_remove_group(&(fingerprint->pf_dev->dev.kobj), &attribute_group);
	cdev_del(&(fingerprint->cdev));
	unregister_chrdev_region(fingerprint->devno, 1);
	input_free_device(fingerprint->input_dev);
	mutex_destroy(&(fingerprint->lock));
	mutex_destroy(&(fingerprint->mutex_lock_clk));
	wakeup_source_trash(&(fingerprint->ttw_wl));
	mutex_destroy(&fingerprint->mutex_lock_irq_switch);
	fingerprint_btb_det_remove(fingerprint);
#if defined(CONFIG_FB)
	if (!fingerprint->fb_notify.notifier_call) {
		fingerprint->fb_notify.notifier_call = NULL;
		fb_unregister_client(&(fingerprint->fb_notify));
	}
#endif
	fp_ops_unregister(&g_ud_fp_ops);
	return SUCCESS;
}

static int fingerprint_suspend(struct device *dev)
{
	if (!dev) {
		pr_err("dev is null\n");
		return -EINVAL;
	}

	pr_info("fingerprint suspend\n");
	if (g_fp_data->spi != NULL)
		mt_spi_disable_master_clk(g_fp_data->spi);
	return FP_RETURN_SUCCESS;
}

static int fingerprint_resume(struct device *dev)
{
	if (!dev) {
		pr_err("dev is null\n");
		return -EINVAL;
	}

	pr_info("fingerprint resume\n");
	if (g_fp_data->spi != NULL)
		mt_spi_enable_master_clk(g_fp_data->spi);
	return FP_RETURN_SUCCESS;
}

static const struct dev_pm_ops fingerprint_pm = {
	.suspend = fingerprint_suspend,
	.resume = fingerprint_resume
};

static const struct of_device_id fingerprint_of_match[] = {
	{ .compatible = "mediatek,mtk_finger", },
	{}
};

MODULE_DEVICE_TABLE(of, fingerprint_of_match);

static struct spi_driver fingerprint_driver = {
	.driver = {
		.name = "fingerprint",
		.owner = THIS_MODULE,
		.of_match_table = fingerprint_of_match,
		.pm = &fingerprint_pm
	},
	.probe = fingerprint_probe,
	.remove = fingerprint_remove
};

static int __init fingerprint_init(void)
{
	int status;

	status = spi_register_driver(&fingerprint_driver);
	if (status < 0) {
		pr_err("%s:status is %d\n", __func__, status);
		return -EINVAL;
	}
	return SUCCESS;
}

static void __exit fingerprint_exit(void)
{
	spi_unregister_driver(&fingerprint_driver);
}
late_initcall(fingerprint_init);
module_exit(fingerprint_exit);

MODULE_LICENSE("GPL v2");
