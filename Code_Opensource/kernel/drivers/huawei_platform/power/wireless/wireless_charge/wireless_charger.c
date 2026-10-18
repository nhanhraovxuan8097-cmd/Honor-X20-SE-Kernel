/*
 * wireless_charger.c
 *
 * logic layer driver for wireless charging
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

#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/of_gpio.h>
#include <linux/delay.h>
#include <linux/time.h>
#include <linux/rtc.h>
#include <huawei_platform/log/hw_log.h>
#include <linux/random.h>
#include <chipset_common/hwpower/charge_pump.h>
#include <huawei_platform/charger/huawei_charger.h>
#include <huawei_platform/power/wireless/wireless_charge_psy.h>
#include <huawei_platform/power/wireless/wireless_charge_ictrl.h>
#include <huawei_platform/power/wireless/wireless_charger.h>
#include <huawei_platform/power/wireless/wireless_transmitter.h>
#include <huawei_platform/power/wireless/wireless_direct_charger.h>
#include <chipset_common/hwpower/wireless_charge/wireless_rx_alarm.h>
#include <chipset_common/hwpower/wired_channel_switch.h>
#include <chipset_common/hwpower/vbus_channel.h>
#include <chipset_common/hwpower/boost_5v.h>
#include <chipset_common/hwpower/wireless_charge/wireless_power_supply.h>
#include <chipset_common/hwpower/power_event_ne.h>
#include <chipset_common/hwpower/power_ui_ne.h>
#include <chipset_common/hwpower/power_dsm.h>
#include <chipset_common/hwpower/power_sysfs.h>
#include <chipset_common/hwpower/power_cmdline.h>
#include <chipset_common/hwpower/power_interface.h>
#include <mt-plat/charger_type.h>

#define HWLOG_TAG wireless_charger
HWLOG_REGIST();

#define ERR_NO_STRING_SIZE  256
#define CAPACITY_FULL       100

static struct wireless_charge_device_ops *g_wireless_ops;
static struct wireless_charge_device_info *g_wireless_di;
static int g_wireless_channel_state = WIRELESS_CHANNEL_OFF;
static int g_wired_channel_state = WIRED_CHANNEL_OFF;
static enum wireless_charge_stage g_wireless_charge_stage = WIRELESS_STAGE_DEFAULT;
static int wireless_normal_charge_flag = 0;
static int wireless_fast_charge_flag = 0;
static int wireless_super_charge_flag = 0;
static struct wakeup_source g_rx_con_wakelock;
static struct wakeup_source g_rx_charge_wakelock;
static struct mutex g_rx_en_mutex;
static int rx_iout_samples[RX_IOUT_SAMPLE_LEN];
static int g_fop_fixed_flag = 0;
static int g_rx_vrect_restore_cnt = 0;
static int g_rx_vout_err_cnt = 0;
static int g_rx_ocp_cnt = 0;
static int g_rx_ovp_cnt = 0;
static int g_rx_otp_cnt = 0;
static int g_rx_imax = WLC_RX_DFT_IOUT_MAX;
static bool g_wlc_start_sample_flag;
static bool g_bst_rst_complete = true;
static bool g_in_wldc_check;
static bool g_high_pwr_test_flag;
static u8 *g_tx_fw_version;
static u8 random[WIRELESS_RANDOM_LEN] = {0};
static u8 tx_cipherkey[WIRELESS_TX_KEY_LEN] = {0};
static u8 g_wc_af_key[WIRELESS_RANDOM_LEN + WIRELESS_TX_KEY_LEN];
static int g_wc_antifake_result;
static int g_wc_antifake_ready;
static bool g_need_recheck_cert;

extern int battery_get_bat_voltage(void);
extern int huawei_battery_capacity(void);

static char chrg_stage[WIRELESS_STAGE_TOTAL][WIRELESS_STAGE_STR_LEN] = {
	{"DEFAULT"}, {"CHECK_TX_ID"}, {"CHECK_TX_ABILITY"}, {"CABLE_DETECT"}, {"CERTIFICATION"},
	{"CHECK_FWUPDATE"}, {"CHARGING"}, {"REGULATION"}, {"REGULATION_DC"}
};

const enum wireless_protocol_tx_type g_qval_err_tx[] = {
	WIRELESS_TX_TYPE_CP39S, WIRELESS_TX_TYPE_CP39S_HK
};
struct wireless_charge_device_info *wlc_get_dev_info(void)
{
	return g_wireless_di;
}

int wireless_charge_ops_register(struct wireless_charge_device_ops *ops)
{
	int ret = 0;

	if (ops != NULL) {
		g_wireless_ops = ops;
	} else {
		hwlog_err("charge ops register fail!\n");
		ret = -EPERM;
	}

	return ret;
}

static void wireless_charge_wake_lock(void)
{
	if (!g_rx_con_wakelock.active) {
		__pm_stay_awake(&g_rx_con_wakelock);
		hwlog_info("wireless_charge wake lock\n");
	}
}
static void wireless_charge_wake_unlock(void)
{
	if (g_rx_con_wakelock.active) {
		__pm_relax(&g_rx_con_wakelock);
		hwlog_info("wireless_charge wake unlock\n");
	}
}

static void wireless_charge_rx_wake_lock(void)
{
	if (!g_rx_charge_wakelock.active) {
		__pm_stay_awake(&g_rx_charge_wakelock);
		hwlog_info("wireless rx charge wake lock\n");
	}
}

static void wireless_charge_rx_wake_unlock(void)
{
	if (g_rx_charge_wakelock.active) {
		__pm_relax(&g_rx_charge_wakelock);
		hwlog_info("wireless rx charge wake unlock\n");
	}
}

static void wireless_charge_msleep(int sleep_ms)
{
	int i;
	int interval = 25; /* ms */
	int cnt = sleep_ms / interval;

	for (i = 0; i < cnt; i++) {
		msleep(interval);
		if (g_wired_channel_state == WIRED_CHANNEL_ON)
			return;
		if (!wireless_charge_check_tx_exist())
			return;
	}
}

static void wireless_charge_set_antifake_result(unsigned int data)
{
	struct wireless_charge_device_info *di = g_wireless_di;

	if (!di) {
		hwlog_err("di is null\n");
		return;
	}

	g_wc_antifake_result = data;
	complete(&di->wc_af_completion);

	hwlog_info("antifake_result=%d\n", g_wc_antifake_result);
}

static int wireless_charge_get_antifake_data(char *buf, unsigned int len)
{
	struct wireless_charge_device_info *di = g_wireless_di;
	int i;

	if (!di) {
		hwlog_err("di is null\n");
		return 0;
	}

	for (i = 0; i < len; i++) {
		buf[i] = g_wc_af_key[i];
		g_wc_af_key[i] = 0;
	}

	return len;
}

static void wireless_charge_en_enable(int enable)
{
	struct wireless_charge_device_info *di = g_wireless_di;
	if (!di) {
		hwlog_err("%s: di null\n", __func__);
		return;
	}
	di->ops->rx_enable(enable);
}
static void wireless_charge_sleep_en_enable(int enable)
{
	struct wireless_charge_device_info *di = g_wireless_di;
	if (!di) {
		hwlog_err("%s: di null\n", __func__);
		return;
	}
	di->ops->rx_sleep_enable(enable);
}

int wlc_get_rx_support_mode(void)
{
	struct wireless_charge_device_info *di = g_wireless_di;

	if (!di) {
		hwlog_err("%s: di null\n", __func__);
		return WLC_RX_SP_BUCK_MODE;
	}

	return di->sysfs_data.rx_support_mode & di->qval_support_mode;
}

int wireless_charge_get_wireless_channel_state(void)
{
	return g_wireless_channel_state;
}
void wireless_charge_set_wireless_channel_state(int state)
{
	hwlog_info("%s %d\n", __func__, state);
	g_wireless_channel_state = state;
}
static void wireless_charge_set_wired_channel_state(int state)
{
	hwlog_info("[%s] %d\n", __func__, state);
	g_wired_channel_state = state;
}
int wireless_charge_get_wired_channel_state(void)
{
	return g_wired_channel_state;
}

static void wlc_set_cur_vmode_id(struct wireless_charge_device_info *di, int id)
{
	di->curr_vmode_index = id;
	hwlog_info("[set_cur_vmode_id] id=%d\n", di->curr_vmode_index);
}

static bool wlc_is_fac_tx(struct wireless_charge_device_info *di)
{
	return (di->tx_cap->type >= QI_PARA_TX_TYPE_FAC_BASE) &&
		(di->tx_cap->type <= QI_PARA_TX_TYPE_FAC_MAX);
}

static bool wlc_is_car_tx(struct wireless_charge_device_info *di)
{
	return (di->tx_cap->type >= QI_PARA_TX_TYPE_CAR_BASE) &&
		(di->tx_cap->type <= QI_PARA_TX_TYPE_CAR_MAX);
}

static void wlc_send_soc_decimal_evt(struct wireless_charge_device_info *di)
{
	int tx_max_pwr_mw;
	int cur_pwr;

	if (wireless_super_charge_flag == 0)
		return;

	tx_max_pwr_mw = di->tx_cap->vout_max / WL_MVOLT_PER_VOLT *
		di->tx_cap->iout_max * WLDC_TX_PWR_RATIO / PERCENT;
	cur_pwr = (tx_max_pwr_mw < di->dts.product_max_pwr) ?
		tx_max_pwr_mw : di->dts.product_max_pwr;
	hwlog_info("[%s] tx_pwr = %d, product_pwr = %d\n",
		__func__, tx_max_pwr_mw, di->dts.product_max_pwr);
	power_event_notify(POWER_NT_SOC_DECIMAL, POWER_NE_SOC_DECIMAL_WL_DC, &cur_pwr);
}

static void wlc_send_max_pwr_evt(struct wireless_charge_device_info *di)
{
	int tx_max_pwr_mw;
	int cur_pwr;

	if ((di->dts.ui_max_pwr <= 0) || (di->dts.product_max_pwr <= 0))
		return;

	if (wireless_super_charge_flag == 0)
		return;

	tx_max_pwr_mw = di->tx_cap->vout_max / WL_MVOLT_PER_VOLT *
		di->tx_cap->iout_max * WLDC_TX_PWR_RATIO / PERCENT;
	hwlog_info("[%s] tx_pwr = %d, ui_pwr = %d, product_pwr = %d\n",
		__func__, tx_max_pwr_mw, di->dts.ui_max_pwr, di->dts.product_max_pwr);
	if (tx_max_pwr_mw < di->dts.ui_max_pwr)
		return;
	if ((tx_max_pwr_mw >= 47000) && (tx_max_pwr_mw < 50000))
		tx_max_pwr_mw = 50000;
	cur_pwr = (tx_max_pwr_mw < di->dts.product_max_pwr) ?
		tx_max_pwr_mw : di->dts.product_max_pwr;
	power_ui_event_notify(POWER_UI_NE_MAX_POWER, &cur_pwr);
}

static void wireless_charge_send_charge_uevent(struct wireless_charge_device_info *di, int icon_type)
{
	int icon = ICON_TYPE_WIRELESS_NORMAL;

	if (WIRELESS_CHANNEL_OFF == g_wireless_channel_state) {
		hwlog_err("%s: not in wireless charging, return\n", __func__);
		return;
	}
	wireless_normal_charge_flag = 0;
	wireless_fast_charge_flag = 0;
	wireless_super_charge_flag = 0;
	switch (icon_type) {
	case WIRELESS_NORMAL_CHARGE_FLAG:
		wireless_normal_charge_flag = 1;
		icon = ICON_TYPE_WIRELESS_NORMAL;
		break;
	case WIRELESS_FAST_CHARGE_FLAG:
		wireless_fast_charge_flag = 1;
		icon = ICON_TYPE_WIRELESS_QUICK;
		break;
	case WIRELESS_SUPER_CHARGE_FLAG:
		wireless_super_charge_flag = 1;
		icon = ICON_TYPE_WIRELESS_SUPER;
		break;
	default:
		hwlog_err("%s: unknown icon_type\n", __func__);
	}

	power_event_notify(POWER_NT_WLC, POWER_NE_WLC_ICON_TYPE, &icon_type);
	hwlog_info("[%s] cur type=%d, last type=%d\n",
		__func__, icon_type, di->curr_icon_type);
	if (di->curr_icon_type ^ icon_type) {
		wireless_connect_send_icon_uevent(icon);
		wlc_send_soc_decimal_evt(di);
		wlc_send_max_pwr_evt(di);
	}

	di->curr_icon_type = icon_type;
}

int wireless_charge_get_rx_iout_limit(void)
{
	int iin_set = RX_IOUT_MIN;
	struct wireless_charge_device_info *di = g_wireless_di;
	if (!di) {
		hwlog_err("%s: di null\n", __func__);
		return iin_set;
	}
	iin_set = min(di->rx_iout_max, di->rx_iout_limit);
	if (di->sysfs_data.rx_iout_max > 0)
		iin_set = min(iin_set, di->sysfs_data.rx_iout_max);
	return iin_set;
}
bool wireless_charge_check_tx_exist(void)
{
	struct wireless_charge_device_info *di = g_wireless_di;
	if (!di) {
		hwlog_err("%s: di null\n", __func__);
		return false;
	}

	return di->ops->check_tx_exist();
}

void wlc_rx_ext_pwr_prev_ctrl(int flag)
{
	struct wireless_charge_device_info *di = g_wireless_di;

	if (di && di->ops && di->ops->ext_pwr_prev_ctrl)
		di->ops->ext_pwr_prev_ctrl(flag);
}

void wlc_rx_ext_pwr_post_ctrl(int flag)
{
	struct wireless_charge_device_info *di = g_wireless_di;

	if (di && di->ops && di->ops->ext_pwr_post_ctrl)
		di->ops->ext_pwr_post_ctrl(flag);
}

static int wireless_charge_send_ept
	(struct wireless_charge_device_info *di, enum wireless_etp_type type)
{
	if (WIRELESS_CHANNEL_OFF == g_wireless_channel_state) {
		hwlog_err("%s: not in wireless charging\n", __func__);
		return -1;
	}
	return di->ops->send_ept(type);
}

static void wlc_rx_chip_reset(struct wireless_charge_device_info *di)
{
	if (di->wlc_err_rst_cnt >= WLC_RST_CNT_MAX)
		return;

	(void)di->ops->chip_reset();
	di->wlc_err_rst_cnt++;
	di->discon_delay_time = WL_RST_DISCONN_DELAY_MS;
}

static void wireless_charge_set_input_current(struct wireless_charge_device_info *di)
{
	int iin_set = wireless_charge_get_rx_iout_limit();

	wlc_set_rx_iout_limit(iin_set);
}

static int wireless_charge_get_tx_id(void)
{
	struct wireless_charge_device_info *di = g_wireless_di;
	if (!di) {
		hwlog_err("%s: di null\n", __func__);
		return -1;
	}
	if (WIRELESS_CHANNEL_OFF == g_wireless_channel_state) {
		hwlog_err("%s: not in wireless charging\n", __func__);
		return -1;
	}
	return wireless_get_tx_id(WIRELESS_PROTOCOL_QI);
}

static int wireless_charge_fix_tx_fop(int fop)
{
	struct wireless_charge_device_info *di = g_wireless_di;
	if (!di) {
		hwlog_err("%s: di null\n", __func__);
		return -1;
	}
	if (WIRELESS_CHANNEL_OFF == g_wireless_channel_state) {
		hwlog_err("%s: not in wireless charging\n", __func__);
		return -1;
	}

	return wireless_fix_tx_fop(WIRELESS_PROTOCOL_QI, fop);
}

static int wireless_charge_unfix_tx_fop(void)
{
	struct wireless_charge_device_info *di = g_wireless_di;
	if (!di) {
		hwlog_err("%s: di null\n", __func__);
		return -1;
	}
	if (WIRELESS_CHANNEL_OFF == g_wireless_channel_state) {
		hwlog_err("%s: not in wireless charging\n", __func__);
		return -1;
	}

	return wireless_unfix_tx_fop(WIRELESS_PROTOCOL_QI);
}

static int wireless_charge_set_tx_vout(int tx_vout)
{
	struct wireless_charge_device_info *di = g_wireless_di;
	if (!di) {
		hwlog_err("%s: di null\n", __func__);
		return -1;
	}
	if (WIRELESS_CHANNEL_OFF == g_wireless_channel_state) {
		hwlog_err("%s: not in wireless charging\n", __func__);
		return -1;
	}
	if (tx_vout > TX_DEFAULT_VOUT &&
		g_wired_channel_state == WIRED_CHANNEL_ON) {
		hwlog_err("%s: wired vbus connect, tx_vout should be set to %dmV at most\n",
			__func__, TX_DEFAULT_VOUT);
		return -1;
	}
	if (di->pwroff_reset_flag && tx_vout > TX_DEFAULT_VOUT) {
		hwlog_err("%s: pwroff_reset_flag = %d, tx_vout should be set to %dmV at most\n",
			__func__, di->pwroff_reset_flag, TX_DEFAULT_VOUT);
		return -1;
	}
	hwlog_info("[%s] tx_vout is set to %dmV\n", __func__, tx_vout);
	return di->ops->set_tx_vout(tx_vout);
}

int wireless_charge_set_rx_vout(int rx_vout)
{
	struct wireless_charge_device_info *di = g_wireless_di;
	if (!di) {
		hwlog_err("%s: di null\n", __func__);
		return -1;
	}
	if (WIRELESS_CHANNEL_OFF == g_wireless_channel_state) {
		hwlog_err("%s: not in wireless charging\n", __func__);
		return -1;
	}
	if (di->pwroff_reset_flag && rx_vout > RX_DEFAULT_VOUT) {
		hwlog_err("%s: pwroff_reset_flag = %d, rx_vout should be set to %dmV at most\n",
			__func__, di->pwroff_reset_flag, RX_DEFAULT_VOUT);
		return -1;
	}
	hwlog_info("%s: rx_vout is set to %dmV\n", __func__, rx_vout);
	return di->ops->set_rx_vout(rx_vout);
}
int wireless_charge_get_rx_vout(void)
{
	struct wireless_charge_device_info *di = g_wireless_di;
	if (!di) {
		hwlog_err("%s: di null\n", __func__);
		return -1;
	}
	return di->ops->get_rx_vout();
}
static int wireless_charge_get_rx_vout_reg(void)
{
	struct wireless_charge_device_info *di = g_wireless_di;
	if (!di) {
		hwlog_err("%s: di null\n", __func__);
		return -1;
	}
	return di->ops->get_rx_vout_reg();
}

int wireless_charge_get_tx_vout_reg(void)
{
	struct wireless_charge_device_info *di = g_wireless_di;

	if (!di) {
		hwlog_err("%s: di null\n", __func__);
		return -1;
	}
	return di->ops->get_tx_vout_reg();
}

int wireless_charge_get_rx_vrect(void)
{
	struct wireless_charge_device_info *di = g_wireless_di;
	if (!di) {
		hwlog_err("%s: di null\n", __func__);
		return -1;
	}
	return di->ops->get_rx_vrect();
}
int wireless_charge_get_rx_iout(void)
{
	struct wireless_charge_device_info *di = g_wireless_di;
	if (!di) {
		hwlog_err("%s: di null\n", __func__);
		return -1;
	}
	return di->ops->get_rx_iout();
}

void wireless_charge_get_tx_adaptor_type(u8 *type)
{
	struct wireless_charge_device_info *di = g_wireless_di;

	if (!di || !type)
		return;

	*type = di->tx_cap->type;
}

static int wireless_charge_get_rx_cep(void)
{
	struct wireless_charge_device_info *di = g_wireless_di;

	if (!di || !di->ops || !di->ops->get_rx_cep)
		return 0;

	return di->ops->get_rx_cep();
}

static int wireless_charge_get_rx_fop(void)
{
	struct wireless_charge_device_info *di = g_wireless_di;
	if (!di) {
		hwlog_err("%s: di null\n", __func__);
		return -1;
	}
	return di->ops->get_rx_fop();
}

struct wireless_protocol_tx_cap *wlc_get_tx_cap(void)
{
	struct wireless_charge_device_info *di = g_wireless_di;

	if (!di)
		return NULL;

	return di->tx_cap;
}

int wlc_get_rx_max_iout(void)
{
	if (g_rx_imax <= 0)
		return WLC_RX_DFT_IOUT_MAX;

	return g_rx_imax;
}

int wlc_get_rx_def_imax(void)
{
	struct wireless_charge_device_info *di = g_wireless_di;

	if (!di || !di->ops || !di->ops->get_rx_def_imax)
		return WLC_RX_DFT_IOUT_MAX;

	return di->ops->get_rx_def_imax();
}

static int wlc_get_rx_temp(void)
{
	struct wireless_charge_device_info *di = g_wireless_di;

	if (!di || !di->ops || !di->ops->get_rx_temp)
		return -1;

	return di->ops->get_rx_temp();
}

int wireless_charge_get_rx_avg_iout(void)
{
	struct wireless_charge_device_info *di = g_wireless_di;
	if (!di) {
		hwlog_err("%s: di null\n", __func__);
		return -1;
	}
	return di->iout_avg;
}

int wlc_get_cp_avg_iout(void)
{
	int cp_ratio = charge_pump_get_cp_ratio(CP_TYPE_MAIN);

	if (cp_ratio <= 0) {
		hwlog_err("%s: cp_ratio err\n", __func__);
		return wireless_charge_get_rx_avg_iout();
	}

	return cp_ratio * wireless_charge_get_rx_avg_iout();
}

int wlc_get_pmode_id_by_mode_name(const char *mode_name)
{
	int i;
	struct wireless_charge_device_info *di = g_wireless_di;

	if (!di || !mode_name) {
		hwlog_err("%s: di null\n", __func__);
		return 0;
	}
	for (i = 0; i < di->mode_data.total_mode; i++) {
		if (!strncmp(mode_name, di->mode_data.mode_para[i].mode_name,
			strlen(di->mode_data.mode_para[i].mode_name)))
			return i;
	}

	return 0;
}

static void wireless_charge_count_avg_iout(
	struct wireless_charge_device_info *di)
{
	int cnt_max;
	const char *cur_mode_name = NULL;

	if (di->monitor_interval <= 0)
		return;

	cnt_max = RX_AVG_IOUT_TIME / di->monitor_interval;

	if (g_bst_rst_complete && (di->iout_avg < RX_LOW_IOUT)) {
		di->iout_high_cnt = 0;
		di->iout_low_cnt++;
		if (di->iout_low_cnt >= cnt_max)
			di->iout_low_cnt = cnt_max;
		return;
	}

	cur_mode_name = di->mode_data.mode_para[di->curr_pmode_index].mode_name;
	if ((di->iout_avg > RX_HIGH_IOUT) || strstr(cur_mode_name, "SC")) {
		di->iout_low_cnt = 0;
		di->iout_high_cnt++;
		if (di->iout_high_cnt >= cnt_max)
			di->iout_high_cnt = cnt_max;
		return;
	}
}

static void wireless_charge_calc_avg_iout(
	struct wireless_charge_device_info *di)
{
	int i;
	static int index = 0;
	int iout_sum = 0;

	rx_iout_samples[index] = wireless_charge_get_rx_iout();
	index = (index+1) % RX_IOUT_SAMPLE_LEN;
	for (i = 0; i < RX_IOUT_SAMPLE_LEN; i++)
		iout_sum += rx_iout_samples[i];
	di->iout_avg = iout_sum/RX_IOUT_SAMPLE_LEN;
}

static void wireless_charge_reset_avg_iout(struct wireless_charge_device_info *di)
{
	int i;

	for (i = 0; i < RX_IOUT_SAMPLE_LEN; i++)
		rx_iout_samples[i] = di->rx_iout_min;
	di->iout_avg = di->rx_iout_min;
}

enum wireless_charge_stage wlc_get_charge_stage(void)
{
	return g_wireless_charge_stage;
}

static void wireless_charge_set_charge_stage(enum wireless_charge_stage charge_stage)
{
	if (charge_stage < WIRELESS_STAGE_TOTAL &&
		g_wireless_charge_stage != charge_stage) {
		g_wireless_charge_stage = charge_stage;
		hwlog_info("[%s] set charge stage to %s\n", __func__, chrg_stage[charge_stage]);
	}
}

static int  wireless_charge_check_fast_charge_succ(
	struct wireless_charge_device_info *di)
{
	if ((wireless_fast_charge_flag || wireless_super_charge_flag) &&
		g_wireless_charge_stage >= WIRELESS_STAGE_CHARGING)
		return WIRELESS_CHRG_SUCC;
	else
		return WIRELESS_CHRG_FAIL;
}

static int  wireless_charge_check_normal_charge_succ(struct wireless_charge_device_info *di)
{
	if (WIRELESS_TYPE_ERR != di->tx_cap->type && !wireless_fast_charge_flag &&
		g_wireless_charge_stage >= WIRELESS_STAGE_CHARGING)
		return WIRELESS_CHRG_SUCC;
	else
		return WIRELESS_CHRG_FAIL;
}

static int wlc_formal_check_direct_charge(const char *m_name)
{
	int ret;

	ret = wldc_formal_check(m_name);
	if (!ret)
		wireless_charge_set_charge_stage(WIRELESS_STAGE_REGULATION_DC);

	return ret;
}

static void wlc_update_thermal_control(u8 thermal_ctrl)
{
	u8 thermal_status;

	thermal_status = thermal_ctrl & WLC_THERMAL_EXIT_SC_MODE;
	if ((thermal_status == WLC_THERMAL_EXIT_SC_MODE) && !g_high_pwr_test_flag)
		wlc_set_plimit_src(WLC_PLIM_THERMAL);
	else
		wlc_clear_plimit_src(WLC_PLIM_THERMAL);
}

static int wireless_set_thermal_ctrl(unsigned char value)
{
	struct wireless_charge_device_info *di = g_wireless_di;

	if (!di || value > 0xFF) /* 0xFF: maximum of u8 */
		return -EINVAL;
	di->sysfs_data.thermal_ctrl = value;
	wlc_update_thermal_control(di->sysfs_data.thermal_ctrl);
	hwlog_info("thermal_ctrl = 0x%x", di->sysfs_data.thermal_ctrl);
	return 0;
}

static int wireless_get_thermal_ctrl(unsigned char *value)
{
	struct wireless_charge_device_info *di = g_wireless_di;

	if (!di || !value)
		return -EINVAL;
	*value = di->sysfs_data.thermal_ctrl;
	return 0;
}

static int wireless_set_auth_service_ready(unsigned int val)
{
	g_wc_antifake_ready = val;
	hwlog_info("wc_antifake_ready=%d\n", g_wc_antifake_ready);
	return 0;
}

static int wireless_get_auth_service_ready(unsigned int *val)
{
	if (!val)
		return -1;

	*val = g_wc_antifake_ready;
	return 0;
}

static struct power_if_ops wl_if_ops = {
	.set_wl_thermal_ctrl = wireless_set_thermal_ctrl,
	.get_wl_thermal_ctrl = wireless_get_thermal_ctrl,
	.set_ready = wireless_set_auth_service_ready,
	.get_ready = wireless_get_auth_service_ready,
	.type_name = "wl",
};

static bool wlc_pmode_final_judge_crit(struct wireless_charge_device_info *di,
	int pmode_index)
{
	int tbatt = 0;
	struct wireless_mode_para *mode_para =
		&di->mode_data.mode_para[pmode_index];

	if (!di->tx_cap->support_12v &&
		(mode_para->ctrl_para.tx_vout == WIRELESS_ADAPTER_12V))
		return false;

	if ((di->tx_vout_max < mode_para->ctrl_para.tx_vout) ||
		(di->rx_vout_max < mode_para->ctrl_para.rx_vout))
		return false;

	bat_temp_get_temperature(BAT_TEMP_MIXED, &tbatt);
	if ((mode_para->tbatt >= 0) && (tbatt >= mode_para->tbatt))
		return false;

	if ((pmode_index == di->curr_pmode_index) &&
		(g_wireless_charge_stage != WIRELESS_STAGE_CHARGING)) {
		if ((mode_para->max_time > 0) &&
			time_after(jiffies, di->curr_power_time_out))
			return false;
	}

	return true;
}

static bool wlc_pmode_normal_judge_crit(struct wireless_charge_device_info *di,
	int pmode_index)
{
	struct wireless_mode_para *mode_para =
		&di->mode_data.mode_para[pmode_index];

	if ((mode_para->expect_cable_detect >= 0) &&
		(di->cable_detect_succ_flag != mode_para->expect_cable_detect))
		return false;
	if ((mode_para->expect_cert >= 0) &&
		(di->cert_succ_flag != mode_para->expect_cert))
		return false;

	return true;
}

static bool wlc_pmode_quick_judge_crit(struct wireless_charge_device_info *di,
	int pmode_index, int crit_type)
{
	struct wireless_mode_para *mode_para =
		&di->mode_data.mode_para[pmode_index];

	if ((di->tx_cap->vout_max < mode_para->tx_vout_min) ||
		(di->product_para.tx_vout < mode_para->ctrl_para.tx_vout) ||
		(di->product_para.rx_vout < mode_para->ctrl_para.rx_vout) ||
		(di->product_para.rx_iout < mode_para->ctrl_para.rx_iout))
		return false;

	if (di->tx_cap->vout_max * di->tx_cap->iout_max <
		mode_para->tx_vout_min * mode_para->tx_iout_min)
		return false;

	return true;
}

static bool wlc_pmode_dc_judge_crit(struct wireless_charge_device_info *di,
	int pmode_index)
{
	struct wireless_mode_para *mode_para =
		&di->mode_data.mode_para[pmode_index];

	if (wldc_prev_check(mode_para->mode_name))
		return false;
	if ((g_wireless_charge_stage == WIRELESS_STAGE_REGULATION_DC) ||
		g_in_wldc_check)
		return true;
	g_in_wldc_check = true;
	if (!wlc_formal_check_direct_charge(mode_para->mode_name)) {
		di->curr_pmode_index = pmode_index;
	} else {
		g_in_wldc_check = false;
		return false;
	}
	g_in_wldc_check = false;

	return true;
}

bool wireless_charge_mode_judge_criterion(int pmode_index, int crit_type)
{
	struct wireless_charge_device_info *di = g_wireless_di;

	if (!di) {
		hwlog_err("%s: di null\n", __func__);
		return false;
	}
	if ((pmode_index < 0) || (pmode_index >= di->mode_data.total_mode))
		return false;

	switch (crit_type) {
	case WLDC_MODE_FINAL_JUDGE_CRIT:
	case WIRELESS_MODE_FINAL_JUDGE_CRIT:
		if (!wlc_pmode_final_judge_crit(di, pmode_index))
			return false;
		/* fall-through */
	case WIRELESS_MODE_NORMAL_JUDGE_CRIT:
		if (!wlc_pmode_normal_judge_crit(di, pmode_index))
			return false;
		/* fall-through */
	case WIRELESS_MODE_QUICK_JUDGE_CRIT:
		if (!wlc_pmode_quick_judge_crit(di, pmode_index, crit_type))
			return false;
		break;
	default:
		hwlog_err("%s: crit_type = %d error\n", __func__, crit_type);
		return false;
	}

	if ((crit_type == WIRELESS_MODE_FINAL_JUDGE_CRIT) &&
		strstr(di->mode_data.mode_para[pmode_index].mode_name, "SC")) {
		if (!wlc_pmode_dc_judge_crit(di, pmode_index))
			return false;
	}

	return true;
}

static int  wireless_charge_check_fac_test_succ(
	struct wireless_charge_device_info *di)
{
	if (di->tx_cap->type == di->standard_tx_adaptor) {
		if (wireless_charge_mode_judge_criterion(1,
			WIRELESS_MODE_QUICK_JUDGE_CRIT))
			return wireless_charge_check_fast_charge_succ(di);
		return  wireless_charge_check_normal_charge_succ(di);
	}
	return WIRELESS_CHRG_FAIL;
}

static void wireless_charge_dsm_dump(struct wireless_charge_device_info *di,
	char* dsm_buff)
{
	int i;
	int tbatt = 0;
	char buff[ERR_NO_STRING_SIZE] = { 0 };
	int soc = huawei_battery_capacity();
	int vrect = wireless_charge_get_rx_vrect();
	int vout = wireless_charge_get_rx_vout();
	int iout = wireless_charge_get_rx_iout();

	bat_temp_get_temperature(BAT_TEMP_MIXED, &tbatt);
	snprintf(buff, sizeof(buff),
		"soc = %d, vrect = %dmV, vout = %dmV, iout = %dmA, iout_avg = %dmA, tbatt = %d\n",
		soc, vrect, vout, iout, di->iout_avg, tbatt);
	strncat(dsm_buff, buff, strlen(buff));
	snprintf(buff, ERR_NO_STRING_SIZE, "iout(mA): ");
	strncat(dsm_buff, buff, strlen(buff));
	for (i = 0; i < RX_IOUT_SAMPLE_LEN; i++) {
		snprintf(buff, ERR_NO_STRING_SIZE, "%d ", rx_iout_samples[i]);
		strncat(dsm_buff, buff, strlen(buff));
	}
}

static u8 wlc_rename_tx_type(struct wireless_charge_device_info *di)
{
	u8 tx_type = di->tx_cap->type;

	if ((tx_type >= QI_PARA_TX_TYPE_FAC_BASE) &&
		(tx_type <= QI_PARA_TX_TYPE_FAC_MAX))
		tx_type %= QI_PARA_TX_TYPE_FAC_BASE;
	else if ((tx_type >= QI_PARA_TX_TYPE_CAR_BASE) &&
		(tx_type <= QI_PARA_TX_TYPE_CAR_MAX))
		tx_type %= QI_PARA_TX_TYPE_CAR_BASE;
	else if ((tx_type >= QI_PARA_TX_TYPE_PWR_BANK_BASE) &&
		(tx_type <= QI_PARA_TX_TYPE_PWR_BANK_MAX))
		tx_type %= QI_PARA_TX_TYPE_PWR_BANK_BASE;

	return tx_type;
}

static void wireless_charge_dsm_report(struct wireless_charge_device_info *di,
	int err_no, char *dsm_buff)
{
	if (wlc_rename_tx_type(di) == WIRELESS_QC) {
		hwlog_info("[%s] ignore err_no:%d, tx_type:%d\n", __func__,
			err_no, di->tx_cap->type);
		return;
	}
	msleep(di->monitor_interval);
	if (g_wireless_channel_state == WIRELESS_CHANNEL_ON) {
		wireless_charge_dsm_dump(di, dsm_buff);
		power_dsm_dmd_report(POWER_DSM_BATTERY, err_no, dsm_buff);
	}
}

static void wireless_charge_send_fan_status_uevent(int fan_status)
{
	power_ui_event_notify(POWER_UI_NE_WL_FAN_STATUS, &fan_status);
}

static bool wlc_is_high_pwr_wltx(struct wireless_charge_device_info *di)
{
	int max_pwr_mw;

	if ((di->tx_cap->type != WIRELESS_OTG_A) &&
		(di->tx_cap->type != WIRELESS_OTG_B))
		return false;

	max_pwr_mw = di->tx_cap->vout_max / WL_MVOLT_PER_VOLT *
		di->tx_cap->iout_max;

	return max_pwr_mw >= WLC_HIGH_PWR_TH_FOR_WLTX;
}

static void wlc_revise_tx_cap(struct wireless_charge_device_info *di)
{
	if (di->support_high_pwr_wltx)
		return;

	if (wlc_is_high_pwr_wltx(di))
		di->tx_cap->no_need_cert = true;
}

static void wireless_charge_get_tx_capability(struct wireless_charge_device_info *di)
{
	if (WIRELESS_CHANNEL_OFF == g_wireless_channel_state) {
		hwlog_err("%s: not in wireless charging\n", __func__);
		return ;
	}
	wireless_get_tx_capability(WIRELESS_PROTOCOL_QI, di->tx_cap);
	wlc_revise_tx_cap(di);
	if (di->tx_cap->support_fan)
		wireless_charge_send_fan_status_uevent(1); /* 1: fan exist */
}

static void wlc_ignore_qval_work(struct work_struct *work)
{
	struct wireless_charge_device_info *di = g_wireless_di;

	if (!di)
		return;

	hwlog_info("[%s] timeout, permit SC mode\n", __func__);
	di->qval_support_mode = WLC_RX_SP_ALL_MODE;
}

static void wlc_preproccess_fod_status(struct wireless_charge_device_info *di)
{
	hwlog_err("%s: tx_fod_err, forbid SC mode\n", __func__);
	di->qval_support_mode = WLC_RX_SP_BUCK_MODE;
	schedule_delayed_work(&di->ignore_qval_work,
		msecs_to_jiffies(30000)); /* 30s timeout to restore state */
}

static bool wlc_need_ignore_fod_status(struct wireless_charge_device_info *di)
{
	int i;

	if (di->ignore_qval <= 0)
		return false;

	for (i = 0; i < ARRAY_SIZE(g_qval_err_tx); i++) {
		if (di->tx_type == g_qval_err_tx[i])
			return true;
	}

	return false;
}

static void wireless_charge_send_fod_status(
	struct wireless_charge_device_info *di)
{
	int ret;

	if (di->fod_status <= 0) {
		hwlog_debug("%s: fod_status invalid\n", __func__);
		return;
	}
	if (!di->tx_cap->support_fod_status) {
		hwlog_err("%s: tx not support fod_status detect\n", __func__);
		return;
	}
	if (wlc_need_ignore_fod_status(di)) {
		wlc_preproccess_fod_status(di);
		return;
	}
	hwlog_info("[%s] status=0x%04x\n", __func__, di->fod_status);
	ret = wireless_send_fod_status(WIRELESS_PROTOCOL_QI, di->fod_status);
	if (!ret) {
		hwlog_info("[%s] succ\n", __func__);
		return;
	}

	hwlog_err("%s: fail\n", __func__);
}

static void wireless_charge_get_tx_prop(struct wireless_charge_device_info *di)
{
	int i;
	u8 tx_type;
	struct wireless_tx_prop_para *tx_prop = NULL;

	if (di->tx_prop_data.total_prop_type <= 0) {
		hwlog_err("%s: total_prop_type is %d\n",
			__func__, di->tx_prop_data.total_prop_type);
		return;
	}

	tx_type = wlc_rename_tx_type(di);
	for (i = 0; i < di->tx_prop_data.total_prop_type; i++) {
		if (tx_type == di->tx_prop_data.tx_prop[i].tx_type) {
			di->curr_tx_type_index = i;
			break;
		}
	}

	if (i == di->tx_prop_data.total_prop_type)
		di->curr_tx_type_index = 0;

	tx_prop = &di->tx_prop_data.tx_prop[di->curr_tx_type_index];
	if (!di->tx_cap->vout_max)
		di->tx_cap->vout_max = tx_prop->tx_default_vout;

	if (!di->tx_cap->iout_max)
		di->tx_cap->iout_max = tx_prop->tx_default_iout;
}

static void wlc_reset_icon_pmode(struct wireless_charge_device_info *di)
{
	int i;

	for (i = 0; i < di->mode_data.total_mode; i++)
		set_bit(i, &di->icon_pmode);
	hwlog_info("[%s] icon_pmode=0x%x", __func__, di->icon_pmode);
}

void wlc_clear_icon_pmode(int pmode)
{
	struct wireless_charge_device_info *di = g_wireless_di;

	if (!di)
		return;
	if ((pmode < 0) || (pmode >= di->mode_data.total_mode))
		return;

	if (test_bit(pmode, &di->icon_pmode)) {
		clear_bit(pmode, &di->icon_pmode);
		hwlog_info("[%s] icon_pmode=0x%x", __func__, di->icon_pmode);
	}
}

static void wlc_revise_icon_display(struct wireless_charge_device_info *di, int *icon_type)
{
	if (*icon_type != WIRELESS_SUPER_CHARGE_FLAG)
		return;

	if (wlc_is_high_pwr_wltx(di))
		*icon_type = WIRELESS_FAST_CHARGE_FLAG;
}

void wireless_charge_icon_display(int crit_type)
{
	int pmode;
	int icon_type;
	struct wireless_charge_device_info *di = g_wireless_di;

	if (!di) {
		hwlog_err("%s: di null\n", __func__);
		return;
	}

	for (pmode = di->mode_data.total_mode - 1; pmode >= 0; pmode--) {
		if (test_bit(pmode, &di->icon_pmode) &&
			wireless_charge_mode_judge_criterion(pmode, crit_type))
			break;
	}

	if (pmode < 0) {
		pmode = 0;
		hwlog_err("%s: no power mode matched, set icon mode to %s\n",
			__func__, di->mode_data.mode_para[pmode].mode_name);
	}

	icon_type = di->mode_data.mode_para[pmode].icon_type;
	wlc_revise_icon_display(di, &icon_type);
	wireless_charge_send_charge_uevent(di, icon_type);
}

static void wlc_get_supported_max_rx_vout(
		struct wireless_charge_device_info *di)
{
	int pmode_index;

	if (!di)
		return;

	pmode_index = di->mode_data.total_mode - 1;
	for (; pmode_index >= 0; pmode_index--) {
		if (wireless_charge_mode_judge_criterion(pmode_index,
			WIRELESS_MODE_QUICK_JUDGE_CRIT))
			break;
	}
	if (pmode_index < 0)
		pmode_index = 0;

	di->supported_rx_vout =
		di->mode_data.mode_para[pmode_index].ctrl_para.rx_vout;

	hwlog_info("[%s] rx_support_mode = 0x%x\n", __func__,
		wlc_get_rx_support_mode());
}

static u8 *wlc_get_tx_fw_version(struct wireless_charge_device_info *di)
{
	if (g_tx_fw_version)
		return g_tx_fw_version;

	return wireless_get_tx_fw_version(WIRELESS_PROTOCOL_QI);
}

static void wireless_charge_get_tx_info(struct wireless_charge_device_info *di)
{
	if (!di->standard_tx) {
		hwlog_err("%s: not standard tx\n", __func__);
		return;
	}

	if ((di->tx_cap->type >= QI_PARA_TX_TYPE_FAC_BASE) &&
		(di->tx_cap->type <= QI_PARA_TX_TYPE_FAC_MAX))
		return;

	g_tx_fw_version = wlc_get_tx_fw_version(di);
	di->tx_type = wireless_get_tx_type(WIRELESS_PROTOCOL_QI);
	hwlog_info("[%s] tx_fw_version = %s, tx_type = %d\n", __func__,
		g_tx_fw_version, di->tx_type);
	wireless_get_tx_bigdata_info(WIRELESS_PROTOCOL_QI);
}

static void wireless_charge_set_default_tx_capability(struct wireless_charge_device_info *di)
{
	di->tx_cap->type = WIRELESS_TYPE_ERR;
	di->tx_cap->vout_max = 5000; /* default: 5V */
	di->tx_cap->iout_max = 1000; /* default: 1A */
	di->tx_cap->can_boost = 0;
	di->tx_cap->cable_ok = 0;
	di->tx_cap->no_need_cert = 0;
	di->tx_cap->support_scp = 0;
	di->tx_cap->support_extra_cap = 0;
	/* extra cap */
	di->tx_cap->support_fan = 0;
	di->tx_cap->support_tec = 0;
	di->tx_cap->support_get_ept = 0;
	di->tx_cap->support_fod_status = 0;
}

static int wireless_charge_wait_adapter_antifake(void)
{
	struct wireless_charge_device_info *di = g_wireless_di;

	if (!di) {
		hwlog_err("%s: di null\n", __func__);
		return -1;
	}

	if (!wait_for_completion_timeout(&di->wc_af_completion,
		WC_AF_WAIT_CT_TIMEOUT)) {
		hwlog_err("bms_auth service wait timeout\n");
		return -1;
	}

	/*
	 * if not timeout,
	 * return the antifake result base on the hash calc result
	 */
	if (g_wc_antifake_result == 0) {
		hwlog_err("bms_auth hash calculate fail\n");
		return -1;
	}

	hwlog_info("bms_auth hash calculate ok\n");
	return 0;
}

static void wlc_send_cert_confirm_msg(
	struct wireless_charge_device_info *di, bool cert_flag)
{
	if (!wlc_is_fac_tx(di))
		return;

	if (!wireless_send_cert_confirm(WIRELESS_PROTOCOL_QI, cert_flag))
		hwlog_info("%s: succ\n", __func__);
}

static int wlc_tx_certification(struct wireless_charge_device_info *di)
{
	int i;
	int ret;
	struct power_event_notify_data n_data;

	if (g_wireless_channel_state == WIRELESS_CHANNEL_OFF) {
		hwlog_err("%s: not in wireless charging\n", __func__);
		return -WLC_ERR_STOP_CHRG;
	}

	ret = wireless_auth_encrypt_start(WIRELESS_PROTOCOL_QI,
		di->dts.antifake_kid,
		random, WIRELESS_RANDOM_LEN, tx_cipherkey, WIRELESS_TX_KEY_LEN);
	if (ret) {
		hwlog_err("%s: get hash from tx fail\n", __func__);
		di->certi_comm_err_cnt++;
		return ret;
	}
	di->certi_comm_err_cnt = 0;

	memset(g_wc_af_key, 0x00, WIRELESS_RANDOM_LEN + WIRELESS_TX_KEY_LEN);
	for (i = 0; i < WIRELESS_RANDOM_LEN; i++)
		g_wc_af_key[i] = random[i];
	for (i = 0; i < WIRELESS_TX_KEY_LEN; i++)
		g_wc_af_key[WIRELESS_RANDOM_LEN + i] = tx_cipherkey[i];

	g_wc_antifake_result = 0;
	n_data.event = "AUTH_WCCT=";
	n_data.event_len = 10; /* length of AUTH_WCCT= */
	power_event_report_uevent(&n_data);
	ret = wireless_charge_wait_adapter_antifake();
	if (ret) {
		di->cert_succ_flag = WIRELESS_CHECK_FAIL;
		wireless_charge_set_charge_stage(WIRELESS_STAGE_CHECK_FWUPDATE);
		return -WLC_ERR_ACK_TIMEOUT;
	}

	return 0;
}

static void wireless_charge_set_ctrl_interval(struct wireless_charge_device_info *di)
{
	if (g_wireless_charge_stage < WIRELESS_STAGE_REGULATION) {
		di->ctrl_interval = CONTROL_INTERVAL_NORMAL;
	} else {
		di->ctrl_interval = CONTROL_INTERVAL_FAST;
	}
}

void wireless_charge_chip_init(int tx_vset)
{
	int ret;
	struct wireless_charge_device_info *di = g_wireless_di;

	if (!di) {
		hwlog_err("%s: di null\n", __func__);
		return;
	}

	ret = di->ops->chip_init(tx_vset, di->tx_type);
	if (ret < 0)
		hwlog_err("%s: rx chip init failed\n", __func__);
}

static void wlc_set_iout_min(struct wireless_charge_device_info *di)
{
	di->rx_iout_max = di->rx_iout_min;
	wireless_charge_set_input_current(di);
}

int wireless_charge_select_vout_mode(int vout)
{
	int id = 0;
	struct wireless_charge_device_info *di = g_wireless_di;

	if (!di) {
		hwlog_err("%s: di null\n", __func__);
		return id;
	}

	for (id = 0; id < di->volt_mode_data.total_volt_mode; id++) {
		if (vout == di->volt_mode_data.volt_mode[id].tx_vout)
			break;
	}
	if (id >= di->volt_mode_data.total_volt_mode) {
		id = 0;
		hwlog_err("%s: match vmode_index failed\n", __func__);
	}
	return id;
}

static void wlc_update_tx_alarm_vmax(struct wireless_charge_device_info *di)
{
	int vmax;

	vmax = wlrx_get_alarm_vlim();
	if (vmax <= 0)
		return;

	di->tx_vout_max = min(di->tx_vout_max, vmax);
	di->rx_vout_max = min(di->rx_vout_max, vmax);
}

static bool wireless_is_in_otg_mode(struct wireless_charge_device_info *di)
{
	int mode = VBUS_CH_NOT_IN_OTG_MODE;

	if (di->ignore_otg_mode)
		return false;

	vbus_ch_get_mode(VBUS_CH_USER_WR_TX,
		VBUS_CH_TYPE_BOOST_GPIO, &mode);

	return mode == VBUS_CH_IN_OTG_MODE;
}

void wireless_charge_update_max_vout_and_iout(bool ignore_cnt_flag)
{
	int soc;
	int i;
	struct wireless_charge_device_info *di = g_wireless_di;

	if (!di) {
		hwlog_err("%s: di null\n", __func__);
		return;
	}

	di->tx_vout_max = di->product_para.tx_vout;
	di->rx_vout_max = di->product_para.rx_vout;
	di->rx_iout_max = di->product_para.rx_iout;
	di->tx_vout_max = min(di->tx_vout_max, di->sysfs_data.tx_vout_max);
	di->rx_vout_max = min(di->rx_vout_max, di->sysfs_data.rx_vout_max);
	di->rx_iout_max = min(di->rx_iout_max, di->sysfs_data.rx_iout_max);

	if (wireless_is_in_otg_mode(di) || di->pwroff_reset_flag) {
		di->tx_vout_max = min(di->tx_vout_max, TX_DEFAULT_VOUT);
		di->rx_vout_max = min(di->rx_vout_max, RX_DEFAULT_VOUT);
		di->rx_iout_max = min(di->rx_iout_max, RX_DEFAULT_IOUT);
	}
	wlc_update_plimit_para();
	wlc_update_tx_alarm_vmax(di);
	/* check volt and curr limit caused by high soc */
	soc = huawei_battery_capacity();
	for(i = 0; i < di->segment_data.segment_para_level; i++) {
		if(soc >= di->segment_data.segment_para[i].soc_min && soc <= di->segment_data.segment_para[i].soc_max) {
			di->tx_vout_max = min(di->tx_vout_max, di->segment_data.segment_para[i].tx_vout_limit);
			di->rx_vout_max = min(di->rx_vout_max, di->segment_data.segment_para[i].rx_vout_limit);
			di->rx_iout_max = min(di->rx_iout_max, di->segment_data.segment_para[i].rx_iout_limit);
			break;
		}
	}
	if (!ignore_cnt_flag && di->iout_low_cnt >= RX_AVG_IOUT_TIME/di->monitor_interval) {
		di->tx_vout_max = min(di->tx_vout_max, TX_DEFAULT_VOUT);
		di->rx_vout_max = min(di->rx_vout_max, RX_DEFAULT_VOUT);
		di->rx_iout_max = min(di->rx_iout_max, RX_DEFAULT_IOUT);
	}
}

static void wlc_notify_charger_vout(struct wireless_charge_device_info *di)
{
	int tx_vout;
	int cp_vout;
	int cp_ratio;

	tx_vout = di->volt_mode_data.volt_mode[di->curr_vmode_index].tx_vout;
	cp_ratio = charge_pump_get_cp_ratio(CP_TYPE_MAIN);
	if (cp_ratio <= 0) {
		hwlog_err("%s: cp_ratio err\n", __func__);
		return;
	}
	hwlog_info("[%s] cp_ratio=%d\n", __func__, cp_ratio);
	cp_vout = tx_vout / cp_ratio;
	power_event_notify(POWER_NT_WLC, POWER_NE_WLC_CHARGER_VBUS, &cp_vout);
}

static void wlc_send_bst_succ_msg(struct wireless_charge_device_info *di)
{
	if (!wlc_is_fac_tx(di) ||
		(g_wireless_charge_stage != WIRELESS_STAGE_CHARGING))
		return;

	if (!wireless_send_rx_boost_succ(WIRELESS_PROTOCOL_QI))
		hwlog_info("[%s] send cmd boost_succ ok\n", __func__);

}

static void wlc_report_bst_fail_dmd(struct wireless_charge_device_info *di)
{
	static bool dsm_report_flag;
	char dsm_buff[POWER_DSM_BUF_SIZE_0512] = { 0 };

	if (++di->boost_err_cnt < BOOST_ERR_CNT_MAX) {
		dsm_report_flag = false;
		return;
	}

	di->boost_err_cnt = BOOST_ERR_CNT_MAX;
	if (dsm_report_flag)
		return;

	wireless_charge_dsm_report(di, ERROR_WIRELESS_BOOSTING_FAIL, dsm_buff);
	dsm_report_flag = true;
}

static int wlc_get_bst_delay_time(struct wireless_charge_device_info *di)
{
	if (!di || !di->ops || !di->ops->get_bst_delay_time)
		return RX_BST_DELAY_TIME;

	return di->ops->get_bst_delay_time();
}

static int wireless_charge_boost_vout(struct wireless_charge_device_info *di,
	int cur_vmode_id, int target_vmode_id)
{
	int vmode;
	int ret;
	int tx_vout;
	int bst_delay;
	int last_vout_reg;

	if (di->boost_err_cnt >= BOOST_ERR_CNT_MAX) {
		hwlog_debug("%s: boost fail exceed %d times\n",
			__func__, BOOST_ERR_CNT_MAX);
		return -WLC_ERR_CHECK_FAIL;
	}

	wlc_set_iout_min(di);
	wireless_charge_msleep(300); /* delay 300ms for ibus stability */
	g_bst_rst_complete = false;
	bst_delay = wlc_get_bst_delay_time(di);

	for (vmode = cur_vmode_id + 1; vmode <= target_vmode_id; vmode++) {
		last_vout_reg = wireless_charge_get_tx_vout_reg();
		tx_vout = di->volt_mode_data.volt_mode[vmode].tx_vout;
		ret = wireless_charge_set_tx_vout(tx_vout);
		if (ret) {
			hwlog_err("%s: boost fail\n", __func__);
			wlc_report_bst_fail_dmd(di);
			(void)wireless_charge_set_tx_vout(last_vout_reg);
			g_bst_rst_complete = true;
			return ret;
		}
		wlc_set_cur_vmode_id(di, vmode);
		wlc_notify_charger_vout(di);
		wlc_set_iout_min(di);
		if (vmode != target_vmode_id)
			wireless_charge_msleep(bst_delay); /* for vrect stability */
	}

	g_bst_rst_complete = true;
	di->boost_err_cnt = 0;
	power_event_notify(POWER_NT_WLC, POWER_NE_WLC_TX_VSET, &tx_vout);

	wlc_send_bst_succ_msg(di);
	return 0;
}

static int wireless_charge_reset_vout(struct wireless_charge_device_info *di,
	int cur_vmode_id, int target_vmode_id)
{
	int ret;
	int vmode;
	int tx_vout;
	int last_vout_reg;

	wlc_set_iout_min(di);
	wireless_charge_msleep(300); /* delay 300ms for ibus stability */
	g_bst_rst_complete = false;

	for (vmode = cur_vmode_id - 1; vmode >= target_vmode_id; vmode--) {
		last_vout_reg = wireless_charge_get_tx_vout_reg();
		tx_vout = di->volt_mode_data.volt_mode[vmode].tx_vout;
		ret = wireless_charge_set_tx_vout(tx_vout);
		if (ret) {
			hwlog_err("%s: reset fail\n", __func__);
			(void)wireless_charge_set_tx_vout(last_vout_reg);
			g_bst_rst_complete = true;
			return ret;
		}
		wlc_set_cur_vmode_id(di, vmode);
		wlc_notify_charger_vout(di);
		wlc_set_iout_min(di);
	}

	g_bst_rst_complete = true;
	return 0;
}

static int wireless_charge_set_vout(int cur_vmode_index, int target_vmode_index)
{
	int ret;
	int tx_vout;
	struct wireless_charge_device_info *di = g_wireless_di;

	if (!di) {
		hwlog_err("%s: di null\n", __func__);
		return -WLC_ERR_PARA_NULL;
	}

	tx_vout = di->volt_mode_data.volt_mode[target_vmode_index].tx_vout;
	if (target_vmode_index > cur_vmode_index)
		ret = wireless_charge_boost_vout(di,
			cur_vmode_index, target_vmode_index);
	else if (target_vmode_index < cur_vmode_index)
		ret = wireless_charge_reset_vout(di,
			cur_vmode_index, target_vmode_index);
	else
		return wireless_charge_set_rx_vout(tx_vout);

	if (g_wired_channel_state == WIRED_CHANNEL_ON) {
		hwlog_err("%s: wired vbus connect\n", __func__);
		return -WLC_ERR_STOP_CHRG;
	}
	if (g_wireless_channel_state == WIRELESS_CHANNEL_OFF) {
		hwlog_err("%s: wireless vbus disconnect\n", __func__);
		return -WLC_ERR_STOP_CHRG;
	}

	if (di->curr_vmode_index == cur_vmode_index)
		return ret;

	tx_vout = di->volt_mode_data.volt_mode[di->curr_vmode_index].tx_vout;
	wireless_charge_chip_init(tx_vout);
	wlc_notify_charger_vout(di);

	return ret;
}

int wldc_set_trx_vout(int vout)
{
	int cur_vmode;
	int target_vmode;
	int tx_vout_reg;

	tx_vout_reg = wireless_charge_get_tx_vout_reg();
	cur_vmode = wireless_charge_select_vout_mode(tx_vout_reg);
	target_vmode = wireless_charge_select_vout_mode(vout);

	return wireless_charge_set_vout(cur_vmode, target_vmode);
}

static int wireless_charge_vout_control
		(struct wireless_charge_device_info *di, int pmode_index)
{
	int ret;
	int target_vout;
	int target_vmode_index;
	int tx_vout_reg;

	if (strstr(di->mode_data.mode_para[pmode_index].mode_name, "SC"))
		return 0;
	if (g_wireless_charge_stage == WIRELESS_STAGE_REGULATION_DC) {
		hwlog_err("%s: in dc mode\n", __func__);
		return -1;
	}
	if (g_wireless_channel_state != WIRELESS_CHANNEL_ON)
		return -1;
	tx_vout_reg = wireless_charge_get_tx_vout_reg();
	if (tx_vout_reg != di->volt_mode_data.volt_mode[di->curr_vmode_index].tx_vout) {
		hwlog_err("%s: tx_vout_reg %dmV != cur_mode_vout %dmV\n", __func__, tx_vout_reg,
			di->volt_mode_data.volt_mode[di->curr_vmode_index].tx_vout);
		ret = wireless_charge_set_tx_vout(di->volt_mode_data.volt_mode[di->curr_vmode_index].tx_vout);
		if (ret)
			hwlog_err("%s: set tx vout fail\n", __func__);
	}
	target_vout = di->mode_data.mode_para[pmode_index].ctrl_para.tx_vout;
	target_vmode_index = wireless_charge_select_vout_mode(target_vout);
	di->tx_vout_max = min(di->tx_vout_max, di->mode_data.mode_para[pmode_index].ctrl_para.tx_vout);
	di->rx_vout_max = min(di->rx_vout_max, di->mode_data.mode_para[pmode_index].ctrl_para.rx_vout);
	return wireless_charge_set_vout(di->curr_vmode_index, target_vmode_index);
}

static void wlc_update_imax_by_tx_plimit(struct wireless_charge_device_info *di)
{
	int ilim;
	struct wireless_ctrl_para *ctrl_para =
		&di->mode_data.mode_para[di->curr_pmode_index].ctrl_para;

	if (!ctrl_para)
		return;

	ilim = wlrx_get_alarm_ilim(ctrl_para->rx_vout);
	if (ilim <= 0)
		return;

	di->rx_iout_max = min(di->rx_iout_max, ilim);
}

static int wlc_start_sample_iout(struct wireless_charge_device_info *di)
{
	if ((di->tx_cap->type < QI_PARA_TX_TYPE_FAC_BASE) ||
		(di->tx_cap->type > QI_PARA_TX_TYPE_FAC_MAX))
		return -WLC_ERR_MISMATCH;
	if (strstr(di->mode_data.mode_para[di->curr_pmode_index].mode_name, "SC"))
		return -WLC_ERR_MISMATCH;

	if (!delayed_work_pending(&di->rx_sample_work)) {
		g_wlc_start_sample_flag = true;
		mod_delayed_work(system_wq, &di->rx_sample_work,
			msecs_to_jiffies(di->sample_delay_time));
	}

	if (g_wlc_start_sample_flag) {
		di->rx_iout_limit = di->rx_iout_max;
		wireless_charge_set_input_current(di);
		return 0;
	}

	return -WLC_ERR_CHECK_FAIL;
}

static void wlc_revise_vout_para(struct wireless_charge_device_info *di)
{
	int ret;
	int rx_vout_reg;
	int tx_vout_reg;
	struct wireless_ctrl_para *ctrl_para = NULL;

	if ((g_wireless_charge_stage == WIRELESS_STAGE_REGULATION_DC) ||
		(g_wireless_channel_state == WIRELESS_CHANNEL_OFF))
		return;

	tx_vout_reg = wireless_charge_get_tx_vout_reg();
	rx_vout_reg = wireless_charge_get_rx_vout_reg();

	ctrl_para = &di->mode_data.mode_para[di->curr_pmode_index].ctrl_para;

	if ((tx_vout_reg <= ctrl_para->tx_vout - RX_VREG_OFFSET) ||
		(tx_vout_reg >= ctrl_para->tx_vout + RX_VREG_OFFSET)) {
		hwlog_err("%s: revise tx_vout\n", __func__);
		ret = wireless_charge_set_tx_vout(ctrl_para->tx_vout);
		if (ret)
			hwlog_err("%s: set tx vout fail\n", __func__);
	}

	if ((rx_vout_reg <= ctrl_para->rx_vout - RX_VREG_OFFSET) ||
		(rx_vout_reg >= ctrl_para->rx_vout + RX_VREG_OFFSET)) {
		hwlog_err("%s: revise rx_vout\n", __func__);
		ret = wireless_charge_set_rx_vout(ctrl_para->rx_vout);
		if (ret)
			hwlog_err("%s: set rx vout fail\n", __func__);
	}
}

static void wlc_update_ilim_by_low_vrect(struct wireless_charge_device_info *di)
{
	static int rx_vrect_low_cnt;
	int cnt_max;
	int vrect = wireless_charge_get_rx_vrect();
	int charger_iin_regval = wlc_get_charger_iinlim_regval();
	struct wireless_mode_para *mode_para =
		&di->mode_data.mode_para[di->curr_pmode_index];

	if (di->ctrl_interval <= 0)
		return;

	cnt_max = RX_VRECT_LOW_RESTORE_TIME / di->ctrl_interval;
	if (vrect < mode_para->vrect_low_th) {
		if (++rx_vrect_low_cnt >= RX_VRECT_LOW_CNT) {
			rx_vrect_low_cnt = RX_VRECT_LOW_CNT;
			hwlog_err("%s: vrect[%dmV]<vrect_low_th[%dmV]\t"
				"decrease rx_iout %dmA\n", __func__, vrect,
				mode_para->vrect_low_th, di->rx_iout_step);
			di->rx_iout_limit = max(RX_VRECT_LOW_IOUT_MIN,
				charger_iin_regval - di->rx_iout_step);
			g_rx_vrect_restore_cnt = cnt_max;
		}
	} else if (g_rx_vrect_restore_cnt > 0) {
		rx_vrect_low_cnt = 0;
		g_rx_vrect_restore_cnt--;
		di->rx_iout_limit = charger_iin_regval;
	} else {
		rx_vrect_low_cnt = 0;
	}
}

static void wlc_update_iout_segment_para(struct wireless_charge_device_info *di)
{
	int i;
	int soc = huawei_battery_capacity();
	struct wireless_segment_para *seg_para = NULL;

	for (i = 0; i < di->segment_data.segment_para_level; i++) {
		seg_para = &di->segment_data.segment_para[i];
		if ((soc >= seg_para->soc_min) &&
			(soc <= seg_para->soc_max)) {
			di->rx_iout_max =
				min(seg_para->rx_iout_limit, di->rx_iout_max);
			break;
		}
	}
}

static void wlc_update_iout_ctrl_para(struct wireless_charge_device_info *di)
{
	int i;
	struct wireless_iout_ctrl_para *ictrl_para = NULL;

	for (i = 0; i < di->iout_ctrl_data.ictrl_para_level; i++) {
		ictrl_para = &di->iout_ctrl_data.ictrl_para[i];
		if ((di->iout_avg >= ictrl_para->iout_min) &&
			(di->iout_avg < ictrl_para->iout_max)) {
			di->rx_iout_limit = ictrl_para->iout_set;
			break;
		}
	}
}

static void wlc_update_iout_para(struct wireless_charge_device_info *di)
{
	wlc_update_iout_segment_para(di);
	wlc_update_iout_ctrl_para(di);
	wlc_update_ilim_by_low_vrect(di);
	wlc_update_imax_by_tx_plimit(di);
}

static void wlc_iout_control(struct wireless_charge_device_info *di)
{
	int ret;
	struct wireless_ctrl_para *ctrl_para =
		&di->mode_data.mode_para[di->curr_pmode_index].ctrl_para;

	if ((g_wireless_charge_stage == WIRELESS_STAGE_REGULATION_DC) ||
		(g_wireless_channel_state == WIRELESS_CHANNEL_OFF))
		return;

	if (di->pwroff_reset_flag)
		return;

	di->rx_iout_max = min(di->rx_iout_max, ctrl_para->rx_iout);
	di->rx_iout_max = min(di->rx_iout_max, di->tx_cap->iout_max);
	di->rx_iout_limit = di->rx_iout_max;

	ret = wlc_start_sample_iout(di);
	if (!ret)
		return;

	wlc_update_iout_para(di);
	wireless_charge_set_input_current(di);
}

static void wireless_charge_interference_work(struct work_struct *work)
{
	int i;
	int tx_fixed_fop;
	int tx_vout_max;
	int rx_vout_max;
	int rx_iout_max;
	struct wireless_charge_device_info *di = g_wireless_di;

	if (!di) {
		hwlog_err("%s: di null\n", __func__);
		return;
	}

	tx_fixed_fop = -1;
	tx_vout_max = di->product_para.tx_vout;
	rx_vout_max = di->product_para.rx_vout;
	rx_iout_max = di->product_para.rx_iout;

	for (i = 0; i < di->interfer_data.total_src; i++) {
		if (!(di->interfer_data.interfer_src_state & BIT(i)))
			continue;
		if (di->interfer_data.interfer_para[i].tx_fixed_fop >= 0)
			tx_fixed_fop =
			    di->interfer_data.interfer_para[i].tx_fixed_fop;
		if (di->interfer_data.interfer_para[i].tx_vout_limit >= 0)
			tx_vout_max = min(tx_vout_max,
			    di->interfer_data.interfer_para[i].tx_vout_limit);
		if (di->interfer_data.interfer_para[i].rx_vout_limit >= 0)
			rx_vout_max = min(rx_vout_max,
			    di->interfer_data.interfer_para[i].rx_vout_limit);
		if (rx_iout_max >= 0)
			rx_iout_max = min(rx_iout_max,
			    di->interfer_data.interfer_para[i].rx_iout_limit);
	}
	di->sysfs_data.tx_fixed_fop = tx_fixed_fop;
	di->sysfs_data.tx_vout_max = tx_vout_max;
	di->sysfs_data.rx_vout_max = rx_vout_max;
	di->sysfs_data.rx_iout_max = rx_iout_max;
	hwlog_info("[%s] fop = %d, tx_rx_vout = %d %d,iout_max = %d\n",
		__func__, di->sysfs_data.tx_fixed_fop,
		di->sysfs_data.tx_vout_max, di->sysfs_data.rx_vout_max,
		di->sysfs_data.rx_iout_max);
}

static void wireless_charger_update_interference_settings
	(struct wireless_charge_device_info *di, u8 interfer_src_state)
{
	int i;

	if (g_high_pwr_test_flag)
		return;

	for (i = 0; i < di->interfer_data.total_src; i++) {
		if (di->interfer_data.interfer_para[i].src_open == interfer_src_state) {
			di->interfer_data.interfer_src_state |= BIT(i);
			break;
		} else if (di->interfer_data.interfer_para[i].src_close == interfer_src_state) {
			di->interfer_data.interfer_src_state &= ~ BIT(i);
			break;
		}
	}
	if (i == di->interfer_data.total_src) {
		hwlog_err("%s: interference settings error\n", __func__);
		return;
	}
	if (!delayed_work_pending(&di->interfer_work)) {
		hwlog_info("[%s] delay %dms to schedule work\n",
			__func__, WIRELESS_INTERFER_TIMEOUT);
		schedule_delayed_work(&di->interfer_work,
			msecs_to_jiffies(WIRELESS_INTERFER_TIMEOUT));
	}
}
static void wireless_charge_update_fop(struct wireless_charge_device_info *di)
{
	int ret;
	if (!di->standard_tx) {
		hwlog_debug("%s: not standard tx, don't update fop\n", __func__);
		return;
	}
	if (di->sysfs_data.tx_fixed_fop > 0 && !g_fop_fixed_flag) {
		ret = wireless_charge_fix_tx_fop(di->sysfs_data.tx_fixed_fop);
		if (ret) {
			hwlog_err("%s: fix tx_fop fail\n", __func__);
			return;
		}
		hwlog_info("[%s] fop fixed to %dkHZ\n", __func__, di->sysfs_data.tx_fixed_fop);
		g_fop_fixed_flag = 1;
	}
	if (di->sysfs_data.tx_fixed_fop <= 0 && g_fop_fixed_flag) {
		ret = wireless_charge_unfix_tx_fop();
		if (ret) {
			hwlog_err("%s: unfix tx_fop fail", __func__);
			return;
		}
		hwlog_info("[%s] fop unfixed succ \n", __func__);
		g_fop_fixed_flag = 0;
	}
}

static void wlc_update_charge_state(struct wireless_charge_device_info *di)
{
	int ret;
	int soc;
	static int retry_cnt;

	if (!di->standard_tx || !wireless_charge_check_tx_exist() ||
		(g_wired_channel_state == WIRED_CHANNEL_ON))
		return;

	if (g_wireless_charge_stage <= WIRELESS_STAGE_CHARGING) {
		retry_cnt = 0;
		return;
	}

	soc = huawei_battery_capacity();
	if (soc >= CAPACITY_FULL)
		di->stat_rcd.chrg_state_cur |= WIRELESS_STATE_CHRG_FULL;
	else
		di->stat_rcd.chrg_state_cur &= ~WIRELESS_STATE_CHRG_FULL;

	if (di->stat_rcd.chrg_state_cur != di->stat_rcd.chrg_state_last) {
		if (retry_cnt >= WLC_SEND_CHARGE_STATE_RETRY_CNT) {
			retry_cnt = 0;
			di->stat_rcd.chrg_state_last =
				di->stat_rcd.chrg_state_cur;
			return;
		}
		hwlog_info("[%s] charge_state=%d\n",
			__func__, di->stat_rcd.chrg_state_cur);
		ret = wireless_send_charge_state(WIRELESS_PROTOCOL_QI,
			di->stat_rcd.chrg_state_cur);
		if (ret) {
			hwlog_err("%s: send charge_state fail\n", __func__);
			retry_cnt++;
			return;
		}
		retry_cnt = 0;
		di->stat_rcd.chrg_state_last = di->stat_rcd.chrg_state_cur;
	}
}

static void wlc_check_voltage(struct wireless_charge_device_info *di)
{
	int cnt_max = RX_VOUT_ERR_CHECK_TIME / di->monitor_interval;
	int tx_vout_reg = wireless_charge_get_tx_vout_reg();
	int vout_reg = wireless_charge_get_rx_vout_reg();
	int vout = wireless_charge_get_rx_vout();
	int vbus = battery_get_vbus();

	if ((vout <= 0) || !g_bst_rst_complete ||
		(g_wireless_charge_stage < WIRELESS_STAGE_CHECK_TX_ID))
		return;

	vout = (vout >= vbus) ? vout : vbus;
	if (vout >= vout_reg * di->rx_vout_err_ratio / PERCENT) {
		g_rx_vout_err_cnt = 0;
		return;
	}

	if (di->iout_avg >= RX_EPT_IGNORE_IOUT)
		return;

	hwlog_err("%s: abnormal vout=%dmV", __func__, vout);
	if (++g_rx_vout_err_cnt < cnt_max)
		return;

	g_rx_vout_err_cnt = cnt_max;
	if (tx_vout_reg >= RX_HIGH_VOUT2) {
		wlc_set_plimit_src(WLC_PLIM_SRC_VOUT_ERR);
		hwlog_err("%s: high vout err\n", __func__);
		return;
	}
	hwlog_info("[%s] vout lower than %d*%d%%mV for %dms, send EPT\n",
		__func__, vout_reg, di->rx_vout_err_ratio,
		RX_VOUT_ERR_CHECK_TIME);
	wireless_charge_send_ept(di, WIRELESS_EPT_ERR_VOUT);
}

static void wlc_clear_interference_settings(void)
{
	struct wireless_charge_device_info *di = g_wireless_di;

	if (!di)
		return;

	di->interfer_data.interfer_src_state = 0;
	schedule_delayed_work(&di->interfer_work, msecs_to_jiffies(0));
}

void wlc_set_high_pwr_test_flag(bool flag)
{
	g_high_pwr_test_flag = flag;

	if (g_high_pwr_test_flag) {
		wlc_clear_plimit_src(WLC_PLIM_THERMAL);
		wlc_clear_interference_settings();
	}
}

bool wlc_get_high_pwr_test_flag(void)
{
	return g_high_pwr_test_flag;
}

static bool wlc_is_night_time(struct wireless_charge_device_info *di)
{
	struct timeval tv;
	struct rtc_time tm;

	if (di->sysfs_data.ignore_fan_ctrl)
		return false;
	if (g_high_pwr_test_flag)
		return false;
	if (wlc_is_car_tx(di))
		return false;

	do_gettimeofday(&tv); /* seconds since 1970-01-01 00:00:00 */
	tv.tv_sec -= sys_tz.tz_minuteswest * 60; /* GMT, 1min equal to 60s */
	rtc_time_to_tm(tv.tv_sec, &tm);

	/* night time: 21:00-7:00 */
	if ((tm.tm_hour >= 21) || (tm.tm_hour < 7))
		return true;

	return false;
}

static void wlc_fan_control_handle(struct wireless_charge_device_info *di,
	int *retry_cnt, u8 limit_val)
{
	int ret;

	if (*retry_cnt >= WLC_FAN_LIMIT_RETRY_CNT) {
		*retry_cnt = 0;
		di->stat_rcd.fan_last = di->stat_rcd.fan_cur;
		return;
	}

	hwlog_info("[%s] limit_val=0x%x\n", __func__, limit_val);
	ret = wireless_set_fan_speed_limit(WIRELESS_PROTOCOL_QI, limit_val);
	if (ret) {
		(*retry_cnt)++;
		return;
	}
	*retry_cnt = 0;
	di->stat_rcd.fan_last = di->stat_rcd.fan_cur;
}

static bool wlc_is_need_fan_control(struct wireless_charge_device_info *di)
{
	if (!di->standard_tx || !di->tx_cap->support_fan)
		return false;

	/* in charger mode, time zone is not available */
	if (power_cmdline_is_factory_mode() ||
		power_cmdline_is_powerdown_charging_mode())
		return false;

	return true;
}

static void wlc_update_fan_control(struct wireless_charge_device_info *di,
	bool force_flag)
{
	static int retry_cnt;
	int tx_pwr;
	u8 thermal_status;
	u8 fan_limit;

	if (!wlc_is_need_fan_control(di))
		return;
	if (!wireless_charge_check_tx_exist() ||
		(g_wired_channel_state == WIRED_CHANNEL_ON))
		return;
	if (!force_flag &&
		(g_wireless_charge_stage <= WIRELESS_STAGE_CHARGING)) {
		retry_cnt = 0;
		return;
	}

	thermal_status = di->sysfs_data.thermal_ctrl &
		WLC_THERMAL_FORCE_FAN_FULL_SPEED;
	tx_pwr = di->tx_cap->vout_max * di->tx_cap->iout_max;
	if (wlc_is_night_time(di)) {
		di->stat_rcd.fan_cur = WLC_FAN_HALF_SPEED_MAX;
		wlc_set_plimit_src(WLC_PLIM_SRC_FAN);
	} else if (tx_pwr <= WLC_FAN_CTRL_PWR) {
		di->stat_rcd.fan_cur = WLC_FAN_FULL_SPEED_MAX;
		wlc_clear_plimit_src(WLC_PLIM_SRC_FAN);
	} else if (thermal_status == WLC_THERMAL_FORCE_FAN_FULL_SPEED) {
		di->stat_rcd.fan_cur = WLC_FAN_FULL_SPEED;
		wlc_clear_plimit_src(WLC_PLIM_SRC_FAN);
	} else {
		di->stat_rcd.fan_cur = WLC_FAN_FULL_SPEED_MAX;
		wlc_clear_plimit_src(WLC_PLIM_SRC_FAN);
	}

	if (di->stat_rcd.fan_last != di->stat_rcd.fan_cur) {
		switch (di->stat_rcd.fan_cur) {
		case WLC_FAN_HALF_SPEED_MAX:
			fan_limit = WLC_FAN_HALF_SPEED_MAX_QI;
			break;
		case WLC_FAN_FULL_SPEED_MAX:
			fan_limit = WLC_FAN_FULL_SPEED_MAX_QI;
			break;
		case WLC_FAN_FULL_SPEED:
			fan_limit = WLC_FAN_FULL_SPEED_QI;
			break;
		default:
			return;
		}
		wlc_fan_control_handle(di, &retry_cnt, fan_limit);
	}
}

static void wireless_charge_update_status(struct wireless_charge_device_info *di)
{
	wireless_charge_update_fop(di);
	wlc_update_charge_state(di);
	wlc_update_fan_control(di, false);
}
static int wireless_charge_set_power_mode(struct wireless_charge_device_info *di, int pmode_index)
{
	int ret;
	if (pmode_index < 0 || pmode_index >= di->mode_data.total_mode)
		return -1;
	ret = wireless_charge_vout_control(di, pmode_index);
	if (!ret) {
		if (pmode_index != di->curr_pmode_index) {
			if (di->mode_data.mode_para[pmode_index].max_time > 0)
				di->curr_power_time_out = jiffies +
					msecs_to_jiffies(di->mode_data.mode_para[pmode_index].max_time * WL_MSEC_PER_SEC);
			di->curr_pmode_index = pmode_index;
			if (wireless_charge_set_rx_vout(di->mode_data.mode_para[di->curr_pmode_index].ctrl_para.rx_vout))
				hwlog_err("%s: set rx vout fail\n", __func__);
		}
	}
	return ret;
}

static void wireless_charge_switch_power_mode(
	struct wireless_charge_device_info *di, int start_id, int end_id)
{
	int ret;
	int p_id;

	if ((g_wireless_charge_stage != WIRELESS_STAGE_CHARGING) &&
		g_wlc_start_sample_flag) {
		hwlog_debug("%s: start sample, don't sw pmode\n", __func__);
		return;
	}
	if ((start_id < 0) || (end_id < 0))
		return;

	for (p_id = start_id; p_id >= end_id; p_id--) {
		if (!wireless_charge_mode_judge_criterion(p_id,
			WIRELESS_MODE_FINAL_JUDGE_CRIT))
			continue;
		if (strstr(di->mode_data.mode_para[p_id].mode_name, "SC"))
			return;
		ret = wireless_charge_set_power_mode(di, p_id);
		if (!ret)
			break;
	}
	if (p_id < 0) {
		di->curr_pmode_index = 0;
		wireless_charge_set_power_mode(di, di->curr_pmode_index);
	}
}

static void wireless_charge_power_mode_control(struct wireless_charge_device_info *di)
{
	if (wireless_charge_mode_judge_criterion(di->curr_pmode_index, WIRELESS_MODE_FINAL_JUDGE_CRIT)) {
		if (WIRELESS_STAGE_CHARGING == g_wireless_charge_stage)
			wireless_charge_switch_power_mode(di, di->mode_data.total_mode - 1, 0);
		else
			wireless_charge_switch_power_mode(di,
				di->mode_data.mode_para[di->curr_pmode_index].expect_mode, di->curr_pmode_index + 1);
	} else {
		wireless_charge_switch_power_mode(di, di->curr_pmode_index - 1, 0);
	}
	wlc_revise_vout_para(di);
	wlc_iout_control(di);
}
int wireless_charge_get_power_mode(void)
{
	struct wireless_charge_device_info *di = g_wireless_di;
	if (!di) {
		hwlog_err("%s: di null\n", __func__);
		return -1;
	}
	return di->curr_pmode_index;
}

int wlc_get_expected_pmode_id(int pmode_id)
{
	struct wireless_charge_device_info *di = g_wireless_di;

	if (!di) {
		hwlog_err("%s: di null\n", __func__);
		return -WLC_ERR_PARA_NULL;
	}

	return di->mode_data.mode_para[pmode_id].expect_mode;
}

void wlc_set_cur_pmode_id(int pmode_id)
{
	struct wireless_charge_device_info *di = g_wireless_di;

	if (!di)
		return;

	if ((pmode_id < 0) || (pmode_id >= di->mode_data.total_mode))
		return;

	di->curr_pmode_index = pmode_id;
}

static void wlc_recheck_cert_preprocess(struct wireless_charge_device_info *di)
{
	/* vout may be 9v, so here reset 5V for cert stability */
	if (wldc_set_trx_vout(TX_DEFAULT_VOUT))
		hwlog_err("%s: set default vout failed\n", __func__);

	wlc_set_iout_min(di);
	wireless_charge_msleep(300); /* delay 300ms for ibus stability */
	wireless_charge_set_charge_stage(WIRELESS_STAGE_CERTIFICATION);
}

static void wireless_charge_regulation(struct wireless_charge_device_info *di)
{
	if ((g_wireless_charge_stage != WIRELESS_STAGE_REGULATION) ||
		(g_wireless_channel_state == WIRELESS_CHANNEL_OFF))
		return;

	if (g_need_recheck_cert && g_wc_antifake_ready) {
		g_need_recheck_cert = false;
		wlc_recheck_cert_preprocess(di);
		return;
	}

	wireless_charge_update_max_vout_and_iout(false);
	wireless_charge_power_mode_control(di);
}

static void wireless_charge_start_charging(
	struct wireless_charge_device_info *di)
{
	const char *cur_mode_name = NULL;
	int cur_mode_tx_vout;

	if ((g_wireless_charge_stage != WIRELESS_STAGE_CHARGING) ||
	    (g_wireless_channel_state == WIRELESS_CHANNEL_OFF))
		return;

	wireless_charge_rx_wake_lock();
	if (di->ignore_otg_mode)
		dc_set_vusb_uv_det(SC_MODE, CHARGE_MULTI_IC, 0); /* disable vusb uv detect */
	/*
	 * avoid that charger has ignored RX_READY notifier_call_chain
	 * when charger vbus is not powered, so here redo notifier call
	 */
	wlc_get_supported_max_rx_vout(di);
	wlc_update_fan_control(di, true);
	wireless_charge_update_max_vout_and_iout(true);
	wireless_charge_icon_display(WIRELESS_MODE_NORMAL_JUDGE_CRIT);
	cur_mode_tx_vout =
		di->mode_data.mode_para[di->curr_pmode_index].ctrl_para.tx_vout;
	cur_mode_tx_vout = min(di->tx_vout_max, cur_mode_tx_vout);
	power_event_notify(POWER_NT_WLC, POWER_NE_WLC_CHARGER_VBUS, &cur_mode_tx_vout);

	di->iout_low_cnt = 0;
	wireless_charge_power_mode_control(di);
	(void)wireless_send_charge_event(WIRELESS_PROTOCOL_QI,
		RX_STATR_CHARGING);
	cur_mode_name =
		di->mode_data.mode_para[di->curr_pmode_index].mode_name;
	if (strstr(cur_mode_name, "SC"))
		return;

	wireless_charge_set_charge_stage(WIRELESS_STAGE_REGULATION);
}

static bool wlc_is_support_set_rpp_format(
	struct wireless_charge_device_info *di)
{
	int ret;
	u8 tx_rpp = 0;

	if (di->pmax <= 0)
		return false;

	if (di->cert_succ_flag != WIRELESS_CHECK_SUCC)
		return false;

	ret = wireless_get_rpp_format(WIRELESS_PROTOCOL_QI, &tx_rpp);
	if (!ret && (tx_rpp == QI_ACK_RPP_FORMAT_24BIT))
		return true;

	return false;
}

static int wlc_set_rpp_format(struct wireless_charge_device_info *di)
{
	int ret;
	int count = 0;

	do {
		ret = wireless_set_rpp_format(WIRELESS_PROTOCOL_QI, di->pmax);
		if (!ret) {
			hwlog_info("%s: succ\n", __func__);
			return 0;
		}
		wireless_charge_msleep(100); /* 100ms delay try again */
		count++;
		hwlog_err("%s: failed, try next time\n", __func__);
	} while (count < WLC_SET_RPP_FORMAT_RETRY_CNT);

	if (count < WLC_SET_RPP_FORMAT_RETRY_CNT) {
		hwlog_info("[%s] succ\n", __func__);
		return 0;
	}

	return -WLC_ERR_I2C_WR;
}

static void wlc_rpp_format_init(struct wireless_charge_device_info *di)
{
	int ret;

	if (!di->standard_tx) {
		hwlog_err("%s: not standard tx, no need init\n", __func__);
		return;
	}
	if ((di->tx_cap->type >= QI_PARA_TX_TYPE_FAC_BASE) &&
		(di->tx_cap->type < QI_PARA_TX_TYPE_FAC_MAX))
		return;

	if (!wlc_is_support_set_rpp_format(di)) {
		wlc_set_plimit_src(WLC_PLIM_SRC_RPP);
		return;
	}

	ret = wlc_set_rpp_format(di);
	if (!ret) {
		hwlog_info("[%s] succ\n", __func__);
		wlc_clear_plimit_src(WLC_PLIM_SRC_RPP);
		return;
	}
	wlc_set_plimit_src(WLC_PLIM_SRC_RPP);
}

static void wireless_charge_check_fwupdate(
	struct wireless_charge_device_info *di)
{
	int ret;

	if (( g_wireless_charge_stage != WIRELESS_STAGE_CHECK_FWUPDATE) ||
		(g_wireless_channel_state == WIRELESS_CHANNEL_OFF))
		return;

	ret = di->ops->check_fwupdate(WIRELESS_RX_MODE);
	if (!ret)
		wireless_charge_chip_init(WIRELESS_CHIP_INIT);

	charge_pump_chip_init(CP_TYPE_MAIN);
	charge_pump_chip_init(CP_TYPE_AUX);
	wlc_rpp_format_init(di);
	wireless_charge_set_charge_stage(WIRELESS_STAGE_CHARGING);
}

static bool wlc_need_check_certification(struct wireless_charge_device_info *di)
{
	int pmode;

	if (di->tx_cap->no_need_cert)
		return false;

	for (pmode = di->mode_data.total_mode - 1; pmode >= 0; pmode--) {
		if (wireless_charge_mode_judge_criterion(pmode,
			WIRELESS_MODE_QUICK_JUDGE_CRIT))
			break;
	}

	if (pmode < 0)
		pmode = 0;

	if (di->mode_data.mode_para[pmode].expect_cert > 0)
		return true;

	hwlog_info("%s: max pmode=%d\n", __func__, pmode);
	return false;
}

static void wireless_charge_check_certification(
	struct wireless_charge_device_info *di)
{
	int ret;
	char dsm_buff[POWER_DSM_BUF_SIZE_0512] = { 0 };

	if ((g_wireless_charge_stage != WIRELESS_STAGE_CERTIFICATION) ||
		(g_wireless_channel_state == WIRELESS_CHANNEL_OFF))
		return;

	if (di->certi_err_cnt >= CERTI_ERR_CNT_MAX) {
		di->cert_succ_flag = WIRELESS_CHECK_FAIL;
		wlc_send_cert_confirm_msg(di, false);
		if (di->certi_comm_err_cnt > 0) {
			wlc_rx_chip_reset(di);
		} else {
			wireless_charge_icon_display(
				WIRELESS_MODE_NORMAL_JUDGE_CRIT);
			wireless_charge_dsm_report(di,
				ERROR_WIRELESS_CERTI_COMM_FAIL, dsm_buff);
		}
		hwlog_err("%s: error exceed %d times\n",
			__func__, CERTI_ERR_CNT_MAX);
	} else if (wlc_need_check_certification(di)) {
		if (!g_wc_antifake_ready) {
			g_need_recheck_cert = true;
			wireless_charge_set_charge_stage(
				WIRELESS_STAGE_CHECK_FWUPDATE);
			return;
		}
		g_need_recheck_cert = false;
		wlc_set_iout_min(di);
		ret = wlc_tx_certification(di);
		if (ret) {
			hwlog_err("%s: fail\n", __func__);
			di->certi_err_cnt++;
			return;
		}
		hwlog_info("[%s] succ\n", __func__);
		power_event_notify(POWER_NT_WLC, POWER_NE_WLC_CERT_SUCC, NULL);
		wlc_send_cert_confirm_msg(di, true);
		di->cert_succ_flag = WIRELESS_CHECK_SUCC;
	} else {
		di->cert_succ_flag = WIRELESS_CHECK_UNKNOWN;
	}

	wireless_charge_set_charge_stage(WIRELESS_STAGE_CHECK_FWUPDATE);
}
static void wireless_charge_cable_detect(struct wireless_charge_device_info *di)
{
	if ((WIRELESS_STAGE_CABLE_DETECT != g_wireless_charge_stage) ||
		(WIRELESS_CHANNEL_OFF == g_wireless_channel_state))
		return;

	if (di->tx_prop_data.tx_prop[di->curr_tx_type_index].need_cable_detect) {
		di->cable_detect_succ_flag = di->tx_cap->cable_ok;
	} else {
		di->cable_detect_succ_flag = WIRELESS_CHECK_UNKNOWN;
	}
	if (WIRELESS_CHECK_FAIL == di->cable_detect_succ_flag) {
		di->cert_succ_flag = WIRELESS_CHECK_FAIL;
		hwlog_err("%s: cable detect failed, set cert_succ_flag %d\n", __func__, di->cert_succ_flag);
		wireless_charge_icon_display(WIRELESS_MODE_NORMAL_JUDGE_CRIT);
		wireless_charge_set_charge_stage(WIRELESS_STAGE_CHECK_FWUPDATE);
		return;
	}
	hwlog_info("[%s] cable_detect_succ_flag: %d\n", __func__, di->cable_detect_succ_flag);
	wireless_charge_set_charge_stage(WIRELESS_STAGE_CERTIFICATION);
}

static void wlc_get_ept_type(struct wireless_charge_device_info *di)
{
	int ret;
	u16 ept_type = 0;

	if (!di->tx_cap->support_get_ept)
		return;

	ret = wireless_get_ept_type(WIRELESS_PROTOCOL_QI, &ept_type);
	if (ret) {
		hwlog_err("%s: failed\n", __func__);
		return;
	}
	hwlog_info("[%s] type=0x%x\n", __func__, ept_type);
}

static void wireless_charge_check_tx_ability(struct wireless_charge_device_info *di)
{
	char dsm_buff[POWER_DSM_BUF_SIZE_0512] = {0};

	if ((WIRELESS_STAGE_CHECK_TX_ABILITY != g_wireless_charge_stage) ||
		(WIRELESS_CHANNEL_OFF == g_wireless_channel_state))
		return;

	hwlog_info("%s ++\n", __func__);
	if (di->tx_ability_err_cnt >= TX_ABILITY_ERR_CNT_MAX) {
		wireless_charge_get_tx_prop(di);
		wireless_charge_get_tx_info(di);
		wireless_charge_send_fod_status(di);
		hwlog_err("%s: error exceed %d times\n",
			__func__, TX_ABILITY_ERR_CNT_MAX);
		if (di->standard_tx &&
			(di->wlc_err_rst_cnt >= WLC_RST_CNT_MAX))
			wireless_charge_dsm_report(di,
				ERROR_WIRELESS_CHECK_TX_ABILITY_FAIL,
				dsm_buff);
		wlc_rx_chip_reset(di);
		wireless_charge_set_charge_stage(WIRELESS_STAGE_CABLE_DETECT);
		return;
	}
	wlc_set_iout_min(di);
	wireless_charge_get_tx_capability(di);
	if (WIRELESS_TYPE_ERR == di->tx_cap->type) {
		hwlog_err("%s: get tx ability failed\n", __func__);
		di->tx_ability_err_cnt ++;
		return;
	}
	power_event_notify(POWER_NT_WLC, POWER_NE_WLC_TX_CAP_SUCC, NULL);
	wireless_charge_get_tx_prop(di);

	if (di->tx_cap->no_need_cert)
		wireless_charge_icon_display(WIRELESS_MODE_NORMAL_JUDGE_CRIT);
	else
		wireless_charge_icon_display(WIRELESS_MODE_QUICK_JUDGE_CRIT);

	wlc_get_ept_type(di);
	wireless_charge_set_charge_stage(WIRELESS_STAGE_CABLE_DETECT);
	wireless_charge_get_tx_info(di);
	wireless_charge_send_fod_status(di);
	hwlog_info("%s --\n", __func__);
}
static void wireless_charge_check_tx_id(struct wireless_charge_device_info *di)
{
	int tx_id;
	if ((WIRELESS_STAGE_CHECK_TX_ID != g_wireless_charge_stage) ||
		(WIRELESS_CHANNEL_OFF == g_wireless_channel_state))
		return;

	hwlog_info("[%s] ++\n", __func__);
	if (di->tx_id_err_cnt >= TX_ID_ERR_CNT_MAX) {
		wireless_charge_get_tx_prop(di);
		hwlog_err("%s: error exceed %d times, fast charge is disabled\n", __func__, TX_ID_ERR_CNT_MAX);
		wireless_charge_set_charge_stage(WIRELESS_STAGE_CABLE_DETECT);
		return;
	}
	wlc_set_iout_min(di);
	tx_id = wireless_charge_get_tx_id();
	if (tx_id < 0) {
		hwlog_err("%s: get id failed\n", __func__);
		di->tx_id_err_cnt++;
		return;
	}
	if (TX_ID_HW != tx_id) {
		wireless_charge_get_tx_prop(di);
		hwlog_err("%s: id(0x%x) is not correct(0x%x)\n", __func__, tx_id, TX_ID_HW);
		wireless_charge_set_charge_stage(WIRELESS_STAGE_CABLE_DETECT);
		return;
	}
	di->standard_tx = 1;
	power_event_notify(POWER_NT_WLC, POWER_NE_WLC_HS_SUCC, NULL);
	wireless_charge_set_charge_stage(WIRELESS_STAGE_CHECK_TX_ABILITY);
	hwlog_info("[%s] --\n", __func__);
	return;
}
static void wireless_charge_rx_stop_charing_config(struct wireless_charge_device_info *di)
{
	int ret;

	ret = di->ops->stop_charging();
	if (ret < 0)
		hwlog_err("%s: rx stop charing config failed\n", __func__);
	wireless_reset_dev_info(WIRELESS_PROTOCOL_QI);
}

static void wlc_state_record_para_init(struct wireless_charge_device_info *di)
{
	di->stat_rcd.chrg_state_cur = 0;
	di->stat_rcd.chrg_state_last = 0;
	di->stat_rcd.fan_cur = WLC_FAN_UNKNOWN_SPEED;
	di->stat_rcd.fan_last = WLC_FAN_UNKNOWN_SPEED;
}

static void wireless_charge_para_init(struct wireless_charge_device_info *di)
{
	di->monitor_interval = MONITOR_INTERVAL;
	di->ctrl_interval = CONTROL_INTERVAL_NORMAL;
	di->tx_vout_max = TX_DEFAULT_VOUT;
	di->rx_vout_max = RX_DEFAULT_VOUT;
	di->rx_iout_max = di->rx_iout_min;
	di->rx_iout_limit = di->rx_iout_min;
	di->standard_tx = 0;
	di->tx_id_err_cnt = 0;
	di->tx_ability_err_cnt = 0;
	di->certi_err_cnt = 0;
	di->certi_comm_err_cnt = 0;
	di->boost_err_cnt = 0;
	di->sysfs_data.en_enable = 0;
	di->iout_high_cnt = 0;
	di->iout_low_cnt = 0;
	di->cable_detect_succ_flag = 0;
	di->cert_succ_flag = 0;
	di->curr_tx_type_index = 0;
	di->curr_pmode_index = 0;
	wlc_set_cur_vmode_id(di, 0);
	di->curr_power_time_out = 0;
	di->pwroff_reset_flag = 0;
	di->supported_rx_vout = RX_DEFAULT_VOUT;
	di->tx_type = WIRELESS_TX_TYPE_UNKNOWN;
	di->qval_support_mode = WLC_RX_SP_ALL_MODE;
	g_rx_vrect_restore_cnt = 0;
	g_rx_vout_err_cnt = 0;
	g_need_recheck_cert = false;
	g_wlc_start_sample_flag = false;
	g_rx_ocp_cnt = 0;
	g_rx_ovp_cnt = 0;
	g_rx_otp_cnt = 0;
	g_tx_fw_version = NULL;
	wlrx_reset_fsk_alarm();
	wlc_reset_plimit();
	wlc_state_record_para_init(di);
	wireless_charge_set_default_tx_capability(di);
	wireless_charge_reset_avg_iout(di);
	wlc_reset_icon_pmode(di);
	wlc_set_iin_prop(di->rx_iout_step, CHARGE_CURRENT_DELAY);
}
static void wireless_charge_control_work(struct work_struct *work)
{
	struct wireless_charge_device_info *di = g_wireless_di;
	if (!di) {
		hwlog_err("%s: di null\n", __func__);
		return;
	}

	wireless_charge_check_tx_id(di);
	wireless_charge_check_tx_ability(di);
	wireless_charge_cable_detect(di);
	wireless_charge_check_certification(di);
	wireless_charge_check_fwupdate(di);
	wireless_charge_start_charging(di);
	wireless_charge_regulation(di);
	wireless_charge_set_ctrl_interval(di);

	if ((g_wireless_channel_state == WIRELESS_CHANNEL_ON) &&
		(g_wireless_charge_stage != WIRELESS_STAGE_REGULATION_DC))
		schedule_delayed_work(&di->wireless_ctrl_work, msecs_to_jiffies(di->ctrl_interval));
}
void wireless_charge_restart_charging(enum wireless_charge_stage stage_from)
{
	struct wireless_charge_device_info *di = g_wireless_di;
	if (!di) {
		hwlog_err("%s: di null\n", __func__);
		return;
	}
	if (WIRELESS_CHANNEL_ON == g_wireless_channel_state &&
		g_wireless_charge_stage >= WIRELESS_STAGE_CHARGING) {
		wireless_charge_set_charge_stage(stage_from);
		schedule_delayed_work(&di->wireless_ctrl_work,
			msecs_to_jiffies(100)); /* 100ms for pmode stability */
	}
}

static void wireless_charge_switch_off(void)
{
	wlps_control(WLPS_SC_SW2, WLPS_CTRL_OFF);
	wlps_control(WLPS_RX_SW_AUX, WLPS_CTRL_OFF);
	charge_pump_chip_enable(CP_TYPE_AUX, false);
	wlps_control(WLPS_RX_SW, WLPS_CTRL_OFF);
	charge_pump_chip_enable(CP_TYPE_MAIN, false);
	wlps_control(WLPS_SC_SW2, WLPS_CTRL_OFF);
}

static void wireless_charge_stop_charging(struct wireless_charge_device_info *di)
{
	hwlog_info("%s ++\n", __func__);
	wireless_charge_sleep_en_enable(RX_SLEEP_EN_ENABLE);
	pd_dpm_ignore_vbus_only_event(false);
	wireless_charge_set_charge_stage(WIRELESS_STAGE_DEFAULT);
	wlc_set_iin_prop(0, 0);
	wireless_charge_rx_stop_charing_config(di);
	wireless_fast_charge_flag = 0;
	wireless_super_charge_flag = 0;
	g_fop_fixed_flag = 0;
	cancel_delayed_work_sync(&di->rx_sample_work);
	cancel_delayed_work_sync(&di->wireless_ctrl_work);
	cancel_delayed_work_sync(&di->ignore_qval_work);
	di->curr_pmode_index = 0;
	di->curr_icon_type = 0;
	di->wlc_err_rst_cnt = 0;
	di->supported_rx_vout = RX_DEFAULT_VOUT;
	wireless_charge_set_default_tx_capability(di);
	wireless_charge_rx_wake_unlock();
	if (di->ignore_otg_mode)
		dc_set_vusb_uv_det(SC_MODE, CHARGE_MULTI_IC, 1); /* enable vusb uv detect */
	hwlog_info("%s --\n", __func__);
}

static void wlc_wireless_vbus_connect_handler(
	enum wireless_charge_stage stage_from)
{
	struct wireless_charge_device_info *di = g_wireless_di;

	if (!di) {
		hwlog_err("%s: di null\n", __func__);
		return;
	}

	if (g_wired_channel_state == WIRED_CHANNEL_ON) {
		hwlog_err("%s: wired vbus connect\n", __func__);
		return;
	}

	wireless_charge_set_wireless_channel_state(WIRELESS_CHANNEL_ON);
	wired_chsw_set_wired_channel(WIRED_CHANNEL_ALL, WIRED_CHANNEL_CUTOFF);
	wlps_control(WLPS_RX_SW, WLPS_CTRL_ON);
	charge_pump_chip_enable(CP_TYPE_MAIN, true);
	wireless_charge_sleep_en_enable(RX_SLEEP_EN_DISABLE);
	wireless_charge_chip_init(WIRELESS_CHIP_INIT);
	di->curr_pmode_index = 0;
	wlc_set_cur_vmode_id(di, 0);
	di->tx_vout_max = di->mode_data.mode_para[0].ctrl_para.tx_vout;
	di->rx_vout_max = di->mode_data.mode_para[0].ctrl_para.rx_vout;
	if (wireless_charge_set_rx_vout(di->rx_vout_max))
		hwlog_err("%s: set rx vout failed\n", __func__);

	if (g_wireless_channel_state == WIRELESS_CHANNEL_ON) {
		wireless_charge_set_charge_stage(stage_from);
		mod_delayed_work(system_wq, &di->wireless_ctrl_work,
			msecs_to_jiffies(di->ctrl_interval));
		power_event_notify(POWER_NT_WLC, POWER_NE_WLC_CHARGER_VBUS, &di->tx_vout_max);
		hwlog_info("%s --\n", __func__);
	}
}

static void wireless_charge_wireless_vbus_disconnect_handler(void)
{
	struct wireless_charge_device_info *di = g_wireless_di;

	if (!di) {
		hwlog_err("%s: di null\n", __func__);
		return;
	}
	if (wireless_charge_check_tx_exist()) {
		hwlog_info("[%s] tx exist, ignore\n", __func__);
		mod_delayed_work(system_wq,
			&di->wireless_monitor_work, msecs_to_jiffies(0));
		mod_delayed_work(system_wq,
			&di->wireless_watchdog_work, msecs_to_jiffies(0));
		wldc_set_charge_stage(WLDC_STAGE_DEFAULT);
		wlc_wireless_vbus_connect_handler(WIRELESS_STAGE_REGULATION);
		return;
	}
	wireless_charge_set_wireless_channel_state(WIRELESS_CHANNEL_OFF);
	wireless_charge_switch_off();
	wlc_handle_sink_event(false);
	wireless_charge_stop_charging(di);
}

static void wireless_charge_wireless_vbus_disconnect_work(
	struct work_struct *work)
{
	wireless_charge_wireless_vbus_disconnect_handler();
}

static void wireless_charge_wired_vbus_connect_work(struct work_struct *work)
{
	int i, vout, ret;
	struct wireless_charge_device_info *di = g_wireless_di;
	if (!di) {
		hwlog_err("%s: di null\n", __func__);
		return;
	}
	mutex_lock(&g_rx_en_mutex);
	vout = wireless_charge_get_rx_vout();
	if (vout >= RX_HIGH_VOUT) {
		wireless_charge_rx_stop_charing_config(di);
		ret = wireless_charge_set_tx_vout(TX_DEFAULT_VOUT);
		ret |= wireless_charge_set_rx_vout(TX_DEFAULT_VOUT);
		if (ret)
			hwlog_err("%s: set trx vout fail\n", __func__);
		if (WIRED_CHANNEL_OFF == g_wired_channel_state) {
			hwlog_err("%s: wired vubs already off, reset rx\n", __func__);
			wlc_rx_chip_reset(di);
		}
		if (!wireless_is_in_tx_mode())
			wireless_charge_en_enable(RX_EN_DISABLE);
		wireless_charge_set_wireless_channel_state(WIRELESS_CHANNEL_OFF);
	} else {
		if (!wireless_is_in_tx_mode())
			wireless_charge_en_enable(RX_EN_DISABLE);
		wireless_charge_set_wireless_channel_state(WIRELESS_CHANNEL_OFF);
	}
	mutex_unlock(&g_rx_en_mutex);
	/* wait for 50ms*10=500ms at most, generally 300ms at most */
	for (i = 0; i < 10; i++) {
		if (wldc_is_stop_charging_complete()) {
			wired_chsw_set_wired_channel(WIRED_CHANNEL_MAIN, WIRED_CHANNEL_RESTORE);
			break;
		}
		msleep(50);
	}
	hwlog_info("wired vbus connect, turn off wireless channel\n");
	wireless_charge_stop_charging(di);
}
static void wireless_charge_wired_vbus_disconnect_work(struct work_struct *work)
{
	mutex_lock(&g_rx_en_mutex);
	wireless_charge_en_enable(RX_EN_ENABLE);
	mutex_unlock(&g_rx_en_mutex);
	hwlog_info("wired vbus disconnect, turn on wireless channel\n");
}
void wireless_charge_wired_vbus_connect_handler(void)
{
	struct wireless_charge_device_info *di = g_wireless_di;

	if (!di) {
		hwlog_err("%s: di null\n", __func__);
		wired_chsw_set_wired_channel(WIRED_CHANNEL_MAIN, WIRED_CHANNEL_RESTORE);
		wireless_charge_switch_off();
		return;
	}
	if (WIRED_CHANNEL_ON == g_wired_channel_state) {
		hwlog_err("%s: already in sink_vbus state, ignore\n", __func__);
		return;
	}
	hwlog_info("[%s] wired vbus connect\n", __func__);
	wireless_super_charge_flag = 0;
	wireless_charge_set_wired_channel_state(WIRED_CHANNEL_ON);
	wldc_tx_disconnect_handler();
	wireless_charge_switch_off();
	if (!wireless_fast_charge_flag)
		wired_chsw_set_wired_channel(WIRED_CHANNEL_MAIN, WIRED_CHANNEL_RESTORE);
	schedule_work(&di->wired_vbus_connect_work);
}
void wireless_charge_wired_vbus_disconnect_handler(void)
{
	struct wireless_charge_device_info *di = g_wireless_di;
	static bool first_in = true;
	if (!di) {
		hwlog_err("%s: di null\n", __func__);
		return;
	}
	if (!first_in && WIRED_CHANNEL_OFF == g_wired_channel_state) {
		hwlog_err("%s: not in sink_vbus state, ignore\n", __func__);
		return;
	}
	first_in = false;
	hwlog_info("[%s] wired vbus disconnect\n", __func__);
	wireless_charge_set_wired_channel_state(WIRED_CHANNEL_OFF);
	wired_chsw_set_wired_channel(WIRED_CHANNEL_ALL, WIRED_CHANNEL_CUTOFF);
	schedule_work(&di->wired_vbus_disconnect_work);
}
#ifdef CONFIG_DIRECT_CHARGER
void direct_charger_disconnect_event(void)
{
	struct wireless_charge_device_info *di = g_wireless_di;
	if (!di) {
		hwlog_err("%s: di null\n", __func__);
		return;
	}
	hwlog_info("wired vbus disconnect in scp charging mode\n");
	wireless_charge_set_wired_channel_state(WIRED_CHANNEL_OFF);
	wired_chsw_set_wired_channel(WIRED_CHANNEL_ALL, WIRED_CHANNEL_CUTOFF);
	schedule_work(&di->wired_vbus_disconnect_work);
}
#endif

void wireless_charger_pmic_vbus_handler(bool vbus_state)
{
	struct wireless_charge_device_info *di = g_wireless_di;
	int tx_vout_reg;

	if (!di)
		return;

	if (wireless_tx_get_tx_open_flag())
		return;
	tx_vout_reg = wireless_charge_get_tx_vout_reg();
	if (!vbus_state && (tx_vout_reg > TX_REG_VOUT) &&
		(di->cert_succ_flag == WIRELESS_CHECK_SUCC))
		wireless_charge_sleep_en_enable(RX_SLEEP_EN_ENABLE);

	if (di->ops && di->ops->pmic_vbus_handler)
		di->ops->pmic_vbus_handler(vbus_state);
}

static int wireless_charge_check_tx_disconnect
			(struct wireless_charge_device_info *di)
{
	if (wireless_charge_check_tx_exist())
		return 0;

	wldc_tx_disconnect_handler();
	wireless_charge_sleep_en_enable(RX_SLEEP_EN_ENABLE);
	wireless_charge_set_wireless_channel_state(WIRELESS_CHANNEL_OFF);
	wireless_charge_rx_stop_charing_config(di);
	cancel_delayed_work_sync(&di->wireless_ctrl_work);
	cancel_delayed_work_sync(&di->wireless_vbus_disconnect_work);
	schedule_delayed_work(&di->wireless_vbus_disconnect_work,
		msecs_to_jiffies(di->discon_delay_time));
	hwlog_err("%s: tx not exist, delay %dms to report disconnect event\n",
		__func__, di->discon_delay_time);

	return -1;
}

void wlc_reset_wireless_charge(void)
{
	struct wireless_charge_device_info *di = g_wireless_di;

	if (!di)
		return;

	if (delayed_work_pending(&di->wireless_vbus_disconnect_work))
		mod_delayed_work(system_wq, &di->wireless_vbus_disconnect_work,
			msecs_to_jiffies(0));
}

static int wlc_monitor_imax_by_time(struct wireless_charge_device_info *di)
{
	int i;
	int vset;
	static unsigned int start_time;
	unsigned int delta_time;

	if ((di->flag.mon_imax == WLC_FIRST_MON_IMAX) ||
		(start_time < WLC_START_MON_TIME_TH))
		start_time = current_kernel_time().tv_sec;

	vset = wireless_charge_get_tx_vout_reg();
	if (vset < RX_HIGH_VOUT2)
		return wlc_get_rx_def_imax();

	delta_time = current_kernel_time().tv_sec - start_time;
	for (i = 0; i < WLC_TIME_PARA_LEVEL; i++) {
		if (delta_time >= di->time_para[i].time_th)
			break;
	}

	if ((i >= WLC_TIME_PARA_LEVEL) ||
		(di->time_para[i].iout_max <= 0))
		return wlc_get_rx_def_imax();

	return di->time_para[i].iout_max;
}

static int wlc_monitor_imax_by_temp(struct wireless_charge_device_info *di)
{
	int i;
	int rx_temp;
	static int last_i;
	int cur_i = last_i;

	rx_temp = wlc_get_rx_temp();
	for (i = 0; i < WLC_TEMP_PARA_LEVEL; i++) {
		if ((rx_temp > di->temp_para[i].temp_lth) &&
			(rx_temp <= di->temp_para[i].temp_hth)) {
			if ((di->flag.mon_imax == WLC_FIRST_MON_IMAX) ||
				(last_i < i) ||
				(di->temp_para[i].temp_hth - rx_temp >
				di->temp_para[i].temp_back))
				cur_i = i;
			else
				cur_i = last_i;
			break;
		}
	}

	if ((i >= WLC_TEMP_PARA_LEVEL) ||
		(di->temp_para[cur_i].iout_max <= 0))
		return wlc_get_rx_def_imax();

	last_i = cur_i;

	return di->temp_para[cur_i].iout_max;
}

static int wlc_monitor_imax_by_chip_fault(struct wireless_charge_device_info *di)
{
	if (!di || !di->ops || !di->ops->get_rx_imax)
		return 5000; /* default 5000ma when chip is ok */

	return di->ops->get_rx_imax();
}

static void wlc_monitor_rx_imax(struct wireless_charge_device_info *di)
{
	int imax;
	int imax_by_time;
	int imax_by_chip_fault;

	imax = wlc_monitor_imax_by_temp(di);
	imax_by_time = wlc_monitor_imax_by_time(di);
	imax_by_chip_fault = wlc_monitor_imax_by_chip_fault(di);
	if (imax > imax_by_time)
		imax = imax_by_time;
	if (imax > imax_by_chip_fault)
		imax = imax_by_chip_fault;

	g_rx_imax = imax;
	if (di->flag.mon_imax == WLC_FIRST_MON_IMAX)
		di->flag.mon_imax = WLC_NON_FIRST_MON_IMAX;
}

static bool wlc_need_show_mon_info(struct wireless_charge_device_info *di)
{
	int iin_regval;
	static int iin_regval_last;
	static int cnt;

	if (g_wireless_charge_stage < WIRELESS_STAGE_CHARGING)
		return true;

	if (++cnt == MONITOR_LOG_INTERVAL / di->monitor_interval) {
		cnt = 0;
		return true;
	}
	if ((di->tx_cap->type >= QI_PARA_TX_TYPE_FAC_BASE) &&
		(di->tx_cap->type <= QI_PARA_TX_TYPE_FAC_MAX))
		return true;

	iin_regval = wlc_get_charger_iinlim_regval();
	if (iin_regval_last != iin_regval) {
		iin_regval_last = iin_regval;
		return true;
	}

	return false;
}

static void wlc_show_monitor_info(struct wireless_charge_device_info *di)
{
	int tbatt = 0;

	if (!wlc_need_show_mon_info(di))
		return;

	bat_temp_get_temperature(BAT_TEMP_MIXED, &tbatt);
	hwlog_info("monitor_info [sys]soc:%-3d tbatt:%d pmode:%d plim_src:0x%02x\t"
		"[dc] warn:%d err:%d\t"
		"[tx] plim:%d\t"
		"[rx] temp:%-3d fop:%-3d cep:%-3d vrect:%-5d vout=%-5d\t"
		"[rx] imax:%-4d iout:%-4d iout_avg:%-4d iin_reg:%-4d\t"
		"[sysfs] fop:%-3d irx:%-4d vrx:%-5d vtx:%-5d\n",
		huawei_battery_capacity(), tbatt,
		di->curr_pmode_index, wlc_get_plimit_src(),
		wldc_get_warning_cnt(), wldc_get_error_cnt(),
		wlrx_get_alarm_plim(), wlc_get_rx_temp(),
		wireless_charge_get_rx_fop(), wireless_charge_get_rx_cep(),
		wireless_charge_get_rx_vrect(), wireless_charge_get_rx_vout(),
		wlc_get_rx_max_iout(), wireless_charge_get_rx_iout(),
		di->iout_avg, wlc_get_charger_iinlim_regval(),
		di->sysfs_data.tx_fixed_fop, di->sysfs_data.rx_iout_max,
		di->sysfs_data.rx_vout_max, di->sysfs_data.tx_vout_max);
}

static void wireless_charge_monitor_work(struct work_struct *work)
{
	int ret;
	struct wireless_charge_device_info *di = g_wireless_di;

	if (!di) {
		hwlog_err("%s: di null\n", __func__);
		return;
	}

	ret = wireless_charge_check_tx_disconnect(di);
	if (ret) {
		hwlog_info("[%s] tx disconnect, stop monitor work\n", __func__);
		return;
	}
	wireless_charge_calc_avg_iout(di);
	wireless_charge_count_avg_iout(di);
	wlc_check_voltage(di);
	wireless_charge_update_status(di);
	wlc_monitor_rx_imax(di);
	wlc_show_monitor_info(di);

	schedule_delayed_work(&di->wireless_monitor_work,
		msecs_to_jiffies(di->monitor_interval));
}

static void wireless_charge_watchdog_work(struct work_struct *work)
{
	int ret;
	struct wireless_charge_device_info *di = g_wireless_di;

	if (!di || !di->ops || !di->ops->kick_watchdog)
		return;

	if (g_wireless_channel_state == WIRELESS_CHANNEL_OFF)
		return;

	ret = di->ops->kick_watchdog();
	if (ret)
		hwlog_err("%s: fail\n", __func__);

	/* kick watchdog at an interval of 100ms */
	schedule_delayed_work(&di->wireless_watchdog_work,
		msecs_to_jiffies(100));
}

static void wireless_charge_rx_sample_work(struct work_struct *work)
{
	struct wireless_charge_device_info *di = container_of(work, struct wireless_charge_device_info, rx_sample_work.work);
	int rx_vout;
	int rx_iout;
	if (!di) {
		hwlog_err("%s: di null\n", __func__);
		return;
	}

	if (!g_wlc_start_sample_flag)
		g_wlc_start_sample_flag = true;

	/* send confirm message to TX */
	rx_vout = di->ops->get_rx_vout();
	rx_iout = di->ops->get_rx_iout();
	wireless_send_rx_vout(WIRELESS_PROTOCOL_QI, rx_vout);
	wireless_send_rx_iout(WIRELESS_PROTOCOL_QI, rx_iout);

	hwlog_info("[%s] rx_vout = %d, rx_iout = %d\n", __func__, rx_vout,rx_iout);

	schedule_delayed_work(&di->rx_sample_work, msecs_to_jiffies(RX_SAMPLE_WORK_DELAY));
}
static void wireless_charge_pwroff_reset_work(struct work_struct *work)
{
	struct wireless_charge_device_info *di =
		container_of(work, struct wireless_charge_device_info, wireless_pwroff_reset_work);
	if (!di) {
		hwlog_err("%s: di null\n", __func__);
		wireless_charge_wake_unlock();
		return;
	}
	if (di->pwroff_reset_flag) {
		msleep(60); /* test result, about 60ms */
		(void)di->ops->chip_reset();
		wireless_charge_set_tx_vout(TX_DEFAULT_VOUT);
		wireless_charge_set_rx_vout(RX_DEFAULT_VOUT);
	}
	wireless_charge_wake_unlock();
}

static void wlc_rx_power_on_ready_handler(
	struct wireless_charge_device_info *di)
{
	static bool rx_pwr_good;

	wldc_set_charge_stage(WLDC_STAGE_DEFAULT);
	wireless_charge_set_charge_stage(WIRELESS_STAGE_DEFAULT);
	wireless_charge_para_init(di);

	if (di->rx_evt.pwr_good == RX_PWR_ON_GOOD) {
		rx_pwr_good = true;
		di->rx_evt.pwr_good == RX_PWR_ON_NOT_GOOD;
	}

	if (((di->rx_evt.type == POWER_NE_WLRX_PWR_ON) && rx_pwr_good) ||
		(di->rx_evt.type == POWER_NE_WLRX_READY)) {
		wltx_reset_reverse_charging();
		wlc_handle_sink_event(true);
	}
	pd_dpm_ignore_vbus_only_event(true);
	mod_delayed_work(system_wq, &di->wireless_monitor_work,
		msecs_to_jiffies(0));
	mod_delayed_work(system_wq, &di->wireless_watchdog_work,
		msecs_to_jiffies(0));
	if (delayed_work_pending(&di->wireless_vbus_disconnect_work))
		cancel_delayed_work_sync(&di->wireless_vbus_disconnect_work);
	if (di->rx_evt.type == POWER_NE_WLRX_READY) {
		if (di->flag.mon_imax != WLC_NO_NEED_MON_IMAX)
			di->flag.mon_imax = WLC_FIRST_MON_IMAX;
		if (!di->wlc_err_rst_cnt)
			wireless_fast_charge_flag = 0;
		di->discon_delay_time = WL_DISCONN_DELAY_MS;
		power_event_notify(POWER_NT_WLC, POWER_NE_WLC_READY, NULL);
		wlc_wireless_vbus_connect_handler(WIRELESS_STAGE_CHECK_TX_ID);
	}
}

static void wlc_handle_tx_bst_err_evt(void)
{
	hwlog_info("handle_tx_bst_err_evt\n");
	wlc_set_plimit_src(WLC_PLIM_TX_BST_ERR);
	wireless_charge_update_max_vout_and_iout(true);
}

static void wlc_handle_rx_ocp_evt(struct wireless_charge_device_info *di)
{
	char dsm_buff[POWER_DSM_BUF_SIZE_0512] = { 0 };

	hwlog_info("handle_rx_ocp_evt\n");
	if (g_wireless_charge_stage < WIRELESS_STAGE_REGULATION)
		return;

	if (++g_rx_ocp_cnt < RX_OCP_CNT_MAX)
		return;

	g_rx_ocp_cnt = RX_OCP_CNT_MAX;
	wireless_charge_dsm_report(di, ERROR_WIRELESS_RX_OCP, dsm_buff);
}

static void wlc_handle_rx_ovp_evt(struct wireless_charge_device_info *di)
{
	char dsm_buff[POWER_DSM_BUF_SIZE_0512] = { 0 };

	hwlog_info("handle_rx_ovp_evt\n");
	if (g_wireless_charge_stage < WIRELESS_STAGE_REGULATION)
		return;

	if (++g_rx_ovp_cnt < RX_OVP_CNT_MAX)
		return;

	g_rx_ovp_cnt = RX_OVP_CNT_MAX;
	wireless_charge_dsm_report(di, ERROR_WIRELESS_RX_OVP, dsm_buff);
}

static void wlc_handle_rx_otp_evt(struct wireless_charge_device_info *di)
{
	char dsm_buff[POWER_DSM_BUF_SIZE_0512] = { 0 };

	hwlog_info("handle_rx_otp_evt\n");
	if (g_wireless_charge_stage < WIRELESS_STAGE_REGULATION)
		return;

	if (++g_rx_otp_cnt < RX_OTP_CNT_MAX)
		return;

	g_rx_otp_cnt = RX_OTP_CNT_MAX;
	wireless_charge_dsm_report(di, ERROR_WIRELESS_RX_OTP, dsm_buff);
}

static void wlc_handle_rx_ldo_off_evt(struct wireless_charge_device_info *di)
{
	hwlog_info("handle_rx_ldo_off_evt\n");
	wlc_handle_sink_event(false);
	cancel_delayed_work_sync(&di->wireless_ctrl_work);
	cancel_delayed_work_sync(&di->wireless_monitor_work);
	cancel_delayed_work_sync(&di->wireless_watchdog_work);
}

static void wlc_rx_event_work(struct work_struct *work)
{
	struct wireless_charge_device_info *di = container_of(work,
		struct wireless_charge_device_info, rx_event_work.work);

	if (!di) {
		wireless_charge_wake_unlock();
		return;
	}

	switch(di->rx_evt.type) {
	case POWER_NE_WLRX_PWR_ON:
		hwlog_info("[%s] RX power on\n", __func__);
		wlc_rx_power_on_ready_handler(di);
		break;
	case POWER_NE_WLRX_READY:
		hwlog_info("[%s] RX ready\n", __func__);
		wlc_rx_power_on_ready_handler(di);
		break;
	case POWER_NE_WLRX_OCP:
		wlc_handle_rx_ocp_evt(di);
		break;
	case POWER_NE_WLRX_OVP:
		wlc_handle_rx_ovp_evt(di);
		break;
	case POWER_NE_WLRX_OTP:
		wlc_handle_rx_otp_evt(di);
		break;
	case POWER_NE_WLRX_LDO_OFF:
		wlc_handle_rx_ldo_off_evt(di);
		break;
	case POWER_NE_WLRX_TX_ALARM:
		wlrx_handle_fsk_alarm(&di->rx_evt.tx_alarm);
		break;
	case POWER_NE_WLRX_TX_BST_ERR:
		wlc_handle_tx_bst_err_evt();
		break;
	default:
		break;
	}
	wireless_charge_wake_unlock();
}

static void wlc_save_rx_evt_data(struct wireless_charge_device_info *di,
	unsigned long event, void *data)
{
	if (!data)
		return;

	switch (di->rx_evt.type) {
	case POWER_NE_WLRX_PWR_ON:
		di->rx_evt.pwr_good = *(int *)data;
		break;
	case POWER_NE_WLRX_TX_ALARM:
		memcpy(&di->rx_evt.tx_alarm, data,
			sizeof(struct wireless_protocol_tx_alarm));
		break;
	default:
		break;
	}
}

static int wireless_charge_rx_event_notifier_call(
	struct notifier_block *rx_event_nb, unsigned long event, void *data)
{
	struct wireless_charge_device_info *di = container_of(rx_event_nb,
		struct wireless_charge_device_info, rx_event_nb);

	if (!di)
		return NOTIFY_OK;

	switch (event) {
	case POWER_NE_WLRX_PWR_ON:
	case POWER_NE_WLRX_READY:
	case POWER_NE_WLRX_OCP:
	case POWER_NE_WLRX_OVP:
	case POWER_NE_WLRX_OTP:
	case POWER_NE_WLRX_LDO_OFF:
	case POWER_NE_WLRX_TX_ALARM:
	case POWER_NE_WLRX_TX_BST_ERR:
		break;
	default:
		return NOTIFY_OK;
	}

	wireless_charge_wake_lock();
	di->rx_evt.type = (int)event;
	wlc_save_rx_evt_data(di, event, data);

	cancel_delayed_work_sync(&di->rx_event_work);
	mod_delayed_work(system_wq, &di->rx_event_work,
		msecs_to_jiffies(0));

	return NOTIFY_OK;
}

static int wlc_sysfs_get_chip_info(char *buf, int len)
{
	struct wireless_charge_device_info *di = g_wireless_di;
	u8 chip_info[WL_CHIP_INFO_STR_LEN] = { 0 };

	if (!di || !di->ops || !di->ops->get_chip_info ||
		!buf || (len < WL_CHIP_INFO_STR_LEN))
		return 0;

	if (di->ops->get_chip_info(chip_info, WL_CHIP_INFO_STR_LEN))
		return 0;

	return snprintf(buf, len, "%s\n", chip_info);
}

static int wlc_sysfs_get_die_id(char *buf, int len)
{
	struct wireless_charge_device_info *di = g_wireless_di;
	u8 die_id[WL_DIE_ID_STR_LEN] = { 0 };

	if (!di || !di->ops || !di->ops->get_die_id ||
		!buf || (len < WL_DIE_ID_STR_LEN))
		return 0;

	if (di->ops->get_die_id(die_id, WL_DIE_ID_STR_LEN))
		return 0;

	return snprintf(buf, len, "%s\n", die_id);
}

static int wlc_sysfs_get_fod_coef(char *buf, int len)
{
	struct wireless_charge_device_info *di = g_wireless_di;
	u8 fod[WLC_FOD_COEF_STR_LEN] = { 0 };

	if (!di || !di->ops || !di->ops->get_rx_fod_coef ||
		!buf || (len < WLC_FOD_COEF_STR_LEN))
		return 0;

	if (di->ops->get_rx_fod_coef(fod, WLC_FOD_COEF_STR_LEN))
		return 0;

	return snprintf(buf, len, "%s\n", fod);
}

static void wlc_sysfs_set_fod_coef(const char *fod_str)
{
	struct wireless_charge_device_info *di = g_wireless_di;

	if (!fod_str || !di || !di->ops || !di->ops->set_rx_fod_coef)
		return;

	(void)di->ops->set_rx_fod_coef(fod_str);
}

static int wireless_charge_check_ops(struct wireless_charge_device_info *di)
{
	if (!di->ops || !di->ops->chip_init ||
		!di->ops->check_fwupdate || !di->ops->set_tx_vout ||
		!di->ops->set_rx_vout || !di->ops->get_rx_vout ||
		!di->ops->get_rx_iout || !di->ops->rx_enable ||
		!di->ops->rx_sleep_enable || !di->ops->check_tx_exist ||
		!di->ops->kick_watchdog || !di->ops->chip_reset ||
		!di->ops->send_ept || !di->ops->pmic_vbus_handler) {
		hwlog_err("wireless_charge ops fail\n");
		return -EINVAL;
	}

	return 0;
}
/*
 * There are a numerous options that are configurable on the wireless receiver
 * that go well beyond what the power_supply properties provide access to.
 * Provide sysfs access to them so they can be examined and possibly modified
 * on the fly.
 */
#ifdef CONFIG_SYSFS
static ssize_t wireless_charge_sysfs_show(struct device *dev,
	struct device_attribute *attr, char *buf);
static ssize_t wireless_charge_sysfs_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count);

static struct power_sysfs_attr_info wireless_charge_sysfs_field_tbl[] = {
	power_sysfs_attr_ro(wireless_charge, 0444, WIRELESS_CHARGE_SYSFS_CHIP_INFO, chip_info),
	power_sysfs_attr_rw(wireless_charge, 0644, WIRELESS_CHARGE_SYSFS_KEY_ID, kid),
	power_sysfs_attr_ro(wireless_charge, 0444, WIRELESS_CHARGE_SYSFS_TX_ADAPTOR_TYPE, tx_adaptor_type),
	power_sysfs_attr_rw(wireless_charge, 0644, WIRELESS_CHARGE_SYSFS_RX_TEMP, rx_temp),
	power_sysfs_attr_rw(wireless_charge, 0644, WIRELESS_CHARGE_SYSFS_VOUT, vout),
	power_sysfs_attr_rw(wireless_charge, 0644, WIRELESS_CHARGE_SYSFS_IOUT, iout),
	power_sysfs_attr_rw(wireless_charge, 0644, WIRELESS_CHARGE_SYSFS_VRECT, vrect),
	power_sysfs_attr_rw(wireless_charge, 0644, WIRELESS_CHARGE_SYSFS_EN_ENABLE, en_enable),
	power_sysfs_attr_ro(wireless_charge, 0444, WIRELESS_CHARGE_SYSFS_WIRELESS_SUCC, wireless_succ),
	power_sysfs_attr_ro(wireless_charge, 0444, WIRELESS_CHARGE_SYSFS_NORMAL_CHRG_SUCC, normal_chrg_succ),
	power_sysfs_attr_ro(wireless_charge, 0444, WIRELESS_CHARGE_SYSFS_FAST_CHRG_SUCC, fast_chrg_succ),
	power_sysfs_attr_rw(wireless_charge, 0644, WIRELESS_CHARGE_SYSFS_FOD_COEF, fod_coef),
	power_sysfs_attr_rw(wireless_charge, 0644, WIRELESS_CHARGE_SYSFS_INTERFERENCE_SETTING, interference_setting),
	power_sysfs_attr_rw(wireless_charge, 0644, WIRELESS_CHARGE_SYSFS_RX_SUPPORT_MODE, rx_support_mode),
	power_sysfs_attr_rw(wireless_charge, 0644, WIRELESS_CHARGE_SYSFS_THERMAL_CTRL, thermal_ctrl),
	power_sysfs_attr_ro(wireless_charge, 0444, WIRELESS_CHARGE_SYSFS_DIE_ID, die_id),
	power_sysfs_attr_rw(wireless_charge, 0644, WIRELESS_CHARGE_SYSFS_IGNORE_FAN_CTRL, ignore_fan_ctrl),
	power_sysfs_attr_rw(wireless_charge, 0644, WIRELESS_CHARGE_SYSFS_AF, af),
};
static struct attribute *wireless_charge_sysfs_attrs[ARRAY_SIZE(wireless_charge_sysfs_field_tbl) + 1];
static const struct attribute_group wireless_charge_sysfs_attr_group = {
	.attrs = wireless_charge_sysfs_attrs,
};

static void wireless_charge_sysfs_create_group(struct device *dev)
{
	power_sysfs_init_attrs(wireless_charge_sysfs_attrs,
		wireless_charge_sysfs_field_tbl, ARRAY_SIZE(wireless_charge_sysfs_field_tbl));
	power_sysfs_create_link_group("hw_power", "charger", "wireless_charger",
		dev, &wireless_charge_sysfs_attr_group);
}

static void wireless_charge_sysfs_remove_group(struct device *dev)
{
	power_sysfs_remove_link_group("hw_power", "charger", "wireless_charger",
		dev, &wireless_charge_sysfs_attr_group);
}
#else
static inline void wireless_charge_sysfs_create_group(struct device *dev)
{
}

static inline void wireless_charge_sysfs_remove_group(struct device *dev)
{
}
#endif

static ssize_t wireless_charge_sysfs_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct power_sysfs_attr_info *info = NULL;
	struct wireless_charge_device_info *di = g_wireless_di;
	int chrg_succ;
	int cur_pmode_id;

	info = power_sysfs_lookup_attr(attr->attr.name,
		wireless_charge_sysfs_field_tbl, ARRAY_SIZE(wireless_charge_sysfs_field_tbl));
	if (!info || !di)
		return -EINVAL;

	cur_pmode_id = di->curr_pmode_index;
	switch (info->name) {
	case WIRELESS_CHARGE_SYSFS_CHIP_INFO:
		return wlc_sysfs_get_chip_info(buf, PAGE_SIZE);
	case WIRELESS_CHARGE_SYSFS_KEY_ID:
		return snprintf(buf, PAGE_SIZE, "%d\n", di->dts.antifake_kid);
	case WIRELESS_CHARGE_SYSFS_TX_ADAPTOR_TYPE:
		return snprintf(buf, PAGE_SIZE, "%d\n", di->tx_cap->type);
	case WIRELESS_CHARGE_SYSFS_RX_TEMP:
		return snprintf(buf, PAGE_SIZE, "%d\n", wlc_get_rx_temp());
	case WIRELESS_CHARGE_SYSFS_VOUT:
		return snprintf(buf, PAGE_SIZE, "%d\n",
			di->ops->get_rx_vout());
	case WIRELESS_CHARGE_SYSFS_IOUT:
		return snprintf(buf, PAGE_SIZE, "%d\n",
			di->ops->get_rx_iout());
	case WIRELESS_CHARGE_SYSFS_VRECT:
		return snprintf(buf, PAGE_SIZE, "%d\n",
			di->ops->get_rx_vrect());
	case WIRELESS_CHARGE_SYSFS_EN_ENABLE:
		return snprintf(buf, PAGE_SIZE, "%d\n",
			di->sysfs_data.en_enable);
	case WIRELESS_CHARGE_SYSFS_WIRELESS_SUCC:
		chrg_succ = wireless_charge_check_fac_test_succ(di);
		return snprintf(buf, PAGE_SIZE, "%d\n", chrg_succ);
	case WIRELESS_CHARGE_SYSFS_NORMAL_CHRG_SUCC:
		chrg_succ = wireless_charge_check_normal_charge_succ(di);
		return snprintf(buf, PAGE_SIZE, "%d\n", chrg_succ);
	case WIRELESS_CHARGE_SYSFS_FAST_CHRG_SUCC:
		chrg_succ = wireless_charge_check_fast_charge_succ(di);
		return snprintf(buf, PAGE_SIZE, "%d\n", chrg_succ);
	case WIRELESS_CHARGE_SYSFS_FOD_COEF:
		return wlc_sysfs_get_fod_coef(buf, PAGE_SIZE);;
	case WIRELESS_CHARGE_SYSFS_INTERFERENCE_SETTING:
		return snprintf(buf, PAGE_SIZE, "%u\n",
			di->interfer_data.interfer_src_state);
	case WIRELESS_CHARGE_SYSFS_RX_SUPPORT_MODE:
		return snprintf(buf, PAGE_SIZE,
			"mode[support|current]:[0x%x|%s]\n",
			di->sysfs_data.rx_support_mode,
			di->mode_data.mode_para[cur_pmode_id].mode_name);
	case WIRELESS_CHARGE_SYSFS_THERMAL_CTRL:
		return snprintf(buf, PAGE_SIZE, "%u\n", di->sysfs_data.thermal_ctrl);
	case WIRELESS_CHARGE_SYSFS_DIE_ID:
		return wlc_sysfs_get_die_id(buf, PAGE_SIZE);
	case WIRELESS_CHARGE_SYSFS_IGNORE_FAN_CTRL:
		return snprintf(buf, PAGE_SIZE, "%d\n",
			di->sysfs_data.ignore_fan_ctrl);
	case WIRELESS_CHARGE_SYSFS_AF:
		return wireless_charge_get_antifake_data(buf,
			WIRELESS_RANDOM_LEN + WIRELESS_TX_KEY_LEN);
		break;
	default:
		break;
	}
	return 0;
}

/**********************************************************
*  Function:       wireless_charge_sysfs_store
*  Discription:    set the value for all wireless charge nodes
*  Parameters:   dev:device
*                      attr:device_attribute
*                      buf:string of node value
*                      count:unused
*  return value:  0-sucess or others-fail
**********************************************************/
static ssize_t wireless_charge_sysfs_store(struct device *dev,
				   struct device_attribute *attr, const char *buf, size_t count)
{
	struct power_sysfs_attr_info *info = NULL;
	struct wireless_charge_device_info *di = g_wireless_di;
	long val = 0;

	info = power_sysfs_lookup_attr(attr->attr.name,
		wireless_charge_sysfs_field_tbl, ARRAY_SIZE(wireless_charge_sysfs_field_tbl));
	if (!info ||!di)
		return -EINVAL;

	switch (info->name) {
	case WIRELESS_CHARGE_SYSFS_KEY_ID:
		if ((kstrtol(buf, POWER_BASE_DEC, &val) < 0) ||
			(val < 0) || (val > 1)) {
			hwlog_err("%s: please input 0 or 1\n", __func__);
			return -EINVAL;
		}
		di->dts.antifake_kid = val;
		hwlog_info("[%s] antifake_key_id=%d\n",
			__func__, di->dts.antifake_kid);
		break;
	case WIRELESS_CHARGE_SYSFS_EN_ENABLE:
		if ((strict_strtol(buf, POWER_BASE_DEC, &val) < 0) ||
			(val < 0) || (val > 1))
			return -EINVAL;
		di->sysfs_data.en_enable = val;
		hwlog_info("set rx en_enable = %d\n", di->sysfs_data.en_enable);
		wireless_charge_en_enable(di->sysfs_data.en_enable);
		wlps_control(WLPS_SYSFS_EN_PWR, di->sysfs_data.en_enable ?
			WLPS_CTRL_ON : WLPS_CTRL_OFF);
		break;
	case WIRELESS_CHARGE_SYSFS_FOD_COEF:
		hwlog_info("[%s] set fod_coef: %s\n", __func__, buf);
		wlc_sysfs_set_fod_coef(buf);
		break;
	case WIRELESS_CHARGE_SYSFS_INTERFERENCE_SETTING:
		hwlog_info("[%s] interference_settings:  %s", __func__, buf);
		if (strict_strtol(buf, POWER_BASE_DEC, &val) < 0)
			return -EINVAL;
		wireless_charger_update_interference_settings(di, (u8)val);
		break;
	case WIRELESS_CHARGE_SYSFS_RX_SUPPORT_MODE:
		if ((strict_strtol(buf, POWER_BASE_HEX, &val) < 0) ||
			(val < 0) || (val > WLC_RX_SP_ALL_MODE))
			return -EINVAL;
		if (!val)
			di->sysfs_data.rx_support_mode = WLC_RX_SP_ALL_MODE;
		else
			di->sysfs_data.rx_support_mode = val;
		hwlog_info("[%s] rx_support_mode = 0x%x", __func__, val);
		break;
	case WIRELESS_CHARGE_SYSFS_THERMAL_CTRL:
		if ((kstrtol(buf, POWER_BASE_DEC, &val) < 0) ||
			(val < 0) || (val > 0xFF)) /* 0xFF: maximum of u8 */
			return -EINVAL;
		wireless_set_thermal_ctrl((unsigned char)val);
		break;
	case WIRELESS_CHARGE_SYSFS_IGNORE_FAN_CTRL:
		if ((kstrtol(buf, POWER_BASE_DEC, &val) < 0) ||
			(val < 0) || (val > 1)) /* 1: ignore 0:otherwise */
			return -EINVAL;
		hwlog_info("[%s] ignore_fan_ctrl=0x%x", __func__, val);
		di->sysfs_data.ignore_fan_ctrl = val;
		break;
	case WIRELESS_CHARGE_SYSFS_AF:
		if ((kstrtol(buf, POWER_BASE_DEC, &val) < 0) || (val < 0) || (val > 1))
			return -EINVAL;

		wireless_charge_set_antifake_result(val);
		break;
	default:
		break;
	}
	return count;
}

static struct wireless_charge_device_info *wireless_charge_device_info_alloc(void)
{
	static struct wireless_charge_device_info *di;
	static struct wireless_protocol_tx_cap *tx_cap;

	di = kzalloc(sizeof(*di), GFP_KERNEL);
	if (!di) {
		hwlog_err("alloc di failed\n");
		return NULL;
	}
	tx_cap = kzalloc(sizeof(*tx_cap), GFP_KERNEL);
	if (!tx_cap) {
		hwlog_err("alloc tx_cap failed\n");
		goto alloc_fail_1;
	}

	di->tx_cap = tx_cap;
	return di;
alloc_fail_1:
	kfree(di);
	return NULL;
}
static void wireless_charge_device_info_free(struct wireless_charge_device_info *di)
{
	if(di) {
		if(di->tx_cap) {
			kfree(di->tx_cap);
			di->tx_cap = NULL;
		}
		if(di->iout_ctrl_data.ictrl_para) {
			kfree(di->iout_ctrl_data.ictrl_para);
			di->iout_ctrl_data.ictrl_para = NULL;
		}
		if(di->mode_data.mode_para) {
			kfree(di->mode_data.mode_para);
			di->mode_data.mode_para = NULL;
		}
		if(di->tx_prop_data.tx_prop) {
			kfree(di->tx_prop_data.tx_prop);
			di->tx_prop_data.tx_prop = NULL;
		}
		if(di->volt_mode_data.volt_mode) {
			kfree(di->volt_mode_data.volt_mode);
			di->volt_mode_data.volt_mode = NULL;
		}
		kfree(di);
		di = NULL;
	}
	g_wireless_di = NULL;
}
static void wireless_charge_shutdown(struct platform_device *pdev)
{
	int ret;
	struct wireless_charge_device_info *di = platform_get_drvdata(pdev);

	hwlog_info("%s ++\n", __func__);
	if (NULL == di) {
		hwlog_err("%s: di is null\n", __func__);
		return;
	}
	if (g_wireless_channel_state == WIRELESS_CHANNEL_ON) {
		di->pwroff_reset_flag = true;
		wireless_charge_switch_off();
		wlps_control(WLPS_RX_EXT_PWR, WLPS_CTRL_OFF);
		msleep(50); /* dalay 50ms for power off */
		ret = wireless_charge_set_tx_vout(ADAPTER_5V *
			WL_MVOLT_PER_VOLT);
		if (ret)
			hwlog_err("%s: wlc sw control fail\n", __func__);
	}
	cancel_delayed_work(&di->rx_sample_work);
	cancel_delayed_work(&di->wireless_ctrl_work);
	cancel_delayed_work(&di->wireless_monitor_work);
	cancel_delayed_work(&di->wireless_watchdog_work);
	cancel_delayed_work(&di->ignore_qval_work);
	hwlog_info("%s --\n", __func__);
}

static int wireless_charge_remove(struct platform_device *pdev)
{
	struct wireless_charge_device_info *di = platform_get_drvdata(pdev);

	hwlog_info("%s ++\n", __func__);
	if (NULL == di) {
		hwlog_err("%s: di is null\n", __func__);
		return 0;
	}

	wireless_charge_sysfs_remove_group(di->dev);
	wakeup_source_trash(&g_rx_con_wakelock);
	wakeup_source_trash(&g_rx_charge_wakelock);
	hwlog_info("%s --\n", __func__);

	return 0;
}
static int wireless_charge_probe(struct platform_device *pdev)
{
	int ret;
	struct wireless_charge_device_info *di = NULL;
	struct device_node *np = NULL;

	di = wireless_charge_device_info_alloc();
	if(!di) {
		hwlog_err("alloc di failed\n");
		return -ENOMEM;
	}

	g_wireless_di = di;
	di->dev = &pdev->dev;
	np = di->dev->of_node;
	di->ops = g_wireless_ops;
	platform_set_drvdata(pdev, di);
	wakeup_source_init(&g_rx_con_wakelock, "rx_con_wakelock");
	wakeup_source_init(&g_rx_charge_wakelock, "rx_charge_wakelock");

	ret = wireless_charge_check_ops(di);
	if (ret)
		goto wireless_charge_fail_0;

	ret = wlc_parse_dts(np, di);
	if (ret)
		goto wireless_charge_fail_0;

	di->sysfs_data.rx_support_mode = WLC_RX_SP_ALL_MODE;
	di->qval_support_mode = WLC_RX_SP_ALL_MODE;
	if (power_cmdline_is_factory_mode())
		di->sysfs_data.rx_support_mode &= ~WLC_RX_SP_SC_2_MODE;
	di->sysfs_data.tx_vout_max = di->product_para.tx_vout;
	di->sysfs_data.rx_vout_max = di->product_para.rx_vout;
	di->sysfs_data.rx_iout_max = di->product_para.rx_iout;
	di->discon_delay_time = WL_DISCONN_DELAY_MS;
	wireless_charge_set_default_tx_capability(di);

	mutex_init(&g_rx_en_mutex);
	INIT_WORK(&di->wired_vbus_connect_work, wireless_charge_wired_vbus_connect_work);
	INIT_WORK(&di->wired_vbus_disconnect_work, wireless_charge_wired_vbus_disconnect_work);
	INIT_DELAYED_WORK(&di->rx_event_work, wlc_rx_event_work);
	INIT_WORK(&di->wireless_pwroff_reset_work, wireless_charge_pwroff_reset_work);
	INIT_DELAYED_WORK(&di->wireless_ctrl_work, wireless_charge_control_work);
	INIT_DELAYED_WORK(&di->rx_sample_work, wireless_charge_rx_sample_work);
	INIT_DELAYED_WORK(&di->wireless_monitor_work, wireless_charge_monitor_work);
	INIT_DELAYED_WORK(&di->wireless_watchdog_work, wireless_charge_watchdog_work);
	INIT_DELAYED_WORK(&di->wireless_vbus_disconnect_work, wireless_charge_wireless_vbus_disconnect_work);
	INIT_DELAYED_WORK(&di->interfer_work,
		wireless_charge_interference_work);
	INIT_DELAYED_WORK(&di->ignore_qval_work, wlc_ignore_qval_work);

	di->rx_event_nb.notifier_call = wireless_charge_rx_event_notifier_call;
	ret = power_event_nc_register(POWER_NT_WLRX, &di->rx_event_nb);
	if (ret < 0) {
		hwlog_err("register rx_connect notifier failed\n");
		goto  wireless_charge_fail_1;
	}
	ret = wlc_power_supply_register(pdev);
	if (ret) {
		hwlog_err("register power supply failed\n");
		goto  wireless_charge_fail_ps;
	}
	if (wireless_charge_check_tx_exist()) {
		wireless_charge_para_init(di);
		wlc_handle_sink_event(true);
		pd_dpm_ignore_vbus_only_event(true);
		wlc_wireless_vbus_connect_handler(WIRELESS_STAGE_CHECK_TX_ID);
		schedule_delayed_work(&di->wireless_monitor_work, msecs_to_jiffies(0));
		schedule_delayed_work(&di->wireless_watchdog_work, msecs_to_jiffies(0));
	} else {
		wireless_charge_switch_off();
	}
	init_completion(&di->wc_af_completion);
	wireless_charge_sysfs_create_group(di->dev);
	power_if_ops_register(&wl_if_ops);
	hwlog_info("wireless_charger probe ok.\n");
	return 0;

wireless_charge_fail_ps:
	(void)power_event_nc_unregister(POWER_NT_WLRX, &di->rx_event_nb);
wireless_charge_fail_1:
wireless_charge_fail_0:
	wakeup_source_trash(&g_rx_con_wakelock);
	wakeup_source_trash(&g_rx_charge_wakelock);
	di->ops = NULL;
	wireless_charge_device_info_free(di);
	platform_set_drvdata(pdev, NULL);
	return ret;
}

static struct of_device_id wireless_charge_match_table[] = {
	{
	 .compatible = "huawei,wireless_charger",
	 .data = NULL,
	},
	{},
};

static struct platform_driver wireless_charge_driver = {
	.probe = wireless_charge_probe,
	.remove = wireless_charge_remove,
	.shutdown = wireless_charge_shutdown,
	.driver = {
		.name = "huawei,wireless_charger",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(wireless_charge_match_table),
	},
};

static int __init wireless_charge_init(void)
{
	hwlog_info("wireless_charger init ok.\n");

	return platform_driver_register(&wireless_charge_driver);
}

static void __exit wireless_charge_exit(void)
{
	platform_driver_unregister(&wireless_charge_driver);
}

late_initcall_sync(wireless_charge_init);
module_exit(wireless_charge_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("wireless charge module driver");
MODULE_AUTHOR("Huawei Technologies Co., Ltd.");
