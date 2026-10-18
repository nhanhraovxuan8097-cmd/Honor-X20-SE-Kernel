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

#include <linux/gpio/consumer.h>

#include <video/mipi_display.h>
#include <video/of_videomode.h>
#include <video/videomode.h>

#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>

#define CONFIG_MTK_PANEL_EXT
#if defined(CONFIG_MTK_PANEL_EXT)
#include "../mediatek/mtk_panel_ext.h"
#include "../mediatek/mtk_log.h"
#include "../mediatek/mtk_drm_graphics_base.h"
#endif
/* enable this to check panel self -bist pattern */
/* #define PANEL_BIST_PATTERN */

/* option function to read data from some panel address */
//#define PANEL_SUPPORT_READBACK

struct debussy {
	struct device *dev;
	struct drm_panel panel;
	struct backlight_device *backlight;
	struct gpio_desc *reset_gpio;

	bool prepared;
	bool enabled;

	int error;
};

#define debussy_dcs_write_seq(ctx, seq...)                                     \
	({                                                                     \
		const u8 d[] = {seq};                                          \
		BUILD_BUG_ON_MSG(ARRAY_SIZE(d) > 64,                           \
				 "DCS sequence too big for stack");            \
		debussy_dcs_write(ctx, d, ARRAY_SIZE(d));                      \
	})

#define debussy_dcs_write_seq_static(ctx, seq...)                              \
	({                                                                     \
		static const u8 d[] = {seq};                                   \
		debussy_dcs_write(ctx, d, ARRAY_SIZE(d));                      \
	})

static inline struct debussy *panel_to_debussy(struct drm_panel *panel)
{
	return container_of(panel, struct debussy, panel);
}

#ifdef PANEL_SUPPORT_READBACK
static int debussy_dcs_read(struct debussy *ctx, u8 cmd, void *data, size_t len)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	ssize_t ret;

	if (ctx->error < 0)
		return 0;

	ret = mipi_dsi_dcs_read(dsi, cmd, data, len);
	if (ret < 0) {
		pr_notice("error %d reading dcs seq:(%#x)\n", ret, cmd);
		ctx->error = ret;
	}

	return ret;
}

static void debussy_panel_get_data(struct debussy *ctx)
{
	u8 buffer[3] = {0};
	static int ret;

	pr_info("%s+\n", __func__);

	if (ret == 0) {
		ret = debussy_dcs_read(ctx, 0x0A, buffer, 1);
		pr_info("%s  0x%08x\n", __func__,
			buffer[0] | (buffer[1] << 8));
		dev_info(ctx->dev, "return %d data(0x%08x) to dsi engine\n",
			 ret, buffer[0] | (buffer[1] << 8));
	}
}
#endif

static void debussy_dcs_write(struct debussy *ctx, const void *data, size_t len)
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
		pr_notice("error %zd writing seq: %ph\n", ret, data);
		ctx->error = ret;
	}
}

static void debussy_panel_init(struct debussy *ctx)
{
	ctx->reset_gpio = devm_gpiod_get(ctx->dev, "reset", GPIOD_OUT_HIGH);
	usleep_range(10 * 1000, 15 * 1000);
	gpiod_set_value(ctx->reset_gpio, 0);
	usleep_range(10 * 1000, 15 * 1000);
	gpiod_set_value(ctx->reset_gpio, 1);
	usleep_range(10 * 1000, 15 * 1000);
	devm_gpiod_put(ctx->dev, ctx->reset_gpio);
	pr_info("%s+\n", __func__);

	debussy_dcs_write_seq_static(ctx, 0x00, 0x02, 0xFF, 0x10);
	debussy_dcs_write_seq_static(ctx, 0x00, 0x02, 0xFB, 0x10);
	debussy_dcs_write_seq_static(ctx, 0x00, 0x02, 0xB9, 0x05);

	debussy_dcs_write_seq_static(ctx, 0x00, 0x02, 0xFF, 0x20);
	debussy_dcs_write_seq_static(ctx, 0x00, 0x02, 0xFB, 0x01);
	debussy_dcs_write_seq_static(ctx, 0x00, 0x02, 0x18, 0x40);

	debussy_dcs_write_seq_static(ctx, 0x00, 0x02, 0xFF, 0x10);
	debussy_dcs_write_seq_static(ctx, 0x00, 0x02, 0xFB, 0x01);
	debussy_dcs_write_seq_static(ctx, 0x00, 0x02, 0xB9, 0x02);

	debussy_dcs_write_seq_static(ctx, 0x00, 0x02, 0xFF, 0xD0);
	debussy_dcs_write_seq_static(ctx, 0x00, 0x02, 0xFB, 0x01);
	debussy_dcs_write_seq_static(ctx, 0x00, 0x02, 0x00, 0x30);
	debussy_dcs_write_seq_static(ctx, 0x00, 0x02, 0x09, 0xEE);
	debussy_dcs_write_seq_static(ctx, 0x00, 0x02, 0x1C, 0x99);
	debussy_dcs_write_seq_static(ctx, 0x00, 0x02, 0x1D, 0x09);

	debussy_dcs_write_seq_static(ctx, 0x00, 0x02, 0xFF, 0xE0);
	debussy_dcs_write_seq_static(ctx, 0x00, 0x02, 0xFB, 0x01);
	debussy_dcs_write_seq_static(ctx, 0x00, 0x02, 0x4F, 0x02);

	debussy_dcs_write_seq_static(ctx, 0x00, 0x02, 0xFF, 0xF0);
	debussy_dcs_write_seq_static(ctx, 0x00, 0x02, 0xFB, 0x01);
	debussy_dcs_write_seq_static(ctx, 0x00, 0x02, 0x3A, 0x08);

	debussy_dcs_write_seq_static(ctx, 0x00, 0x02, 0xFF, 0x10);
	debussy_dcs_write_seq_static(ctx, 0x00, 0x02, 0xFB, 0x01);
	debussy_dcs_write_seq_static(ctx, 0x00, 0x06, 0x3B, 0x03, 0xF1, 0x1A, 0x04, 0x04);

	debussy_dcs_write_seq_static(ctx, 0x11);
	msleep(120);

	debussy_dcs_write_seq_static(ctx, 0x29);
	pr_info("%s-\n", __func__);
}

static int debussy_disable(struct drm_panel *panel)
{
	struct debussy *ctx = panel_to_debussy(panel);

	if (!ctx->enabled)
		return 0;

	if (ctx->backlight) {
		ctx->backlight->props.power = FB_BLANK_POWERDOWN;
		backlight_update_status(ctx->backlight);
	}

	ctx->enabled = false;

	return 0;
}

static int debussy_unprepare(struct drm_panel *panel)
{
	struct debussy *ctx = panel_to_debussy(panel);

	pr_info("%s\n", __func__);

	if (!ctx->prepared)
		return 0;

	debussy_dcs_write_seq_static(ctx, MIPI_DCS_ENTER_SLEEP_MODE);
	debussy_dcs_write_seq_static(ctx, MIPI_DCS_SET_DISPLAY_OFF);
	msleep(200);

	ctx->reset_gpio = devm_gpiod_get(ctx->dev, "reset", GPIOD_OUT_HIGH);
	gpiod_set_value(ctx->reset_gpio, 0);
	devm_gpiod_put(ctx->dev, ctx->reset_gpio);

	ctx->error = 0;
	ctx->prepared = false;

	return 0;
}

static int debussy_prepare(struct drm_panel *panel)
{
	struct debussy *ctx = panel_to_debussy(panel);
	int ret;

	pr_info("%s+\n", __func__);
	if (ctx->prepared)
		return 0;

	debussy_panel_init(ctx);

	ret = ctx->error;
	if (ret < 0)
		debussy_unprepare(panel);

	ctx->prepared = true;

#ifdef PANEL_SUPPORT_READBACK
	debussy_panel_get_data(ctx);
#endif
	pr_info("%s-\n", __func__);
	return ret;
}

static int debussy_enable(struct drm_panel *panel)
{
	struct debussy *ctx = panel_to_debussy(panel);

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
	.clock = 316825,
	.hdisplay = 1600,
	.hsync_start = 1600 + 20,
	.hsync_end = 1600 + 20 + 10,
	.htotal = 1600 + 20 + 10 + 22,
	.vdisplay = 2560,
	.vsync_start = 2560 + 26,
	.vsync_end = 2560 + 26 + 4,
	.vtotal = 2560 + 26 + 4 + 241,
	.vrefresh = 120,
};

static const struct drm_display_mode performance_mode = {
	.clock = 316825,
	.hdisplay = 1600,
	.hsync_start = 1600 + 20,
	.hsync_end = 1600 + 20 + 10,
	.htotal = 1600 + 20 + 10 + 22,
	.vdisplay = 2560,
	.vsync_start = 2560 + 26,
	.vsync_end = 2560 + 26 + 4,
	.vtotal = 2560 + 26 + 4 + 241,
	.vrefresh = 120,
};

#if defined(CONFIG_MTK_PANEL_EXT)
static struct mtk_panel_params ext_params = {
	.pll_clk = 538,
	.output_mode = MTK_PANEL_DUAL_PORT,
	.lcm_cmd_if = MTK_PANEL_SINGLE_PORT,
	.cust_esd_check = 0,
	.esd_check_enable = 0,
	.lcm_esd_check_table[0] = {
	.cmd = 0x0A, .count = 1, .para_list[0] = 0x9C,
	},
	.is_cphy = 1,
	.data_rate = 1075,
	.dyn_fps = {
		.switch_en = 1,
		.vact_timing_fps = 120,
	},
	.dyn = {
		.switch_en = 1,
		.pll_clk = 550,
		.hfp = 26,
		.vfp = 20,
	},
};

static struct mtk_panel_params ext_params_90hz = {
	.pll_clk = 538,
	.output_mode = MTK_PANEL_DUAL_PORT,
	.lcm_cmd_if = MTK_PANEL_SINGLE_PORT,
	.vfp_low_power = 1291,
	.cust_esd_check = 0,
	.esd_check_enable = 0,
	.lcm_esd_check_table[0] = {
	.cmd = 0x0A, .count = 1, .para_list[0] = 0x9C,
	},
	.is_cphy = 1,
	.data_rate = 1075,
	.dyn_fps = {
		.switch_en = 1,
		.vact_timing_fps = 120,
	},
	.dyn = {
		.switch_en = 1,
		.pll_clk = 550,
		.vfp_lp_dyn = 1291,
		.hfp = 26,
		.vfp = 20,
	},
};

static int debussy_setbacklight_cmdq(void *dsi, dcs_write_gce cb,
		void *handle, unsigned int level)
{
	char bl_tb0[] = {0x51, 0xf, 0xff};

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

static int mtk_panel_ext_param_set(struct drm_panel *panel,
			 unsigned int mode)
{
	struct mtk_panel_ext *ext = find_panel_ext(panel);
	int ret = 0;

	if (mode == 0)
		ext->params = &ext_params;
	else if (mode == 1)
		ext->params = &ext_params_90hz;
	else
		ret = 1;

	return ret;
}

static int mtk_panel_ext_param_get(struct mtk_panel_params *ext_para,
			 unsigned int mode)
{
	int ret = 0;

	if (mode == 0)
		ext_para = &ext_params;
	else if (mode == 1)
		ext_para = &ext_params_90hz;
	else
		ret = 1;

	return ret;

}

static int panel_ext_reset(struct drm_panel *panel, int on)
{
	struct debussy *ctx = panel_to_debussy(panel);

	ctx->reset_gpio =
		devm_gpiod_get(ctx->dev, "reset", GPIOD_OUT_HIGH);
	gpiod_set_value(ctx->reset_gpio, on);
	devm_gpiod_put(ctx->dev, ctx->reset_gpio);

	return 0;
}

static struct mtk_panel_funcs ext_funcs = {
	.reset = panel_ext_reset,
	.set_backlight_cmdq = debussy_setbacklight_cmdq,
	.ext_param_set = mtk_panel_ext_param_set,
	.ext_param_get = mtk_panel_ext_param_get,
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

static int debussy_get_modes(struct drm_panel *panel)
{
	struct drm_display_mode *mode;
	struct drm_display_mode *mode2;

	mode = drm_mode_duplicate(panel->drm, &default_mode);
	if (!mode) {
		pr_notice("failed to add mode %ux%ux@%u\n",
			default_mode.hdisplay, default_mode.vdisplay,
			default_mode.vrefresh);
		return -ENOMEM;
	}

	drm_mode_set_name(mode);
	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	drm_mode_probed_add(panel->connector, mode);

	mode2 = drm_mode_duplicate(panel->drm, &performance_mode);
	if (!mode2) {
		pr_notice("failed to add mode %ux%ux@%u\n",
			performance_mode.hdisplay,
			performance_mode.vdisplay,
			performance_mode.vrefresh);
		return -ENOMEM;
	}

	drm_mode_set_name(mode2);
	mode2->type = DRM_MODE_TYPE_DRIVER;
	drm_mode_probed_add(panel->connector, mode2);

	panel->connector->display_info.width_mm = 70;
	panel->connector->display_info.height_mm = 152;

	return 1;
}

static const struct drm_panel_funcs debussy_drm_funcs = {
	.disable = debussy_disable,
	.unprepare = debussy_unprepare,
	.prepare = debussy_prepare,
	.enable = debussy_enable,
	.get_modes = debussy_get_modes,
};

static int debussy_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct debussy *ctx;
	struct device_node *backlight;
	int ret;

	pr_info("%s+\n", __func__);
	ctx = devm_kzalloc(dev, sizeof(struct debussy), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	mipi_dsi_set_drvdata(dsi, ctx);

	ctx->dev = dev;
	dsi->lanes = 3;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_SYNC_PULSE
			 |MIPI_DSI_MODE_LPM | MIPI_DSI_MODE_EOT_PACKET |
			  MIPI_DSI_CLOCK_NON_CONTINUOUS;

	backlight = of_parse_phandle(dev->of_node, "backlight", 0);
	if (backlight) {
		ctx->backlight = of_find_backlight_by_node(backlight);
		of_node_put(backlight);

		if (!ctx->backlight)
			return -EPROBE_DEFER;
	}

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio)) {
		pr_notice("cannot get reset-gpios %ld\n",
			PTR_ERR(ctx->reset_gpio));
		return PTR_ERR(ctx->reset_gpio);
	}
	devm_gpiod_put(dev, ctx->reset_gpio);

	ctx->prepared = true;
	ctx->enabled = true;

	drm_panel_init(&ctx->panel);
	ctx->panel.dev = dev;
	ctx->panel.funcs = &debussy_drm_funcs;

	ret = drm_panel_add(&ctx->panel);
	if (ret < 0)
		return ret;

	ret = mipi_dsi_attach(dsi);
	if (ret < 0)
		drm_panel_remove(&ctx->panel);

#if defined(CONFIG_MTK_PANEL_EXT)
	ret = mtk_panel_ext_create(dev, &ext_params, &ext_funcs, &ctx->panel);
	if (ret < 0)
		return ret;
#endif

	pr_info("%s-\n", __func__);

	return ret;
}

static int debussy_remove(struct mipi_dsi_device *dsi)
{
	struct debussy *ctx = mipi_dsi_get_drvdata(dsi);

	mipi_dsi_detach(dsi);
	drm_panel_remove(&ctx->panel);

	return 0;
}

static const struct of_device_id debussy_of_match[] = {
	{
		.compatible = "debussy,nt36523,cphy,vdo",
	},
	{} };

MODULE_DEVICE_TABLE(of, debussy_of_match);

static struct mipi_dsi_driver debussy_driver = {
	.probe = debussy_probe,
	.remove = debussy_remove,
	.driver = {
			.name = "panel-nt36523-cphy-vdo",
			.owner = THIS_MODULE,
			.of_match_table = debussy_of_match,
		},
};

module_mipi_dsi_driver(debussy_driver);

MODULE_AUTHOR("Elon Hsu <elon.hsu@mediatek.com>");
MODULE_DESCRIPTION("nt36523 Panel Driver");
MODULE_LICENSE("GPL v2");
