#ifndef LCD_DEFS_H
#define LCD_DEFS_H
#include <linux/types.h>
#include <drm/drm_print.h>
#include "dsi_panel.h"

#define LCD_WARN(fmt, ...)	DRM_WARN("[LCD /W]: "fmt, ##__VA_ARGS__)
#define LCD_ERR(fmt, ...)	DRM_DEV_ERROR(NULL, "[LCD /E]: " fmt, \
								##__VA_ARGS__)
#define LCD_INFO(fmt, ...)	DRM_DEV_INFO(NULL, "[LCD /I]: "fmt, \
								##__VA_ARGS__)
/* fb max number */
#define FB_MAX 32

#define HBM_SET_MAX_LEVEL 5000

#define LCD_FAIL (-1)
#define LCD_OK   0
#define NOT_SUPPORT 0

enum HBM_CFG_TYPE {
	HBM_FOR_FP = 0,
	HBM_FOR_MMI = 1,
	HBM_FOR_LIGHT = 2,
	LOCAL_HBM_FOR_MMI = 3,
	LOCAL_HBM_FOR_IDENTIFY = 4
};

enum lcd_panel_state {
	LCD_POWER_OFF,
	LCD_POWER_ON,
	LCD_LP_ON,
	LCD_HS_ON,
};

struct panel_info *get_panel_info(int idx);
int get_fb_index(struct device *dev);

#ifdef CONFIG_LCD_FACTORY
int factory_init(struct dsi_panel *panel, struct panel_info *pinfo);
#endif
#endif
