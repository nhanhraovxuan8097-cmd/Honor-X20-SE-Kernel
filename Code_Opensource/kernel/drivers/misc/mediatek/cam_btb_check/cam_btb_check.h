/*
 * camkit_btb_check.h
 *
 * Copyright (c) 2021-2021 Honor Technologies Co., Ltd.
 *
 * camera btb check driver
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
#ifndef CAM_BTB_CHECK_H
#define CAM_BTB_CHECK_H

struct cam_btb_pinctrl_info {
	struct pinctrl *pinctrl;
	struct pinctrl_state *gpio_state_active;
	struct pinctrl_state *gpio_state_suspend;
};

struct cam_btb_check_ctrl {
	struct platform_device *pdev;
	struct device *dev;
	struct device_node *of_node;
};

void check_camera_btb_gpio_info(unsigned int sensor_id);

#endif // CAM_BTB_CHECK_H