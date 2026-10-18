/*
 * smpl.c
 *
 * smpl(sudden momentary power loss) error monitor driver
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

#include "smpl.h"
#include <chipset_common/hwpower/power_dsm.h>
#include <huawei_platform/power/common_module/power_platform.h>
#include <chipset_common/hwpower/power_printk.h>

#define HWLOG_TAG smpl
HWLOG_REGIST();

static struct smpl_dev *g_smpl_dev;
static unsigned int g_smpl_happened;

static int __init smpl_parse_early_cmdline(char *p)
{
	if (!p) {
		hwlog_err("cmdline is null\n");
		return 0;
	}

	hwlog_info("normal_reset_type=%s\n", p);

	/* AP_S_SMPL = 0x51  BR_POWERON_BY_SMPL = 0x26 */
	if (strstr(p, "SMPL"))
		g_smpl_happened = 1;

	hwlog_info("smpl happened=%d\n", g_smpl_happened);
	return 0;
}
early_param("normal_reset_type", smpl_parse_early_cmdline);

static void smpl_error_monitor_work(struct work_struct *work)
{
	char buf[POWER_DSM_BUF_SIZE_0128] = { 0 };

	hwlog_info("monitor_work begin\n");

	if (!g_smpl_happened)
		return;

	snprintf(buf, POWER_DSM_BUF_SIZE_0128 - 1,
		"smpl brand=%s t_bat=%d, volt=%d, soc=%d\n",
		power_platform_get_battery_brand(),
		power_platform_get_battery_temperature(),
		power_platform_get_battery_voltage(),
		power_platform_get_battery_capacity());
	power_dsm_dmd_report(POWER_DSM_SMPL, ERROR_NO_SMPL, buf);
	hwlog_info("exception happened: %s\n", buf);
}

static int smpl_probe(struct platform_device *pdev)
{
	struct smpl_dev *l_dev = NULL;

	l_dev = kzalloc(sizeof(*l_dev), GFP_KERNEL);
	if (!l_dev)
		return -ENOMEM;

	g_smpl_dev = l_dev;

	INIT_DELAYED_WORK(&l_dev->monitor_work, smpl_error_monitor_work);
	schedule_delayed_work(&l_dev->monitor_work,
		msecs_to_jiffies(DELAY_TIME_FOR_MONITOR_WORK));
	return 0;
}

static int smpl_remove(struct platform_device *pdev)
{
	if (!g_smpl_dev)
		return -ENODEV;

	kfree(g_smpl_dev);
	g_smpl_dev = NULL;
	return 0;
}

static const struct of_device_id smpl_match_table[] = {
	{
		.compatible = "huawei,smpl",
		.data = NULL,
	},
	{},
};

static struct platform_driver smpl_driver = {
	.probe = smpl_probe,
	.remove = smpl_remove,
	.driver = {
		.name = "huawei,smpl",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(smpl_match_table),
	},
};

static int __init smpl_init(void)
{
	return platform_driver_register(&smpl_driver);
}

static void __exit smpl_exit(void)
{
	platform_driver_unregister(&smpl_driver);
}

module_init(smpl_init);
module_exit(smpl_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("smpl error monitor module driver");
MODULE_AUTHOR("Huawei Technologies Co., Ltd.");
