/*
 * cw2217_fuel_gauge.h
 *
 * coul with cw2217 driver
 *
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

#ifndef _CW2217_FG_H_
#define _CW2217_FG_H_

#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/workqueue.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/time.h>
#include <linux/slab.h>
#include <linux/version.h>
#include <linux/sizes.h>

/* Customer need to change
 * this for enable/disable log
 */
#define CWFG_ENABLE_LOG         1
/* Customer need to change this number
 * according to the principleof hardware
 */
#define CWFG_I2C_BUSNUM         5
#define DOUBLE_SERIES_BATTERY   0

#define CW_PROPERTIES "cw2217-bat"

#define REG_CHIP_ID             0x00
#define REG_VCELL_H             0x02
#define REG_VCELL_L             0x03
#define REG_SOC_INT             0x04
#define REG_SOC_DECIMAL         0x05
#define REG_TEMP                0x06
#define REG_MODE_CONFIG         0x08
#define REG_GPIO_CONFIG         0x0A
#define REG_SOC_ALERT           0x0B
#define REG_CURRENT_H           0x0E
#define REG_CURRENT_L           0x0F
#define REG_BATINFO             0x10
#define REG_USER                0xA2
#define REG_CYCLE               0xA4
#define REG_SOH                 0xA6
#define REG_STB_CURRENT_H       0xA8
#define REG_STB_CURRENT_L       0xA9
#define MODE_SLEEP              0x30
#define MODE_NORMAL             0x00
#define MODE_DEFAULT            0xF0
#define CONFIG_UPDATE_FLG       0x80
#define CHIP_ID                 0xA0
#define REG_VERSION             0xA3

#define GPIO_CONFIG_MIN_TEMP        (0x00 << 4)
#define GPIO_CONFIG_MAX_TEMP        (0x00 << 5)
#define GPIO_CONFIG_SOC_CHANGE      (0x00 << 6)
#define GPIO_CONFIG_MIN_TEMP_MARK   (0x01 << 4)
#define GPIO_CONFIG_MAX_TEMP_MARK   (0x01 << 5)
#define GPIO_CONFIG_SOC_CHANGE_MARK (0x01 << 6)
#define ATHD                         0x0 /* 0x7F */
#define DEFINED_MAX_TEMP             450
#define DEFINED_MIN_TEMP             0

#define DESIGN_CAPACITY              4000
#define CWFG_NAME                    "cw2217"
#define SIZE_BATINFO                 80
#define UI_FULL                      100
#define CW2217_TEMP_ABR_LOW         (-400)
#define CW2217_TEMP_ABR_HIGH        (800)
#define CW2217_CAPACITY_TH           7
#define HUNDRED                     100
#define THOUSAND                    1000
#define READ_BAT_INFO_RETRY_TIME    25
#define READ_COUL_INT_RETRY_TIME    30
#define CW_INIT_RETRY_TIME          3
#define QUEUE_DELAYED_WORK_TIME     8000
#define NTC_PARA_LEVEL              20

struct cw_battery {
	struct i2c_client *client;
	struct device *dev;
	struct workqueue_struct *cwfg_workqueue;
	struct delayed_work battery_delay_work;
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 1, 0)
	struct power_supply cw_bat;
#else
	struct power_supply *cw_bat;
#endif
	/* User set */
	unsigned int design_capacity;
	/* IC value */
	int chip_id;
	int voltage;
	long curr;
	int capacity;
	int temp;
	int fg_para_version;
	int rsense;
	int ntc_compensation_is;
	struct compensation_para ntc_temp_compensation_para[NTC_PARA_LEVEL];
};

struct cw2217_fg_display_data {
	int temp;
	int vbat;
	int ibat;
	int avg_ibat;
	int rm;
	int soc;
	int fcc;
};

enum ntc_temp_compensation_para_info {
	NTC_PARA_ICHG = 0,
	NTC_PARA_VALUE,
	NTC_PARA_TOTAL,
};

#endif /* _CW2217_FG_H_ */
