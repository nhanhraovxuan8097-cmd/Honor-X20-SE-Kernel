/*
 * lcd_panel.h
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
#ifndef LCD_PANEL_H
#define LCD_PANEL_H
#ifdef CONFIG_LCD_FACTORY
#include "lcd_factory.h"
#endif

#include "lcd_sysfs.h"

#define HBM_EXIT	0
#define HBM_ENTER	1

/* LCD init step */
enum panel_init_step {
    PANEL_INIT_NONE = 0,
    PANEL_INIT_POWER_ON,
    PANEL_INIT_SEND_SEQUENCE,
    PANEL_INIT_MIPI_LP_SEND_SEQUENCE,
    PANEL_INIT_MIPI_HS_SEND_SEQUENCE,
};

/* LCD uninit step */
enum panel_uninit_step {
    PANEL_UNINIT_NONE = 0,
    PANEL_UNINIT_POWER_OFF,
    PANEL_UNINIT_SEND_SEQUENCE,
    PANEL_UNINIT_MIPI_LP_SEND_SEQUENCE,
    PANEL_UNINIT_MIPI_HS_SEND_SEQUENCE,
};

struct hbm_desc {
	bool enabled;
	int mode;
};

struct lcd_kit_2d_barcode {
	u32 support;
	u32 offset;
};

struct lcd_kit_sn_data {
	u32 support;
	unsigned char sn_code[LCD_SN_CODE_LENGTH];
};

struct lcd_kit_oem_info {
	u32 support;
	struct lcd_kit_2d_barcode barcode_2d;
	struct lcd_kit_sn_data sn_data;
};

struct panel_info {
	const char *panel_name;
	int panel_state;
#ifdef CONFIG_LCD_FACTORY
	struct lcd_fact_info *fact_info;
#endif
	struct hbm_desc hbm;
	struct delayed_work read_sn_delayed_work;
	struct lcd_kit_oem_info oeminfo;
	struct dsi_panel *panel;
	bool local_hbm_enabled;
	u32 *fps_list;
	u32 fps_list_len;
};

struct panel_data {
	struct panel_info *pinfo;
	int (*create_sysfs)(struct kobject *obj);
	int (*panel_init)(struct dsi_panel *panel);
	int (*panel_hbm_set)(struct dsi_panel *panel,
		struct display_engine_ddic_hbm_param *hbm_cfg);
	int (*panel_hbm_fp_set)(struct dsi_panel *panel, int mode);
	int (*on)(struct dsi_panel *panel, int step);
	int (*off)(struct dsi_panel *panel, int step);
	int (*print_bkl)(struct dsi_panel *panel, u32 bl_lvl);
};

extern struct panel_data g_panel_data;

/* oem info cmd */
struct oem_info_cmd {
	unsigned char type;
	int (*func)(struct panel_info *pinfo, char *oem_data, int len);
};
#endif
