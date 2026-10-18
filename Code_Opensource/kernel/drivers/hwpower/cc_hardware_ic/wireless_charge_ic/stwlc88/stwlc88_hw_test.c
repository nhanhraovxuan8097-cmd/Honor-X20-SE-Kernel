/*
 * stwlc88_hw_test.c
 *
 * stwlc88 hardware test driver
 *
 * Copyright (c) 2020-2020 Huawei Technologies Co., Ltd.
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

#include "stwlc88.h"

#define HWLOG_TAG wireless_stwlc88_hw_test
HWLOG_REGIST();

static int stwlc88_hw_test_pwr_good_gpio(void)
{
	wlps_control(WLPS_RX_EXT_PWR, WLPS_CTRL_ON);
	power_usleep(DT_USLEEP_10MS);
	if (!stwlc88_is_pwr_good()) {
		hwlog_err("pwr_good_gpio: failed\n");
		wlps_control(WLPS_RX_EXT_PWR, WLPS_CTRL_OFF);
		return -EINVAL;
	}
	wlps_control(WLPS_RX_EXT_PWR, WLPS_CTRL_OFF);

	hwlog_info("[pwr_good_gpio] succ\n");
	return 0;
};

static int stwlc88_hw_test_reverse_cp(int type)
{
	if (type == CHK_RVS_CP_PREV_PREPARE) {
		wireless_tx_set_tx_open_flag(true);
		wltx_activate_tx_chip();
	} else {
		wireless_tx_set_tx_open_flag(false);
	}

	return 0;
}

static struct wireless_hw_test_ops g_stwlc88_hw_test_ops = {
	.chk_pwr_good_gpio = stwlc88_hw_test_pwr_good_gpio,
	.chk_reverse_cp_prepare = stwlc88_hw_test_reverse_cp,
};

int stwlc88_hw_test_ops_register(void)
{
	return wireless_hw_test_ops_register(&g_stwlc88_hw_test_ops);
};
