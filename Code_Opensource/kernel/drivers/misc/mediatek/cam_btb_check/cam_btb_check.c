/*
 * cam_btb_check.c
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

#include "cam_btb_check.h"

#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/regulator/consumer.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/of_gpio.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/pinctrl/consumer.h>
#include <securec.h>
#include <cam_dmd_util.h>

#define MAX_CAMERAS 5
#define MAX_BTB_CHECK_GPIO_PER_CAM 4

enum PULL_TYPE{
	GPIO_PULL_UP = 0,
	GPIO_PULL_DOWN,
};

static unsigned int \
	g_gpio_list[MAX_CAMERAS][MAX_BTB_CHECK_GPIO_PER_CAM];

static struct cam_btb_pinctrl_info g_btb_pctrl[MAX_CAMERAS];

static int check_cable(unsigned int cable_gpio)
{
	int ret;

	ret = gpio_request(cable_gpio, NULL);
	if (ret) {
		pr_err("cable gpio gpio_request fail");
	}
	ret = gpio_get_value(cable_gpio);

	gpio_free(cable_gpio);

	pr_info("cable gpio %u ret=%d", cable_gpio, ret);

	return ret;
}

static void cam_btb_gpio_check(unsigned int sensor_id, enum PULL_TYPE type,
	int *gpio_value_table, int len)
{
	int i;
	int ret;
	struct cam_btb_pinctrl_info *p_btb_pctrl = g_btb_pctrl + sensor_id;

	if (IS_ERR_OR_NULL(p_btb_pctrl)) {
		pr_err("cam_btb_pinctrl_info is NULL");
		return;
	}

	if (IS_ERR_OR_NULL(p_btb_pctrl->pinctrl) ||
		IS_ERR_OR_NULL(p_btb_pctrl->gpio_state_active) ||
		IS_ERR_OR_NULL(p_btb_pctrl->gpio_state_suspend)) {
		pr_err("cannot set pin state");
		return;
	}

	if (type == GPIO_PULL_UP)
		ret = pinctrl_select_state(p_btb_pctrl->pinctrl,
			p_btb_pctrl->gpio_state_active);
	else
		ret = pinctrl_select_state(p_btb_pctrl->pinctrl,
			p_btb_pctrl->gpio_state_suspend);

	if (ret) {
		pr_err("cannot set pin state:%d", type);
		return;
	}

	for (i = 0; i < len; i++) {
		if (g_gpio_list[sensor_id][i])
			gpio_value_table[i] = check_cable(g_gpio_list[sensor_id][i]);
	}

	return;
}

static int btb_pinctrl_init(unsigned int sensor_id, struct device *dev)
{
	struct cam_btb_pinctrl_info *p_btb_ctrl = g_btb_pctrl + sensor_id;

	if (IS_ERR_OR_NULL(dev)) {
		pr_err("device NULL");
		return -EINVAL;
	}

	if (IS_ERR_OR_NULL(p_btb_ctrl)) {
		pr_err("cam_btb_pinctrl_info is NULL");
		return -EINVAL;
	}

	p_btb_ctrl->pinctrl = devm_pinctrl_get(dev);
	if (IS_ERR_OR_NULL(p_btb_ctrl->pinctrl)) {
		pr_err("Getting pinctrl handle failed");
		return -EINVAL;
	}

	p_btb_ctrl->gpio_state_active =
		pinctrl_lookup_state(p_btb_ctrl->pinctrl,
				"cam_btb_default");
	if (IS_ERR_OR_NULL(p_btb_ctrl->gpio_state_active)) {
		pr_err(
			"Failed to get the active state pinctrl handle");
		return -EINVAL;
	}

	p_btb_ctrl->gpio_state_suspend
		= pinctrl_lookup_state(p_btb_ctrl->pinctrl,
				"cam_btb_suspend");
	if (IS_ERR_OR_NULL(p_btb_ctrl->gpio_state_suspend)) {
		pr_err(
			"Failed to get the suspend state pinctrl handle");
		return -EINVAL;
	}

	return 0;
}

void check_camera_btb_gpio_info(unsigned int sensor_id)
{
	int i = 0;
	int gpio_value_pull_up[MAX_BTB_CHECK_GPIO_PER_CAM] = {0};
	int gpio_value_pull_down[MAX_BTB_CHECK_GPIO_PER_CAM] = {0};

	pr_err("enter check_camera_btb_gpio_info");
	if (sensor_id >= MAX_CAMERAS) {
		pr_err("sensor_id exceed the normal range!");
		return;
	}

	if (g_gpio_list[sensor_id][0] == 0) {
		pr_err("normal exit, no such gpio");
		return;
	}

	cam_btb_gpio_check(sensor_id, GPIO_PULL_UP, gpio_value_pull_up,
		MAX_BTB_CHECK_GPIO_PER_CAM);
	cam_btb_gpio_check(sensor_id, GPIO_PULL_DOWN, gpio_value_pull_down,
		MAX_BTB_CHECK_GPIO_PER_CAM);

	for (i = 0; i < MAX_BTB_CHECK_GPIO_PER_CAM; i++) {
		if (g_gpio_list[sensor_id][i]) {
			if (gpio_value_pull_up[i] != gpio_value_pull_down[i]) {
				pr_err("cameraId: %d btb cheeck failed", sensor_id);
				camkit_hiview_report_id(DSM_CAMERA_I2C_ERR, sensor_id,
					BTB_GPIO_CHECK_FAILED);
				return;
			}
		}
	}
	return;
}
EXPORT_SYMBOL(check_camera_btb_gpio_info);

static void cam_btb_check_fill_subdev_data(struct device_node *node)
{
	int ret;
	unsigned int sensor_id;
	int gpio_count;
	int gpio_value;
	int i = 0;
	int gpio_list_lens = 0;
	struct platform_device *sub_dev = NULL;

	sub_dev = of_find_device_by_node(node);
	if (IS_ERR_OR_NULL(sub_dev)) {
		pr_err("sub device not found");
		return;
	}

	ret = of_property_read_u32(node, "sensor-id", &sensor_id);
	if (ret < 0) {
		pr_err("get root sensor-id fail!");
		return;
	}
	if (sensor_id >= MAX_CAMERAS) {
		pr_err("get root sensor-id exceed the normal range!");
		return;
	}

	gpio_count = of_gpio_named_count(node, "cam-btb-gpios");
	if (gpio_count > MAX_BTB_CHECK_GPIO_PER_CAM) {
		pr_err("cam %d get gpio_count max!", sensor_id);
		gpio_count = MAX_BTB_CHECK_GPIO_PER_CAM;
	}

	for (i = 0; i < gpio_count; i++) {
		gpio_value = of_get_named_gpio(node, "cam-btb-gpios", i);
		if (gpio_value > 0) {
			g_gpio_list[sensor_id][i] = gpio_value;
			pr_info("cam %d gpio[%d] is %d", sensor_id, i,
				g_gpio_list[sensor_id][i]);
			gpio_list_lens++;
		}
	}

	if (!gpio_list_lens) {
		return;
	}

	ret = btb_pinctrl_init(sensor_id, &sub_dev->dev);
	if (ret != 0) {
		pr_err("btb pinctrl init failed!");
		return;
	}
}

static void cam_btb_check_driver_parse_dt(
	struct cam_btb_check_ctrl *p_btb_ctrl)
{
	int ret;

	struct device_node *root = p_btb_ctrl->of_node;
	struct device_node *child = NULL;

	if (!root) {
		pr_err("root is NULL");
		return;
	}

	ret = of_platform_populate(p_btb_ctrl->of_node, NULL, NULL,
		p_btb_ctrl->dev);
	if (ret)
		pr_err("failed to add sub device, ret=%d", ret);

	for_each_child_of_node(root, child)
		cam_btb_check_fill_subdev_data(child);

	return;
}

static int cam_btb_check_driver_probe(
	struct platform_device *pdev)
{
	int rc = 0;
	struct cam_btb_check_ctrl *cam_btb_dev_ctrl = NULL;

	pr_info("cam_btb_check_driver_probe");

	/* Create btb control structure */
	cam_btb_dev_ctrl = devm_kzalloc(&pdev->dev,
		sizeof(struct cam_btb_check_ctrl), GFP_KERNEL);
	if (IS_ERR_OR_NULL(cam_btb_dev_ctrl))
		return -1;

	cam_btb_dev_ctrl->pdev = pdev;
	cam_btb_dev_ctrl->dev = &pdev->dev;
	cam_btb_dev_ctrl->of_node = pdev->dev.of_node;

	cam_btb_check_driver_parse_dt(cam_btb_dev_ctrl);

	platform_set_drvdata(pdev, cam_btb_dev_ctrl);

	return rc;
}

static int cam_btb_check_driver_remove(struct platform_device *pdev)
{
	struct cam_btb_check_ctrl *dev_ctrl = NULL;

	dev_ctrl = platform_get_drvdata(pdev);
	if (IS_ERR_OR_NULL(dev_ctrl)) {
		pr_err("cam btk check is NULL");
		return -1;
	}

	pr_info("remove cam btb check platform driver");

	platform_set_drvdata(pdev, NULL);
	devm_kfree(&pdev->dev, dev_ctrl);

	return 0;
}

static const struct of_device_id cam_btb_check_driver_dt_match[] = {
	{.compatible = "camera,btb_check"},
	{}
};

MODULE_DEVICE_TABLE(of, cam_btb_check_driver_dt_match);

static struct platform_driver cam_btb_check_driver = {
	.probe = cam_btb_check_driver_probe,
	.driver = {
		.name = "camera,btb_check",
		.owner = THIS_MODULE,
		.of_match_table = cam_btb_check_driver_dt_match,
		.suppress_bind_attrs = true,
	},
	.remove = cam_btb_check_driver_remove,
};

static int __init cam_btb_check_driver_init(void)
{
	int rc = 0;

	pr_info("cam_btb_check_driver_init");
	rc = platform_driver_register(&cam_btb_check_driver);
	if (rc < 0) {
		pr_err("platform_driver_register Failed: rc = %d", rc);
		return rc;
	}

	return rc;
}

static void __exit cam_btb_check_driver_exit(void)
{
	platform_driver_unregister(&cam_btb_check_driver);
}

module_init(cam_btb_check_driver_init);
module_exit(cam_btb_check_driver_exit);

MODULE_DESCRIPTION("cam btb check driver");
MODULE_LICENSE("GPL v2");