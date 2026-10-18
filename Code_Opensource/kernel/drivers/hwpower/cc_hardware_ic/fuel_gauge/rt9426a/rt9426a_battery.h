/*
 * rt9426a_battery.h
 *
 * driver for rt9426a battery fuel gauge
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

#ifndef _RT9426A_BATTERY_H
#define _RT9426A_BATTERY_H

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/power_supply.h>
#include <linux/delay.h>
#include <linux/of_gpio.h>
#include <linux/interrupt.h>
#include <linux/uaccess.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/regmap.h>
#include <linux/workqueue.h>

#ifdef CONFIG_HUAWEI_POWER_EMBEDDED_ISOLATION
#include <chipset_common/hwpower/power_algorithm.h>
#include <chipset_common/hwpower/power_event_ne.h>
#include <chipset_common/hwpower/power_log.h>
#include <chipset_common/hwpower/coul_interface.h>
#include <chipset_common/hwpower/battery_model_public.h>
#include <chipset_common/hwpower/power_printk.h>
#include <chipset_common/hwpower/coul_calibration.h>
#include <chipset_common/hwpower/power_dts.h>
#endif /* CONFIG_HUAWEI_POWER_EMBEDDED_ISOLATION */

#define RT9426A_DRIVER_VER		0x0001

#define RT9426A_Unseal_Key		0x12345678
#define RT9426A_DEVICE_ID		0x426A
#define RT9426A_EXTREG_SIZE		224
#define OCV_INDEX_MIN                   0
#define OCV_INDEX_MAX                   4

#define RT9426A_REG_CNTL		0x00
#define RT9426A_REG_RSVD		0x02
#define RT9426A_REG_CURR		0x04
#define RT9426A_REG_TEMP		0x06
#define RT9426A_REG_VBAT		0x08
#define RT9426A_REG_FLAG1		0x0A
#define RT9426A_REG_FLAG2		0x0C
#define RT9426A_REG_DEVICE_ID		0x0E
#define RT9426A_REG_RM			0x10
#define RT9426A_REG_FCC			0x12
#define RT9426A_REG_AI			0x14
#define RT9426A_REG_BCCOMP		0x1A
#define RT9426A_REG_DUMMY		0x1E
#define RT9426A_REG_INTT		0x28
#define RT9426A_REG_CYC			0x2A
#define RT9426A_REG_SOC			0x2C
#define RT9426A_REG_SOH			0x2E
#define RT9426A_REG_FLAG3		0x30
#define RT9426A_REG_IRQ			0x36
#define RT9426A_REG_ADV			0x3A
#define RT9426A_REG_DC			0x3C
#define RT9426A_REG_BDCNTL		0x3E
#define RT9426A_REG_SWINDOW1		0x40
#define RT9426A_REG_SWINDOW2		0x42
#define RT9426A_REG_SWINDOW3		0x44
#define RT9426A_REG_SWINDOW4		0x46
#define RT9426A_REG_SWINDOW5		0x48
#define RT9426A_REG_SWINDOW6		0x4A
#define RT9426A_REG_SWINDOW7		0x4C
#define RT9426A_REG_SWINDOW8		0x4E
#define RT9426A_REG_SWINDOW9		0x50
#define RT9426A_REG_OCV			0x62
#define RT9426A_REG_AV			0x64
#define RT9426A_REG_AT			0x66
#define RT9426A_REG_TOTAL_CHKSUM	0x68
#define RT9426A_REG_RSVD2		0x7C

#define RT9426A_BATPRES_MASK		0x0040
#define RT9426A_RI_MASK			0x0100
#define RT9426A_BATEXIST_FLAG_MASK	0x8000
#define RT9426A_USR_TBL_USED_MASK	0x0800
#define RT9426A_CSCOMP1_OCV_MASK	0x0300
#define RT9426A_UNSEAL_MASK		0x0003
#define RT9426A_UNSEAL_STATUS		0x0001
#define RT9426A_GAUGE_BUSY_MASK		0x0008

#define RT9426A_SMOOTH_POLL		20
#define RT9426A_NORMAL_POLL		30
#define RT9426A_SOCALRT_MASK		0x20
#define RT9426A_SOCL_SHFT		0
#define RT9426A_SOCL_MASK		0x1F
#define RT9426A_SOCL_MAX		32
#define RT9426A_SOCL_MIN		1

#define RT9426A_RDY_MASK		0x0080

#define RT9426A_UNSEAL_PASS		0
#define RT9426A_UNSEAL_FAIL		1
#define RT9426A_PAGE_0			0
#define RT9426A_PAGE_1			1
#define RT9426A_PAGE_2			2
#define RT9426A_PAGE_3			3
#define RT9426A_PAGE_4			4
#define RT9426A_PAGE_5			5
#define RT9426A_PAGE_6			6
#define RT9426A_PAGE_7			7
#define RT9426A_PAGE_8			8
#define RT9426A_PAGE_9			9
#define RT9426A_PAGE_10			10
#define RT9426A_PAGE_11			11
#define RT9426A_PAGE_12			12
#define RT9426A_PAGE_13			13
#define RT9426A_PAGE_14			14
#define RT9426A_PAGE_15			15

#define RT9426A_TOTAL_CHKSUM_CMD	0x9A12
#define RT9426A_WPAGE_CMD		0x6550
#define RT9426A_SEAL_CMD		0x0020

/* for calibration */
#define RT9426A_CALI_ENTR_CMD		0x0081
#define RT9426A_CALI_EXIT_CMD		0x0080
#define RT9426A_CURR_CONVERT_CMD	0x0009
#define RT9426A_VOLT_CONVERT_CMD	0x008C
#define RT9426A_CALI_MODE_MASK		0x1000
#define RT9426A_SYS_TICK_ON_CMD		0xBBA1
#define RT9426A_SYS_TICK_OFF_CMD	0xBBA0

#define RT9426A_CALI_MODE_PASS		0
#define RT9426A_CALI_MODE_FAIL		1

/* for Enter/Exit Shutdown */
#define RT9426A_SHDN_MASK		0x4000
#define RT9426A_SHDN_ENTR_CMD		0x64AA
#define RT9426A_SHDN_EXIT_CMD		0x6400

#define TA_IS_CONNECTED			1
#define TA_IS_DISCONNECTED		0
#define RT9426A_FD_TBL_IDX		4
#define RT9426A_FD_DATA_IDX		10
#define RT9426A_FD_BASE			2500
/* unit:0.01mR ; 50 x 0.01 = 0.5mR */
#define RT9426A_NEW_RS_UNIT		50

/* for Handling of Cycle Cnt & BCCOMP */
#define RT9426A_SET_CYCCNT_KEY		0xCC01
/* for checking result of writing ocv */
#define RT9426A_WRITE_OCV_PASS		0
#define RT9426A_WRITE_OCV_FAIL		(-1)
#define RT9426A_IDX_OF_OCV_CKSUM	76

#ifdef CONFIG_HUAWEI_POWER_EMBEDDED_ISOLATION
#define RT9426A_READ_PAGE_CMD		0x6500
#define RT9426A_WRITE_PAGE_CMD		0x6550
#define RT9426A_ENTR_SLP_CMD		0x74AA
#define RT9426A_EXIT_SLP_CMD		0x7400
#define RT9426A_FC_VTH_DEFAULT_VAL	0x0078
#define RT9426A_EXTEND_REG		0x78
#define RT9426A_CHG_CURR_VAL		500
#define RT9426A_FULL_CAPCACITY		100
#define RT9426A_CAPACITY_TH		7
#define RT9426A_GAIN_DEFAULT_VAL	128
#define RT9426A_GAIN_BASE_VAL		512
#define RT9426A_COUL_DEFAULT_VAL	1000000
#define RT9426A_GAIN_DEFAULT_VAL	128
#define RT9426A_GAIN_BASE_VAL		512
#define RT9426A_TBATICAL_MIN_A		752000
#define RT9426A_TBATICAL_MAX_A		1246000
#define RT9426A_TBATCAL_MIN_A		752000
#define RT9426A_TBATCAL_MAX_A		1246000
#define NTC_PARA_LEVEL			20
#define COMPENSATION_THRESHOLD		200    /* 20oC */
#define RT9426A_TEMP_ABR_LOW		(-400)
#define RT9426A_TEMP_ABR_HIGH		800
#define LV_SMOOTH_V_TH			3250   /* 3250mV */
#define LV_SMOOTH_S_TH			4      /* 4% */
#define LV_SMOOTH_T_MIN			100    /* 10oC */
#define LV_SMOOTH_T_MAX			500    /* 50oC */
#define LV_SMOOTH_I_TH			(-400) /* -400*2.5 = -1000 mA */
#define RT9426A_HIGH_BYTE_MASK		0xFF00
#define RT9426A_LOW_BYTE_MASK		0x00FF
#define RT9426A_BYTE_BITS		8
#define RT9426A_SHUTDOWN_RETRY_TIMES	5
#define RT9426A_DT_OFFSET_PARA_SIZE	3
#define RT9426A_DT_EXTREG_PARA_SIZE 	3
#define RT9426A_BAT_TEMP_VAL		250
#define RT9426A_BAT_VOLT_VAL		3800
#define RT9426A_BAT_CURR_VAL		1000
#define RT9426A_DESIGN_CAP_VAL		2000
#define RT9426A_DESIGN_FCC_VAL		2000
#define RT9426A_VOLT_SOURCE_NONE	0
#define RT9426A_VOLT_SOURCE_VBAT	1
#define RT9426A_VOLT_SOURCE_OCV		2
#define RT9426A_VOLT_SOURCE_AV		3
#define RT9426A_TEMP_SOURCE_NONE	0
#define RT9426A_TEMP_SOURCE_TEMP	1
#define RT9426A_TEMP_SOURCE_INIT	2
#define RT9426A_TEMP_SOURCE_AT		3
#define RT9426A_CURR_SOURCE_NONE	0
#define RT9426A_CURR_SOURCE_CURR	1
#define RT9426A_CURR_SOURCE_AI		2
#define RT9426A_OFFSET_INTERPLO_SIZE	2
#define RT9426A_SOC_OFFSET_SIZE		2
#define RT9426A_ICC_THRESHOLD_SIZE	3
#define RT9426A_OCV_ROW_SIZE		10
#define RT9426A_OCV_COL_SIZE		8
#define RT9426A_EXTREG_TABLE_LENTH	16
#define RT9426A_EXTREG_TABLE_SIZE	14
#define RT9426A_BLOCK_PAGE_SIZE		8
#define RT9426A_DTSI_VER_SIZE		2
#define RT9426A_COUL_OFFSET_VAL     1000
#define RT9426A_TBATICAL_MIN_B      (-20000)
#define RT9426A_TBATICAL_MAX_B      20000


enum comp_offset_typs {
	FG_COMP = 0,
	SOC_OFFSET,
	EXTREG_UPDATE,
};

enum ntc_temp_compensation_para_info {
	NTC_PARA_ICHG = 0,
	NTC_PARA_VALUE,
	NTC_PARA_TOTAL,
};

enum {
	RT9426A_DISPLAY_TEMP = 0,
	RT9426A_DISPLAY_VBAT,
	RT9426A_DISPLAY_IBAT,
	RT9426A_DISPLAY_AVG_IBAT,
	RT9426A_DISPLAY_RM,
	RT9426A_DISPLAY_SOC,
	RT9426A_DISPLAY_DISIGN_FCC,
	RT9426A_DISPLAY_FCC,
};

struct rt9426a_display_data {
	int temp;
	int vbat;
	int ibat;
	int avg_ibat;
	int rm;
	int soc;
	int fcc;
};
#endif /* CONFIG_HUAWEI_POWER_EMBEDDED_ISOLATION */

struct data_point {
	union {
		int x;
		int voltage;
		int soc;
	};
	union {
		int y;
		int temperature;
	};
	union {
		int z;
		int curr;
	};
	union {
		int w;
		int offset;
	};
};

struct submask_condition {
	int x, y, z;
	int order_x, order_y, order_z;
	int xnr, ynr, znr;
	const struct data_point *mesh_src;
};

struct soc_offset_table {
	int soc_voltnr;
	int tempnr;
	struct data_point *soc_offset_data;
};

/* temperature source table */
enum {
	RT9426A_TEMP_FROM_AP,
	RT9426A_TEMP_FROM_IC,
};

struct fg_ocv_table {
	int data[RT9426A_OCV_COL_SIZE];
};

struct fg_extreg_table {
	u8 data[RT9426A_EXTREG_TABLE_LENTH];
};

struct rt9426a_platform_data {
	u32 dtsi_version[RT9426A_DTSI_VER_SIZE];
	u32 para_version;
	int soc_offset_size[RT9426A_SOC_OFFSET_SIZE];
	struct soc_offset_table soc_offset;
	int offset_interpolation_order[RT9426A_OFFSET_INTERPLO_SIZE];
	struct fg_extreg_table extreg_table[RT9426A_EXTREG_TABLE_SIZE];
	int battery_type;
	char *bat_name;
	int boot_gpio;
	int chg_sts_gpio;
	int chg_inh_gpio;
	int chg_done_gpio;
	u32 temp_source;
	u32 volt_source;
	u32 curr_source;

	/* current scaling in unit of 0.01 Ohm */
	u32 rs_ic_setting;
	u32 rs_schematic;
	int smooth_soc_en;
	/* aging cv for aging_sts=0~4 */
	u32 fcc[5];
	u32 fc_vth[5];
	unsigned int ocv_table[5][10][8];
	/* update ieoc setting by dtsi for icc_sts = 0~4 */
	u32 icc_threshold[3];
	u32 ieoc_setting[4];
#ifdef CONFIG_HUAWEI_POWER_EMBEDDED_ISOLATION
	u32 rt9426a_config_ver;
	bool force_use_aux_cali_para;
	int ntc_compensation_is;
	struct compensation_para ntc_temp_compensation_para[NTC_PARA_LEVEL];
#endif /* CONFIG_HUAWEI_POWER_EMBEDDED_ISOLATION */
};

enum {
	RT9426A_INIT_SEAL_ERR = -6,
	RT9426A_INIT_DEV_ID_ERR = -5,
	RT9426A_INIT_CKSUM_ERR = -4,
	RT9426A_INIT_UNSEAL_ERR = -3,
	RT9426A_INIT_BYPASS = -2,
	RT9426A_INIT_FAIL = -1,
	RT9426A_INIT_PASS = 0,
};
#endif /* _RT9426A_BATTERY_H */
