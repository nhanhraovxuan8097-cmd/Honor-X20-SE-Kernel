/*
 * lcd_sysfs.c
 *
 * lcd sysfs function for lcd driver
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
#include "lcd_sysfs.h"

/* oem info */
static unsigned char oem_info_type = INVALID_TYPE;
static int lcd_get_2d_barcode(struct panel_info *pinfo, char *oem_data, int len);
static int lcd_get_project_id(struct panel_info *pinfo, char *oem_data, int len);

static struct oem_info_cmd oem_read_cmds[] = {
	{ PROJECT_ID_TYPE, lcd_get_project_id },
	{ BARCODE_2D_TYPE, lcd_get_2d_barcode },
};

static struct fb_info *get_fb_info(struct device *dev)
{
    struct fb_info *fbi = NULL;

    fbi = dev_get_drvdata(dev);
    return fbi;
}

int get_fb_index(struct device *dev)
{
	struct fb_info *fbinfo = NULL;

    if (!dev) {
        LCD_ERR("dev is null\n");
        return 0;
    }

	fbinfo = get_fb_info(dev);
	if (!fbinfo) {
        LCD_ERR("fbinfo is null\n");
		return 0;
	}
	return fbinfo->node;
}

static int lcd_get_project_id(struct panel_info *pinfo, char *oem_data, int len)
{
	return 0;
}

static int lcd_get_2d_barcode(struct panel_info *pinfo, char *oem_data, int len)
{
	char read_value[OEM_INFO_SIZE_MAX] = {0};

	if (!pinfo->oeminfo.barcode_2d.support) {
		LCD_ERR("not support 2d barcode\n");
		return LCD_FAIL;
	}
	dsi_panel_receive_data(pinfo->panel,
		DSI_CMD_READ_OEMINFO, read_value, OEM_INFO_SIZE_MAX - 1);
	oem_data[0] = BARCODE_2D_TYPE;
	oem_data[1] = BARCODE_BLOCK_NUM + pinfo->oeminfo.barcode_2d.offset;
	strncat(oem_data, read_value, strlen(read_value));
	return LCD_OK;
}

static ssize_t lcd_model_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	ssize_t ret = 0;
	struct panel_info *pinfo = NULL;

	pinfo = get_panel_info(get_fb_index(dev));
	if (!pinfo) {
		LCD_ERR("pinfo is null\n");
		return -EINVAL;
	}
	if (pinfo->panel_name)
		ret = snprintf(buf, PAGE_SIZE, "%s\n", pinfo->panel_name);
	return ret;
}

static ssize_t lcd_type_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	ssize_t ret = 0;

	return ret;
}

static ssize_t panel_info_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	ssize_t ret;
	char panel_type[10] = {0};
	struct panel_info *pinfo = NULL;
	struct dsi_panel *panel;

	pinfo = get_panel_info(get_fb_index(dev));
	if (!pinfo || !pinfo->panel) {
		LCD_ERR("null pointer\n");
		return -EINVAL;
	}
	panel = pinfo->panel;
	if (panel->panel_type == DSI_DISPLAY_PANEL_TYPE_LCD)
		strncpy(panel_type, "LCD", strlen("LCD"));
	else if (panel->panel_type == DSI_DISPLAY_PANEL_TYPE_OLED)
		strncpy(panel_type, "AMOLED", strlen("AMOLED"));
	else
		strncpy(panel_type, "INVALID", strlen("INVALID"));
	ret = snprintf(buf, PAGE_SIZE, "blmax:%u,blmin:%u,blmax_nit_actual:%d,blmax_nit_standard:%d,lcdtype:%s\n",
		panel->bl_config.bl_max_level, panel->bl_config.bl_min_level,
		panel->bl_config.bl_actual_max_nit, panel->bl_config.bl_max_nit,
		panel_type);
	return ret;
}

static ssize_t	lcd_voltage_enable_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	return count;
}

static ssize_t amoled_acl_ctrl_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	ssize_t ret = 0;

	return ret;
}

static ssize_t	amoled_acl_ctrl_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	return count;
}

static ssize_t amoled_vr_mode_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	ssize_t ret = 0;

	return ret;
}

static ssize_t	amoled_vr_mode_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	return count;
}

static ssize_t lcd_support_mode_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	ssize_t ret = 0;

	return ret;
}

static ssize_t	lcd_support_mode_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	return count;
}

static ssize_t	gamma_dynamic_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	return count;
}

static ssize_t frame_count_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	ssize_t ret = 0;

	return ret;
}

static ssize_t frame_update_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	ssize_t ret = 0;

	return ret;
}

static ssize_t	frame_update_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	return count;
}

static ssize_t lcd_fps_scence_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int ret;
	char str[BUF_MAX] = {0};
	char tmp[10] = {0};
	int i;
	u32 fps;
	struct panel_info *pinfo = NULL;
	struct dsi_panel *panel = NULL;

	pinfo = get_panel_info(get_fb_index(dev));
	if (!pinfo || !pinfo->panel) {
		LCD_ERR("pinfo or panel is null\n");
		return -EINVAL;
	}
	panel = pinfo->panel;
	if (pinfo->fps_list_len && pinfo->fps_list) {
		if (!panel->cur_mode)
			ret = snprintf(str, sizeof(str), "current_fps:%d;default_fps:%d",
				pinfo->fps_list[0], pinfo->fps_list[0]);
		else
			ret = snprintf(str, sizeof(str), "current_fps:%d;default_fps:%d",
				panel->cur_mode->timing.refresh_rate, pinfo->fps_list[0]);
		strncat(str, ";support_fps_list:", strlen(";support_fps_list:"));
		for (i = 0; i < pinfo->fps_list_len; i++) {
			fps = pinfo->fps_list[i];
			if (i > 0)
				strncat(str, ",", strlen(","));
			ret += snprintf(tmp, sizeof(tmp), "%d", fps);
			strncat(str, tmp, strlen(tmp));
		}
		ret = snprintf(buf, PAGE_SIZE, "%s\n", str);
	} else {
		fps = 60;
		ret = snprintf(buf, sizeof(str), "current_fps:%d;default_fps:%d;support_fps_list:%d",
			fps, fps, fps);
	}
	LCD_INFO("%s\n", str);
	return ret;
}

static ssize_t	lcd_fps_scence_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	return count;
}

static ssize_t alpm_setting_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	ssize_t ret = 0;

	return ret;
}

static ssize_t	alpm_setting_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	ssize_t ret;
	unsigned int mode = 0;
	struct panel_info *pinfo = NULL;
	struct dsi_panel *panel = NULL;

	pinfo = get_panel_info(get_fb_index(dev));
	if (!pinfo) {
		LCD_ERR("pinfo is null\n");
		return -EINVAL;
	}

	panel = pinfo->panel;
	if (!panel) {
		LCD_ERR("panel is null\n");
		return -EINVAL;
	}

	ret = sscanf(buf, "%u", &mode);
	if (!ret) {
		LCD_ERR("sscanf return invaild:%d\n", ret);
		return -EINVAL;
	}
	LCD_INFO("alpm mode:%d\n", mode);

	if (panel->pdata->pinfo->panel_state == LCD_POWER_ON ||
		panel->pdata->pinfo->panel_state == LCD_POWER_OFF) {
        LCD_WARN("not in alpm mode:%d\n", panel->power_mode);
    	return count;
   }
	mutex_lock(&panel->panel_lock);
	switch (mode) {
	case ALPM_LOW_LIGHT:
		dsi_panel_set_cmd(panel, DSI_CMD_SET_ALPM_LOW_LIGHT);
		break;
	case ALPM_MIDDLE_LIGHT:
		dsi_panel_set_cmd(panel, DSI_CMD_SET_ALPM_MIDDLE_LIGHT);
		break;
	case ALPM_HIGH_LIGHT:
		dsi_panel_set_cmd(panel, DSI_CMD_SET_ALPM_HIGH_LIGHT);
		break;
	default:
		LCD_ERR("not support alpm mode:%d\n", mode);
		break;
	}
	mutex_unlock(&panel->panel_lock);
	return count;
}

static ssize_t lcd_func_switch_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	ssize_t ret = 0;

	return ret;
}

static ssize_t	lcd_func_switch_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	return count;
}

static ssize_t lcd_reg_read_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	ssize_t ret = 0;

	return ret;
}

static ssize_t	lcd_reg_read_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	return count;
}

static ssize_t ddic_oem_info_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int i;
	int ret;
	int length;
	char oem_info_data[OEM_INFO_SIZE_MAX] = {0};
	char str_oem[OEM_INFO_SIZE_MAX + 1] = {0};
	char str_tmp[OEM_INFO_SIZE_MAX + 1] = {0};
	struct panel_info *pinfo = NULL;

	pinfo = get_panel_info(get_fb_index(dev));
	if (!pinfo) {
		LCD_ERR("pinfo is null\n");
		return -EINVAL;
	}
	if (pinfo->panel_state != LCD_HS_ON) {
		LCD_ERR("panel is power off!\n");
		return -EINVAL;
	}

	if (!pinfo->oeminfo.support) {
		LCD_ERR("oem info is not support\n");
		return LCD_FAIL;
	}

	if (oem_info_type <= INVALID_TYPE || oem_info_type >= MAX_OEM_TYPE) {
		LCD_ERR("oem_info_type is invalid, write ddic_oem_info again, then read\n");
		return LCD_FAIL;
	}

	length = sizeof(oem_read_cmds) / sizeof(oem_read_cmds[0]);
	for (i = 0; i < length; i++) {
		if (oem_info_type == oem_read_cmds[i].type) {
			LCD_INFO("cmd = %d\n", oem_info_type);
			if (oem_read_cmds[i].func)
				(*oem_read_cmds[i].func)(pinfo, oem_info_data, OEM_INFO_SIZE_MAX);
		}
	}

	/* parse data */
	memset(str_oem, 0, sizeof(str_oem));
	for (i = 0; i < oem_info_data[1]; i++) {
		memset(str_tmp, 0, sizeof(str_tmp));
		snprintf(str_tmp, sizeof(str_tmp),
				"%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,",
				oem_info_data[0 + i * BARCODE_BLOCK_LEN],
				oem_info_data[1 + i * BARCODE_BLOCK_LEN],
				oem_info_data[2 + i * BARCODE_BLOCK_LEN],
				oem_info_data[3 + i * BARCODE_BLOCK_LEN],
				oem_info_data[4 + i * BARCODE_BLOCK_LEN],
				oem_info_data[5 + i * BARCODE_BLOCK_LEN],
				oem_info_data[6 + i * BARCODE_BLOCK_LEN],
				oem_info_data[7 + i * BARCODE_BLOCK_LEN],
				oem_info_data[8 + i * BARCODE_BLOCK_LEN],
				oem_info_data[9 + i * BARCODE_BLOCK_LEN],
				oem_info_data[10 + i * BARCODE_BLOCK_LEN],
				oem_info_data[11 + i * BARCODE_BLOCK_LEN],
				oem_info_data[12 + i * BARCODE_BLOCK_LEN],
				oem_info_data[13 + i * BARCODE_BLOCK_LEN],
				oem_info_data[14 + i * BARCODE_BLOCK_LEN],
				oem_info_data[15 + i * BARCODE_BLOCK_LEN]);

		strncat(str_oem, str_tmp, strlen(str_tmp));
	}

	LCD_INFO("str_oem = %s\n", str_oem);
	ret = snprintf(buf, PAGE_SIZE, "%s\n", str_oem);
	return ret;
}

static ssize_t	ddic_oem_info_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct panel_info *pinfo = get_panel_info(get_fb_index(dev));

	if (!buf || !pinfo) {
		LCD_ERR("pointer is NULL\n");
		return -EINVAL;
	}

	if (pinfo->panel_state != LCD_HS_ON) {
		LCD_ERR("panel is power off!\n");
		return LCD_FAIL;
	}

	if (!pinfo->oeminfo.support) {
		LCD_ERR("oem info is not support\n");
		return LCD_FAIL;
	}

	oem_info_type = (unsigned char)strtoul(buf, NULL, 0);
	LCD_INFO("write Type = %d\n", oem_info_type);
	return count;
}

static ssize_t effect_bl_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	ssize_t ret = 0;

	return ret;
}

static ssize_t	effect_bl_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	return count;
}

static int lcd_fps_get_scence(int fps)
{
	int ret_fps;

	switch (fps) {
	case FPS_60_SCENCE:
		ret_fps = FPS_60_INDEX;
		break;
	case FPS_90_SCENCE:
		ret_fps = FPS_90_INDEX;
		break;
	case FPS_120_SCENCE:
		ret_fps = FPS_120_INDEX;
		break;
	default:
		ret_fps = FPS_60_INDEX;
		break;
	}
	return ret_fps;
}

static ssize_t lcd_fps_order_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int ret;
	char str[BUF_MAX] = {0};
	char tmp[10] = {0};
	int i;
	int fps_index;
	struct panel_info *pinfo = NULL;

	pinfo = get_panel_info(get_fb_index(dev));
	if (!pinfo || !pinfo->panel) {
		LCD_ERR("pinfo or panel is null\n");
		return -EINVAL;
	}
	if (pinfo->fps_list_len < 1 || !pinfo->fps_list) {
		ret = snprintf(buf, PAGE_SIZE, "0\n");
		return ret;
	} else {
		ret = snprintf(str, sizeof(str), "1,%d,%d", 0,
			pinfo->fps_list_len);
		for (i = 0; i < pinfo->fps_list_len; i++) {
			fps_index = lcd_fps_get_scence(pinfo->fps_list[i]);
			if (i == 0)
				strncat(str, ",", strlen(","));
			else
				strncat(str, ";", strlen(";"));
			ret += snprintf(tmp, sizeof(tmp), "%d:%d", fps_index, ORDER_DELAY);
			strncat(str, tmp, strlen(tmp));
		}
	}
	LCD_INFO("%s\n", str);
	ret = snprintf(buf, PAGE_SIZE, "%s\n", str);
	return ret;
}

static ssize_t panel_sncode_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int i;
	ssize_t ret;
	char str_tmp[OEM_INFO_SIZE_MAX] = {0};
	char str_oem[OEM_INFO_SIZE_MAX] = {0};
	struct panel_info *pinfo = NULL;

	pinfo = get_panel_info(get_fb_index(dev));
	if (!pinfo || !buf) {
		LCD_ERR("point is null\n");
		return -EINVAL;
	}

	if (!pinfo->oeminfo.sn_data.support) {
		LCD_ERR("not support sn data\n");
		return LCD_FAIL;
	}
	for(i = 0; i < sizeof(pinfo->oeminfo.sn_data.sn_code); i++) {
		memset(str_tmp, 0, sizeof(str_tmp));
		ret = snprintf(str_tmp, sizeof(str_tmp), "%d,",
				pinfo->oeminfo.sn_data.sn_code[i]);
		if (ret < 0) {
			LCD_ERR("snprintf fail\n");
			return LCD_FAIL;
		}
		strncat(str_oem, str_tmp, strlen(str_tmp));
	}
	ret = snprintf(buf, PAGE_SIZE, "%s\n", str_oem);
	if (ret < 0) {
		LCD_ERR("snprintf fail\n");
		return LCD_FAIL;
	}
	LCD_INFO("%s\n", buf);
	return ret;
}

static DEVICE_ATTR(lcd_model, 0644, lcd_model_show, NULL);
static DEVICE_ATTR(lcd_display_type, 0644, lcd_type_show, NULL);
static DEVICE_ATTR(panel_info, 0644, panel_info_show, NULL);
static DEVICE_ATTR(lcd_voltage_enable, 0200, NULL,
	lcd_voltage_enable_store);
static DEVICE_ATTR(amoled_acl, 0644, amoled_acl_ctrl_show,
	amoled_acl_ctrl_store);
static DEVICE_ATTR(amoled_vr_mode, 0644, amoled_vr_mode_show,
	amoled_vr_mode_store);
static DEVICE_ATTR(lcd_support_mode, 0644, lcd_support_mode_show,
	lcd_support_mode_store);
static DEVICE_ATTR(gamma_dynamic, 0644, NULL, gamma_dynamic_store);
static DEVICE_ATTR(frame_count, 0444, frame_count_show, NULL);
static DEVICE_ATTR(frame_update, 0644, frame_update_show,
	frame_update_store);
static DEVICE_ATTR(lcd_fps_scence, 0644, lcd_fps_scence_show,
	lcd_fps_scence_store);
static DEVICE_ATTR(alpm_setting, 0644, alpm_setting_show,
	alpm_setting_store);
static DEVICE_ATTR(lcd_func_switch, 0644, lcd_func_switch_show,
	lcd_func_switch_store);
static DEVICE_ATTR(lcd_reg_read, 0600, lcd_reg_read_show,
	lcd_reg_read_store);
static DEVICE_ATTR(ddic_oem_info, 0644, ddic_oem_info_show,
	ddic_oem_info_store);
static DEVICE_ATTR(effect_bl, 0644, effect_bl_show,
	effect_bl_store);
static DEVICE_ATTR(lcd_fps_order, 0644, lcd_fps_order_show,
	NULL);
static DEVICE_ATTR(panel_sncode, 0644, panel_sncode_show, NULL);

struct attribute *lcd_sysfs_attrs[] = {
	&dev_attr_lcd_model.attr,
	&dev_attr_lcd_display_type.attr,
	&dev_attr_panel_info.attr,
	&dev_attr_lcd_voltage_enable.attr,
	&dev_attr_amoled_acl.attr,
	&dev_attr_amoled_vr_mode.attr,
	&dev_attr_lcd_support_mode.attr,
	&dev_attr_gamma_dynamic.attr,
	&dev_attr_frame_count.attr,
	&dev_attr_frame_update.attr,
	&dev_attr_lcd_fps_scence.attr,
	&dev_attr_alpm_setting.attr,
	&dev_attr_lcd_func_switch.attr,
	&dev_attr_lcd_reg_read.attr,
	&dev_attr_ddic_oem_info.attr,
	&dev_attr_effect_bl.attr,
	&dev_attr_lcd_fps_order.attr,
	&dev_attr_panel_sncode.attr,
	NULL,
};

struct attribute_group lcd_sysfs_attr_group = {
	.attrs = lcd_sysfs_attrs,
};

int lcd_create_sysfs(struct kobject *obj)
{
	int rc;

	rc = sysfs_create_group(obj, &lcd_sysfs_attr_group);
	if (rc) {
		LCD_ERR("sysfs group creation failed, rc=%d\n", rc);
		return rc;
	}
#ifdef CONFIG_LCD_FACTORY
	rc = lcd_create_fact_sysfs(obj);
	if (rc) {
		LCD_ERR("factory sysfs group creation failed, rc=%d\n", rc);
		return rc;
	}
#endif
	return rc;
}
