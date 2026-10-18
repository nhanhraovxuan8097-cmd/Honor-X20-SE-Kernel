/*
 * Copyright (c) 2015 MediaTek Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/backlight.h>
#include <linux/delay.h>
#include <drm/drmP.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_panel.h>

#include <video/mipi_display.h>
#include <video/of_videomode.h>
#include <video/videomode.h>

#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/gpio/consumer.h>

#define CONFIG_MTK_PANEL_EXT
#if defined(CONFIG_MTK_PANEL_EXT)
#include "../mediatek/mtk_panel_ext.h"
#include "../mediatek/mtk_log.h"
#include "../mediatek/mtk_drm_graphics_base.h"

static char bl_tb0[] = {0x51, 0xf, 0xff};
#endif

/* enable this to check panel self -bist pattern */
/* #define PANEL_BIST_PATTERN */

/* option function to read data from some panel address */
/* #define PANEL_SUPPORT_READBACK */

struct truly {
	struct device *dev;
	struct drm_panel panel;
	struct backlight_device *backlight;
	struct gpio_desc *reset_gpio;

	bool prepared;
	bool enabled;

	int error;
};

#define truly_dcs_write_seq(ctx, seq...)                                     \
	({                                                                     \
		const u8 d[] = {seq};                                          \
		BUILD_BUG_ON_MSG(ARRAY_SIZE(d) > 64,                           \
				 "DCS sequence too big for stack");            \
		truly_dcs_write(ctx, d, ARRAY_SIZE(d));                      \
	})

#define truly_dcs_write_seq_static(ctx, seq...)                              \
	({                                                                     \
		static const u8 d[] = {seq};                                   \
		truly_dcs_write(ctx, d, ARRAY_SIZE(d));                      \
	})

static inline struct truly *panel_to_truly(struct drm_panel *panel)
{
	return container_of(panel, struct truly, panel);
}

#ifdef PANEL_SUPPORT_READBACK
static int truly_dcs_read(struct truly *ctx, u8 cmd, void *data, size_t len)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	ssize_t ret;

	if (ctx->error < 0)
		return 0;

	ret = mipi_dsi_dcs_read(dsi, cmd, data, len);
	if (ret < 0) {
		dev_err(ctx->dev, "error %d reading dcs seq:(%#x)\n", ret, cmd);
		ctx->error = ret;
	}

	return ret;
}

static void truly_panel_get_data(struct truly *ctx)
{
	u8 buffer[3] = {0};
	static int ret;

	if (ret == 0) {
		ret = truly_dcs_read(ctx, 0x0A, buffer, 1);
		dev_info(ctx->dev, "return %d data(0x%08x) to dsi engine\n",
			 ret, buffer[0] | (buffer[1] << 8));
	}
}
#endif

static void truly_dcs_write(struct truly *ctx, const void *data, size_t len)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	ssize_t ret;
	char *addr;

	if (ctx->error < 0)
		return;

	addr = (char *)data;
	if ((int)*addr < 0xB0)
		ret = mipi_dsi_dcs_write_buffer(dsi, data, len);
	else
		ret = mipi_dsi_generic_write(dsi, data, len);
	if (ret < 0) {
		dev_err(ctx->dev, "error %zd writing seq: %ph\n", ret, data);
		ctx->error = ret;
	}
}

static void truly_panel_init(struct truly *ctx)
{
	pr_info("%s +\n", __func__);

	ctx->reset_gpio = devm_gpiod_get(ctx->dev, "reset", GPIOD_OUT_HIGH);
	usleep_range(10 * 1000, 15 * 1000);
	gpiod_set_value(ctx->reset_gpio, 0);
	usleep_range(10 * 1000, 15 * 1000);
	gpiod_set_value(ctx->reset_gpio, 1);
	usleep_range(10 * 1000, 15 * 1000);
	devm_gpiod_put(ctx->dev, ctx->reset_gpio);

	msleep(200);

	/* LCD Pannel position and display */
	truly_dcs_write_seq_static(ctx, 0x36, 0x40);
	truly_dcs_write_seq_static(ctx, 0xB0, 0x00);

	truly_dcs_write_seq_static(ctx, 0xD6, 0x01);
#if (LCM_DSI_CMD_MODE)
	truly_dcs_write_seq_static(ctx, 0xB3, 0x04, 0x00, 0x00);
#else
	truly_dcs_write_seq_static(ctx, 0xB3, 0x14, 0x00, 0x00);
#endif
	truly_dcs_write_seq_static(ctx, 0xB4, 0x00);
	truly_dcs_write_seq_static(ctx, 0xB6, 0x3A, 0xD3);
	/* This register controls DSI virtual channel of PortA setting. */
	truly_dcs_write_seq_static(ctx, 0xBE, 0x04);
	truly_dcs_write_seq_static(ctx, 0xC3, 0x00, 0x00, 0x00);
	truly_dcs_write_seq_static(ctx, 0xC5, 0x00);
	truly_dcs_write_seq_static(ctx, 0xC0, 0x00, 0x00, 0x00, 0x00);
	/* #Display setting 1 */
	/* {0xC1, 35, {0x00,0x61,0x00,0x20,0x8C,0xA4,0x16,0xFB,0xBF,0x98,0x83,
	 * 0xDC,0x7B,0xCF,0x35,0x74,0x4C,0xF9,0x9F,0x2D,0x95,0x88,
	 * 0x00,0x00,0x00,0x00,0x00,0x00,0x02,0x60,0x23,0x03,0x00,
	 * 0xFF,0x11}},
	 */
	truly_dcs_write_seq_static(ctx, 0xC1, 0x00, 0x61, 0x00, 0x20, 0x8C,
				   0xA4, 0x16, 0xFB, 0xBF, 0x98, 0x83, 0x9A,
				   0x7B, 0xCF, 0x35, 0x74, 0x4C, 0xF9, 0x9F,
				   0x2D, 0x95, 0x88, 0x00, 0x00, 0x00, 0x00,
				   0x00, 0x00, 0x02, 0x63, 0x23, 0x03, 0x00,
				   0xFF, 0x11);
	/* #Display setting 2 */
	truly_dcs_write_seq_static(ctx, 0xC2, 0x0A, 0x0A, 0x00, 0x08, 0x08,
				   0xF0, 0x00, 0x04);
	/* #Source timing setting */
	truly_dcs_write_seq_static(ctx, 0xC4, 0x70, 0x00, 0x00, 0x33, 0x33,
				   0x033, 0x33, 0x33, 0x33, 0x33, 0x33, 0x01,
				   0x05, 0x01);
	/* #LTPS timing setting */
	truly_dcs_write_seq_static(ctx, 0xC6, 0x5A, 0x29, 0x29, 0x01, 0x01,
				   0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00,
				   0x00, 0x00, 0x00, 0x00, 0x00, 0x06,
				   0x15, 0x08, 0x5A);
	/* #Panel pin control */
	truly_dcs_write_seq_static(ctx, 0xCB, 0x7F, 0xE0, 0x07, 0xFF, 0x00,
				   0x00, 0x00, 0x00, 0x54, 0xE0, 0x07, 0x2A,
				   0xC8, 0x00, 0x00);
	/* #Panel intreface control */
	truly_dcs_write_seq_static(ctx, 0xCC, 0x11);
	/* #Sequencer timing control */
	truly_dcs_write_seq_static(ctx, 0xD7, 0x82, 0xFF, 0x21, 0x8E, 0x8C,
				   0xF1, 0x87, 0x3F, 0x7E, 0x10, 0x00, 0x00,
				   0x8F);
	/* #Sequencer control */
	truly_dcs_write_seq_static(ctx, 0xD9, 0x00, 0x00);
	/* #Power setting(Charge pump setting */
	truly_dcs_write_seq_static(ctx, 0xD0, 0x11, 0x17, 0x17, 0xFD);
	/* #Power setting for internal Power */
	truly_dcs_write_seq_static(ctx, 0xD2, 0xCD, 0x2B, 0x2B, 0x33, 0x10,
				   0x33, 0x33, 0x33, 0x77, 0x77, 0x33, 0x33,
				   0x33, 0x00, 0x00, 0x00);
	/* vplvl 2b 4v vnlvl  2b -4v  12  4.02V ENTER ABNORMAL SEQUENCE */
	/* #VCOM setting */
	truly_dcs_write_seq_static(ctx, 0xD5, 0x06, 0x00, 0x00, 0x01, 0x1E,
				   0x01, 0x1E);
	/* Gamma Setting with RGB separated setting ON */
	truly_dcs_write_seq_static(ctx, 0xC7, 0x00, 0x0C, 0x14, 0x1D, 0x2C,
				   0x3A, 0x44, 0x54, 0x38, 0x40, 0x4C, 0x59,
				   0x63, 0x6B, 0x7F, 0x00, 0x0C, 0x14, 0x1D,
				   0x2C, 0x3A, 0x44, 0x54, 0x38, 0x40, 0x4C,
				   0x59, 0x63, 0x6B, 0x7F);
	truly_dcs_write_seq_static(ctx, 0xC8, 0x01, 0x00, 0x00, 0x00, 0x00,
				   0xFC, 0xEF, 0x00, 0x00, 0x00, 0x00, 0xFC,
				   0xEF, 0x00, 0x00, 0x00, 0x00, 0xFC, 0x0F);
	truly_dcs_write_seq_static(ctx, 0xB8, 0x57, 0x3D, 0x19, 0x1E, 0x0A,
				   0x50, 0x50);
	truly_dcs_write_seq_static(ctx, 0xB9, 0x6F, 0x3D, 0x28, 0x3C, 0x14,
				   0xC8, 0xC8);
	truly_dcs_write_seq_static(ctx, 0xBA, 0xB5, 0x33, 0x41, 0x64, 0x23,
				   0xA0, 0xA0);
	truly_dcs_write_seq_static(ctx, 0xCE, 0x55, 0x40, 0x49, 0x53, 0x59,
				   0x5E, 0x63, 0x68, 0x6E, 0x74, 0x7E, 0x8A,
				   0x98, 0xA8, 0xBB, 0xD0, 0xFF, 0x04, 0x00,
				   0x04, 0x04, 0x00, 0x00, 0x69, 0x5A);
	/* self check module */
	/* Manufacturer Command Access Protect */
	truly_dcs_write_seq_static(ctx, 0xB0, 0x03);
	truly_dcs_write_seq_static(ctx, 0x35, 0x00);
	truly_dcs_write_seq_static(ctx, 0x53, 0x2C); /* BL=1h */
	truly_dcs_write_seq_static(ctx, 0x29);
	msleep(20);
	truly_dcs_write_seq_static(ctx, 0x11);
	msleep(120);
	pr_info("%s -\n", __func__);
}

static int truly_disable(struct drm_panel *panel)
{
	struct truly *ctx = panel_to_truly(panel);

	if (!ctx->enabled)
		return 0;

	if (ctx->backlight) {
		ctx->backlight->props.power = FB_BLANK_POWERDOWN;
		backlight_update_status(ctx->backlight);
	}

	ctx->enabled = false;

	return 0;
}

static int truly_unprepare(struct drm_panel *panel)
{
	struct truly *ctx = panel_to_truly(panel);

	if (!ctx->prepared)
		return 0;

	truly_dcs_write_seq_static(ctx, MIPI_DCS_ENTER_SLEEP_MODE);
	truly_dcs_write_seq_static(ctx, MIPI_DCS_SET_DISPLAY_OFF);
	msleep(200);

	ctx->reset_gpio = devm_gpiod_get(ctx->dev, "reset", GPIOD_OUT_HIGH);
	gpiod_set_value(ctx->reset_gpio, 0);
	devm_gpiod_put(ctx->dev, ctx->reset_gpio);

	ctx->error = 0;
	ctx->prepared = false;

	return 0;
}

static int truly_prepare(struct drm_panel *panel)
{
	struct truly *ctx = panel_to_truly(panel);
	int ret;

	pr_info("%s\n", __func__);
	if (ctx->prepared)
		return 0;

	truly_panel_init(ctx);

	ret = ctx->error;
	if (ret < 0)
		truly_unprepare(panel);

	ctx->prepared = true;

#ifdef PANEL_SUPPORT_READBACK
	truly_panel_get_data(ctx);
#endif

	return ret;
}

static int truly_enable(struct drm_panel *panel)
{
	struct truly *ctx = panel_to_truly(panel);

	if (ctx->enabled)
		return 0;

	if (ctx->backlight) {
		ctx->backlight->props.power = FB_BLANK_UNBLANK;
		backlight_update_status(ctx->backlight);
	}

	ctx->enabled = true;

	return 0;
}

static const struct drm_display_mode default_mode = {
	.clock = 240240,
	.hdisplay = 1440,
	.hsync_start = 1440 + 60,
	.hsync_end = 1440 + 60 + 10,
	.htotal = 1440 + 60 + 10 + 30,
	.vdisplay = 2560,
	.vsync_start = 2560 + 30,
	.vsync_end = 2560 + 30 + 4,
	.vtotal = 2560 + 30 + 4 + 6,
	.vrefresh = 60,
};

#if defined(CONFIG_MTK_PANEL_EXT)
static struct mtk_panel_params ext_params = {
	.pll_clk = 300,
	.physical_width_um = 70200,
	.physical_height_um = 152100,
	.cust_esd_check = 0,
	.esd_check_enable = 0,
	.output_mode = MTK_PANEL_DUAL_PORT,
	.lcm_cmd_if = MTK_PANEL_SINGLE_PORT,
	.lcm_esd_check_table[0] = {
			.cmd = 0x53, .count = 1, .para_list[0] = 0x00,
	},

	.lane_swap_en = 1,
	.lane_swap[MIPITX_PHY_PORT_1][MIPITX_PHY_LANE_0] = MIPITX_PHY_LANE_2,
	.lane_swap[MIPITX_PHY_PORT_1][MIPITX_PHY_LANE_1] = MIPITX_PHY_LANE_CK,
	.lane_swap[MIPITX_PHY_PORT_1][MIPITX_PHY_LANE_2] = MIPITX_PHY_LANE_3,
	.lane_swap[MIPITX_PHY_PORT_1][MIPITX_PHY_LANE_3] = MIPITX_PHY_LANE_1,
	.lane_swap[MIPITX_PHY_PORT_1][MIPITX_PHY_LANE_CK] = MIPITX_PHY_LANE_0,
	.lane_swap[MIPITX_PHY_PORT_1][MIPITX_PHY_LANE_RX] = MIPITX_PHY_LANE_CK,

	.lane_swap[MIPITX_PHY_PORT_0][MIPITX_PHY_LANE_0] = MIPITX_PHY_LANE_CK,
	.lane_swap[MIPITX_PHY_PORT_0][MIPITX_PHY_LANE_1] = MIPITX_PHY_LANE_2,
	.lane_swap[MIPITX_PHY_PORT_0][MIPITX_PHY_LANE_2] = MIPITX_PHY_LANE_1,
	.lane_swap[MIPITX_PHY_PORT_0][MIPITX_PHY_LANE_3] = MIPITX_PHY_LANE_0,
	.lane_swap[MIPITX_PHY_PORT_0][MIPITX_PHY_LANE_CK] = MIPITX_PHY_LANE_3,
	.lane_swap[MIPITX_PHY_PORT_0][MIPITX_PHY_LANE_RX] = MIPITX_PHY_LANE_3,
};

static int truly_setbacklight_cmdq(void *dsi, dcs_write_gce cb, void *handle,
				   unsigned int level)
{
	if (level > 255)
		level = 255;

	level = level * 4095 / 255;
	bl_tb0[1] = ((level >> 8) & 0xf);
	bl_tb0[2] = (level & 0xff);

	if (!cb)
		return -1;

	cb(dsi, handle, bl_tb0, ARRAY_SIZE(bl_tb0));

	return 0;
}

static struct mtk_panel_funcs ext_funcs = {
	.set_backlight_cmdq = truly_setbacklight_cmdq,
};
#endif

struct panel_desc {
	const struct drm_display_mode *modes;
	unsigned int num_modes;

	unsigned int bpc;

	struct {
		unsigned int width;
		unsigned int height;
	} size;

	/**
	 * @prepare: the time (in milliseconds) that it takes for the panel to
	 *           become ready and start receiving video data
	 * @enable: the time (in milliseconds) that it takes for the panel to
	 *          display the first valid frame after starting to receive
	 *          video data
	 * @disable: the time (in milliseconds) that it takes for the panel to
	 *           turn the display off (no content is visible)
	 * @unprepare: the time (in milliseconds) that it takes for the panel
	 *             to power itself down completely
	 */
	struct {
		unsigned int prepare;
		unsigned int enable;
		unsigned int disable;
		unsigned int unprepare;
	} delay;
};

static int truly_get_modes(struct drm_panel *panel)
{
	struct drm_display_mode *mode;
	struct drm_display_mode *mode2;

	mode = drm_mode_duplicate(panel->drm, &default_mode);
	if (!mode) {
		dev_err(panel->drm->dev, "failed to add mode %ux%ux@%u\n",
			default_mode.hdisplay, default_mode.vdisplay,
			default_mode.vrefresh);
		return -ENOMEM;
	}

	drm_mode_set_name(mode);
	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	drm_mode_probed_add(panel->connector, mode);

	panel->connector->display_info.width_mm = 70;
	panel->connector->display_info.height_mm = 152;

	return 1;
}

static const struct drm_panel_funcs truly_drm_funcs = {
	.disable = truly_disable,
	.unprepare = truly_unprepare,
	.prepare = truly_prepare,
	.enable = truly_enable,
	.get_modes = truly_get_modes,
};

static int truly_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct truly *ctx;
	struct device_node *backlight;
	int ret;

	pr_info("%s+%d\n", __func__, __LINE__);
	ctx = devm_kzalloc(dev, sizeof(struct truly), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;
	pr_info("%s+%d\n", __func__, __LINE__);

	mipi_dsi_set_drvdata(dsi, ctx);
	pr_info("%s+%d\n", __func__, __LINE__);

	ctx->dev = dev;
	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO
			 | MIPI_DSI_MODE_LPM | MIPI_DSI_MODE_EOT_PACKET
			 | MIPI_DSI_CLOCK_NON_CONTINUOUS;
	pr_info("%s+%d\n", __func__, __LINE__);

	backlight = of_parse_phandle(dev->of_node, "backlight", 0);
	if (backlight) {
		ctx->backlight = of_find_backlight_by_node(backlight);
		of_node_put(backlight);

		if (!ctx->backlight)
			return -EPROBE_DEFER;
	}
	pr_info("%s+%d\n", __func__, __LINE__);

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio)) {
		dev_err(dev, "cannot get reset-gpios %ld\n",
			PTR_ERR(ctx->reset_gpio));
		return PTR_ERR(ctx->reset_gpio);
	}
	devm_gpiod_put(dev, ctx->reset_gpio);

#ifndef CONFIG_MTK_DISP_NO_LK
	ctx->prepared = true;
	ctx->enabled = true;
#endif

	pr_info("%s+%d\n", __func__, __LINE__);

	drm_panel_init(&ctx->panel);
	ctx->panel.dev = dev;
	ctx->panel.funcs = &truly_drm_funcs;
	pr_info("%s+%d\n", __func__, __LINE__);

	ret = drm_panel_add(&ctx->panel);
	if (ret < 0)
		return ret;
	pr_info("%s+%d\n", __func__, __LINE__);

	ret = mipi_dsi_attach(dsi);
	if (ret < 0)
		drm_panel_remove(&ctx->panel);
	pr_info("%s+%d\n", __func__, __LINE__);

#if defined(CONFIG_MTK_PANEL_EXT)
	ret = mtk_panel_ext_create(dev, &ext_params, &ext_funcs, &ctx->panel);
	if (ret < 0)
		return ret;
#endif

	pr_info("%s-\n", __func__);

	return ret;
}

static int truly_remove(struct mipi_dsi_device *dsi)
{
	struct truly *ctx = mipi_dsi_get_drvdata(dsi);

	mipi_dsi_detach(dsi);
	drm_panel_remove(&ctx->panel);

	return 0;
}

static const struct of_device_id truly_of_match[] = {
	{
		.compatible = "truly,r63419,ws3142",
	},
	{} };

MODULE_DEVICE_TABLE(of, truly_of_match);

static struct mipi_dsi_driver truly_driver = {
	.probe = truly_probe,
	.remove = truly_remove,
	.driver = {
			.name = "panel-truly-r63419-vdo-ws3142",
			.owner = THIS_MODULE,
			.of_match_table = truly_of_match,
		},
};

module_mipi_dsi_driver(truly_driver);

MODULE_AUTHOR("Jitao Shi <jitao.shi@mediatek.com>");
MODULE_DESCRIPTION("truly r63419 ws3142 Panel Driver");
MODULE_LICENSE("GPL v2");

