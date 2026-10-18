/*
 * lcd_factory.c
 *
 * lcd factory test function for lcd driver
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
#ifndef LCD_FACTORY_H
#define LCD_FACTORY_H

#define LCD_CMD_NAME_MAX 100
#define MAX_REG_READ_COUNT 4

struct lcd_checkreg {
	bool enabled;
	int expect_count;
	uint8_t *expect_val;
};

struct lcd_fact_info {
	/* test config */
	char lcd_cmd_now[LCD_CMD_NAME_MAX];
	int pt_flag;
	int pt_reset_enable;
	struct lcd_checkreg checkreg;
};

#endif
