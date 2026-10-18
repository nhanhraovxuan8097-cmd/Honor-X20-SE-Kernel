/*
 * mt5735_rx.c
 *
 * mt5735 rx driver
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

#include "mt5735.h"

#define HWLOG_TAG wireless_mt5735_rx
HWLOG_REGIST();

static const char * const g_mt5735_rx_irq_name[] = {
	/* [n]: n means bit in registers */
	[0]  = "rx_rst",
	[1]  = "rx_otp",
	[8]  = "rx_power_on",
	[9]  = "rx_ready",
	[10] = "rx_output_on",
	[11] = "rx_output_off",
	[12] = "rx_pldo",
	[13] = "rx_ocp",
	[14] = "rx_ovp",
	[15] = "rx_vout_ovp",
	[16] = "rx_epp",
	[17] = "rx_send_pkt_timeout",
	[18] = "rx_send_pkt_succ",
	[19] = "rx_recv_fsk_succ",
};

static int mt5735_rx_get_temp(void)
{
	int ret;
	u16 temp = 0;

	ret = mt5735_read_word(MT5735_RX_CHIP_TEMP_ADDR, &temp);
	if (ret)
		return ret;

	return (int)(temp); /* chip_temp in 0.1degC */
}

static int mt5735_rx_get_fop(void)
{
	int ret;
	u16 fop = 0;

	ret = mt5735_read_word(MT5735_RX_OP_FREQ_ADDR, &fop);
	if (ret)
		return ret;

	return (int)(fop / 10); /* 10: fop unit */
}

static int mt5735_rx_get_cep(void)
{
	int ret;
	s8 cep = 0;

	ret = mt5735_read_byte(MT5735_RX_CE_VAL_ADDR, (u8 *)&cep);
	if (ret)
		return ret;

	return (int)cep;
}

static int mt5735_rx_get_vrect(void)
{
	int ret;
	u16 vrect = 0;

	ret = mt5735_read_word(MT5735_RX_VRECT_ADDR, &vrect);
	if (ret)
		return ret;

	return (int)vrect;
}

static int mt5735_rx_get_vout(void)
{
	int ret;
	u16 vout = 0;

	ret = mt5735_read_word(MT5735_RX_VOUT_ADDR, &vout);
	if (ret)
		return ret;

	return (int)vout;
}

static int mt5735_rx_get_iout(void)
{
	int ret;
	u16 iout = 0;

	ret = mt5735_read_word(MT5735_RX_IOUT_ADDR, &iout);
	if (ret)
		return ret;

	return (int)iout;
}

static int mt5735_rx_get_rx_vout_reg(void)
{
	int ret;
	u16 vreg = 0;

	ret = mt5735_read_word(MT5735_RX_VOUT_SET_ADDR, &vreg);
	if (ret)
		return ret;

	return (int)(vreg);
}

static int mt5735_rx_get_tx_vout_reg(void)
{
	int ret;
	u16 tx_vreg;

	ret = mt5735_read_word(MT5735_RX_FC_VOLT_ADDR, &tx_vreg);
	if (ret)
		return ret;

	return (int)tx_vreg;
}

static int mt5735_rx_get_def_imax(void)
{
	return MT5735_DFT_IOUT_MAX;
}

static void mt5735_rx_show_irq(u32 intr)
{
	u32 i;

	for (i = 0; i < ARRAY_SIZE(g_mt5735_rx_irq_name); i++) {
		if (intr & BIT(i))
			hwlog_info("[rx_irq] %s\n", g_mt5735_rx_irq_name[i]);
	}
}

static int mt5735_rx_get_interrupt(u32 *intr)
{
	int ret;

	ret = mt5735_read_block(MT5735_RX_IRQ_ADDR, (u8 *)intr,
		MT5735_RX_IRQ_LEN);
	if (ret)
		return ret;

	hwlog_info("[get_interrupt] irq=0x%08x\n", *intr);
	mt5735_rx_show_irq(*intr);

	return 0;
}

static int mt5735_rx_clear_irq(u32 intr)
{
	int ret;

	ret = mt5735_write_block(MT5735_RX_IRQ_CLR_ADDR, (u8 *)&intr,
		MT5735_RX_IRQ_CLR_LEN);
	ret += mt5735_write_word_mask(MT5735_RX_CMD_ADDR,
		MT5735_RX_CMD_CLEAR_INT,
		MT5735_RX_CMD_CLEAR_INT_SHIFT, MT5735_RX_CMD_VAL);
	if (ret) {
		hwlog_err("clear_irq: failed\n");
		return ret;
	}

	return 0;
}

static void mt5735_rx_ext_pwr_prev_ctrl(int flag)
{
	int ret;
	u32 wr_buff;

	if (flag == WLPS_CTRL_ON)
		wr_buff = MT5735_RX_CMD_RX_LDO5V_EN;
	else
		wr_buff = MT5735_RX_CMD_RX_LDO5V_DIS;

	hwlog_info("[ext_pwr_prev_ctrl] ldo_5v %s\n",
		(flag == WLPS_CTRL_ON) ? "on" : "off");
	ret = mt5735_write_block(MT5735_RX_CMD_ADDR, (u8 *)&wr_buff,
		MT5735_RX_CMD_LEN);
	if (ret)
		hwlog_err("ext_pwr_prev_ctrl: write reg failed\n");
}

static int mt5735_rx_set_rx_vout(int vol)
{
	int ret;

	if ((vol < MT5735_RX_VOUT_MIN) || (vol > MT5735_RX_VOUT_MAX)) {
		hwlog_err("set_rx_vout: out of range\n");
		return -WLC_ERR_PARA_WRONG;
	}

	ret = mt5735_write_word(MT5735_RX_VOUT_SET_ADDR, (u16)vol);
	ret += mt5735_write_word_mask(MT5735_RX_CMD_ADDR,
		MT5735_RX_CMD_SET_RX_VOUT,
		MT5735_RX_CMD_SET_RX_VOUT_SHIFT, MT5735_RX_CMD_VAL);
	if (ret) {
		hwlog_err("set_rx_vout: failed\n");
		return ret;
	}

	return 0;
}

static bool mt5735_rx_is_cp_open(void)
{
	int rx_ratio;
	int rx_vset;
	int rx_vout;
	int cp_vout;

	if (!charge_pump_is_cp_open(CP_TYPE_MAIN))
		return false;

	rx_ratio = charge_pump_get_cp_ratio(CP_TYPE_MAIN);
	rx_vset = mt5735_rx_get_rx_vout_reg();
	rx_vout =  mt5735_rx_get_vout();
	cp_vout = charge_pump_get_cp_vout(CP_TYPE_MAIN);
	cp_vout = (cp_vout > 0) ? cp_vout : wldc_get_ls_vbus();

	hwlog_info("[is_cp_open] [rx] ratio:%d vset:%d vout:%d [cp] vout:%d\n",
		rx_ratio, rx_vset, rx_vout, cp_vout);
	if ((cp_vout * rx_ratio) < (rx_vout - MT5735_RX_FC_VOUT_ERR_LTH))
		return false;
	if ((cp_vout * rx_ratio) > (rx_vout + MT5735_RX_FC_VOUT_ERR_UTH))
		return false;

	return true;
}

static int mt5735_rx_check_cp_mode(struct mt5735_dev_info *di)
{
	int i;
	int cnt;
	int ret;

	ret = charge_pump_set_cp_mode(CP_TYPE_MAIN);
	if (ret) {
		hwlog_err("check_cp_mode: set cp_mode failed\n");
		return ret;
	}
	cnt = MT5735_RX_BPCP_TIMEOUT / MT5735_RX_BPCP_SLEEP_TIME;
	for (i = 0; i < cnt; i++) {
		msleep(MT5735_RX_BPCP_SLEEP_TIME);
		if (mt5735_rx_is_cp_open()) {
			hwlog_info("[check_cp_mode] set cp_mode succ\n");
			return 0;
		}
		if (di->g_val.rx_stop_chrg_flag)
			return -WLC_ERR_STOP_CHRG;
	}

	return -WLC_ERR_MISMATCH;
}

static int mt5735_rx_send_fc_cmd(int vset)
{
	int ret;

	ret = mt5735_write_word(MT5735_RX_FC_VOLT_ADDR, (u16)vset);
	if (ret) {
		hwlog_err("send_fc_cmd: set fc_reg failed\n");
		return ret;
	}
	ret = mt5735_write_word_mask(MT5735_RX_CMD_ADDR, MT5735_RX_CMD_SEND_FC,
		MT5735_RX_CMD_SEND_FC_SHIFT, MT5735_RX_CMD_VAL);
	if (ret) {
		hwlog_err("send_fc_cmd: send fc_cmd failed\n");
		return ret;
	}

	return 0;
}

static bool mt5735_rx_is_fc_succ(struct mt5735_dev_info *di, int vset)
{
	int i;
	int cnt;
	int vout;

	cnt = MT5735_RX_FC_VOUT_TIMEOUT / MT5735_RX_FC_VOUT_SLEEP_TIME;
	for (i = 0; i < cnt; i++) {
		if (di->g_val.rx_stop_chrg_flag &&
			(vset > MT5735_RX_FC_VOUT_DEFAULT))
			return false;
		msleep(MT5735_RX_FC_VOUT_SLEEP_TIME);
		vout = mt5735_rx_get_vout();
		if ((vout >= vset - MT5735_RX_FC_VOUT_ERR_LTH) &&
			(vout <= vset + MT5735_RX_FC_VOUT_ERR_UTH)) {
			hwlog_info("[is_fc_succ] succ, cost_time: %dms\n",
				(i + 1) * MT5735_RX_FC_VOUT_SLEEP_TIME);
			(void)mt5735_rx_set_rx_vout(vset);
			return true;
		}
	}

	return false;
}

static void mt5735_ask_mode_cfg(u8 mode_cfg)
{
	int ret;
	u8 val = 0;

	ret = mt5735_write_byte(MT5735_RX_ASK_CFG_ADDR, mode_cfg);
	if (ret)
		hwlog_err("ask_mode_cfg: write fail\n");

	ret = mt5735_read_byte(MT5735_RX_ASK_CFG_ADDR, &val);
	if (ret) {
		hwlog_err("ask_mode_cfg: read fail\n");
		return;
	}

	hwlog_info("[ask_mode_cfg] val=0x%x\n", val);
}

static void mt5735_set_mode_cfg(int vset)
{
	if (vset <= RX_HIGH_VOUT) {
		mt5735_ask_mode_cfg(MT5735_BOTH_CAP_POSITIVE);
		return;
	}
	if (!power_cmdline_is_factory_mode())
		mt5735_ask_mode_cfg(MT5735_CAP_C_NEGATIVE);
	else
		mt5735_ask_mode_cfg(MT5735_BOTH_CAP_POSITIVE);
}

static int mt5735_rx_set_tx_vout(int vset)
{
	int ret;
	int i;
	struct mt5735_dev_info *di = NULL;

	mt5735_get_dev_info(&di);
	if (!di)
		return -WLC_ERR_PARA_NULL;

	if (vset >= RX_HIGH_VOUT2) {
		ret = mt5735_rx_check_cp_mode(di);
		if (ret)
			return ret;
	}

	mt5735_set_mode_cfg(vset);

	for (i = 0; i < MT5735_RX_FC_VOUT_RETRY_CNT; i++) {
		if (di->g_val.rx_stop_chrg_flag &&
			(vset > MT5735_RX_FC_VOUT_DEFAULT))
			return -WLC_ERR_STOP_CHRG;
		ret = mt5735_rx_send_fc_cmd(vset);
		if (ret) {
			hwlog_err("set_tx_vout: send fc_cmd failed\n");
			continue;
		}
		hwlog_info("[set_tx_vout] send fc_cmd, cnt: %d\n", i);
		if (mt5735_rx_is_fc_succ(di, vset)) {
			if (vset < RX_HIGH_VOUT2)
				(void)charge_pump_set_bp_mode(CP_TYPE_MAIN);
			mt5735_set_mode_cfg(vset);
			hwlog_info("[set_tx_vout] succ\n");
			return 0;
		}
	}

	return -WLC_ERR_MISMATCH;
}

static int mt5735_rx_send_ept(enum wireless_etp_type ept_type)
{
	int ret;

	switch (ept_type) {
	case WIRELESS_EPT_ERR_VRECT:
	case WIRELESS_EPT_ERR_VOUT:
		break;
	default:
		return -WLC_ERR_PARA_WRONG;
	}
	ret = mt5735_write_byte(MT5735_RX_EPT_MSG_ADDR, ept_type);
	ret += mt5735_write_word_mask(MT5735_RX_CMD_ADDR,
		MT5735_RX_CMD_SEND_EPT,
		MT5735_RX_CMD_SEND_EPT_SHIFT, MT5735_RX_CMD_VAL);
	if (ret) {
		hwlog_err("send_ept: failed, ept=0x%x\n", ept_type);
		return ret;
	}

	return 0;
}

static bool mt5735_rx_check_tx_exist(void)
{
	int ret;
	u16 mode = 0;

	ret = mt5735_get_mode(&mode);
	if (ret) {
		hwlog_err("check_tx_exist: get rx mode failed\n");
		return false;
	}
	if (mode == MT5735_OP_MODE_RX)
		return true;

	hwlog_info("[check_tx_exist] mode = %u\n", mode);
	return false;
}

static int mt5735_rx_kick_watchdog(void)
{
	return mt5735_write_word(MT5735_RX_WDT_FEED_ADDR, 0);
}

static int mt5735_rx_get_fod(char *fod_str, int len)
{
	int i;
	int ret;
	char tmp[MT5735_RX_FOD_TMP_STR_LEN] = { 0 };
	u8 fod_arr[MT5735_RX_FOD_LEN] = { 0 };

	if (!fod_str || (len != WLC_FOD_COEF_STR_LEN))
		return -WLC_ERR_PARA_WRONG;

	memset(fod_str, 0, len);
	ret = mt5735_read_block(MT5735_RX_FOD_ADDR, fod_arr, MT5735_RX_FOD_LEN);
	if (ret) {
		hwlog_err("get_fod: read fod failed\n");
		return ret;
	}

	for (i = 0; i < MT5735_RX_FOD_LEN; i++) {
		snprintf(tmp, MT5735_RX_FOD_TMP_STR_LEN, "%x ", fod_arr[i]);
		strncat(fod_str, tmp, strlen(tmp));
	}

	return 0;
}

static int mt5735_rx_set_fod(const char *fod_str)
{
	int ret;
	char *cur = (char *)fod_str;
	char *token = NULL;
	int i;
	u8 val = 0;
	const char *sep = " ,";
	u8 fod_arr[MT5735_RX_FOD_LEN] = { 0 };

	if (!fod_str) {
		hwlog_err("set_fod: input fod_str err\n");
		return -WLC_ERR_PARA_NULL;
	}

	for (i = 0; i < MT5735_RX_FOD_LEN; i++) {
		token = strsep(&cur, sep);
		if (!token) {
			hwlog_err("set_fod: input fod_str number err\n");
			return -WLC_ERR_PARA_WRONG;
		}
		ret = kstrtou8(token, POWER_BASE_DEC, &val);
		if (ret) {
			hwlog_err("set_fod: input fod_str type err\n");
			return -WLC_ERR_PARA_WRONG;
		}
		fod_arr[i] = val;
		hwlog_info("[set_fod] fod[%d]=0x%x\n", i, fod_arr[i]);
	}

	return mt5735_write_block(MT5735_RX_FOD_ADDR, fod_arr,
		MT5735_RX_FOD_LEN);
}

static int mt5735_rx_init_fod_coef(struct mt5735_dev_info *di)
{
	int tx_vset;
	int ret;
	u8 *rx_fod = NULL;

	tx_vset = mt5735_rx_get_tx_vout_reg();
	hwlog_info("[init_fod_coef] tx_vout_reg: %dmV\n", tx_vset);

	if (tx_vset < 9000) /* (0, 9)V, set 5v fod */
		rx_fod = di->rx_fod_5v;
	else if (tx_vset < 15000) /* [9, 15)V, set 9V fod */
		rx_fod = di->rx_fod_9v;
	else if (tx_vset < 18000) /* [15, 18)V, set 15V fod */
		rx_fod = di->rx_fod_15v;
	else
		return -WLC_ERR_MISMATCH;

	ret = mt5735_write_block(MT5735_RX_FOD_ADDR, rx_fod, MT5735_RX_FOD_LEN);
	if (ret) {
		hwlog_err("init_fod_coef: write fod failed\n");
		return ret;
	}

	return ret;
}

static int mt5735_rx_chip_init(int init_type, int tx_type)
{
	int ret = 0;
	struct mt5735_dev_info *di = NULL;

	mt5735_get_dev_info(&di);
	if (!di)
		return -WLC_ERR_PARA_NULL;

	switch (init_type) {
	case WIRELESS_CHIP_INIT:
		hwlog_info("[chip_init] default chip init\n");
		di->g_val.rx_stop_chrg_flag = false;
		ret += mt5735_write_word(MT5735_RX_FC_VRECT_DIFF_ADDR,
			MT5735_RX_FC_VRECT_DIFF);
		ret += mt5735_write_word(MT5735_RX_WDT_TIMEOUT_ADDR,
			MT5735_RX_WDT_TIMEOUT);
		/* fall through */
	case ADAPTER_5V * WL_MVOLT_PER_VOLT:
		hwlog_info("[chip_init] 5v chip init\n");
		ret += mt5735_write_block(MT5735_RX_LDO_CFG_ADDR,
			di->rx_ldo_cfg_5v, MT5735_RX_LDO_CFG_LEN);
		ret += mt5735_rx_init_fod_coef(di);
		mt5735_set_mode_cfg(init_type);
		break;
	case ADAPTER_9V * WL_MVOLT_PER_VOLT:
		hwlog_info("[chip_init] 9v chip init\n");
		ret += mt5735_write_block(MT5735_RX_LDO_CFG_ADDR,
			di->rx_ldo_cfg_9v, MT5735_RX_LDO_CFG_LEN);
		ret += mt5735_rx_init_fod_coef(di);
		break;
	case ADAPTER_12V * WL_MVOLT_PER_VOLT:
		hwlog_info("[chip_init] 12v chip init\n");
		ret += mt5735_write_block(MT5735_RX_LDO_CFG_ADDR,
			di->rx_ldo_cfg_12v, MT5735_RX_LDO_CFG_LEN);
		ret += mt5735_rx_init_fod_coef(di);
		break;
	case WILREESS_SC_CHIP_INIT:
		hwlog_info("[chip_init] sc chip init\n");
		ret += mt5735_write_block(MT5735_RX_LDO_CFG_ADDR,
			di->rx_ldo_cfg_sc, MT5735_RX_LDO_CFG_LEN);
		ret += mt5735_rx_init_fod_coef(di);
		break;
	default:
		hwlog_info("chip_init: input para invalid\n");
		break;
	}

	return ret;
}

static int mt5735_rx_stop_charging(void)
{
	int wired_channel_state;
	struct mt5735_dev_info *di = NULL;

	mt5735_get_dev_info(&di);
	if (!di)
		return -WLC_ERR_PARA_NULL;

	di->g_val.rx_stop_chrg_flag = true;

	if (!di->g_val.irq_abnormal_flag)
		return 0;

	wired_channel_state = wireless_charge_get_wired_channel_state();
	if (wired_channel_state != WIRED_CHANNEL_ON) {
		hwlog_info("[stop_charging] irq_abnormal, keep rx_sw on\n");
		di->g_val.irq_abnormal_flag = true;
		wlps_control(WLPS_RX_SW, WLPS_CTRL_ON);
	} else {
		di->irq_cnt = 0;
		di->g_val.irq_abnormal_flag = false;
		mt5735_enable_irq();
		hwlog_info("[stop_charging] wired channel on, enable irq\n");
	}

	return 0;
}

static int mt5735_rx_data_rcvd_handler(struct mt5735_dev_info *di)
{
	int ret;
	int i;
	u8 cmd;
	u8 buff[QI_PKT_LEN] = { 0 };

	ret = mt5735_read_block(MT5735_RCVD_MSG_HEADER_ADDR,
		buff, QI_PKT_LEN);
	if (ret) {
		hwlog_err("data_received_handler: read received data failed\n");
		return ret;
	}

	cmd = buff[QI_PKT_CMD];
	hwlog_info("[data_received_handler] cmd: 0x%x\n", cmd);
	for (i = QI_PKT_DATA; i < QI_PKT_LEN; i++)
		hwlog_info("[data_received_handler] data: 0x%x\n", buff[i]);

	switch (cmd) {
	case QI_CMD_TX_ALARM:
	case QI_CMD_ACK_BST_ERR:
		di->irq_val &= ~MT5735_RX_IRQ_DATA_RCVD;
		if (di->g_val.qi_hdl &&
			di->g_val.qi_hdl->hdl_non_qi_fsk_pkt)
			di->g_val.qi_hdl->hdl_non_qi_fsk_pkt(buff, QI_PKT_LEN);
		break;
	default:
		break;
	}
	return 0;
}

void mt5735_rx_abnormal_irq_handler(struct mt5735_dev_info *di)
{
	static struct timespec64 ts64_timeout;
	struct timespec64 ts64_interval;
	struct timespec64 ts64_now;

	ts64_now = current_kernel_time64();
	ts64_interval.tv_sec = 0;
	ts64_interval.tv_nsec = WIRELESS_INT_TIMEOUT_TH * NSEC_PER_MSEC;

	if (!di)
		return;

	hwlog_info("[handle_abnormal_irq] irq_cnt = %d\n", ++di->irq_cnt);
	/* power on irq occurs first time, so start monitor now */
	if (di->irq_cnt == 1) {
		ts64_timeout = timespec64_add_safe(ts64_now, ts64_interval);
		if (ts64_timeout.tv_sec == TIME_T_MAX) {
			di->irq_cnt = 0;
			hwlog_err("handle_abnormal_irq: time overflow\n");
			return;
		}
	}

	if (timespec64_compare(&ts64_now, &ts64_timeout) < 0)
		return;

	if (di->irq_cnt < WIRELESS_INT_CNT_TH) {
		di->irq_cnt = 0;
		return;
	}

	di->g_val.irq_abnormal_flag = true;
	wlps_control(WLPS_RX_SW, WLPS_CTRL_ON);
	mt5735_disable_irq_nosync();
	gpio_set_value(di->gpio_sleep_en, RX_SLEEP_EN_DISABLE);
	hwlog_err("handle_abnormal_irq: more than %d irq in %ds, disable irq\n",
		WIRELESS_INT_CNT_TH, WIRELESS_INT_TIMEOUT_TH / WL_MSEC_PER_SEC);
}

static void mt5735_rx_ready_handler(struct mt5735_dev_info *di)
{
	int wired_ch_state;

	wired_ch_state = wireless_charge_get_wired_channel_state();
	if (wired_ch_state == WIRED_CHANNEL_ON) {
		hwlog_err("rx_ready_handler: wired channel on, ignore\n");
		return;
	}

	hwlog_info("[rx_ready_handler] rx ready, goto wireless charging\n");
	di->g_val.rx_stop_chrg_flag = false;
	di->irq_cnt = 0;
	wired_chsw_set_wired_channel(WIRED_CHANNEL_ALL, WIRED_CHANNEL_CUTOFF);
	wlps_control(WLPS_RX_EXT_PWR, WLPS_CTRL_ON);
	msleep(CHANNEL_SW_TIME);
	gpio_set_value(di->gpio_sleep_en, RX_SLEEP_EN_DISABLE);
	wlps_control(WLPS_RX_EXT_PWR, WLPS_CTRL_OFF);
	power_event_notify(POWER_NT_WLRX, POWER_NE_WLRX_READY, NULL);
}

static void mt5735_rx_power_on_handler(struct mt5735_dev_info *di)
{
	int ret;
	u16 rx_ss = 0; /* ss: signal strength */
	int pwr_flag = RX_PWR_ON_NOT_GOOD;
	int wired_ch_state;

	wired_ch_state = wireless_charge_get_wired_channel_state();
	if (wired_ch_state == WIRED_CHANNEL_ON) {
		hwlog_err("rx_power_on_handler: wired channel on, ignore\n");
		return;
	}

	mt5735_rx_abnormal_irq_handler(di);
	ret = mt5735_read_word(MT5735_RX_SS_ADDR, &rx_ss);
	hwlog_info("[rx_power_on_handler] get ss=%u %s\n",
		rx_ss, ret ? "failed" : "succ");
	if ((rx_ss > di->rx_ss_good_lth) && (rx_ss <= MT5735_RX_SS_MAX))
		pwr_flag = RX_PWR_ON_GOOD;
	power_event_notify(POWER_NT_WLRX, POWER_NE_WLRX_PWR_ON, &pwr_flag);
}

static void mt5735_rx_mode_irq_recheck(struct mt5735_dev_info *di)
{
	int ret;
	u32 irq_val = 0;

	if (gpio_get_value(di->gpio_int))
		return;

	hwlog_info("[rx_mode_irq_recheck] gpio_int low, re-check irq\n");
	ret = mt5735_rx_get_interrupt(&irq_val);
	if (ret)
		return;

	if (irq_val & MT5735_RX_IRQ_READY)
		mt5735_rx_ready_handler(di);

	mt5735_rx_clear_irq(MT5735_RX_IRQ_CLR_ALL);
}

static void mt5735_rx_fault_irq_handler(struct mt5735_dev_info *di)
{
	if (di->irq_val & MT5735_RX_IRQ_OCP) {
		di->irq_val &= ~MT5735_RX_IRQ_OCP;
		power_event_notify(POWER_NT_WLRX, POWER_NE_WLRX_OCP, NULL);
	}

	if (di->irq_val & MT5735_RX_IRQ_OVP) {
		di->irq_val &= ~MT5735_RX_IRQ_OVP;
		power_event_notify(POWER_NT_WLRX, POWER_NE_WLRX_OVP, NULL);
	}

	if (di->irq_val & MT5735_RX_IRQ_OTP) {
		di->irq_val &= ~MT5735_RX_IRQ_OTP;
		power_event_notify(POWER_NT_WLRX, POWER_NE_WLRX_OTP, NULL);
	}
}

void mt5735_rx_mode_irq_handler(struct mt5735_dev_info *di)
{
	int ret;

	if (!di)
		return;

	ret = mt5735_rx_get_interrupt(&di->irq_val);
	if (ret) {
		hwlog_err("irq_handler: read irq failed, clear\n");
		mt5735_rx_clear_irq(MT5735_RX_IRQ_CLR_ALL);
		mt5735_rx_abnormal_irq_handler(di);
		goto rechk_irq;
	}

	mt5735_rx_clear_irq(di->irq_val);

	if (di->irq_val & MT5735_RX_IRQ_POWER_ON) {
		di->irq_val &= ~MT5735_RX_IRQ_POWER_ON;
		mt5735_rx_power_on_handler(di);
	}
	if (di->irq_val & MT5735_RX_IRQ_READY) {
		di->irq_val &= ~MT5735_RX_IRQ_READY;
		mt5735_rx_ready_handler(di);
	}
	if (di->irq_val & MT5735_RX_IRQ_DATA_RCVD)
		mt5735_rx_data_rcvd_handler(di);

	mt5735_rx_fault_irq_handler(di);

rechk_irq:
	mt5735_rx_mode_irq_recheck(di);
}

static void mt5735_rx_pmic_vbus_handler(bool vbus_state)
{
	int ret;
	int wired_ch_state;
	u32 irq_val = 0;
	struct mt5735_dev_info *di = NULL;

	mt5735_get_dev_info(&di);
	if (!di)
		return;

	if (!vbus_state || !di->g_val.irq_abnormal_flag)
		return;

	wired_ch_state = wireless_charge_get_wired_channel_state();
	if (wired_ch_state == WIRED_CHANNEL_ON)
		return;

	if (!mt5735_rx_check_tx_exist())
		return;

	ret = mt5735_rx_get_interrupt(&irq_val);
	if (ret) {
		hwlog_err("pmic_vbus_handler: read irq failed, clear\n");
		return;
	}
	hwlog_info("[pmic_vbus_handler] irq_val=0x%x\n", irq_val);
	if (irq_val & MT5735_RX_IRQ_READY) {
		mt5735_rx_clear_irq(MT5735_RX_IRQ_CLR_ALL);
		mt5735_rx_ready_handler(di);
		di->irq_cnt = 0;
		di->g_val.irq_abnormal_flag = false;
		mt5735_enable_irq();
	}
}

void mt5735_rx_probe_check_tx_exist(void)
{
	struct mt5735_dev_info *di = NULL;

	mt5735_get_dev_info(&di);
	if (!di)
		return;

	if (mt5735_rx_check_tx_exist()) {
		mt5735_rx_clear_irq(MT5735_RX_IRQ_CLR_ALL);
		hwlog_info("[rx_probe_check_tx_exist] rx exsit\n");
		mt5735_rx_ready_handler(di);
	} else {
		mt5735_sleep_enable(RX_SLEEP_EN_ENABLE);
	}
}

void mt5735_rx_shutdown_handler(void)
{
	int ret;

	wlps_control(WLPS_RX_EXT_PWR, WLPS_CTRL_OFF);
	msleep(50); /* dalay 50ms for power off */
	ret = mt5735_rx_set_tx_vout(ADAPTER_5V * WL_MVOLT_PER_VOLT);
	ret += mt5735_rx_set_rx_vout(ADAPTER_5V * WL_MVOLT_PER_VOLT);
	if (ret)
		hwlog_err("set vout failed\n");
	mt5735_chip_enable(RX_EN_DISABLE);
	msleep(MT5735_SHUTDOWN_SLEEP_TIME);
	mt5735_chip_enable(RX_EN_ENABLE);
}

static struct wireless_charge_device_ops g_mt5735_rx_ops = {
	.chip_init              = mt5735_rx_chip_init,
	.chip_reset             = mt5735_chip_reset,
	.rx_enable              = mt5735_chip_enable,
	.rx_sleep_enable        = mt5735_sleep_enable,
	.ext_pwr_prev_ctrl      = mt5735_rx_ext_pwr_prev_ctrl,
	.get_chip_info          = mt5735_get_chip_info_str,
	.get_rx_def_imax        = mt5735_rx_get_def_imax,
	.get_rx_vrect           = mt5735_rx_get_vrect,
	.get_rx_vout            = mt5735_rx_get_vout,
	.get_rx_iout            = mt5735_rx_get_iout,
	.get_rx_vout_reg        = mt5735_rx_get_rx_vout_reg,
	.get_tx_vout_reg        = mt5735_rx_get_tx_vout_reg,
	.set_tx_vout            = mt5735_rx_set_tx_vout,
	.set_rx_vout            = mt5735_rx_set_rx_vout,
	.get_rx_fop             = mt5735_rx_get_fop,
	.get_rx_cep             = mt5735_rx_get_cep,
	.get_rx_temp            = mt5735_rx_get_temp,
	.get_rx_fod_coef        = mt5735_rx_get_fod,
	.set_rx_fod_coef        = mt5735_rx_set_fod,
	.check_tx_exist         = mt5735_rx_check_tx_exist,
	.kick_watchdog          = mt5735_rx_kick_watchdog,
	.send_ept               = mt5735_rx_send_ept,
	.stop_charging          = mt5735_rx_stop_charging,
	.pmic_vbus_handler      = mt5735_rx_pmic_vbus_handler,
	.check_fwupdate         = mt5735_fw_sram_update,
	.need_chk_pu_shell      = NULL,
	.set_pu_shell_flag      = NULL,
};

static struct wlrx_ic_ops g_mt5735_rx_ic_ops = {
	.get_dev_node           = mt5735_dts_dev_node,
};

int mt5735_rx_ops_register(void)
{
	int ret;
	struct mt5735_dev_info *di = NULL;

	mt5735_get_dev_info(&di);
	if (!di)
		return -1;

	g_mt5735_rx_ic_ops.dev_data = (void *)di;
	ret = wireless_charge_ops_register(&g_mt5735_rx_ops);
	ret += wlrx_ic_ops_register(&g_mt5735_rx_ic_ops, WLTRX_IC_TYPE_MAIN);

	return ret;
}
