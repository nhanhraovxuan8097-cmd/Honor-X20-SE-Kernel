/*
 * lcd_factory.c
 *
 * lcd factory test function for lcd driver
 *
 * Copyright (c) 2021-2022 Honor Technologies Co., Ltd.
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
#include <linux/device.h>
#include "lcd_defs.h"
#include "lcd_panel.h"
#include "lcd_factory.h"

static char *g_sence_array[] = {
	"LCD_INCOME0",   "MMI0",   "RUNNINGTEST0", "PROJECT_MENU0",
	"LCD_INCOME1",   "MMI1",   "RUNNINGTEST1",  "PROJECT_MENU1",
	"LCD_INCOME2",   "MMI2",   "RUNNINGTEST2",  "PROJECT_MENU2",
	"LCD_INCOME3",   "MMI3",   "RUNNINGTEST3",  "PROJECT_MENU3",
	"LCD_INCOME4",   "MMI4",   "RUNNINGTEST4",  "PROJECT_MENU4",
	"LCD_INCOME5",   "MMI5",   "RUNNINGTEST5",  "PROJECT_MENU5",
	"LCD_INCOME6",   "MMI6",   "RUNNINGTEST6",  "PROJECT_MENU6",
	"LCD_INCOME7",   "MMI7",   "RUNNINGTEST7",  "PROJECT_MENU7",
	"LCD_INCOME8",   "MMI8",   "RUNNINGTEST8",  "PROJECT_MENU8",
	"LCD_INCOME9",   "MMI9",   "RUNNINGTEST9",  "PROJECT_MENU9",
	"LCD_INCOME10",  "MMI10",  "RUNNINGTEST10",  "PROJECT_MENU10",
	"LCD_INCOME11",  "MMI11",  "RUNNINGTEST11",  "PROJECT_MENU11",
	"LCD_INCOME12",  "MMI12",  "RUNNINGTEST12",  "PROJECT_MENU12",
	"LCD_INCOME13",  "MMI13",  "RUNNINGTEST13",  "PROJECT_MENU13",
	"LCD_INCOME14",  "MMI14",  "RUNNINGTEST14",  "PROJECT_MENU14",
	"LCD_INCOME15",  "MMI15",  "RUNNINGTEST15",  "PROJECT_MENU15",
	"LCD_INCOME16",  "MMI16",  "RUNNINGTEST16",  "PROJECT_MENU16",
	"LCD_INCOME17",  "MMI17",  "RUNNINGTEST17",  "PROJECT_MENU17",
	"LCD_INCOME18",  "MMI18",  "RUNNINGTEST18",  "PROJECT_MENU18",
	"LCD_INCOME19",  "MMI19",  "RUNNINGTEST19",  "PROJECT_MENU19",
	"LCD_INCOME20",  "MMI20",  "RUNNINGTEST20",  "PROJECT_MENU20",
	"LCD_INCOME21",  "MMI21",  "RUNNINGTEST21",  "PROJECT_MENU21",
	"LCD_INCOME22",  "MMI22",  "RUNNINGTEST22",  "PROJECT_MENU22",
	"LCD_INCOME23",  "MMI23",  "RUNNINGTEST23",  "PROJECT_MENU23",
	"LCD_INCOME24",  "MMI24",  "RUNNINGTEST24",  "PROJECT_MENU24",
	"LCD_INCOME25",  "MMI25",  "RUNNINGTEST25",  "PROJECT_MENU25",
	"LCD_INCOME26",  "MMI26",  "RUNNINGTEST26",  "PROJECT_MENU26",
	"LCD_INCOME27",  "MMI27",  "RUNNINGTEST27",  "PROJECT_MENU27",
	"LCD_INCOME28",  "MMI28",  "RUNNINGTEST28",  "PROJECT_MENU28",
	"LCD_INCOME29",  "MMI29",  "RUNNINGTEST29",  "PROJECT_MENU29",
	"LCD_INCOME30",  "MMI30",  "RUNNINGTEST30",  "PROJECT_MENU30",
	"LCD_INCOME31",  "MMI31",  "RUNNINGTEST31",  "PROJECT_MENU31",
	"LCD_INCOME32",  "MMI32",  "RUNNINGTEST32",  "PROJECT_MENU32",
	"CURRENT1_0",    "CURRENT1_1", "CURRENT1_2",  "CURRENT1_3",
	"CURRENT1_4",    "CURRENT1_5", "CHECKSUM1",  "CHECKSUM2",
	"CHECKSUM3",     "CHECKSUM4", "BL_OPEN_SHORT",  "PCD_ERRORFLAG",
	"DOTINVERSION",  "CHECKREG", "COLUMNINVERSION",   "POWERONOFF",
	"BLSWITCH", "CURRENT_TEST_MODE", "OVER_CURRENT_DETECTION",
	"OVER_VOLTAGE_DETECTION", "GENERAL_TEST", "VERTICAL_LINE",
	"DDIC_LV_DETECT", "AVDD_DETECT",
};

static char *g_cmd_array[] = {
	"CURRENT1_0",   "CURRENT1_0",  "CURRENT1_0",  "CURRENT1_0",
	"CURRENT1_1",   "CURRENT1_1",  "CURRENT1_1",  "CURRENT1_1",
	"CURRENT1_2",   "CURRENT1_2",  "CURRENT1_2",  "CURRENT1_2",
	"CURRENT1_3",   "CURRENT1_3",  "CURRENT1_3",  "CURRENT1_3",
	"CURRENT1_4",   "CURRENT1_4",  "CURRENT1_4",  "CURRENT1_4",
	"CURRENT1_5",   "CURRENT1_5",  "CURRENT1_5",  "CURRENT1_5",
	"CHECKSUM1",   "CHECKSUM1",   "CHECKSUM1", "CHECKSUM1",
	"CHECKSUM2",   "CHECKSUM2",   "CHECKSUM2", "CHECKSUM2",
	"CHECKSUM3",    "CHECKSUM3",   "CHECKSUM3", "CHECKSUM3",
	"CHECKSUM4",   "CHECKSUM4",   "CHECKSUM4", "CHECKSUM4",
	"BL_OPEN_SHORT",   "BL_OPEN_SHORT",   "BL_OPEN_SHORT", "BL_OPEN_SHORT",
	"PCD_ERRORFLAG",   "PCD_ERRORFLAG",  "PCD_ERRORFLAG", "PCD_ERRORFLAG",
	"DOTINVERSION",    "DOTINVERSION",  "DOTINVERSION", "DOTINVERSION",
	"CHECKREG",    "CHECKREG",  "CHECKREG", "CHECKREG",
	"COLUMNINVERSION", "COLUMNINVERSION", "COLUMNINVERSION", "COLUMNINVERSION",
	"POWERONOFF",   "POWERONOFF",  "POWERONOFF",  "POWERONOFF",
	"BLSWITCH",    "BLSWITCH",  "BLSWITCH", "BLSWITCH",
	"GPU_TEST",   "GPU_TEST",  "GPU_TEST", "GPU_TEST",
	"GENERAL_TEST", "GENERAL_TEST", "GENERAL_TEST", "GENERAL_TEST",
	"VERTICAL_LINE_1", "VERTICAL_LINE_1", "VERTICAL_LINE_1",
	"VERTICAL_LINE_1", "VERTICAL_LINE_2", "VERTICAL_LINE_2",
	"VERTICAL_LINE_2", "VERTICAL_LINE_2", "VERTICAL_LINE_3",
	"VERTICAL_LINE_3", "VERTICAL_LINE_3", "VERTICAL_LINE_3",
	"VERTICAL_LINE_4", "VERTICAL_LINE_4", "VERTICAL_LINE_4",
	"VERTICAL_LINE_4", "VERTICAL_LINE_5", "VERTICAL_LINE_5",
	"VERTICAL_LINE_5", "VERTICAL_LINE_5", "DDIC_LV_DET_CMD_1",
	"DDIC_LV_DET_CMD_1",    "DDIC_LV_DET_CMD_1",    "DDIC_LV_DET_CMD_1",
	"DDIC_LV_DET_RESULT_1", "DDIC_LV_DET_RESULT_1", "DDIC_LV_DET_RESULT_1",
	"DDIC_LV_DET_RESULT_1", "DDIC_LV_DET_CMD_2",    "DDIC_LV_DET_CMD_2",
	"DDIC_LV_DET_CMD_2",    "DDIC_LV_DET_CMD_2",    "DDIC_LV_DET_RESULT_2",
	"DDIC_LV_DET_RESULT_2", "DDIC_LV_DET_RESULT_2", "DDIC_LV_DET_RESULT_2",
	"DDIC_LV_DET_CMD_3",    "DDIC_LV_DET_CMD_3",    "DDIC_LV_DET_CMD_3",
	"DDIC_LV_DET_CMD_3",    "DDIC_LV_DET_RESULT_3", "DDIC_LV_DET_RESULT_3",
	"DDIC_LV_DET_RESULT_3", "DDIC_LV_DET_RESULT_3", "DDIC_LV_DET_CMD_4",
	"DDIC_LV_DET_CMD_4",    "DDIC_LV_DET_CMD_4",    "DDIC_LV_DET_CMD_4",
	"DDIC_LV_DET_RESULT_4", "DDIC_LV_DET_RESULT_4", "DDIC_LV_DET_RESULT_4",
	"DDIC_LV_DET_RESULT_4", "AVDD_DET_GET_RESULT", "AVDD_DET_GET_RESULT",
	"AVDD_DET_GET_RESULT", "AVDD_DET_GET_RESULT",
	"/sys/class/ina231/ina231_0/ina231_set," \
	"/sys/class/ina231/ina231_0/ina231_value," \
	"1,9999999,1,9999999,1,99999",
	"/sys/class/ina231/ina231_0/ina231_set," \
	"/sys/class/ina231/ina231_0/ina231_value," \
	"1,9999999,1,9999999,1,99999",
	"/sys/class/ina231/ina231_0/ina231_set," \
	"/sys/class/ina231/ina231_0/ina231_value," \
	"1,9999999,1,9999999,1,99999",
	"/sys/class/ina231/ina231_0/ina231_set," \
	"/sys/class/ina231/ina231_0/ina231_value," \
	"1,9999999,1,9999999,1,99999",
	"/sys/class/ina231/ina231_0/ina231_set," \
	"/sys/class/ina231/ina231_0/ina231_value," \
	"1,9999999,1,9999999,1,99999",
	"/sys/class/ina231/ina231_0/ina231_set," \
	"/sys/class/ina231/ina231_0/ina231_value," \
	"1,9999999,1,9999999,1,99999",
	"/sys/class/graphics/fb0/lcd_checksum",
	"/sys/class/graphics/fb0/lcd_checksum",
	"/sys/class/graphics/fb0/lcd_checksum",
	"/sys/class/graphics/fb0/lcd_checksum",
	"/sys/class/graphics/fb0/bl_self_test",
	"/sys/class/graphics/fb0/amoled_pcd_errflag_check",
	"/sys/class/graphics/fb0/lcd_inversion_mode",
	"/sys/class/graphics/fb0/lcd_check_reg",
	"/sys/class/graphics/fb0/lcd_inversion_mode",
	"/sys/class/graphics/fb0/lcd_check_reg",
	"/sys/class/graphics/fb0/lcd_check_reg",
	"LCD_CUR_DET_MODE",
	"OVER_CURRENT_DETECTION",
	"OVER_VOLTAGE_DETECTION",
	"/sys/class/graphics/fb0/lcd_general_test",
	"/sys/class/graphics/fb0/vertical_line_test",
	"/sys/class/graphics/fb0/ddic_lv_detect",
	"/sys/class/graphics/fb0/avdd_detect",
};

struct lcd_fact_info *get_fact_info(int idx)
{
	struct lcd_fact_info *fact_info = NULL;
	struct panel_info *pinfo = NULL;

	pinfo = get_panel_info(idx);
	if (!pinfo) {
		LCD_ERR("pinfo is null\n");
		return NULL;
	}
	fact_info = pinfo->fact_info;
	return fact_info;
}

static int lcd_current_det(struct lcd_fact_info *fact_info)
{
	int current_check_result = 0;

	return current_check_result;
}

static int lcd_lv_det(struct lcd_fact_info *fact_info)
{
	int lv_check_result = 0;

	return lv_check_result;
}

static ssize_t lcd_inversion_mode_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	ssize_t ret = 0;

	return ret;
}

static ssize_t	lcd_inversion_mode_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	return count;
}

static ssize_t lcd_scan_mode_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	ssize_t ret = 0;

	return ret;
}

static ssize_t	lcd_scan_mode_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	return count;
}

static ssize_t lcd_check_reg_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int i;
	ssize_t ret = LCD_OK;
	uint8_t *exp_val = NULL;
	uint8_t read_value[MAX_REG_READ_COUNT] = {0};
	struct panel_info *pinfo = get_panel_info(get_fb_index(dev));

	if (!pinfo || !pinfo->fact_info) {
		LCD_ERR("pointer is null\n");
		return -EINVAL;
	}

	if (pinfo->panel_state != LCD_HS_ON) {
		LCD_ERR("panel is power off!\n");
		return ret;
	}

	if (!pinfo->fact_info->checkreg.enabled) {
		LCD_INFO("not support check reg\n");
		return ret;
	}

	exp_val = pinfo->fact_info->checkreg.expect_val;
	dsi_panel_receive_data(pinfo->panel,
			DSI_CMD_GET_CHECK_REG, read_value, MAX_REG_READ_COUNT - 1);
	for (i = 0; i < pinfo->fact_info->checkreg.expect_count; i++) {
		if (read_value[i] != exp_val[i]) {
			ret = LCD_FAIL;
			LCD_ERR("read_value[%u] = 0x%x, but expect_ptr[%u] = 0x%x!\n",
					i, read_value[i], i, exp_val[i]);
			break;
		}
		LCD_INFO("read_value[%u] = 0x%x same with expect value!\n",
				i, read_value[i]);
	}

	if (!ret)
		ret = snprintf(buf, PAGE_SIZE, "OK");
	else
		ret = snprintf(buf, PAGE_SIZE, "FAIL");
	LCD_INFO("checkreg result:%s\n", buf);
	return ret;
}

static ssize_t lcd_gram_check_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	ssize_t ret = 0;
	int checksum_result = 0;

	ret = snprintf(buf, PAGE_SIZE, "%d\n", checksum_result);
	return ret;
}

static ssize_t	lcd_gram_check_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	return count;
}

static ssize_t lcd_sleep_ctrl_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	ssize_t ret = 0;
	struct panel_info *pinfo = NULL;

	pinfo = get_panel_info(get_fb_index(dev));
	if (!pinfo) {
		LCD_ERR("pinfo is null\n");
		return -EINVAL;
	}
	ret = snprintf(buf, PAGE_SIZE, "PT test mod = %d\n", pinfo->fact_info->pt_flag);
	return ret;
}

static ssize_t	lcd_sleep_ctrl_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned long val = 0;
	struct panel_info *pinfo = NULL;

	if (buf == NULL) {
		LCD_ERR("buf is null\n");
		return -EINVAL;
	}
	val = simple_strtoul(buf, NULL, 0);
	pinfo = get_panel_info(get_fb_index(dev));
	if (!pinfo) {
		LCD_ERR("pinfo is null\n");
		return -EINVAL;
	}
	pinfo->fact_info->pt_flag = val;
	return count;
}

static ssize_t lcd_amoled_pcd_errflag_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	ssize_t ret = 0;
	int check_result = 0;

	ret = snprintf(buf, PAGE_SIZE, "%d\n", check_result);
	return ret;
}

static ssize_t	lcd_amoled_pcd_errflag_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	return count;
}

static ssize_t lcd_test_config_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int i;
	int ret;
	struct lcd_fact_info *fact_info = NULL;

	fact_info = get_fact_info(get_fb_index(dev));
	if (!fact_info) {
		LCD_ERR("fact_info is null\n");
		return -EINVAL;
	}
	for (i = 0; i < (int)ARRAY_SIZE(g_sence_array); i++) {
		if (g_sence_array[i] == NULL) {
			ret = -EINVAL;
			LCD_INFO("Sence cmd is end, total num is:%d\n", i);
			break;
		}
		if (!strncmp(fact_info->lcd_cmd_now, g_sence_array[i],
			strlen(fact_info->lcd_cmd_now))) {
			LCD_INFO("current test cmd:%s,return cmd:%s\n",
				fact_info->lcd_cmd_now, g_cmd_array[i]);
			ret = snprintf(buf, PAGE_SIZE, g_cmd_array[i]);
		}
	}
	/* current and voltage test */
	if (buf) {
		if (!strncmp(buf, "OVER_CURRENT_DETECTION",
			strlen("OVER_CURRENT_DETECTION"))) {
			ret = lcd_current_det(fact_info);
			return snprintf(buf, PAGE_SIZE, "%d", ret);
		}
		if (!strncmp(buf, "OVER_VOLTAGE_DETECTION",
			strlen("OVER_VOLTAGE_DETECTION"))) {
			ret = lcd_lv_det(fact_info);
			return snprintf(buf, PAGE_SIZE, "%d", ret);
		}
	}
	return ret;
}

static ssize_t	lcd_test_config_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct lcd_fact_info *fact_info = NULL;

	fact_info = get_fact_info(get_fb_index(dev));
	if (!fact_info) {
		LCD_ERR("fact_info is null\n");
		return -EINVAL;
	}

	if (strlen(buf) < LCD_CMD_NAME_MAX) {
		memcpy(fact_info->lcd_cmd_now, buf, strlen(buf) + 1);
		LCD_INFO("current test cmd:%s\n", fact_info->lcd_cmd_now);
	} else {
		memcpy(fact_info->lcd_cmd_now, "INVALID", strlen("INVALID") + 1);
		LCD_ERR("invalid test cmd:%s\n", fact_info->lcd_cmd_now);
	}
	return count;
}

static ssize_t lcd_lv_detect_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	ssize_t ret = 0;

	ret = snprintf(buf, PAGE_SIZE, "%d", ret);
	return ret;
}

static ssize_t lcd_current_detect_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	ssize_t ret = 0;

	ret = snprintf(buf, PAGE_SIZE, "%d", ret);
	return ret;
}

static ssize_t lcd_general_test_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	ssize_t ret = 0;

	if (!ret)
		ret = snprintf(buf, PAGE_SIZE, "OK\n");
	else
		ret = snprintf(buf, PAGE_SIZE, "FAIL\n");
	return ret;
}

static ssize_t lcd_vertical_line_test_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	ssize_t ret = 0;

	if (!ret)
		ret = snprintf(buf, PAGE_SIZE, "OK\n");
	return ret;
}

static ssize_t	lcd_vertical_line_test_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	return count;
}

static ssize_t lcd_oneside_mode_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	ssize_t ret = 0;

	if (!ret)
		ret = snprintf(buf, PAGE_SIZE, "OK\n");
	return ret;
}

static ssize_t	lcd_oneside_mode_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	return count;
}

static ssize_t lcd_avdd_detect_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	ssize_t ret = 0;

	if (!ret)
		ret = snprintf(buf, PAGE_SIZE, "%d\n", 0);
	return ret;
}

static ssize_t lcd_ddic_lv_detect_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	ssize_t ret = 0;

	if (!buf) {
		LCD_ERR("buf is null\n");
		return -EINVAL;
	}
	if (!ret)
		ret = snprintf(buf, PAGE_SIZE, "%d\n", 0);
	return ret;
}

static ssize_t	lcd_ddic_lv_detect_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	return count;
}

static DEVICE_ATTR(lcd_inversion_mode, 0644, lcd_inversion_mode_show,
	lcd_inversion_mode_store);
static DEVICE_ATTR(lcd_scan_mode, 0644, lcd_scan_mode_show,
	lcd_scan_mode_store);
static DEVICE_ATTR(lcd_check_reg, 0444, lcd_check_reg_show, NULL);
static DEVICE_ATTR(lcd_checksum, 0644, lcd_gram_check_show,
	lcd_gram_check_store);
static DEVICE_ATTR(lcd_sleep_ctrl, 0644, lcd_sleep_ctrl_show,
	lcd_sleep_ctrl_store);
static DEVICE_ATTR(amoled_pcd_errflag_check, 0644,
	lcd_amoled_pcd_errflag_show,
	lcd_amoled_pcd_errflag_store);
static DEVICE_ATTR(lcd_test_config, 0640, lcd_test_config_show,
	lcd_test_config_store);
static DEVICE_ATTR(lv_detect, 0640, lcd_lv_detect_show, NULL);
static DEVICE_ATTR(current_detect, 0640, lcd_current_detect_show, NULL);
static DEVICE_ATTR(lcd_general_test, 0444, lcd_general_test_show, NULL);
static DEVICE_ATTR(vertical_line_test, 0644,
	lcd_vertical_line_test_show, lcd_vertical_line_test_store);
static DEVICE_ATTR(lcd_oneside_mode, 0644, lcd_oneside_mode_show,
	lcd_oneside_mode_store);
static DEVICE_ATTR(avdd_detect, 0644, lcd_avdd_detect_show, NULL);
static DEVICE_ATTR(ddic_lv_detect, 0644, lcd_ddic_lv_detect_show,
	lcd_ddic_lv_detect_store);

/* factory attrs */
struct attribute *lcd_factory_attrs[] = {
	&dev_attr_lcd_inversion_mode.attr,
	&dev_attr_lcd_scan_mode.attr,
	&dev_attr_lcd_check_reg.attr,
	&dev_attr_lcd_checksum.attr,
	&dev_attr_lcd_sleep_ctrl.attr,
	&dev_attr_amoled_pcd_errflag_check.attr,
	&dev_attr_lcd_test_config.attr,
	&dev_attr_lv_detect.attr,
	&dev_attr_current_detect.attr,
	&dev_attr_lcd_general_test.attr,
	&dev_attr_vertical_line_test.attr,
	&dev_attr_lcd_oneside_mode.attr,
	&dev_attr_avdd_detect.attr,
	&dev_attr_ddic_lv_detect.attr,
	NULL,
};

struct attribute_group lcd_fac_sysfs_attr_group = {
	.attrs = lcd_factory_attrs,
};

int lcd_create_fact_sysfs(struct kobject *obj)
{
	int rc;

	rc = sysfs_create_group(obj, &lcd_fac_sysfs_attr_group);
	if (rc)
		LCD_ERR("sysfs group creation failed, rc=%d\n", rc);
	return rc;
}

static void parse_check_reg(struct lcd_fact_info *fact_info,
	struct dsi_parser_utils *utils)
{
	int i = 0;
	int val = 0;

	fact_info->checkreg.enabled = utils->read_bool(utils->data,
			"qcom,mdss-dsi-panel-lcd-check-reg-enabled");
	if (!fact_info->checkreg.enabled)
		return;

	fact_info->checkreg.expect_count = of_property_count_u32_elems(utils->data,
			"qcom,mdss-dsi-panel-lcd-check-reg-expect_vals");

	fact_info->checkreg.expect_val = kcalloc(fact_info->checkreg.expect_count,
			sizeof(int), GFP_KERNEL);
	for (i = 0; i < fact_info->checkreg.expect_count; i++) {
		of_property_read_u32_index(utils->data,
			"qcom,mdss-dsi-panel-lcd-check-reg-expect_vals", i, &val);
		fact_info->checkreg.expect_val[i] = val;
	}
}

int factory_init(struct dsi_panel *panel, struct panel_info *pinfo)
{
	struct dsi_parser_utils *utils = &panel->utils;

	if (!utils) {
		LCD_ERR("utils is null\n");
		return -1;
	}
	pinfo->fact_info = kzalloc(sizeof(struct lcd_fact_info), GFP_KERNEL);
	if (!pinfo->fact_info) {
		LCD_ERR("kzalloc factory info fail\n");
		return -1;
	}
	pinfo->fact_info->pt_reset_enable = utils->read_bool(utils->data,
		"qcom,mdss-dsi-panel-pt-reset-enabled");
	parse_check_reg(pinfo->fact_info, utils);
	return 0;
}
