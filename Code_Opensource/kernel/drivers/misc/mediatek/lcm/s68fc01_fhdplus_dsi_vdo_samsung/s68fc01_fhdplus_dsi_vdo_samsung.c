/*
 * Copyright (C) 2015 MediaTek Inc.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#define LOG_TAG "LCM"

#ifndef BUILD_LK
#  include <linux/string.h>
#  include <linux/kernel.h>
#endif
#include "lcm_drv.h"

#ifdef BUILD_LK
#  include <platform/upmu_common.h>
#  include <platform/mt_gpio.h>
#  include <platform/mt_i2c.h>
#  include <platform/mt_pmic.h>
#  include <string.h>
#elif defined(BUILD_UBOOT)
#  include <asm/arch/mt_gpio.h>
#endif

#ifdef BUILD_LK
#  define LCM_LOGI(string, args...)  dprintf(0, "[LK/"LOG_TAG"]"string, ##args)
#  define LCM_LOGD(string, args...)  dprintf(1, "[LK/"LOG_TAG"]"string, ##args)
#else
#  define LCM_LOGI(fmt, args...)  pr_debug("[KERNEL/"LOG_TAG"]"fmt, ##args)
#  define LCM_LOGD(fmt, args...)  pr_debug("[KERNEL/"LOG_TAG"]"fmt, ##args)
#endif

static struct LCM_UTIL_FUNCS lcm_util;

#define SET_RESET_PIN(v)	(lcm_util.set_reset_pin((v)))
#define MDELAY(n)		(lcm_util.mdelay(n))
#define UDELAY(n)		(lcm_util.udelay(n))

#define dsi_set_cmdq_V5(ppara, size, hs) \
		lcm_util.dsi_set_cmdq_V5(ppara, size, hs)
#define dsi_set_cmdq_V2(cmd, count, ppara, force_update) \
		lcm_util.dsi_set_cmdq_V2(cmd, count, ppara, force_update)
#define dsi_set_cmdq_V22(cmdq, cmd, count, ppara, force_update) \
		lcm_util.dsi_set_cmdq_V22(cmdq, cmd, count, ppara, force_update)
#define dsi_set_cmdq(pdata, queue_size, force_update) \
		lcm_util.dsi_set_cmdq(pdata, queue_size, force_update)
#define wrtie_cmd(cmd) lcm_util.dsi_write_cmd(cmd)
#define write_regs(addr, pdata, byte_nums) \
		lcm_util.dsi_write_regs(addr, pdata, byte_nums)
#define read_reg(cmd)	lcm_util.dsi_dcs_read_lcm_reg(cmd)
#define read_reg_v2(cmd, buffer, buffer_size) \
		lcm_util.dsi_dcs_read_lcm_reg_v2(cmd, buffer, buffer_size)

#ifndef BUILD_LK
#  include <linux/kernel.h>
#  include <linux/module.h>
#  include <linux/fs.h>
#  include <linux/slab.h>
#  include <linux/init.h>
#  include <linux/list.h>
#  include <linux/i2c.h>
#  include <linux/irq.h>
#  include <linux/uaccess.h>
#  include <linux/interrupt.h>
#  include <linux/io.h>
#  include <linux/platform_device.h>
#endif
#define FRAME_WIDTH			(1080)
#define FRAME_HEIGHT			(2400)

/* physical size in um */
#define LCM_PHYSICAL_WIDTH		(64500)
#define LCM_PHYSICAL_HEIGHT		(129000)
#define LCM_DENSITY			(480)

#define REGFLAG_DELAY			0xFFFC
#define REGFLAG_UDELAY			0xFFFB
#define REGFLAG_END_OF_TABLE		0xFFFD
#define REGFLAG_RESET_LOW		0xFFFE
#define REGFLAG_RESET_HIGH		0xFFFF
#define REGFLAG_CMD       0xFFFA


#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif
#if defined(CONFIG_RT5081_PMU_DSV) || defined(CONFIG_MT6370_PMU_DSV)
static struct regulator *disp_bias_pos;
static struct regulator *disp_bias_neg;
static int regulator_inited;
#endif
#define CMD_HBM_ENABLE		0xE0
#define CMD_HBM_DISABLE		0x20
static bool hbm_en;
static bool hbm_wait;

struct LCM_setting_table {
	unsigned int cmd;
	unsigned char count;
	unsigned char para_list[64];
};

static struct LCM_setting_table hbm[] = {
		{0x53, 1, {0xe0} }
};


static struct LCM_setting_table lcm_suspend_setting[] = {
	{0x28, 0, {} },
	{REGFLAG_DELAY, 10, {} },
	{0x10, 0, {} },
	{REGFLAG_DELAY, 150, {} },
	{REGFLAG_END_OF_TABLE, 0x00, {} }

};

static struct LCM_setting_table init_setting_vdo[] = {
	{0x9F, 2, {0xA5, 0xA5} },
	{0x11, 0, {} },
	{REGFLAG_DELAY, 20, {} },
	{0x9F, 2, {0x5A, 0x5A} },
	/* TE vsync ON */
	{0x9F, 2, {0xA5, 0xA5} },
	{0x35, 1, {0x00} },
	{0x9F, 2, {0x5A, 0x5A} },
	/* FAIL SAFE Setting */
	{0xFC, 2, {0x5A, 0x5A} },
	{0xED, 12, {0x00, 0x01, 0x00, 0x40, 0x04, 0x08,
		0xA8, 0x84, 0x4A, 0x73, 0x02, 0x0A} },
	{0xFC, 2, {0xA5, 0xA5} },
	/* ELVSS Dim Setting */
	{0xF0, 2, {0x5A, 0x5A} },
	{0xB0, 1, {0x05} },
	{0xB3, 1, {0x87} },
	{0xF0, 2, {0xA5, 0xA5} },
	/* Backlight Dimming Setting */
	{0x53, 1, {0x20} },
	/* ACL off */
	{0x55, 1, {0x00} },
	{REGFLAG_DELAY, 100, {} },
	/* Display On*/
	{0x9F, 2, {0xA5, 0xA5} },
	{0x29, 0, {} },
	{0x9F, 2, {0x5A, 0x5A} },
	{REGFLAG_END_OF_TABLE, 0x00, {} }

};

static struct LCM_setting_table
__maybe_unused lcm_deep_sleep_mode_in_setting[] = {
	{0x28, 0, {0x00} },
	{REGFLAG_DELAY, 50, {} },
	{0x10, 1, {0x00} },
	{REGFLAG_DELAY, 150, {} },
};

static struct LCM_setting_table __maybe_unused lcm_sleep_out_setting[] = {
	{0x11, 1, {0x00} },
	{REGFLAG_DELAY, 120, {} },
	{0x29, 1, {0x00} },
	{REGFLAG_DELAY, 50, {} },
};

static struct LCM_setting_table bl_level[] = {
	{0x51, 2, {0x03, 0xFF} }
};

static void push_table(void *cmdq, struct LCM_setting_table *table,
		       unsigned int count, unsigned char force_update)
{
	unsigned int i;
	unsigned int cmd;

	for (i = 0; i < count; i++) {
		cmd = table[i].cmd;
		switch (cmd) {
		case REGFLAG_DELAY:
			if (table[i].count <= 10)
				MDELAY(table[i].count);
			else
				MDELAY(table[i].count);
			break;
		case REGFLAG_UDELAY:
			UDELAY(table[i].count);
			break;
		case REGFLAG_END_OF_TABLE:
			break;
		default:
			if (cmdq == NULL)
				dsi_set_cmdq_V2(cmd, table[i].count,
					 table[i].para_list, force_update);
			else
				dsi_set_cmdq_V22(cmdq, cmd, table[i].count,
					 table[i].para_list, force_update);
			break;
		}
	}
}

static void lcm_set_util_funcs(const struct LCM_UTIL_FUNCS *util)
{
	memcpy(&lcm_util, util, sizeof(struct LCM_UTIL_FUNCS));
}

static void lcm_get_params(struct LCM_PARAMS *params)
{
	memset(params, 0, sizeof(struct LCM_PARAMS));

	params->type = LCM_TYPE_DSI;

	params->width = FRAME_WIDTH;
	params->height = FRAME_HEIGHT;

	params->dsi.mode = SYNC_PULSE_VDO_MODE;
	params->dsi.switch_mode = CMD_MODE;
	params->dsi.switch_mode_enable = 0;

	/* DSI */
	/* Command mode setting */
	params->dsi.LANE_NUM = LCM_FOUR_LANE;
	/* The following defined the fomat for data coming from LCD engine. */
	params->dsi.data_format.color_order = LCM_COLOR_ORDER_RGB;
	params->dsi.data_format.trans_seq = LCM_DSI_TRANS_SEQ_MSB_FIRST;
	params->dsi.data_format.padding = LCM_DSI_PADDING_ON_LSB;
	params->dsi.data_format.format = LCM_DSI_FORMAT_RGB888;

	/* Highly depends on LCD driver capability. */
	params->dsi.packet_size = 256;
	/* video mode timing */

	params->dsi.PS = LCM_PACKED_PS_24BIT_RGB888;

	params->dsi.vertical_sync_active = 2;
	params->dsi.vertical_backporch = 9;
	params->dsi.vertical_frontporch = 21;
	params->dsi.vertical_active_line = FRAME_HEIGHT;

	params->dsi.horizontal_sync_active = 14;
	params->dsi.horizontal_backporch = 22;
	params->dsi.horizontal_frontporch = 48;
	params->dsi.horizontal_active_pixel = FRAME_WIDTH;
	params->dsi.ssc_disable = 1;
#ifndef CONFIG_FPGA_EARLY_PORTING
	/* this value must be in MTK suggested table */
	params->dsi.PLL_CLOCK = 549;
	/* this value must be in MTK suggested table */
	params->dsi.data_rate  = 1098;
#else
	params->dsi.pll_div1 = 0;
	params->dsi.pll_div2 = 0;
	params->dsi.fbk_div = 0x1;
#endif

	params->dsi.ssc_disable = 1;
	params->dsi.clk_lp_per_line_enable = 0;
	params->dsi.esd_check_enable = 1;
	params->dsi.customization_esd_check_enable = 0;
#if 0
	params->dsi.lcm_esd_check_table[0].cmd = 0x0a;
	params->dsi.lcm_esd_check_table[0].count = 1;
	params->dsi.lcm_esd_check_table[0].para_list[0] = 0x9F;
#endif

#ifdef MTK_ROUND_CORNER_SUPPORT
	params->round_corner_params.round_corner_en = 1;
	params->round_corner_params.full_content = 0;
	params->round_corner_params.w = ROUND_CORNER_W;
	params->round_corner_params.h = ROUND_CORNER_H;
	params->round_corner_params.lt_addr = left_top;
	params->round_corner_params.lb_addr = left_bottom;
	params->round_corner_params.rt_addr = right_top;
	params->round_corner_params.rb_addr = right_bottom;
#endif
	params->hbm_en_time = 2;
	params->hbm_dis_time = 0;
	}

#if defined(CONFIG_RT5081_PMU_DSV) || defined(CONFIG_MT6370_PMU_DSV)
int lcm_bias_regulator_init(void)
{
	int ret = 0;

	if (regulator_inited)
		return ret;

	/* please only get regulator once in a driver */
	disp_bias_pos = regulator_get(NULL, "dsv_pos");
	if (IS_ERR(disp_bias_pos)) { /* handle return value */
		ret = PTR_ERR(disp_bias_pos);
		pr_info("get dsv_pos fail, error: %d\n", ret);
		return ret;
	}

	disp_bias_neg = regulator_get(NULL, "dsv_neg");
	if (IS_ERR(disp_bias_neg)) { /* handle return value */
		ret = PTR_ERR(disp_bias_neg);
		pr_info("get dsv_neg fail, error: %d\n", ret);
		return ret;
	}

	regulator_inited = 1;
	return ret; /* must be 0 */

}

int lcm_bias_enable(void)
{
	int ret = 0;
	int retval = 0;

	lcm_bias_regulator_init();

	/* set voltage with min & max*/
	ret = regulator_set_voltage(disp_bias_pos, 5500000, 5500000);
	if (ret < 0)
		pr_info("set voltage disp_bias_pos fail, ret = %d\n", ret);
	retval |= ret;

	ret = regulator_set_voltage(disp_bias_neg, 5500000, 5500000);
	if (ret < 0)
		pr_info("set voltage disp_bias_neg fail, ret = %d\n", ret);
	retval |= ret;

	/* enable regulator */
	ret = regulator_enable(disp_bias_pos);
	if (ret < 0)
		pr_info("enable regulator disp_bias_pos fail, ret = %d\n",
			ret);
	retval |= ret;

	ret = regulator_enable(disp_bias_neg);
	if (ret < 0)
		pr_info("enable regulator disp_bias_neg fail, ret = %d\n",
			ret);
	retval |= ret;

	return retval;
}


int lcm_bias_disable(void)
{
	int ret = 0;
	int retval = 0;

	lcm_bias_regulator_init();

	ret = regulator_disable(disp_bias_neg);
	if (ret < 0)
		pr_info("disable regulator disp_bias_neg fail, ret = %d\n",
			ret);
	retval |= ret;

	ret = regulator_disable(disp_bias_pos);
	if (ret < 0)
		pr_info("disable regulator disp_bias_pos fail, ret = %d\n",
			ret);
	retval |= ret;

	return retval;
}

#else
int lcm_bias_regulator_init(void)
{
	return 0;
}

int lcm_bias_enable(void)
{
	return 0;
}

int lcm_bias_disable(void)
{
	return 0;
}
#endif
/* turn on gate ic & control voltage to 5.5V */
static void lcm_init_power(void)
{
	lcm_bias_enable();
}

static void lcm_suspend_power(void)
{
	SET_RESET_PIN(0);
	lcm_bias_disable();
}

/* turn on gate ic & control voltage to 5.5V */
static void lcm_resume_power(void)
{
	SET_RESET_PIN(0);
	lcm_init_power();
}

static void lcm_init(void)
{
	SET_RESET_PIN(0);
	MDELAY(15);
	SET_RESET_PIN(1);
	MDELAY(1);
	SET_RESET_PIN(0);
	MDELAY(10);

	SET_RESET_PIN(1);
	MDELAY(10);

	push_table(NULL, init_setting_vdo, ARRAY_SIZE(init_setting_vdo), 1);
	LCM_LOGI("s68fc01_fhdplus----tps6132----lcm mode = vdo mode :%d----\n",
		lcm_dsi_mode);
	hbm_en = false;
}

static void lcm_suspend(void)
{
	push_table(NULL, lcm_suspend_setting,
		   ARRAY_SIZE(lcm_suspend_setting), 1);
	hbm_en = false;
}

static void lcm_resume(void)
{
	lcm_init();
}

static unsigned int lcm_ata_check(unsigned char *buffer)
{
#ifndef BUILD_LK
	unsigned int ret = 0;
	unsigned int id[3] = {0x83, 0x11, 0x2B};
	unsigned int data_array[3];
	unsigned char read_buf[3];

	data_array[0] = 0x00033700; /* set max return size = 3 */
	dsi_set_cmdq(data_array, 1, 1);

	read_reg_v2(0x04, read_buf, 3); /* read lcm id */

	LCM_LOGI("ATA read = 0x%x, 0x%x, 0x%x\n",
		 read_buf[0], read_buf[1], read_buf[2]);

	if ((read_buf[0] == id[0]) &&
	    (read_buf[1] == id[1]) &&
	    (read_buf[2] == id[2]))
		ret = 1;
	else
		ret = 0;

	return ret;
#else
	return 0;
#endif
}

static void lcm_setbacklight_cmdq(void *handle, unsigned int level)
{
	bl_level[0].para_list[0] = (level >> 6) & 0x03;
	bl_level[0].para_list[1] = (level << 2) & 0xFF;
	push_table(handle, bl_level, ARRAY_SIZE(bl_level), 1);
	LCM_LOGI("%s,s68fc01 backlight: level=%d,p0:%d,p1:%d\n",
		__func__, level, bl_level[0].para_list[0],
		bl_level[0].para_list[1]);
}

static bool lcm_get_hbm_state(void)
{
	return hbm_en;
}

static bool lcm_get_hbm_wait(void)
{
	return hbm_wait;
}

static bool lcm_set_hbm_wait(bool wait)
{
	bool old = hbm_wait;

	hbm_wait = wait;
	return old;
}

static bool lcm_set_hbm_cmdq(bool en, void *qhandle)
{
	bool old = hbm_en;

	if (hbm_en == en)
		goto done;

	if (en)
		hbm[0].para_list[0] = CMD_HBM_ENABLE;
	else
		hbm[0].para_list[0] = CMD_HBM_DISABLE;

	push_table(qhandle, hbm, ARRAY_SIZE(hbm), 1);

	hbm_en = en;
	lcm_set_hbm_wait(true);

done:
	return old;
}

static void lcm_update(unsigned int x, unsigned int y, unsigned int width,
	unsigned int height)
{
	unsigned int x0 = x;
	unsigned int y0 = y;
	unsigned int x1 = x0 + width - 1;
	unsigned int y1 = y0 + height - 1;

	unsigned char x0_MSB = ((x0 >> 8) & 0xFF);
	unsigned char x0_LSB = (x0 & 0xFF);
	unsigned char x1_MSB = ((x1 >> 8) & 0xFF);
	unsigned char x1_LSB = (x1 & 0xFF);
	unsigned char y0_MSB = ((y0 >> 8) & 0xFF);
	unsigned char y0_LSB = (y0 & 0xFF);
	unsigned char y1_MSB = ((y1 >> 8) & 0xFF);
	unsigned char y1_LSB = (y1 & 0xFF);

	unsigned int data_array[16];

#ifdef LCM_SET_DISPLAY_ON_DELAY
	lcm_set_display_on();
#endif

	data_array[0] = 0x00053902;
	data_array[1] = (x1_MSB << 24) | (x0_LSB << 16) | (x0_MSB << 8) | 0x2a;
	data_array[2] = (x1_LSB);
	dsi_set_cmdq(data_array, 3, 1);

	data_array[0] = 0x00053902;
	data_array[1] = (y1_MSB << 24) | (y0_LSB << 16) | (y0_MSB << 8) | 0x2b;
	data_array[2] = (y1_LSB);
	dsi_set_cmdq(data_array, 3, 1);

	data_array[0] = 0x002c3909;
	dsi_set_cmdq(data_array, 1, 0);
}


static struct LCM_setting_table_V3 lcm_aod_area[] = {
	/* AOD Setting */
	{REGFLAG_ESCAPE_ID, 0x81, 0x2C,
		{0x3c, 0x0C, 0x07, 0x03, 0x21, 0x90,
		0x0C, 0x07, 0x19, 0x02, 0xEE,
		0x18, 0x06, 0x38, 0x45, 0x14,
		0x00, 0x06, 0x51, 0x46, 0xA4,
		0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00} },
	/* Image Data Write for AOD Mode */
	/* Display on */
	{REGFLAG_ESCAPE_ID, 0x29, 0, {} }
};

static void lcm_set_aod_area(void *handle, unsigned char *area)
{
#if 0
	unsigned int i = 0;

	for (i = 0; i < lcm_aod_area[0].count; i++)
		lcm_aod_area[0].para_list[i] = area[i];
#endif
	dsi_set_cmdq_V5(lcm_aod_area,
				ARRAY_SIZE(lcm_aod_area), 0);

}
static struct LCM_setting_table_V3 lcm_sleep_in_setting_v3[] = {

	{REGFLAG_ESCAPE_ID, 0x28, 0, {} },
	{REGFLAG_ESCAPE_ID, REGFLAG_DELAY_MS_V3, 10, {} },
	{REGFLAG_ESCAPE_ID, 0x10, 0, {} },
	//PANEL cv switch need
	{REGFLAG_ESCAPE_ID, REGFLAG_DELAY_MS_V3, 100, {} },
};

static struct LCM_setting_table_V3 lcm_normal_to_aod[] = {

	/* Internal VDO Packet generation enable*/
	{REGFLAG_ESCAPE_ID, 0xF0, 2, {0x5A, 0x5A} },
	{REGFLAG_ESCAPE_ID, 0xFC, 2, {0x5A, 0x5A} },
	{REGFLAG_ESCAPE_ID, 0xB0, 2, {0x14, 0xFE} },
	{REGFLAG_ESCAPE_ID, 0xFE, 1, {0x12} },
	{REGFLAG_ESCAPE_ID, 0xF0, 2, {0xA5, 0xA5} },
	{REGFLAG_ESCAPE_ID, 0xFC, 2, {0xA5, 0xA5} },

	/*Sleep out*/
	{REGFLAG_ESCAPE_ID, 0x11, 0, {} },
	{REGFLAG_ESCAPE_ID, REGFLAG_DELAY_MS_V3, 20, {} },

	/* MIPI Mode cmd */
	{REGFLAG_ESCAPE_ID, 0xF0, 2, {0x5A, 0x5A} },
	{REGFLAG_ESCAPE_ID, 0xF2, 1, {0x03} },
	{REGFLAG_ESCAPE_ID, 0xF0, 2, {0xA5, 0xA5} },

	/* TE vsync ON */
	{REGFLAG_ESCAPE_ID, 0x35, 1, {0x00} },
	{REGFLAG_ESCAPE_ID, REGFLAG_DELAY_MS_V3, 20, {} },


	/* PCD setting off */
	{REGFLAG_ESCAPE_ID, 0xF0, 2, {0x5A, 0x5A} },
	{REGFLAG_ESCAPE_ID, 0xEA, 1, {0x48} },
	{REGFLAG_ESCAPE_ID, 0xF0, 2, {0xA5, 0xA5} },

	/* AOD AMP ON */
	{REGFLAG_ESCAPE_ID, 0xFC, 2, {0x5A, 0x5A} },
	{REGFLAG_ESCAPE_ID, 0xB0, 2, {0x06, 0xFD} },
	{REGFLAG_ESCAPE_ID, 0xFD, 2, {0x85} },
	{REGFLAG_ESCAPE_ID, 0xFC, 2, {0xA5, 0xA5} },

	/* AOD Mode On Setting */
	{REGFLAG_ESCAPE_ID, 0x53, 1, {0x22} },

	/* Internal VDO Packet generation enable*/
	{REGFLAG_ESCAPE_ID, 0xF0, 2, {0x5A, 0x5A} },
	{REGFLAG_ESCAPE_ID, 0xFC, 2, {0x5A, 0x5A} },
	{REGFLAG_ESCAPE_ID, 0xB0, 2, {0x14, 0xFE} },
	{REGFLAG_ESCAPE_ID, 0xFE, 1, {0x10} },
	{REGFLAG_ESCAPE_ID, 0xF0, 2, {0xA5, 0xA5} },
	{REGFLAG_ESCAPE_ID, 0xFC, 2, {0xA5, 0xA5} },

	/*AOD IP Setting*/
	{REGFLAG_ESCAPE_ID, 0xF0, 2, {0x5A, 0x5A} },
	{REGFLAG_ESCAPE_ID, 0xB0, 2, {0x03, 0xC2} },
	{REGFLAG_ESCAPE_ID, 0xC2, 1, {0x04} },
	{REGFLAG_ESCAPE_ID, 0xF0, 2, {0xA5, 0xA5} },

	/*seed setting*/
	{REGFLAG_ESCAPE_ID, 0xF0, 2, {0x5A, 0x5A} },
	{REGFLAG_ESCAPE_ID, 0x80, 1, {0x92} },
	{REGFLAG_ESCAPE_ID, 0xB1, 1, {0x00} },
	{REGFLAG_ESCAPE_ID, 0xB0, 2, {0x2B, 0xB1} },
	{REGFLAG_ESCAPE_ID, 0xB1, 21, {0xE0, 0x00, 0x06,
		0x10, 0xFF, 0x00, 0x00, 0x00, 0xFF, 0x2A, 0xFF,
		0xE2, 0xFF, 0x00, 0xEE, 0xFF, 0xF1, 0x00, 0xFF, 0xFF, 0xFF} },
	{REGFLAG_ESCAPE_ID, 0xB0, 2, {0x55, 0xB1} },
	{REGFLAG_ESCAPE_ID, 0xB1, 1, {0x80} },
	{REGFLAG_ESCAPE_ID, 0xF0, 2, {0xA5, 0xA5} },

	{REGFLAG_ESCAPE_ID, REGFLAG_DELAY_MS_V3, 100, {} },

	#if 1
	/* Image Data Write for AOD Mode */
	/* Display on */
	{REGFLAG_ESCAPE_ID, 0x29, 0, {} },

	/* MIPI Video cmd*/
	{REGFLAG_ESCAPE_ID, 0xF0, 2, {0x5A, 0x5A} },
	{REGFLAG_ESCAPE_ID, 0xF2, 1, {0x0F} },
	{REGFLAG_ESCAPE_ID, 0xF0, 2, {0xA5, 0xA5} },
	#endif
};


static struct LCM_setting_table_V3 lcm_aod_to_normal[] = {


	{REGFLAG_ESCAPE_ID, 0xF0, 2, {0x5A, 0x5A} },
	{REGFLAG_ESCAPE_ID, 0xF2, 1, {0x0F} },
	{REGFLAG_ESCAPE_ID, 0xF0, 2, {0xA5, 0xA5} },

	/* AOD Mode off Setting */
	{REGFLAG_ESCAPE_ID, 0x53, 1, {0x20} },

	/*seed setting*/
	{REGFLAG_ESCAPE_ID, 0xF0, 2, {0x5A, 0x5A} },
	{REGFLAG_ESCAPE_ID, 0x80, 1, {0x92} },
	{REGFLAG_ESCAPE_ID, 0xB1, 1, {0x00} },
	{REGFLAG_ESCAPE_ID, 0xB0, 2, {0x2B, 0xB1} },
	{REGFLAG_ESCAPE_ID, 0xB1, 21, {0xE0, 0x00,
		0x06, 0x10, 0xFF, 0x00, 0x00, 0x00, 0xFF,
		0x2A, 0xFF, 0xE2, 0xFF, 0x00, 0xEE, 0xFF,
		0xF1, 0x00, 0xFF, 0xFF, 0xFF} },
	{REGFLAG_ESCAPE_ID, 0xB0, 2, {0x55, 0xB1} },
	{REGFLAG_ESCAPE_ID, 0xB1, 1, {0x80} },
	{REGFLAG_ESCAPE_ID, 0xF0, 2, {0xA5, 0xA5} },

	/*normal mode backlight setting*/
	{REGFLAG_ESCAPE_ID, 0x51, 2, {0x03, 0xff} },
	/* Display on */
	{REGFLAG_ESCAPE_ID, 0x29, 0, {} },
};

static void lcm_aod(int enter)
{

	if (enter) {
		SET_RESET_PIN(0);
		MDELAY(2);
		SET_RESET_PIN(1);
		dsi_set_cmdq_V5(lcm_sleep_in_setting_v3,
			ARRAY_SIZE(lcm_sleep_in_setting_v3), 0);
		dsi_set_cmdq_V5(lcm_normal_to_aod,
			ARRAY_SIZE(lcm_normal_to_aod), 0);
	} else {
		dsi_set_cmdq_V5(lcm_aod_to_normal,
			ARRAY_SIZE(lcm_aod_to_normal), 0);
	}

}

#define lcm_doze_delay 1
static int lcm_get_doze_delay(void)
{
	return lcm_doze_delay;
}

struct LCM_DRIVER s68fc01_fhdplus_dsi_vdo_samsung_lcm_drv = {
	.name = "s68fc01_fhdplus_dsi_vdo_samsung_lcm_drv",
	.set_util_funcs = lcm_set_util_funcs,
	.get_params = lcm_get_params,
	.init = lcm_init,
	.suspend = lcm_suspend,
	.resume = lcm_resume,
	.init_power = lcm_init_power,
	.resume_power = lcm_resume_power,
	.suspend_power = lcm_suspend_power,
	.set_backlight_cmdq = lcm_setbacklight_cmdq,
	.ata_check = lcm_ata_check,
	.update = lcm_update,
	.set_hbm_cmdq = lcm_set_hbm_cmdq,
	.get_hbm_state = lcm_get_hbm_state,
	.get_hbm_wait = lcm_get_hbm_wait,
	.set_hbm_wait = lcm_set_hbm_wait,
	.set_aod_area_cmdq = lcm_set_aod_area,
	.aod = lcm_aod,
	.get_doze_delay = lcm_get_doze_delay,
};

