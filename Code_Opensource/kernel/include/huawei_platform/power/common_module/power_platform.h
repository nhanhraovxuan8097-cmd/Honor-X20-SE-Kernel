/*
 * power_platform.h
 *
 * differentiated interface related to chip platform
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

#ifndef _POWER_PLATFORM_H_
#define _POWER_PLATFORM_H_

#include <huawei_platform/power/wireless/wireless_tx_pwr_ctrl.h>
#include <huawei_platform/power/direct_charger/direct_charger.h>
#include <huawei_platform/power/huawei_charger_common.h>
#include <chipset_common/hwpower/direct_charge/direct_charge_ic_manager.h>
#include <linux/power/huawei_power_proxy.h>
#include <mt-plat/charger_class.h>
#include <mt-plat/mtk_battery.h>

#define POWER_PLATFOR_SOC_UI_OFFSET    50
#define MT63XX_ADC_CHANNEL_USBID       1001

extern int mt635x_auxadc_get_sample(int channel);
extern int mt635x_auxadc_get_voltage(int channel);
int power_get_battery_has_removed_flag(void);

static inline int power_platform_get_filter_soc(int base)
{
	return power_proxy_get_filter_sum(base);
}

static inline void power_platform_sync_filter_soc(int rep_soc,
	int round_soc, int base)
{
	power_proxy_sync_filter_soc(rep_soc, round_soc, base);
}

static inline void power_platform_cancle_capacity_work(void)
{
	power_proxy_cancle_capacity_work();
}

static inline void power_platform_restart_capacity_work(void)
{
	power_proxy_restart_capacity_work();
}

static inline int power_platform_get_adc_sample(int adc_channel)
{
	return mt635x_auxadc_get_sample(adc_channel);
}

static inline int power_platform_get_adc_voltage(int adc_channel)
{
	if (adc_channel == MT63XX_ADC_CHANNEL_USBID)
		return get_charger_bat_id_vol();
	return mt635x_auxadc_get_voltage(adc_channel);
}

static inline int power_platform_get_battery_capacity(void)
{
	return battery_get_soc();
}

static inline int power_platform_get_battery_ui_capacity(void)
{
	return battery_get_uisoc();
}

static inline int power_platform_get_battery_temperature(void)
{
	return battery_get_bat_temperature();
}

static inline int power_platform_get_rt_battery_temperature(void)
{
	return battery_get_bat_temperature();
}

static inline char *power_platform_get_battery_brand(void)
{
	return huawei_get_battery_type();
}

static inline int power_platform_get_battery_voltage(void)
{
	return battery_get_bat_voltage();
}

static inline int power_platform_get_battery_current(void)
{
	return battery_get_bat_current();
}

static inline int power_platform_get_battery_current_avg(void)
{
	return battery_get_bat_avg_current();
}

static inline int power_platform_is_battery_removed(void)
{
	return power_get_battery_has_removed_flag();
}

static inline int power_platform_is_battery_exit(void)
{
	int batt_present = 0;

	power_proxy_is_battery_exit(&batt_present);

	return batt_present;
}

static inline unsigned int power_platform_get_charger_type(void)
{
	return (unsigned int)huawei_get_charger_type();
}

static inline int power_platform_get_vbus_status(void)
{
	return -1;
}

static inline int power_platform_pmic_enable_boost(int value)
{
	return 0;
}

static inline int power_platform_get_vusb_status(int *value)
{
	return -1;
}

static inline bool power_platform_usb_state_is_host(void)
{
	return false;
}

static inline bool power_platform_pogopin_is_support(void)
{
	return false;
}

static inline bool power_platform_pogopin_otg_from_buckboost(void)
{
	return false;
}

static inline bool power_platform_get_cancel_work_flag(void)
{
	return false;
}

static inline bool power_platform_get_sysfs_wdt_disable_flag(void)
{
	return false;
}

static inline void power_platform_charge_stop_sys_wdt(void)
{
}

static inline void power_platform_charge_feed_sys_wdt(unsigned int timeout)
{
}

static inline int power_platform_set_max_input_current(void)
{
	return -1;
}

static inline void power_platform_start_acr_calc(void)
{
}

static inline int power_platform_get_acr_resistance(int *acr_value)
{
	return -1;
}

static inline int power_platform_get_bat_btb_voltage(int type)
{
	return dc_get_bat_btb_voltage(SC_MODE, type);
}

#ifdef CONFIG_DIRECT_CHARGER
static inline bool power_platform_in_dc_charging_stage(void)
{
	return direct_charge_in_charging_stage() == DC_IN_CHARGING_STAGE;
}
#else
static inline bool power_platform_in_dc_charging_stage(void)
{
	return false;
}
#endif /* CONFIG_DIRECT_CHARGER */

static inline void power_platform_set_charge_batfet(int val)
{
}

#endif /* _POWER_PLATFORM_H_ */
