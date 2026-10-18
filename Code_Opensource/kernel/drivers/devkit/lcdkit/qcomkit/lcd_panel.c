/*
 * lcd_panel.c
 *
 * lcd panel function for lcd driver
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
#include "lcd_defs.h"
#include "lcd_sysfs.h"
#include "lcd_kit_core.h"
#ifdef CONFIG_APP_INFO
#include <misc/app_info.h>
#endif
#include <huawei_platform/log/log_jank.h>
#include "lcd_panel.h"

/* panel information */
static struct panel_info *g_panel_info[FB_MAX];
/* current registered fb num */
static int registered_fb_num;
/* notify fp hbm completed */
extern void ud_fp_on_hbm_completed(void);

struct panel_info *get_panel_info(int idx)
{
	if (idx >= FB_MAX) {
		LCD_ERR("idx exceed max fb number\n");
		return NULL;
	}
	return g_panel_info[idx];
}

static void lcd_get_sn_code(struct work_struct *work)
{
	static int read_count = 0;
	char read_value[OEM_INFO_SIZE_MAX] = {0};
	struct panel_info *pinfo = NULL;

	pinfo = container_of(work, struct panel_info, read_sn_delayed_work.work);
	if (!pinfo) {
		LCD_ERR("pinfo is null\n");
		return;
	}
	if (dsi_panel_receive_data(pinfo->panel,
			DSI_CMD_READ_OEMINFO, read_value, OEM_INFO_SIZE_MAX - 1) < 0 &&
			read_count < READ_SN_MAX_COUNT) {
		read_count++;
		schedule_delayed_work(&pinfo->read_sn_delayed_work,
				msecs_to_jiffies(DELAY_READ_SN));
		LCD_INFO("%d read sn work\n", read_count);
		return;
	}
	if (read_count < READ_SN_MAX_COUNT)
		memcpy(pinfo->oeminfo.sn_data.sn_code, read_value, LCD_SN_CODE_LENGTH);
	LCD_INFO("sn: %s\n", pinfo->oeminfo.sn_data.sn_code);
	cancel_delayed_work(&pinfo->read_sn_delayed_work);
}

static void panel_parse_oem_info(struct dsi_panel *panel, struct panel_info *pinfo)
{
	struct dsi_parser_utils *utils = &panel->utils;

	/* parse oeminfo */
	pinfo->oeminfo.support = utils->read_bool(utils->data,
			"qcom,mdss-dsi-panel-oem-info-enabled");
	if (!pinfo->oeminfo.support) {
		LCD_INFO("not support oem info\n");
		return;
	}
	pinfo->oeminfo.barcode_2d.support = utils->read_bool(utils->data,
			"qcom,mdss-dsi-panel-oem-2d-barcode-enabled");
	if (pinfo->oeminfo.barcode_2d.support) {
		if (utils->read_u32(utils->data, "qcom,mdss-dsi-panel-oem-2d-barcode-offset",
				&pinfo->oeminfo.barcode_2d.offset))
			DSI_ERR("failed to read: qcom,mdss-dsi-panel-oem-2d-barcode-offset\n");
	}
	pinfo->oeminfo.sn_data.support = utils->read_bool(utils->data,
			"qcom,mdss-dsi-panel-oem-sn-enabled");
	if (pinfo->oeminfo.sn_data.support){
		INIT_DELAYED_WORK(&pinfo->read_sn_delayed_work,
				lcd_get_sn_code);
		if (!schedule_delayed_work(&pinfo->read_sn_delayed_work,
					msecs_to_jiffies(WORK_DELAY_TIME_READ_SN)))
			LCD_ERR("read sn work fail\n");
	}
}

static void panel_parse_fps(struct panel_info *pinfo,
	struct dsi_parser_utils *utils)
{
	int rc;

	pinfo->fps_list_len = utils->count_u32_elems(utils->data,
				  "qcom,dsi-fps-list");
	if (pinfo->fps_list_len < 1) {
		LCD_ERR("fps list not present\n");
		return;
	}

	pinfo->fps_list = kcalloc(pinfo->fps_list_len, sizeof(u32),
			GFP_KERNEL);
	if (!pinfo->fps_list)
		return;

	rc = utils->read_u32_array(utils->data,
			"qcom,dsi-fps-list",
			pinfo->fps_list,
			pinfo->fps_list_len);
	if (rc) {
		LCD_ERR("fps rate list parse failed\n");
		kfree(pinfo->fps_list);
		pinfo->fps_list = NULL;
	}
}

static int panel_parse(struct dsi_panel *panel, struct panel_info *pinfo)
{
	int rc = 0;
	struct dsi_parser_utils *utils = &panel->utils;

	if (!utils) {
		LCD_ERR("utils is null\n");
		return -1;
	}
	/* parse dt */
	pinfo->hbm.enabled = utils->read_bool(utils->data,
		"qcom,mdss-dsi-panel-hbm-enabled");
        pinfo->local_hbm_enabled = utils->read_bool(utils->data,
                "qcom,mdss-dsi-panel-local-hbm-enabled");
        rc = utils->read_u32(utils->data, "qcom,mdss-dsi-bl-max-nit",
		&panel->bl_config.bl_max_nit);
	if (rc)
		DSI_ERR("failed to read: qcom,mdss-dsi-bl-max-nit, rc=%d\n", rc);
	panel_parse_oem_info(panel, pinfo);
	panel_parse_fps(pinfo, utils);
	return 0;
}

static int panel_init(struct dsi_panel *panel)
{
	int ret;
	struct panel_info *pinfo = NULL;

	if (!panel || !panel->name || !panel->pdata) {
		LCD_ERR("null pointer\n");
		return -1;
	}
	pinfo = kzalloc(sizeof(struct panel_info), GFP_KERNEL);
	if (!pinfo) {
		LCD_ERR("kzalloc pinfo fail\n");
		return -1;
	}
	pinfo->panel_name = panel->name;
	pinfo->panel_state = LCD_POWER_ON;
	/* panel dtsi parse */
	ret = panel_parse(panel, pinfo);
	if (ret)
		LCD_ERR("panel parse failed\n");
#ifdef CONFIG_LCD_FACTORY
	ret = factory_init(panel, pinfo);
	if (ret)
		LCD_ERR("factory init failed\n");
#endif
#ifdef CONFIG_APP_INFO
	/* set app_info */
	ret = app_info_set("lcd type", panel->name);
    if (ret)
            LCD_ERR("set app info failed\n");
#endif
	panel->pdata->pinfo = pinfo;
	if (registered_fb_num >= FB_MAX) {
		kfree(pinfo);
		LCD_ERR("exceed max fb number\n");
		return -1;
	}
	pinfo->panel = panel;
	g_panel_info[registered_fb_num++] = pinfo;
	return 0;
}

static int panel_hbm_mmi_set(struct dsi_panel *panel, int level)
{
	struct hbm_desc *hbm = NULL;

	hbm = &(panel->pdata->pinfo->hbm);
	if (!hbm->enabled)
		return 0;
	if (level == 0) {
		dsi_panel_set_hbm_level(panel, panel->bl_config.bl_level);
		hbm->mode = HBM_EXIT;
		return 0;
	}
	dsi_panel_set_hbm_level(panel, level);
	hbm->mode = HBM_ENTER;
	return 0;
}

static int panel_hbm_set_handle(struct dsi_panel *panel, int dimming,
	int level)
{
	int rc;
	static int last_level = 0;
	struct hbm_desc *hbm = NULL;

	hbm = &(panel->pdata->pinfo->hbm);
	if (!hbm->enabled)
		return 0;
	if ((level < 0) || (level > HBM_SET_MAX_LEVEL)) {
		LCD_ERR("input param invalid, hbm_level %d!\n", level);
		return -1;
	}
	if (level > 0) {
		if (last_level == 0) {
			/* enable hbm */
			rc = dsi_panel_set_cmd(panel, DSI_CMD_SET_HBM_ENABLE);
			if (rc)
				LCD_ERR("set hbm enable failed\n");
			if (!dimming) {
				rc = dsi_panel_set_cmd(panel, DSI_CMD_SET_HBM_DIMM_OFF);
				if (rc)
					LCD_ERR("set hbm dimming off failed\n");
			}
			hbm->mode = HBM_ENTER;
		}
	} else {
		if (last_level == 0) {
			/* disable dimming */
			rc = dsi_panel_set_cmd(panel, DSI_CMD_SET_HBM_DIMM_OFF);
			if (rc)
				LCD_ERR("set hbm dimming off failed\n");
		} else {
			/* exit hbm */
			if (dimming) {
				rc = dsi_panel_set_cmd(panel, DSI_CMD_SET_HBM_DIMM_ON);
				if (rc)
					LCD_ERR("set hbm dimming on failed\n");
			} else {
				rc = dsi_panel_set_cmd(panel, DSI_CMD_SET_HBM_DIMM_OFF);
				if (rc)
					LCD_ERR("set hbm dimming off failed\n");
			}
			rc = dsi_panel_set_cmd(panel, DSI_CMD_SET_HBM_DISABLE);
			if (rc)
				LCD_ERR("set hbm disable failed\n");
			hbm->mode = HBM_EXIT;
		}
	}
	/* set hbm level */
	rc = dsi_panel_set_hbm_level(panel, level);
	if (rc)
		LCD_ERR("set backlight failed\n");
	last_level = level;
	return 0;
}

static int panel_local_hbm_mmi_set(struct dsi_panel *panel, int level)
{
	LCD_INFO("local_hbm_enabled:%d\n",panel->pdata->pinfo->local_hbm_enabled);
	if (!panel->pdata->pinfo->local_hbm_enabled)
		return -1;

	if (panel->power_mode == SDE_MODE_DPMS_LP1 ||
		panel->power_mode == SDE_MODE_DPMS_LP2) {
		LCD_INFO("current mode is aod and lhbm return\n");
		return -1;
	}

	if (level == 0) {
		dsi_panel_set_cmd(panel, DSI_CMD_SET_LHBM_DISABLE);
		return 0;
	}
	dsi_panel_set_cmd(panel, DSI_CMD_SET_LHBM_ENABLE);
	return 0;
}

static int panel_local_hbm_identify_set(struct dsi_panel *panel, int level)
{
	LCD_INFO("local_hbm_enabled:%d\n",panel->pdata->pinfo->local_hbm_enabled);
	if (!panel->pdata->pinfo->local_hbm_enabled)
		return -1;

	if (panel->power_mode == SDE_MODE_DPMS_LP1 ||
		panel->power_mode == SDE_MODE_DPMS_LP2) {
		LCD_INFO("current mode is aod and lhbm return\n");
		return -1;
	}

	if (level == 0) {
		dsi_panel_set_cmd(panel, DSI_CMD_SET_LHBM_DISABLE);
		return 0;
	}
	dsi_panel_set_cmd(panel, DSI_CMD_SET_LHBM_ENABLE);
	ud_fp_on_hbm_completed();
	return 0;
}

static int panel_hbm_set(struct dsi_panel *panel,
	struct display_engine_ddic_hbm_param *hbm_cfg)
{
	int rc;

	if (!panel || !panel->pdata || !panel->pdata->pinfo) {
		LCD_ERR("panel have null pointer\n");
		return -EINVAL;
	}
	if (!hbm_cfg) {
		LCD_ERR("hbm_cfg is null\n");
		return -EINVAL;
	}
	LCD_INFO("hbm tpye:%d, level:%d, dimming:%d\n", hbm_cfg->type,
		hbm_cfg->level, hbm_cfg->dimming);
	mutex_lock(&panel->panel_lock);
	switch (hbm_cfg->type) {
	case HBM_FOR_MMI:
		rc = panel_hbm_mmi_set(panel, hbm_cfg->level);
		break;
	case HBM_FOR_LIGHT:
		rc = panel_hbm_set_handle(panel, hbm_cfg->level, hbm_cfg->dimming);
		break;
	case LOCAL_HBM_FOR_MMI:
		rc = panel_local_hbm_mmi_set(panel, hbm_cfg->level);
		break;
	case LOCAL_HBM_FOR_IDENTIFY:
		rc = panel_local_hbm_identify_set(panel, hbm_cfg->level);
		break;
	default:
		LCD_ERR("not support type:%d\n", hbm_cfg->type);
		rc = -1;
	}
	mutex_unlock(&panel->panel_lock);
	return rc;
}

static int panel_on(struct dsi_panel *panel, int step)
{
	int rc = 0;
	struct ts_kit_ops *ts_ops = ts_kit_get_ops();

	if (!panel || !panel->pdata || !panel->pdata->pinfo) {
		LCD_ERR("pointer is null\n");
		return -1;
	}
	switch (step) {
	case PANEL_INIT_NONE:
		LCD_INFO("panel init none step\n");
		LOG_JANK_D(JLID_KERNEL_LCD_POWER_ON, "%s", "LCD_POWER_ON");
		break;
	case PANEL_INIT_POWER_ON:
		LCD_INFO("panel init power on step\n");
		panel->pdata->pinfo->panel_state = LCD_POWER_ON;
		if (ts_ops && ts_ops->ts_power_notify)
			ts_ops->ts_power_notify(TS_RESUME_DEVICE, NO_SYNC);
		break;
	case PANEL_INIT_MIPI_LP_SEND_SEQUENCE:
		LCD_INFO("panel init mipi lp step\n");
		panel->pdata->pinfo->panel_state = LCD_LP_ON;
		if (ts_ops && ts_ops->ts_power_notify)
			ts_ops->ts_power_notify(TS_AFTER_RESUME, NO_SYNC);
		break;
	case PANEL_INIT_MIPI_HS_SEND_SEQUENCE:
		LCD_INFO("panel init mipi hs step\n");
		panel->pdata->pinfo->panel_state = LCD_HS_ON;
		break;
	default:
		LCD_ERR("not support step:%d\n", step);
		rc = -1;
		break;
	}
	return rc;
}

static int panel_off(struct dsi_panel *panel, int step)
{
	int rc = 0;
	struct ts_kit_ops *ts_ops = ts_kit_get_ops();

	if (!panel || !panel->pdata || !panel->pdata->pinfo) {
		LCD_ERR("pointer is null\n");
		return -1;
	}
	switch (step) {
	case PANEL_UNINIT_NONE:
		LCD_INFO("panel uninit none step\n");
		break;
	case PANEL_UNINIT_MIPI_HS_SEND_SEQUENCE:
		LCD_INFO("panel uninit mipi hs step\n");
		if (ts_ops && ts_ops->ts_power_notify)
			ts_ops->ts_power_notify(TS_EARLY_SUSPEND, NO_SYNC);
		break;
	case PANEL_UNINIT_MIPI_LP_SEND_SEQUENCE:
		LCD_INFO("panel uninit mipi lp step\n");
		break;
	case PANEL_UNINIT_POWER_OFF:
		panel->pdata->pinfo->panel_state = LCD_POWER_OFF;
		panel->pdata->pinfo->hbm.mode = HBM_EXIT;
		LCD_INFO("panel uninit power off step\n");
		if (ts_ops && ts_ops->ts_power_notify) {
			ts_ops->ts_power_notify(TS_BEFORE_SUSPEND, NO_SYNC);
			ts_ops->ts_power_notify(TS_SUSPEND_DEVICE, NO_SYNC);
		}
		LOG_JANK_D(JLID_KERNEL_LCD_POWER_OFF, "%s", "LCD_POWER_OFF");
		break;
	default:
		LCD_ERR("not support step:%d\n", step);
		rc = -1;
		break;
	}
	return rc;
}

static int panel_hbm_fp_set(struct dsi_panel *panel, int mode)
{
	int bl_level;

	if (!panel->pdata->pinfo) {
		LCD_ERR("pinfo is null\n");
		return -1;
	}
	if (mode == HBM_MODE_ON) {
		dsi_panel_set_cmd(panel, DSI_CMD_SET_FP_HBM_ENABLE);
		panel->pdata->pinfo->hbm.mode = HBM_ENTER;
		ud_fp_on_hbm_completed();
	} else {
		dsi_panel_set_cmd(panel, DSI_CMD_SET_FP_HBM_DISABLE);
		panel->pdata->pinfo->hbm.mode = HBM_EXIT;
		bl_level = panel->bl_config.bl_level;
		dsi_panel_set_hbm_level(panel, bl_level);
	}
	return 0;
}

static int print_backlight(struct dsi_panel *panel, u32 bl_lvl)
{
	static int last_level = 0;

	if (last_level == 0 && bl_lvl != 0) {
		LCD_INFO("screen on, backlight level = %d\n", bl_lvl);
		LOG_JANK_D(JLID_KERNEL_LCD_BACKLIGHT_ON, "LCD_BACKLIGHT_ON,%u", bl_lvl);
	} else if (last_level !=0 && bl_lvl == 0) {
		LCD_INFO("screen off, backlight level = %d\n", bl_lvl);
		LOG_JANK_D(JLID_KERNEL_LCD_BACKLIGHT_OFF, "LCD_BACKLIGHT_OFF");
	}
	LCD_INFO("backlight level = %d\n", bl_lvl);
	last_level = bl_lvl;
	return 0;
}

struct panel_data g_panel_data = {
	.panel_init = panel_init,
	.panel_hbm_set = panel_hbm_set,
	.panel_hbm_fp_set = panel_hbm_fp_set,
	.create_sysfs = lcd_create_sysfs,
	.on = panel_on,
	.off = panel_off,
	.print_bkl = print_backlight,
};
