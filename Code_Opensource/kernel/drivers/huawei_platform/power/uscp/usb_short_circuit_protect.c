/*
 * usb_short_circuit_protect.c
 *
 * usb short circuit protect
 *
 * Copyright (c) 2019-2019 Huawei Technologies Co., Ltd.
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
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/io.h>
#include <linux/jiffies.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/version.h>
#include <linux/pm_wakeup.h>
#include <linux/timer.h>
#include <linux/hrtimer.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/thermal.h>
#include <linux/regulator/consumer.h>
#include <linux/iio/consumer.h>
#include <linux/notifier.h>
#include "mtk_auxadc.h"
#include <mt-plat/mtk_battery.h>
#include <huawei_platform/power/direct_charger/direct_charger.h>
#include <chipset_common/hwpower/power_thermalzone.h>
#include <chipset_common/hwpower/power_temp.h>
#include <chipset_common/hwpower/power_event_ne.h>
#include <chipset_common/hwpower/power_supply_interface.h>
#include <chipset_common/hwpower/power_debug.h>
#include <chipset_common/hwpower/power_printk.h>

#define HWLOG_TAG uscp_ap
HWLOG_REGIST();

#define INVALID_DELTA_TEMP         0
#define USCP_DEFAULT_CHK_CNT       (-1)
#define USCP_START_CHK_CNT         0
#define USCP_END_CHK_CNT           1001
#define USCP_CHK_CNT_STEP          1
#define USCP_INSERT_CHG_CNT        1100
#define FAST_MONITOR_INTERVAL      300  /* 300 ms */
#define NORMAL_MONITOR_INTERVAL    10000  /* 10000 ms */
#define GPIO_HIGH                  1
#define GPIO_LOW                   0
#define INTERVAL_BASE              0
#define ADC_SAMPLE_RETRY_MAX       3
#define USB_TEMP_CNT               2
#define TUSB_TEMP_UPPER_LIMIT      100
#define TUSB_TEMP_LOWER_LIMIT      (-30)
#define CONVERT_TEMP_UNIT          10
#define INVALID_BATT_TEMP          (-255)
#define ADC_VOL_INIT               (-1)
#define AVG_COUNT                  3
#define FG_NOT_READY               0
#define DEFAULT_TUSB_THRESHOLD     40
#define HIZ_ENABLE                 1
#define HIZ_DISABLE                0
#define DMD_HIZ_DISABLE            0
#define DELAY_FOR_GPIO_SET         10
#define ERROR_TUSB_THR             46 /* tusb table for more 125 degree */
#define NORMAL_TEMP                938 /* tusb table for 25 degree */
#define VOLTAGE_MAP_WIDTH          2
#define GET_NUM_MID                2
#define DEFAULT_ADC_CHANNEL        3
#define HIZ_CURRENT                100 /* mA */
#define IIN_CURR_MAX               2000 /* mA */
#define USCP_ERROR                 (-1)
#ifdef CONFIG_DIRECT_CHARGER
static int is_scp_charger; /* scp charger status 0 or 1 */
static unsigned int first_in = 1; /* first enter uscp 1, exit 0  for check adapter */
#endif /* CONFIG_DIRECT_CHARGER */
#ifdef CONFIG_MT6873_USCP_ADC
struct iio_channel *usb_uscp_channel_raw;
#endif

struct uscp_device_info {
	struct device *dev;
	struct device_node *np_node;
	struct workqueue_struct *uscp_wq;
	struct work_struct uscp_check_wk;
	struct power_supply *ac_psy;
	struct power_supply *usb_psy;
	struct power_supply *chg_psy;
	struct power_supply *batt_psy;
	struct power_supply *hwbatt_psy;
	struct notifier_block event_nb;
	struct hrtimer timer;
	int usb_present;
	int gpio_uscp;
	int uscp_threshold_tusb;
	int open_mosfet_temp;
	int open_hiz_temp;
	int close_mosfet_temp;
	int interval_switch_temp;
	int check_interval;
	int keep_check_cnt;
	int dmd_hiz_enable;
	int using_mtcharger_adc;
	bool is_suspend;
	struct delayed_work type_work;
	struct wakeup_source wakelock;
};

static bool g_uscp_enable;
static bool g_uscp_dmd_enable = true;
static bool g_is_uscp_mode;
static bool g_is_hiz_mode;
static bool g_uscp_dmd_enable_hiz = true;
static int g_uscp_adc_channel = DEFAULT_ADC_CHANNEL;
static struct uscp_device_info *g_uscp_device;

static ssize_t uscp_dbg_show(void *dev_data, char *buf, size_t size)
{
	struct uscp_device_info *dev_p = (struct uscp_device_info *)dev_data;

	if (!buf || !dev_p) {
		hwlog_err("buf or dev_p is null\n");
		return scnprintf(buf, size, "buf or dev_p is null\n");
	}

	return scnprintf(buf, size,
		"uscp_threshold_tusb=%d\n"
		"open_mosfet_temp=%d\n"
		"close_mosfet_temp=%d\n"
		"interval_switch_temp=%d\n",
		dev_p->uscp_threshold_tusb,
		dev_p->open_mosfet_temp,
		dev_p->close_mosfet_temp,
		dev_p->interval_switch_temp);
}

static ssize_t uscp_dbg_store(void *dev_data, const char *buf, size_t size)
{
	struct uscp_device_info *dev_p = (struct uscp_device_info *)dev_data;
	int uscp_tusb = 0;
	int open_temp = 0;
	int close_temp = 0;
	int switch_temp = 0;

	if (!buf || !dev_p) {
		hwlog_err("buf or dev_p is null\n");
		return -EINVAL;
	}

	/* 4: four parameters */
	if (sscanf(buf, "%d %d %d %d",
		&uscp_tusb, &open_temp, &close_temp, &switch_temp) != 4) {
		hwlog_err("unable to parse input:%s\n", buf);
		return -EINVAL;
	}

	dev_p->uscp_threshold_tusb = uscp_tusb;
	dev_p->open_mosfet_temp = open_temp;
	dev_p->close_mosfet_temp = close_temp;
	dev_p->interval_switch_temp = switch_temp;

	hwlog_info("uscp_threshold_tusb=%d\n", dev_p->interval_switch_temp);
	hwlog_info("open_mosfet_temp=%d\n", dev_p->open_mosfet_temp);
	hwlog_info("close_mosfet_temp=%d\n", dev_p->close_mosfet_temp);
	hwlog_info("interval_switch_temp=%d\n", dev_p->interval_switch_temp);

	return size;
}

static void uscp_dbg_register(struct uscp_device_info *di)
{
	power_dbg_ops_register("uscp_para", di,
		(power_dbg_show)uscp_dbg_show, (power_dbg_store)uscp_dbg_store);
}

static void uscp_wake_lock(struct uscp_device_info *di)
{
	if (!di->wakelock.active) {
		hwlog_info("wake lock\n");
		__pm_stay_awake(&di->wakelock);
	}
}

static void uscp_wake_unlock(struct uscp_device_info *di)
{
	if (di->wakelock.active) {
		hwlog_info("wake unlock\n");
		__pm_relax(&di->wakelock);
	}
}

int is_in_uscp_mode(void)
{
	return g_is_uscp_mode;
}

static int get_propety_int(struct power_supply *psy, int propery)
{
	int rc;
	union power_supply_propval ret = {0};

	if (!psy || !(psy->desc) || !(psy->desc->get_property)) {
		pr_err("get input source power supply node failed!\n");
		return -EINVAL;
	}
	rc = psy->desc->get_property(psy, propery, &ret);
	if (rc) {
		pr_err("couldn't get type rc = %d\n", rc);
		ret.intval = -EINVAL;
	}

	return ret.intval;
}

void charge_set_hiz_enable(const int hiz_mode)
{
	int rc;
	static int save_iin_curr = IIN_CURR_MAX;
	int iin_hiz_curr;

	if (hiz_mode == HIZ_ENABLE) {
		save_iin_curr = get_propety_int(g_uscp_device->hwbatt_psy,
			POWER_SUPPLY_PROP_INPUT_CURRENT_MAX);
		save_iin_curr = max(save_iin_curr, IIN_CURR_MAX);
		iin_hiz_curr = HIZ_CURRENT;
		pr_info("%s hiz mode, curr = %d, iin_curr = %d\n",
			__func__, iin_hiz_curr, save_iin_curr);
	} else {
		iin_hiz_curr = save_iin_curr;
		pr_info("%s non-hiz mode, curr = %d\n", __func__, iin_hiz_curr);
	}

	rc = power_supply_set_int_property_value("hwbatt",
		POWER_SUPPLY_PROP_INPUT_CURRENT_MAX, iin_hiz_curr);
	if (rc < 0)
		pr_err("couldn't set hiz enable\n");
}

static void charge_type_handler(struct uscp_device_info *di, const int type)
{
	int interval;

	pr_info("%s uscp enable = %d\n", __func__, g_uscp_enable);
	if (!g_uscp_enable || di->is_suspend)
		return;

	if (type == 0) {
		pr_info("usb present = %d, do nothing\n", type);
		return;
	}

	if (hrtimer_active(&(di->timer))) {
		pr_info("timer already started, do nothing\n");
		return;
	}

	pr_info("start uscp check\n");
	interval = INTERVAL_BASE;
#ifdef CONFIG_DIRECT_CHARGER
	first_in = 1;
#endif
	/* record 30 seconds after the charger; */
	/* just insert 30s = (1100 - 1001 + 1) * 300ms */
	di->keep_check_cnt = USCP_INSERT_CHG_CNT;
	hrtimer_start(&di->timer, ktime_set((interval / MSEC_PER_SEC),
		((interval % MSEC_PER_SEC) * USEC_PER_SEC)), HRTIMER_MODE_REL);
}

static int get_usb_temp_value(void)
{
	int temp = power_temp_get_average_value(POWER_TEMP_USB_PORT) / POWER_TEMP_UNIT;

	pr_info("uscp adc get temp = %d\n", temp);
	return temp;
}

static int get_batt_temp_value(void)
{
	int ret;

	ret = get_propety_int(g_uscp_device->batt_psy, POWER_SUPPLY_PROP_TEMP);
	if (ret == -EINVAL) {
		pr_err("%s get temp error\n", __func__);
		return INVALID_BATT_TEMP;
	}

	return (ret / CONVERT_TEMP_UNIT);
}

static void set_interval(struct uscp_device_info *di, const int temp)
{
	if (!di) {
		pr_err("%s di is NULL\n", __func__);
		return;
	}

	if (temp > di->interval_switch_temp) {
		di->check_interval = FAST_MONITOR_INTERVAL;
		di->keep_check_cnt = USCP_START_CHK_CNT;
		pr_info("after set cnt = %d\n", di->keep_check_cnt);
	} else {
		pr_info("before set cnt = %d\n", di->keep_check_cnt);
		if (di->keep_check_cnt > USCP_END_CHK_CNT) {
			/* check the temperature, */
			/* per 0.3 second for 100 times , */
			/* when the charger just insert. */
			di->keep_check_cnt -= USCP_CHK_CNT_STEP;
			di->check_interval = FAST_MONITOR_INTERVAL;
			g_is_uscp_mode = false;
		} else if (di->keep_check_cnt == USCP_END_CHK_CNT) {
			/* reset the flag when, */
			/* the temperature status is stable */
			di->keep_check_cnt = USCP_DEFAULT_CHK_CNT;
			di->check_interval = NORMAL_MONITOR_INTERVAL;
			g_is_uscp_mode = false;
			uscp_wake_unlock(di);
		} else if (di->keep_check_cnt >= USCP_START_CHK_CNT) {
			di->keep_check_cnt = di->keep_check_cnt +
				USCP_CHK_CNT_STEP;
			di->check_interval = FAST_MONITOR_INTERVAL;
		} else {
			di->check_interval = NORMAL_MONITOR_INTERVAL;
			g_is_uscp_mode = false;
		}
	}
}

static void protection_process(struct uscp_device_info *di, const int tbatt,
	const int tusb)
{
	int tdiff;
#ifdef CONFIG_DIRECT_CHARGER
	int ret;
	int state;
#endif /* CONFIG_DIRECT_CHARGER */

	if (!di) {
		pr_err("%s di is NULL\n", __func__);
		return;
	}

	tdiff = tusb - tbatt;
	if ((tbatt != INVALID_BATT_TEMP) && (tusb >= di->uscp_threshold_tusb) &&
		(tdiff >= di->open_hiz_temp)) {
		g_is_hiz_mode = true;
		pr_err("enable charge hiz\n");
		charge_set_hiz_enable(HIZ_ENABLE);
	}
	if ((tbatt != INVALID_BATT_TEMP) && (tusb >= di->uscp_threshold_tusb) &&
		(tdiff >= di->open_mosfet_temp)) {
		uscp_wake_lock(di);
		g_is_uscp_mode = true;

#ifdef CONFIG_DIRECT_CHARGER
		direct_charge_set_stop_charging_flag(1); /* set direct flag 1 */

		/* wait until direct charger stop complete */
		while (true) {
			state = direct_charge_get_stage_status();
			if (direct_charge_get_stop_charging_complete_flag() &&
				((state == DC_STAGE_DEFAULT) ||
				(state == DC_STAGE_CHARGE_DONE)))
				break;
		}

		direct_charge_set_stop_charging_flag(0);  /* set direct flag 0 */
		if (state == DC_STAGE_DEFAULT) {
			if (first_in) {
				if (direct_charge_detect_adapter_again())
					is_scp_charger = 0;
				else
					is_scp_charger = 1;

				first_in = 0;
			}
		} else if (state == DC_STAGE_CHARGE_DONE) {
			is_scp_charger = 1;
		}

		if (is_scp_charger) {
			/* close charging path: 0 disable direct charger */
			ret = dc_set_adapter_output_enable(0);
			if (!ret) {
				pr_err("disable adapter output success\n");
				msleep(200); /* delay 200ms */
			} else {
				pr_err("disable adapter output fail\n");
			}
		}
#endif /* CONFIG_DIRECT_CHARGER */

		gpio_set_value(di->gpio_uscp, GPIO_HIGH);  /* open mosfet */
		pr_info("pull up gpio_uscp\n");
	} else if (tdiff <= di->close_mosfet_temp) {

#ifdef CONFIG_DIRECT_CHARGER
		if (is_scp_charger) {
			/* open charging path: 1 enable direct charger */
			ret = dc_set_adapter_output_enable(1);
			if (!ret)
				pr_err("enable adapter output success\n");
			else
				pr_err("enable adapter output fail\n");
		}
#endif /* CONFIG_DIRECT_CHARGER */

		if (g_is_uscp_mode) {
			/* close mosfet */
			gpio_set_value(di->gpio_uscp, GPIO_LOW);
			msleep(DELAY_FOR_GPIO_SET);
			charge_set_hiz_enable(HIZ_DISABLE);
			g_is_hiz_mode = false;
			pr_info("pull down gpio_uscp\n");
		}
		if (g_is_hiz_mode) {
			charge_set_hiz_enable(HIZ_DISABLE);
			g_is_hiz_mode = false;
			pr_info("disable charge hiz\n");
		}
	}
}

static void check_temperature(struct uscp_device_info *di)
{
	int tusb;
	int tbatt;
	int tdiff;
	int ret;

	if (!di) {
		pr_err("%s di is NULL\n", __func__);
		return;
	}

	tusb = get_usb_temp_value();
	tbatt = get_batt_temp_value();
	pr_info("tusb = %d, tbatt = %d\n", tusb, tbatt);
	tdiff = tusb - tbatt;

	if ((tusb < TUSB_TEMP_LOWER_LIMIT) || (tusb > TUSB_TEMP_UPPER_LIMIT)) {
		pr_err("uscp: usb temp out of range!! ignore\n");
		return;
	}

	if (tbatt == INVALID_BATT_TEMP) {
		tdiff = INVALID_DELTA_TEMP;
		pr_err("get battery adc temp err, not care\n");
	}

	if (di->dmd_hiz_enable && (tusb >= di->uscp_threshold_tusb) &&
		(tdiff >= di->open_hiz_temp) && g_uscp_dmd_enable_hiz) {
		ret = power_dsm_dmd_report(POWER_DSM_USCP,
			ERROR_NO_USB_SHORT_PROTECT_HIZ,
			"usb short happened,open hiz\n");
		if (ret)
			pr_err("usb short: hiz dmd fail\n");
		else
			g_uscp_dmd_enable_hiz = false;
	}

	if ((tusb >= di->uscp_threshold_tusb) &&
		(tdiff >= di->open_mosfet_temp) &&
		 g_uscp_dmd_enable) {
		ret = power_dsm_dmd_report(POWER_DSM_USCP,
			ERROR_NO_USB_SHORT_PROTECT,
			"usb short happened");
		if (ret)
			pr_err("usb short: mosfet dmd fail\n");
		else
			g_uscp_dmd_enable = false;
	}

	set_interval(di, tdiff);
	protection_process(di, tbatt, tusb);
}

static void uscp_check_work(struct work_struct *work)
{
	struct uscp_device_info *di = NULL;
	int interval;
	int type;

#ifdef CONFIG_HLTHERM_RUNTEST
	pr_info("HLTHERM disable uscp protect\n");
	return;
#endif

	if (!work) {
		pr_err("%s: work is NULL\n", __func__);
		return;
	}

	di = container_of(work, struct uscp_device_info, uscp_check_wk);
	if (!di) {
		pr_err("%s: get uscp device info is NULL\n", __func__);
		return;
	}

	type = get_propety_int(di->hwbatt_psy, POWER_SUPPLY_PROP_CHARGE_TYPE);

#ifdef CONFIG_DIRECT_CHARGER
	if ((di->keep_check_cnt == USCP_DEFAULT_CHK_CNT) && (type == POWER_SUPPLY_TYPE_UNKNOWN) &&
		(direct_charge_get_stage_status() == DC_STAGE_DEFAULT))
#else
	if ((di->keep_check_cnt == USCP_DEFAULT_CHK_CNT) && (type == POWER_SUPPLY_TYPE_UNKNOWN))
#endif /* CONFIG_DIRECT_CHARGER */
	{
		g_uscp_dmd_enable = true;
		g_uscp_dmd_enable_hiz = true;
		gpio_set_value(di->gpio_uscp, GPIO_LOW);  /* close mosfet */
		di->check_interval = NORMAL_MONITOR_INTERVAL;
		g_is_uscp_mode = false;
		di->keep_check_cnt = USCP_INSERT_CHG_CNT;
#ifdef CONFIG_DIRECT_CHARGER
		first_in = 1;
		is_scp_charger = 0;
#endif /* CONFIG_DIRECT_CHARGER */

		pr_info("charger type is %d, stop checking\n", type);
		return;
	}

	check_temperature(di);
	interval = di->check_interval;
	hrtimer_start(&di->timer, ktime_set(interval / MSEC_PER_SEC,
		(interval % MSEC_PER_SEC) * USEC_PER_SEC), HRTIMER_MODE_REL);
}

static enum hrtimer_restart uscp_timer_func(struct hrtimer *timer)
{
	struct uscp_device_info *di = NULL;

	if (!timer) {
		pr_err("%s: timer is NULL\n", __func__);
		return HRTIMER_NORESTART;
	}

	di = container_of(timer, struct uscp_device_info, timer);
	if (!di) {
		pr_err("%s: get uscp device info is NULL\n", __func__);
		return HRTIMER_NORESTART;
	}
	queue_work(di->uscp_wq, &di->uscp_check_wk);
	return HRTIMER_NORESTART;
}

static int uscp_init_power_supply(struct uscp_device_info *info)
{
	struct power_supply *ac_psy = NULL;
	struct power_supply *usb_psy = NULL;
	struct power_supply *chg_psy = NULL;
	struct power_supply *batt_psy = NULL;
	struct power_supply *hwbatt_psy = NULL;

	ac_psy = power_supply_get_by_name("ac");
	if (!ac_psy) {
		pr_err("%s ac supply not found\n", __func__);
		return -EPROBE_DEFER;
	}

	usb_psy = power_supply_get_by_name("usb");
	if (!usb_psy) {
		pr_err("%s usb supply not found\n", __func__);
		return -EPROBE_DEFER;
	}

	chg_psy = power_supply_get_by_name("charger");
	if (!chg_psy) {
		pr_err("%s chg supply not found\n", __func__);
		return -EPROBE_DEFER;
	}

	batt_psy = power_supply_get_by_name("battery");
	if (!batt_psy) {
		pr_err("%s batt supply not found\n", __func__);
		return -EPROBE_DEFER;
	}
	hwbatt_psy = power_supply_get_by_name("hwbatt");
	if (!hwbatt_psy) {
		pr_err("%s hwbatt supply not found\n", __func__);
		return -EPROBE_DEFER;
	}

	info->ac_psy = ac_psy;
	info->usb_psy = usb_psy;
	info->chg_psy = chg_psy;
	info->batt_psy = batt_psy;
	info->hwbatt_psy = hwbatt_psy;
	return 0;
}

static int uscp_gpio_init(struct device_node *np, struct uscp_device_info *info)
{
	int ret;

	info->gpio_uscp = of_get_named_gpio(np, "gpio_uscp", 0);
	if (!gpio_is_valid(info->gpio_uscp)) {
		pr_err("gpio_uscp is not valid\n");
		return -EINVAL;
	}
	pr_info("gpio_uscp = %d\n", info->gpio_uscp);

	ret = gpio_request(info->gpio_uscp, "uscp");
	if (ret) {
		pr_err("could not request gpio_uscp\n");
		return -EINVAL;
	}
	gpio_direction_output(info->gpio_uscp, GPIO_LOW);

	return 0;
}

static int uscp_parse_dt(struct device_node *np, struct uscp_device_info *info)
{
	int ret;

	ret = of_property_read_u32(np, "uscp_adc", &g_uscp_adc_channel);
	if (ret)
		g_uscp_adc_channel = DEFAULT_ADC_CHANNEL;
	pr_info("get uscp_adc = %d, ret = %d\n", g_uscp_adc_channel, ret);

	ret = of_property_read_u32(np, "uscp_threshold_tusb",
		&(info->uscp_threshold_tusb));
	if (ret)
		info->uscp_threshold_tusb = DEFAULT_TUSB_THRESHOLD;
	pr_info("get uscp_threshold_tusb = %d, ret = %d\n",
		info->uscp_threshold_tusb, ret);

	ret = of_property_read_u32(np, "open_mosfet_temp",
		&(info->open_mosfet_temp));
	if (ret) {
		pr_err("get open_mosfet_temp info fail\n");
		return ret;
	}
	pr_info("open_mosfet_temp = %d\n", info->open_mosfet_temp);

	ret = of_property_read_u32(np, "close_mosfet_temp",
		&(info->close_mosfet_temp));
	if (ret) {
		pr_err("get close_mosfet_temp info fail\n");
		return ret;
	}
	pr_info("close_mosfet_temp = %d\n", info->close_mosfet_temp);

	ret = of_property_read_u32(np, "interval_switch_temp",
		&(info->interval_switch_temp));
	if (ret) {
		pr_err("get interval_switch_temp info fail\n");
		return ret;
	}
	pr_info("interval_switch_temp = %d\n", info->interval_switch_temp);

	ret = of_property_read_u32(np, "open_hiz_temp", &(info->open_hiz_temp));
	if (ret)
		info->open_hiz_temp = info->open_mosfet_temp;
	pr_info("get open_hiz_temp = %d, ret = %d\n", info->open_hiz_temp, ret);

	ret = of_property_read_u32(np, "dmd_hiz_enable",
		&(info->dmd_hiz_enable));
	if (ret)
		info->dmd_hiz_enable = DMD_HIZ_DISABLE;
	pr_info("get dmd_hiz_enable = %d, ret = %d\n",
		info->dmd_hiz_enable, ret);

	ret = of_property_read_u32(np, "using_mtcharger_adc",
		&(info->using_mtcharger_adc));
	if (ret)
		info->using_mtcharger_adc = 0;
	pr_info("get using_mtcharger_adc = %d, ret = %d\n",
		info->using_mtcharger_adc, ret);

	return 0;
}

static int check_ntc_error(void)
{
	int temp;
#ifndef CONFIG_HLTHERM_RUNTEST
	int ret;
#endif

	temp = get_usb_temp_value();
	if (temp > TUSB_TEMP_UPPER_LIMIT || temp < TUSB_TEMP_LOWER_LIMIT) {
#ifndef CONFIG_HLTHERM_RUNTEST
		ret = power_dsm_dmd_report(POWER_DSM_USCP,
			ERROR_NO_USB_SHORT_PROTECT_NTC, "ntc error happened\n");
		if (ret)
			pr_err("ntc error report dmd fail\n");
#endif
		pr_err("ntc error happend\n");
		return USCP_ERROR;
	}
	return 0;
}

static int check_batt_present(struct uscp_device_info *info)
{
	int batt_present;

	batt_present = get_propety_int(info->batt_psy,
		POWER_SUPPLY_PROP_PRESENT);
	if (!batt_present)
		return USCP_ERROR;

	return 0;
}

static int check_uscp_enable(struct uscp_device_info *info)
{
	int ret;

	ret = check_ntc_error();
	if (ret) {
		pr_err("check uscp ntc fail");
		return ret;
	}
	ret = check_batt_present(info);
	if (ret) {
		pr_err("check uscp batt present fail");
		return ret;
	}
	g_uscp_enable = true;
	pr_info("check uscp enable ok");
	return 0;
}

static int uscp_event_notifier_call(struct notifier_block *nb,
	unsigned long event, void *data)
{
	struct uscp_device_info *di = g_uscp_device;

	if (!di)
		return NOTIFY_OK;

	switch (event) {
	case POWER_NE_START_CHARGING:
		charge_type_handler(di, true);
		return NOTIFY_OK;
	case POWER_NE_STOP_CHARGING:
		charge_type_handler(di, false);
		return NOTIFY_OK;
	default:
		return NOTIFY_OK;
	}
}

static void get_usb_present(struct uscp_device_info *info)
{
	int ac_online;
	int usb_online;

	ac_online = get_propety_int(info->ac_psy, POWER_SUPPLY_PROP_ONLINE);
	usb_online = get_propety_int(info->usb_psy, POWER_SUPPLY_PROP_ONLINE);
	info->usb_present = ac_online || usb_online;
	pr_info("usb present = %d\n", info->usb_present);
}

static int uscp_get_raw_data(int adc_channel, long *data, void *dev_data)
{
	int i;
	int ret;
	int t_sample = ADC_VOL_INIT;

	if (!data || !dev_data)
		return -1;

	for (i = 0; i < ADC_SAMPLE_RETRY_MAX; ++i) {
#ifdef CONFIG_MT6873_USCP_ADC
		ret = iio_read_channel_processed(usb_uscp_channel_raw, &t_sample);
		if (g_uscp_device && g_uscp_device->using_mtcharger_adc) {
			/* 1000 : trans to mv */
			t_sample = t_sample / 1000;
			/* scale : 4096mv /1800mv */
			t_sample = t_sample * 4096 / 1800;
		} else {
			/* scale : 1500mv /1800mv */
			t_sample = t_sample * 1500 / 1800;
		}
#else
		ret = IMM_GetOneChannelValue_Cali(g_uscp_adc_channel, &t_sample);
#ifdef CONFIG_MACH_MT6765
		/* 1000 : trans to mv */
		t_sample = t_sample / 1000;
		/* scale : 4096mv /1800mv */
		t_sample = t_sample * 4096 / 1800;
#endif
#endif
		if (ret < 0)
			pr_err("adc read fail\n");
		else
			break;
	}

	*data = t_sample;
	return 0;
}


static struct power_tz_ops uscp_temp_sensing_ops = {
	.get_raw_data = uscp_get_raw_data,
};

static int uscp_probe(struct platform_device *pdev)
{
	int ret;
	struct device_node *np = NULL;
	struct uscp_device_info *di = NULL;

	pr_info("%s enter", __func__);
	if (!pdev) {
		pr_err("%s: pdev is NULL\n", __func__);
		return -EINVAL;
	}

	np = pdev->dev.of_node;
	if (!np) {
		pr_err("%s: np is NULL\n", __func__);
		return -EINVAL;
	}

	di = kzalloc(sizeof(*di), GFP_KERNEL);
	if (!di)
		return -ENOMEM;

	di->dev = &pdev->dev;
	dev_set_drvdata(&(pdev->dev), di);
	di->keep_check_cnt = USCP_INSERT_CHG_CNT;
	g_is_uscp_mode = false;
	g_uscp_device = di;

#ifdef CONFIG_MT6873_USCP_ADC
	usb_uscp_channel_raw = iio_channel_get(&pdev->dev, "uscp_channel");
	ret = IS_ERR(usb_uscp_channel_raw);
	if (ret) {
		pr_err("[%s] fail to usb adc auxadc iio ch5: %d\n",	__func__, ret);
		return ret;
	}
#endif

	uscp_temp_sensing_ops.dev_data = (void *)g_uscp_device;
	ret = power_tz_ops_register(&uscp_temp_sensing_ops, "uscp");
	if (ret)
		pr_err("thermalzone ops register fail\n");

	ret = uscp_init_power_supply(di);
	if (ret)
		goto free_mem;

	ret = uscp_gpio_init(np, di);
	if (ret)
		goto free_mem;

	wakeup_source_init(&di->wakelock, "uscp_wakelock");

	ret = uscp_parse_dt(np, di);
	if (ret)
		goto free_gpio;

	ret = check_uscp_enable(di);
	if (ret)
		goto free_gpio;

	di->uscp_wq = create_singlethread_workqueue(
		"usb_short_circuit_protect_wq");
	INIT_WORK(&di->uscp_check_wk, uscp_check_work);
	hrtimer_init(&di->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	di->timer.function = uscp_timer_func;

	di->event_nb.notifier_call = uscp_event_notifier_call;
	ret = power_event_nc_register(POWER_NT_CHARGING, &di->event_nb);
	if (ret)
		goto free_gpio;

	get_usb_present(di);
	charge_type_handler(di, di->usb_present);

	platform_set_drvdata(pdev, di);
	uscp_dbg_register(di);
	return 0;

free_gpio:
	gpio_free(di->gpio_uscp);
	wakeup_source_trash(&di->wakelock);
free_mem:
	kfree(di);
	di = NULL;
	g_uscp_device = NULL;
	g_uscp_enable = false;
	return ret;
}

static int uscp_remove(struct platform_device *pdev)
{
	struct uscp_device_info *di = platform_get_drvdata(pdev);

	if (!di)
		return -ENODEV;

	wakeup_source_trash(&di->wakelock);
	gpio_free(di->gpio_uscp);
	power_event_nc_unregister(POWER_NT_CHARGING, &di->event_nb);
	kfree(di);
	g_uscp_device = NULL;
	return 0;
}

#ifndef CONFIG_HLTHERM_RUNTEST
#ifdef CONFIG_PM
static int uscp_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct uscp_device_info *di = NULL;

	if (!pdev) {
		pr_err("%s: pdev is NULL\n", __func__);
		return -EINVAL;
	}

	di = platform_get_drvdata(pdev);
	if (!di) {
		pr_err("%s: get uscp device info is NULL\n", __func__);
		return -ENODEV;
	}

	di->is_suspend = true;
	if (g_uscp_enable == false) {
		pr_info("%s: uscp enable false\n", __func__);
		return 0;
	}

	pr_info("%s:+\n", __func__);
	cancel_work_sync(&di->uscp_check_wk);
	hrtimer_cancel(&di->timer);
	pr_info("%s:-\n", __func__);
	return 0;
}

static int uscp_resume(struct platform_device *pdev)
{
	struct uscp_device_info *di = NULL;

	if (!pdev) {
		pr_err("%s: pdev is NULL\n", __func__);
		return -EINVAL;
	}

	di = platform_get_drvdata(pdev);
	if (!di) {
		pr_err("%s: get uscp device info is NULL\n", __func__);
		return -ENODEV;
	}

	di->is_suspend = false;
	if (g_uscp_enable == false) {
		pr_info("%s: uscp enable false\n", __func__);
		return 0;
	}

	get_usb_present(di);
	if (di->usb_present == false) {
		pr_err("%s: di->usb_present = %d\n", __func__, di->usb_present);
		return 0;
	}
	pr_info("%s:+ di->usb_present = %d\n", __func__, di->usb_present);
	queue_work(di->uscp_wq, &di->uscp_check_wk);
	pr_info("%s:-\n", __func__);
	return 0;
}
#endif
#endif

static const struct of_device_id uscp_match_table[] = {
	{
		.compatible = "huawei,usb_short_circuit_protect",
		.data = NULL,
	},
	{},
};

static struct platform_driver uscp_driver = {
	.probe = uscp_probe,
#ifndef CONFIG_HLTHERM_RUNTEST
#ifdef CONFIG_PM
	/*
	 * depend on IPC driver
	 * so we set SR suspend/resume and IPC is suspend_late/early_resume
	 */
	.suspend = uscp_suspend,
	.resume = uscp_resume,
#endif /* CONFIG_PM */
#endif
	.remove = uscp_remove,
	.driver = {
		.name = "huawei,usb_short_circuit_protect",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(uscp_match_table),
	},
};

static int __init uscp_init(void)
{
	return platform_driver_register(&uscp_driver);
}

static void __exit uscp_exit(void)
{
	platform_driver_unregister(&uscp_driver);
}

device_initcall_sync(uscp_init);
module_exit(uscp_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("usb port short circuit protect module driver");
MODULE_AUTHOR("Huawei Technologies Co., Ltd.");
