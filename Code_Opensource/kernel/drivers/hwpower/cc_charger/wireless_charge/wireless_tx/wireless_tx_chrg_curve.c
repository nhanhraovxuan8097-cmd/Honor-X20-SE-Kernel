/*
 * wireless_tx_chrg_curve.c
 *
 * tx charging curve for wireless reverse charging
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

#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/workqueue.h>
#include <chipset_common/hwpower/power_printk.h>
#include <chipset_common/hwpower/wireless_protocol.h>
#include <chipset_common/hwpower/wireless_charge/wireless_tx_chrg_curve.h>

#define HWLOG_TAG wireless_tx_cc
HWLOG_REGIST();

static struct wltx_cc_dev_info *g_tx_cc_di;

static void wltx_cc_time_monitor(struct wltx_cc_dev_info *di)
{
	int i;
	int delta_time;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0))
	struct timespec64 now;
#endif

	if (di->cfg.time_alarm_level <= 0)
		return;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0))
	ktime_get_coarse_real_ts64(&now);
	delta_time = (int)(now.tv_sec - di->start_time);
#else
	delta_time = (int)(current_kernel_time().tv_sec - di->start_time);
#endif

	for (i = 0; i < di->cfg.time_alarm_level; i++) {
		if (delta_time >= di->cfg.time_alarm[i].time_th)
			break;
	}

	if (i >= di->cfg.time_alarm_level)
		return;

	wltx_update_alarm_data(&di->cfg.time_alarm[i].alarm, TX_ALARM_SRC_TIME);
}

static void wltx_cc_monitor_work(struct work_struct *work)
{
	struct wltx_cc_dev_info *di = container_of(work,
		struct wltx_cc_dev_info, mon_work.work);

	if (!di || !di->need_monitor)
		return;

	wltx_cc_time_monitor(di);
	schedule_delayed_work(&di->mon_work,
		msecs_to_jiffies(WLTX_CC_MON_INTERVAL));
}

void wltx_start_monitor_chrg_curve(struct wltx_cc_cfg *cfg,
	const unsigned int delay)
{
	struct wltx_cc_dev_info *di = g_tx_cc_di;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0))
	struct timespec64 now;
#endif
	if (!di || !cfg)
		return;

	hwlog_info("start monitor chrg_curve\n");
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0))
	ktime_get_coarse_real_ts64(&now);
#endif
	if (delayed_work_pending(&di->mon_work))
		cancel_delayed_work_sync(&di->mon_work);
	di->need_monitor = true;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0))
	di->start_time = now.tv_sec;
#else
	di->start_time = current_kernel_time().tv_sec;
#endif
	memcpy(&di->cfg, cfg, sizeof(di->cfg));
	schedule_delayed_work(&di->mon_work, msecs_to_jiffies(delay));
}

void wltx_stop_monitor_chrg_curve(void)
{
	struct wltx_cc_dev_info *di = g_tx_cc_di;

	if (!di)
		return;

	hwlog_info("stop monitor chrg_curve\n");
	di->need_monitor = false;
}

static int __init wltx_cc_init(void)
{
	struct wltx_cc_dev_info *di = NULL;

	di = kzalloc(sizeof(*di), GFP_KERNEL);
	if (!di)
		return -ENOMEM;

	g_tx_cc_di = di;

	INIT_DELAYED_WORK(&di->mon_work, wltx_cc_monitor_work);
	return 0;
}

static void __exit wltx_cc_exit(void)
{
	struct wltx_cc_dev_info *di = g_tx_cc_di;

	kfree(di);
	g_tx_cc_di = NULL;
}

device_initcall(wltx_cc_init);
module_exit(wltx_cc_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("wireless tx charge_curve driver");
MODULE_AUTHOR("Huawei Technologies Co., Ltd.");
