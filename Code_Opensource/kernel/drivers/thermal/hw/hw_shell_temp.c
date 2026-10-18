/*
 * hw_shell_temp.c
 *
 * shell temp calculation
 *
 * Copyright (c) 2017-2020 Huawei Technologies Co., Ltd.
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

#include <linux/module.h>
#include <linux/err.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/device.h>
#include <asm/page.h>
#include <linux/thermal.h>

#define CREATE_TRACE_POINTS
#define DEFAULT_SHELL_TEMP	25

#include <securec.h>

struct temperature_node_t {
	struct device *device;
	int ambient;
};

struct hw_thermal_class {
	struct class *thermal_class;
	struct temperature_node_t temperature_node;
};

struct hw_thermal_class hw_thermal_info;

struct hw_shell_t {
	int temp;
	struct thermal_zone_device *tz_dev;
};

static ssize_t
hw_shell_show_temp(struct device *dev, struct device_attribute *devattr,
		char *buf)
{
	struct hw_shell_t *hw_shell = NULL;

	if (dev == NULL || devattr == NULL || buf == NULL)
		return 0;

	if (dev->driver_data == NULL)
		return 0;

	hw_shell = dev->driver_data;

	return snprintf_s(buf, PAGE_SIZE, PAGE_SIZE - 1, "%d\n",
			  hw_shell->temp);
}

#define MIN_TEMPERATURE (-40000)
#define MAX_TEMPERATURE 145000
#define DECIMAL 10
static ssize_t
hw_shell_store_temp(struct device *dev, struct device_attribute *devattr,
			const char *buf, size_t count)
{
	int temp;
	struct platform_device *pdev = NULL;
	struct hw_shell_t *hw_shell = NULL;

	if (dev == NULL || devattr == NULL || buf == NULL)
		return 0;

	if (kstrtoint(buf, DECIMAL, &temp) != 0) {
		pr_err("%s Invalid input para\n", __func__);
		return -EINVAL;
	}

	if (temp < MIN_TEMPERATURE || temp > MAX_TEMPERATURE)
		return -EINVAL;

	pdev = container_of(dev, struct platform_device, dev);
	hw_shell = platform_get_drvdata(pdev);

	hw_shell->temp = temp;
	return (ssize_t)count;
}
static DEVICE_ATTR(temp, S_IWUSR | S_IRUGO,
		hw_shell_show_temp, hw_shell_store_temp);

static struct attribute *hw_shell_attributes[] = {
	&dev_attr_temp.attr,
	NULL
};

static struct attribute_group hw_shell_attribute_group = {
	.attrs = hw_shell_attributes,
};

static BLOCKING_NOTIFIER_HEAD(ambient_chain_head);
int register_ambient_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&ambient_chain_head, nb);
}
EXPORT_SYMBOL_GPL(register_ambient_notifier);

int unregister_ambient_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&ambient_chain_head, nb);
}
EXPORT_SYMBOL_GPL(unregister_ambient_notifier);

int ambient_notifier_call_chain(int val)
{
	return blocking_notifier_call_chain(&ambient_chain_head, 0, &val);
}

#define SHOW_TEMP(temp_name)					\
static ssize_t show_##temp_name					\
(struct device *dev, struct device_attribute *attr, char *buf)	\
{								\
	if (dev == NULL || attr == NULL || buf == NULL)		\
		return 0;					\
								\
	return snprintf_s(buf, PAGE_SIZE, PAGE_SIZE - 1, "%d\n", \
			  (int)hw_thermal_info.temperature_node.temp_name); \
}

SHOW_TEMP(ambient);

#define STORE_TEMP(temp_name)					\
static ssize_t store_##temp_name				\
(struct device *dev, struct device_attribute *attr,		\
 const char *buf, size_t count)					\
{								\
	int temp_name = 0;					\
	int prev_temp;						\
								\
	if (dev == NULL || attr == NULL || buf == NULL)		\
		return 0;					\
								\
	if (kstrtoint(buf, 10, &temp_name)) /*lint !e64*/	\
		return -EINVAL;					\
								\
	prev_temp = hw_thermal_info.temperature_node.temp_name;	\
	hw_thermal_info.temperature_node.temp_name = temp_name;	\
	if (temp_name != prev_temp)				\
		ambient_notifier_call_chain(temp_name);		\
								\
	return (ssize_t)count;					\
}

STORE_TEMP(ambient);

/*lint -e84 -e846 -e514 -e778 -e866 -esym(84,846,514,778,866,*)*/
#define TEMP_ATTR_RW(temp_name)				\
static DEVICE_ATTR(temp_name, S_IWUSR | S_IRUSR | S_IRGRP | S_IWGRP,	\
		   show_##temp_name, store_##temp_name)

TEMP_ATTR_RW(ambient);
/*lint -e84 -e846 -e514 -e778 -e866 +esym(84,846,514,778,866,*)*/

int hw_get_shell_temp(struct thermal_zone_device *thermal, int *temp)
{
	struct hw_shell_t *hw_shell = thermal->devdata;

	if (hw_shell == NULL || temp == NULL)
		return -EINVAL;

	*temp = hw_shell->temp;

	return 0;
}

/*lint -e785*/
struct thermal_zone_device_ops shell_thermal_zone_ops = {
	.get_temp = hw_get_shell_temp,
};

/*lint +e785*/

static int create_file_node(struct platform_device *pdev, struct attribute_group *attr)
{
	struct device *dev = &pdev->dev;
	struct device_node *dev_node = dev->of_node;
	int ret;
	ret = sysfs_create_link(&hw_thermal_info.temperature_node.device->kobj, &pdev->dev.kobj, dev_node->name);
	if (ret != 0) {
		pr_err("%s: create hw_thermal device file error: %d\n", dev_node->name, ret);
		return -EINVAL;
	}
	ret = sysfs_create_group(&pdev->dev.kobj, attr);
	if (ret != 0) {
		pr_err("%s: create shell file error: %d\n", dev_node->name, ret);
		sysfs_remove_link(&hw_thermal_info.temperature_node.device->kobj, dev_node->name);
		return -EINVAL;
	}
	return 0;
}

static int hw_shell_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *dev_node = dev->of_node;
	int ret;
	struct hw_shell_t *hw_shell = NULL;

	if (!of_device_is_available(dev_node)) {
		dev_err(dev, "HW shell dev not found\n");
		return -ENODEV;
	}

	hw_shell = kzalloc(sizeof(struct hw_shell_t) , GFP_KERNEL);
	if (hw_shell == NULL) {
		pr_err("no enough memory\n");
		return -ENOMEM;
	}

	hw_shell->tz_dev
		= thermal_zone_device_register(dev_node->name, 0, 0, hw_shell,
					       &shell_thermal_zone_ops, NULL, 0, 0);
	if (IS_ERR(hw_shell->tz_dev)) {
		dev_err(dev, "register thermal zone for shell failed.\n");
		ret = -ENODEV;
		goto free_mem;
	}

	hw_shell->temp = DEFAULT_SHELL_TEMP * 1000; 
	pr_info("%s: temp %d\n", dev_node->name, hw_shell->temp);

	platform_set_drvdata(pdev, hw_shell);

	ret = create_file_node(pdev, &hw_shell_attribute_group);
	return ret; /*lint !e429*/

free_mem:
	kfree(hw_shell);
	return ret;
}

static int hw_shell_remove(struct platform_device *pdev)
{
	struct hw_shell_t *hw_shell = platform_get_drvdata(pdev);

	if (hw_shell != NULL) {
		platform_set_drvdata(pdev, NULL);
		thermal_zone_device_unregister(hw_shell->tz_dev);
		kfree(hw_shell);
	}

	return 0;
}

/*lint -e785*/
static struct of_device_id hw_shell_of_match[] = {
	{ .compatible = "hw,shell-temp" },
	{},
};

/*lint +e785*/
MODULE_DEVICE_TABLE(of, hw_shell_of_match);

int shell_temp_pm_resume(struct platform_device *pdev)
{
	struct hw_shell_t *hw_shell = NULL;

	pr_info("%s+\n", __func__);
	hw_shell = platform_get_drvdata(pdev);

	if (hw_shell != NULL) {
		hw_shell->temp = DEFAULT_SHELL_TEMP * 1000;
		pr_info("%s: temp %d\n", hw_shell->tz_dev->type, hw_shell->temp);
	}
	pr_info("%s-\n", __func__);

	return 0;
}

/*lint -e64 -e785 -esym(64,785,*)*/
static struct platform_driver hw_shell_platdrv = {
	.driver = {
		.name = "hw-shell-temp",
		.owner = THIS_MODULE,
		.of_match_table = hw_shell_of_match,
	},
	.probe = hw_shell_probe,
	.remove = hw_shell_remove,
	.resume = shell_temp_pm_resume,
};

/*lint -e64 -e785 +esym(64,785,*)*/
#ifdef CONFIG_HW_IPA_THERMAL
extern struct class *ipa_get_thermal_class(void);
#endif

static int __init hw_shell_init(void)
{
	int ret;
	struct class *class = NULL;

	/* create huawei thermal class */
#ifdef CONFIG_HW_IPA_THERMAL
	class = ipa_get_thermal_class();
#endif
	if (!class) {
		hw_thermal_info.thermal_class = class_create(THIS_MODULE, "hw_thermal"); /*lint !e64*/
		if (IS_ERR(hw_thermal_info.thermal_class)) {
			pr_err("Huawei thermal class create error\n");
			return PTR_ERR(hw_thermal_info.thermal_class);
		}
	} else {
		hw_thermal_info.thermal_class = class;
	}

	/* create device "temp" */
	hw_thermal_info.temperature_node.device =
		device_create(hw_thermal_info.thermal_class, NULL, 0, NULL, "temp");
	if (IS_ERR(hw_thermal_info.temperature_node.device)) {
		pr_err("hw_thermal:temperature_node device create error\n");
		if (!class)
			class_destroy(hw_thermal_info.thermal_class);
		hw_thermal_info.thermal_class = NULL;
		return PTR_ERR(hw_thermal_info.temperature_node.device);
	}
	/* create an ambient node for thermal-daemon. */
	ret = device_create_file(hw_thermal_info.temperature_node.device,
				 &dev_attr_ambient);
	if (ret != 0) {
		pr_err("hw_thermal:ambient node create error\n");
		device_destroy(hw_thermal_info.thermal_class, 0);
		if (!class)
			class_destroy(hw_thermal_info.thermal_class);
		hw_thermal_info.thermal_class = NULL;
		return ret;
	}

	return platform_driver_register(&hw_shell_platdrv); /*lint !e64*/
}

static void __exit hw_shell_exit(void)
{
	if (hw_thermal_info.thermal_class != NULL) {
		device_destroy(hw_thermal_info.thermal_class, 0);
#ifdef CONFIG_HW_IPA_THERMAL
		if (!ipa_get_thermal_class())
			class_destroy(hw_thermal_info.thermal_class);
#else
		class_destroy(hw_thermal_info.thermal_class);
#endif
	}
	platform_driver_unregister(&hw_shell_platdrv);
}

/*lint -e528 -esym(528,*)*/
module_init(hw_shell_init);
module_exit(hw_shell_exit);
/*lint -e528 +esym(528,*)*/

/*lint -e753 -esym(753,*)*/
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("thermal shell temp module driver");
MODULE_AUTHOR("Huawei Technologies Co., Ltd.");
/*lint -e753 +esym(753,*)*/
