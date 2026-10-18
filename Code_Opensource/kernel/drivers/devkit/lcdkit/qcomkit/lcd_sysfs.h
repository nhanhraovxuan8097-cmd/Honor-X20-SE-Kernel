/*
 * lcd_sysfs.h
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
#ifndef LCD_SYSFS_H
#define LCD_SYSFS_H

#define BUF_MAX 200

/* lcd fps scence */
#define FPS_60_SCENCE		60
#define FPS_90_SCENCE		90
#define FPS_120_SCENCE		120
#define ORDER_DELAY 200

/* lcd fps index */
enum {
	FPS_60_INDEX = 0,
	FPS_90_INDEX = 1,
	FPS_120_INDEX = 2,
};

enum alpm_mode {
	ALPM_HIGH_LIGHT = 1,
	ALPM_LOW_LIGHT = 3,
	ALPM_MIDDLE_LIGHT = 6,
};

/* oem info */
#define OEM_INFO_SIZE_MAX 500
#define OEM_INFO_BLOCK_NUM 1
/* 2d barcode */
#define BARCODE_BLOCK_NUM 3
#define BARCODE_BLOCK_LEN 16
/* sn code */
#define LCD_SN_CODE_LENGTH 54
#define WORK_DELAY_TIME_READ_SN 20000
#define DELAY_READ_SN 2000
#define READ_SN_MAX_COUNT 3
#define INVALID_TYPE (-1)

enum oem_type {
	PROJECT_ID_TYPE,
	BARCODE_2D_TYPE,
	MAX_OEM_TYPE,
};

int lcd_create_sysfs(struct kobject *obj);
int lcd_create_fact_sysfs(struct kobject *obj);
#endif
