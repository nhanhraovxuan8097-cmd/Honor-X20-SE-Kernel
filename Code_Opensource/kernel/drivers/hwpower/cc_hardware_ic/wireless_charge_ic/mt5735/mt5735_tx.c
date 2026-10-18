/*
 * mt5735_tx.c
 *
 * mt5735 tx driver
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

#define HWLOG_TAG wireless_mt5735_tx
HWLOG_REGIST();

static bool g_st88_tx_open_flag;

static const char * const g_mt5735_tx_irq_name[] = {
	/* [n]: n means bit in registers */
	[0]  = "tx_rst",
	[1]  = "tx_otp",
	[8]  = "tx_ss_pkt_rcvd",
	[9]  = "tx_id_pkt_rcvd",
	[10] = "tx_cfg_pkt_rcvd",
	[11] = "tx_pwr_trans",
	[12] = "tx_rmv_pwr",
	[13] = "tx_disable",
	[14] = "tx_enable",
	[15] = "tx_pp_pkt_rcvd",
	[16] = "tx_dping_rcvd",
	[17] = "tx_ept_pkt_rcvd",
	[18] = "tx_start_ping",
	[19] = "tx_ovp",
	[20] = "tx_ocp",
	[23] = "tx_fod_det",
	[24] = "tx_cep_timeout",
	[25] = "tx_rpp_timeout",
};
static const char * const g_mt5735_tx_ept_name[] = {
	/* [n]: n means bit in registers */
	[0]  = "tx_ept_src_ovp",
	[1]  = "tx_ept_src_ocp",
	[2]  = "tx_ept_src_otp",
	[3]  = "tx_ept_src_fod",
	[4]  = "tx_ept_src_cmd",
	[5]  = "tx_ept_src_rx",
	[6]  = "tx_ept_src_cep_timeout",
	[7]  = "tx_ept_src_rpp_timeout",
	[8]  = "tx_ept_src_rx_rst",
	[9]  = "tx_ept_src_sys_err",
	[10] = "tx_ept_src_ss_timeout",
	[11] = "tx_ept_src_ss",
	[12] = "tx_ept_src_id",
	[13] = "tx_ept_src_cfg",
	[14] = "tx_ept_src_cfg_cnt",
	[15] = "tx_ept_src_pch",
	[16] = "tx_ept_src_xid",
	[17] = "tx_ept_src_nego",
	[18] = "tx_ept_src_nego_timeout",
};

static int mt5735_sw2tx(void)
{
	int ret;
	int i;
	u16 mode = 0;
	u32 reg_val;
	int cnt = MT5735_SW2TX_RETRY_TIME / MT5735_SW2TX_SLEEP_TIME;

	for (i = 0; i < cnt; i++) {
		if (!g_st88_tx_open_flag) {
			hwlog_err("sw2tx: tx_open_flag false\n");
			return -WLC_ERR_STOP_CHRG;
		}
		msleep(MT5735_SW2TX_SLEEP_TIME);
		ret = mt5735_get_mode(&mode);
		if (ret) {
			hwlog_err("sw2tx: get mode failed\n");
			continue;
		}
		if (mode == MT5735_OP_MODE_TX) {
			hwlog_info("sw2tx: succ, cnt=%d\n", i);
			msleep(MT5735_SW2TX_SLEEP_TIME);
			return 0;
		}
		reg_val = MT5735_TX_CMD_EN_TX;
		ret = mt5735_write_block(MT5735_TX_CMD_ADDR, (u8 *)&reg_val,
		MT5735_TX_CMD_LEN);
		if (ret)
			hwlog_err("sw2tx: write cmd(sw2tx) failed\n");
	}
	hwlog_err("sw2tx: failed, cnt=%d\n", i);
	return -WLC_ERR_I2C_WR;
}

static bool mt5735_tx_is_tx_mode(void)
{
	int ret;
	u32 mode = 0;

	ret = mt5735_read_block(MT5735_OP_MODE_ADDR, (u8 *)&mode,
		MT5735_OP_MODE_LEN);
	if (ret) {
		hwlog_err("is_tx_mode: get op_mode failed\n");
		return false;
	}

	return (mode & MT5735_OP_MODE_TX);
}

static bool mt5735_tx_is_rx_mode(void)
{
	int ret;
	u32 mode = 0;

	ret = mt5735_read_block(MT5735_OP_MODE_ADDR, (u8 *)&mode,
		MT5735_OP_MODE_LEN);
	if (ret) {
		hwlog_err("is_rx_mode: get rx mode failed\n");
		return false;
	}

	return (mode & MT5735_OP_MODE_RX);
}

static void mt5735_tx_set_tx_open_flag(bool enable)
{
	g_st88_tx_open_flag = enable;
}

static int mt5735_tx_mode_vset(int tx_vset)
{
	int ret;
	u8 gpio_val;

	if (tx_vset == MT5735_TX_PS_VOLT_5V5)
		gpio_val = MT5735_TX_PS_GPIO_PU;
	else if (tx_vset == MT5735_TX_PS_VOLT_6V8)
		gpio_val = MT5735_TX_PS_GPIO_OPEN;
	else if (tx_vset == MT5735_TX_PS_VOLT_10V)
		gpio_val = MT5735_TX_PS_GPIO_PD;
	else
		return -WLC_ERR_PARA_WRONG;

	ret = mt5735_write_byte_mask(MT5735_TX_CUST_CTRL_ADDR,
		MT5735_TX_PS_GPIO_MASK, MT5735_TX_PS_GPIO_SHIFT, gpio_val);
	if (ret) {
		hwlog_err("tx_mode_vset: write failed\n");
		return ret;
	}

	return 0;
}

static int mt5735_tx_set_pt_bridge(u8 type)
{
	int ret;

	ret = mt5735_write_byte_mask(MT5735_TX_CUST_CTRL_ADDR,
		MT5735_TX_PT_BRIDGE_MASK, MT5735_TX_PT_BRIDGE_SHIFT, type);
	if (ret) {
		hwlog_err("set pt bridge: write failed\n");
		return ret;
	}

	return 0;
}

static int mt5735_tx_set_ping_bridge(u8 type)
{
	int ret;

	ret = mt5735_write_byte_mask(MT5735_TX_CUST_CTRL_ADDR,
		MT5735_TX_PING_BRIDGE_MASK, MT5735_TX_PING_BRIDGE_SHIFT, type);
	ret += mt5735_tx_set_pt_bridge(MT5735_TX_PT_BRIDGE_NO_CHANGE);
	if (ret) {
		hwlog_err("set ping bridge: write failed\n");
		return ret;
	}

	return 0;
}

static int mt5735_tx_set_bridge(int v_ask, enum wltx_bridge_type type)
{
	switch (type) {
	case WLTX_PING_HALF_BRIDGE:
		return mt5735_tx_set_ping_bridge(MT5735_TX_PING_HALF_BRIDGE);
	case WLTX_PING_FULL_BRIDGE:
		return mt5735_tx_set_ping_bridge(MT5735_TX_PING_FULL_BRIDGE);
	case WLTX_PT_HALF_BRIDGE:
		return mt5735_tx_set_pt_bridge(MT5735_TX_PT_HALF_BRIDGE);
	case WLTX_PT_FULL_BRIDGE:
		return mt5735_tx_set_pt_bridge(MT5735_TX_PT_FULL_BRIDGE);
	default:
		return -EINVAL;
	}
}

static bool mt5735_tx_check_rx_disconnect(void)
{
	struct mt5735_dev_info *di = NULL;

	mt5735_get_dev_info(&di);
	if (!di)
		return true;

	if (di->ept_type & MT5735_TX_EPT_SRC_CEP_TIMEOUT) {
		di->ept_type &= ~MT5735_TX_EPT_SRC_CEP_TIMEOUT;
		hwlog_info("[check_rx_disconnect] rx disconnect\n");
		return true;
	}

	return false;
}

static int mt5735_tx_get_ping_interval(u16 *ping_interval)
{
	int ret;
	u16 data = 0;

	if (!ping_interval) {
		hwlog_err("get_ping_interval: para null\n");
		return -WLC_ERR_PARA_NULL;
	}

	ret = mt5735_read_word(MT5735_TX_PING_INTERVAL_ADDR, &data);
	if (ret) {
		hwlog_err("get_ping_interval: read failed\n");
		return ret;
	}
	*ping_interval = data / MT5735_TX_PING_INTERVAL_STEP;

	return 0;
}

static int mt5735_tx_set_ping_interval(u16 ping_interval)
{
	int ret;
	u16 reg_val;

	if ((ping_interval < MT5735_TX_PING_INTERVAL_MIN) ||
		(ping_interval > MT5735_TX_PING_INTERVAL_MAX)) {
		hwlog_err("set_ping_interval: para out of range\n");
		return -WLC_ERR_PARA_WRONG;
	}

	reg_val = ping_interval * MT5735_TX_PING_INTERVAL_STEP;
	ret = mt5735_write_word(MT5735_TX_PING_INTERVAL_ADDR, reg_val);
	if (ret) {
		hwlog_err("set_ping_interval: write failed\n");
		return ret;
	}

	return 0;
}

static int mt5735_tx_get_ping_frequency(u16 *ping_freq)
{
	int ret;
	u16 data = 0;

	if (!ping_freq) {
		hwlog_err("get_ping_frequency: para null\n");
		return -WLC_ERR_PARA_NULL;
	}

	ret = mt5735_read_word(MT5735_TX_PING_FREQ_ADDR, &data);
	if (ret) {
		hwlog_err("get_ping_frequency: read failed\n");
		return ret;
	}
	*ping_freq = data / MT5735_TX_PING_STEP;

	return 0;
}

static int mt5735_tx_set_ping_frequency(u16 ping_freq)
{
	int ret;

	if ((ping_freq < MT5735_TX_PING_FREQ_MIN) ||
		(ping_freq > MT5735_TX_PING_FREQ_MAX)) {
		hwlog_err("set_ping_frequency: para out of range\n");
		return -WLC_ERR_PARA_WRONG;
	}

	ret = mt5735_write_word(MT5735_TX_PING_FREQ_ADDR,
		ping_freq * MT5735_TX_PING_STEP);
	if (ret) {
		hwlog_err("set_ping_frequency: write failed\n");
		return ret;
	}

	return ret;
}

static int mt5735_tx_get_min_fop(u16 *fop)
{
	int ret;
	u16 data = 0;

	if (!fop) {
		hwlog_err("get_min_fop: para null\n");
		return -WLC_ERR_PARA_NULL;
	}

	ret = mt5735_read_word(MT5735_TX_MIN_FOP_ADDR, &data);
	if (ret) {
		hwlog_err("get_min_fop: read failed\n");
		return ret;
	}
	*fop = data / MT5735_TX_FOP_STEP;

	return 0;
}

static int mt5735_tx_set_min_fop(u16 fop)
{
	int ret;

	if ((fop < MT5735_TX_MIN_FOP) || (fop > MT5735_TX_MAX_FOP)) {
		hwlog_err("set_min_fop: para out of range\n");
		return -WLC_ERR_PARA_WRONG;
	}

	ret = mt5735_write_word(MT5735_TX_MIN_FOP_ADDR,
		fop * MT5735_TX_FOP_STEP);
	if (ret) {
		hwlog_err("set_min_fop: write failed\n");
		return ret;
	}

	return 0;
}

static int mt5735_tx_get_max_fop(u16 *fop)
{
	int ret;
	u16 data = 0;

	if (!fop) {
		hwlog_err("get_max_fop: para null\n");
		return -WLC_ERR_PARA_NULL;
	}

	ret = mt5735_read_word(MT5735_TX_MAX_FOP_ADDR, &data);
	if (ret) {
		hwlog_err("get_max_fop: read failed\n");
		return ret;
	}
	*fop = (u16)data / MT5735_TX_FOP_STEP;

	return 0;
}

static int mt5735_tx_set_max_fop(u16 fop)
{
	int ret;

	if ((fop < MT5735_TX_MIN_FOP) || (fop > MT5735_TX_MAX_FOP)) {
		hwlog_err("set_max_fop: para out of range\n");
		return -WLC_ERR_PARA_WRONG;
	}

	ret = mt5735_write_word(MT5735_TX_MAX_FOP_ADDR,
		fop * MT5735_TX_FOP_STEP);
	if (ret) {
		hwlog_err("set_max_fop: write failed\n");
		return ret;
	}

	return 0;
}

static int mt5735_tx_get_fop(u16 *fop)
{
	int ret;
	u16 val = 0;

	if (!fop) {
		hwlog_err("get_fop: para null\n");
		return -WLC_ERR_PARA_NULL;
	}

	ret = mt5735_read_word(MT5735_TX_OP_FREQ_ADDR, &val);
	if (ret) {
		hwlog_err("get_fop: failed\n");
		return ret;
	}

	*fop = val / MT5735_TX_OP_FREQ_STEP; /* 1kHz=1000Hz */
	return 0;
}

static int mt5735_tx_set_duty_cycle(u8 min_dc, u8 max_dc)
{
	int ret;

	if (min_dc > max_dc)
		return -EINVAL;

	ret = mt5735_write_byte(MT5735_TX_PING_DC_ADDR, MT5735_TX_PING_DC_VAL);
	if (ret) {
		hwlog_err("set_duty_cycle: failed\n");
		return ret;
	}

	return 0;
}

static int mt5735_tx_get_temp(u8 *chip_temp)
{
	int ret;
	u16 temp = 0;

	if (!chip_temp) {
		hwlog_err("get_temp: para null\n");
		return -WLC_ERR_PARA_NULL;
	}

	ret = mt5735_read_word(MT5735_TX_CHIP_TEMP_ADDR, &temp);
	if (ret) {
		hwlog_err("get_temp: read failed\n");
		return ret;
	}

	*chip_temp = (u8)(temp);
	return 0;
}

static int mt5735_tx_get_vin(u16 *tx_vin)
{
	int ret;

	if (!tx_vin) {
		hwlog_err("get_vin: para null\n");
		return -WLC_ERR_PARA_NULL;
	}

	ret = mt5735_read_word(MT5735_TX_VIN_ADDR, tx_vin);
	if (ret) {
		hwlog_err("get_vin: read failed\n");
		return ret;
	}

	return 0;
}

static int mt5735_tx_get_vrect(u16 *tx_vrect)
{
	int ret;

	if (!tx_vrect) {
		hwlog_err("get_vrect: para null\n");
		return -WLC_ERR_PARA_NULL;
	}

	ret = mt5735_read_word(MT5735_TX_VRECT_ADDR, tx_vrect);
	if (ret) {
		hwlog_err("get_vrect: read failed\n");
		return ret;
	}

	return 0;
}

static int mt5735_tx_get_iin(u16 *tx_iin)
{
	int ret;

	if (!tx_iin) {
		hwlog_err("get_iin: para null\n");
		return -WLC_ERR_PARA_NULL;
	}

	ret = mt5735_read_word(MT5735_TX_IIN_ADDR, tx_iin);
	if (ret) {
		hwlog_err("get_iin: read failed\n");
		return ret;
	}

	return 0;
}

static int mt5735_tx_set_ilimit(int tx_ilim)
{
	return 0;
}

static int mt5735_tx_set_fod_coef(u32 pl_th, u8 pl_cnt)
{
	return 0;
}

static void mt5735_tx_set_rp_dm_timeout_val(u8 val)
{
}

static int mt5735_tx_stop_config(void)
{
	return 0;
}

static void mt5735_tx_activate_chip(void)
{
	int ret;

	ret = mt5735_sw2tx();
	if (ret)
		hwlog_err("activate_tx_chip: sw2tx failed\n");
}

static int mt5735_tx_set_irq_en(u32 val)
{
	int ret;

	ret = mt5735_write_block(MT5735_TX_IRQ_EN_ADDR, (u8 *)&val,
		MT5735_TX_IRQ_EN_LEN);
	if (ret) {
		hwlog_err("irq_en: write failed\n");
		return ret;
	}

	return 0;
}

static void mt5735_tx_select_init_para(struct mt5735_dev_info *di,
	enum wltx_open_type type)
{
	switch (type) {
	case WLTX_OPEN_BY_CLIENT:
		di->tx_init_para.ping_freq = MT5735_TX_PING_FREQ;
		di->tx_init_para.ping_interval = MT5735_TX_PING_INTERVAL;
		break;
	case WLTX_OPEN_BY_COIL_TEST:
		di->tx_init_para.ping_freq = MT5735_COIL_TEST_PING_FREQ;
		di->tx_init_para.ping_interval = MT5735_COIL_TEST_PING_INTERVAL;
		break;
	default:
		di->tx_init_para.ping_freq = MT5735_TX_PING_FREQ;
		di->tx_init_para.ping_interval = MT5735_TX_PING_INTERVAL;
		break;
	}
}

static int mt5735_tx_set_init_para(struct mt5735_dev_info *di)
{
	int ret;
	u32 reg_val;

	ret = mt5735_sw2tx();
	if (ret) {
		hwlog_err("set_init_para: sw2tx failed\n");
		return ret;
	}
	reg_val = MT5735_TX_CMD_OTP;
	ret = mt5735_write_byte(MT5735_TX_OTP_TH_ADDR, MT5735_TX_OTP_TH);
	ret += mt5735_write_block(MT5735_TX_CMD_ADDR, (u8 *)&reg_val,
		MT5735_TX_CMD_LEN);
	reg_val = MT5735_TX_CMD_OVP;
	ret += mt5735_write_word(MT5735_TX_OVP_TH_ADDR, MT5735_TX_OVP_TH);
	ret += mt5735_write_block(MT5735_TX_CMD_ADDR, (u8 *)&reg_val,
		MT5735_TX_CMD_LEN);
	reg_val = MT5735_TX_CMD_OCP;
	ret += mt5735_write_word(MT5735_TX_OCP_TH_ADDR, MT5735_TX_OCP_TH);
	ret += mt5735_write_block(MT5735_TX_CMD_ADDR, (u8 *)&reg_val,
		MT5735_TX_CMD_LEN);
	ret += mt5735_tx_set_fod_coef(MT5735_TX_PLOSS_TH_VAL,
		MT5735_TX_PLOSS_CNT_VAL);
	ret += mt5735_tx_set_ping_frequency(di->tx_init_para.ping_freq);
	ret += mt5735_tx_set_min_fop(MT5735_TX_MIN_FOP);
	ret += mt5735_tx_set_max_fop(MT5735_TX_MAX_FOP);
	ret += mt5735_tx_set_ping_interval(di->tx_init_para.ping_interval);
	ret += mt5735_tx_set_duty_cycle(MT5735_TX_MIN_DC, MT5735_TX_MAX_DC);
	ret += mt5735_tx_set_irq_en(MT5735_TX_IRQ_EN_VAL);
	if (ret) {
		hwlog_err("set_init_para: write failed\n");
		return -WLC_ERR_I2C_W;
	}

	return 0;
}

static int mt5735_tx_chip_init(enum wltx_open_type type)
{
	struct mt5735_dev_info *di = NULL;

	mt5735_get_dev_info(&di);
	if (!di)
		return -WLC_ERR_PARA_NULL;

	di->irq_cnt = 0;
	di->g_val.irq_abnormal_flag = false;
	mt5735_enable_irq();

	mt5735_tx_select_init_para(di, type);
	return mt5735_tx_set_init_para(di);
}

static int mt5735_tx_enable_tx_mode(bool enable)
{
	int ret;
	u32 reg_val;

	if (enable) {
		reg_val = MT5735_TX_CMD_START_TX;
		ret = mt5735_write_block(MT5735_TX_CMD_ADDR, (u8 *)&reg_val,
			MT5735_TX_CMD_LEN);
	} else {
		reg_val = MT5735_TX_CMD_STOP_TX;
		ret = mt5735_write_block(MT5735_TX_CMD_ADDR, (u8 *)&reg_val,
			MT5735_TX_CMD_LEN);
	}

	if (ret) {
		hwlog_err("%s tx_mode failed\n", enable ? "enable" : "disable");
		return ret;
	}

	return 0;
}

static void mt5735_tx_show_ept_type(u32 ept)
{
	u32 i;

	for (i = 0; i < ARRAY_SIZE(g_mt5735_tx_ept_name); i++) {
		if (ept & BIT(i))
			hwlog_info("[tx_ept] %s\n", g_mt5735_tx_ept_name[i]);
	}
}

static int mt5735_tx_get_ept_type(u32 *ept)
{
	int ret;
	u32 data = 0;

	if (!ept) {
		hwlog_err("get_ept_type: para null\n");
		return -WLC_ERR_PARA_NULL;
	}

	ret = mt5735_read_block(MT5735_TX_EPT_SRC_ADDR, (u8 *)&data,
		MT5735_TX_EPT_SRC_LEN);
	if (ret) {
		hwlog_err("get_ept_type: read failed\n");
		return ret;
	}
	hwlog_info("[get_ept_type] type=0x%08x", data);
	mt5735_tx_show_ept_type(data);
	*ept = data;

	ret = mt5735_write_word(MT5735_TX_EPT_SRC_ADDR, 0);
	if (ret) {
		hwlog_err("get_ept_type: clr failed\n");
		return ret;
	}

	return 0;
}

static void mt5735_tx_ept_handler(struct mt5735_dev_info *di)
{
	int ret;

	ret = mt5735_tx_get_ept_type(&di->ept_type);
	if (ret)
		return;

	switch (di->ept_type) {
	case MT5735_TX_EPT_SRC_RX_EPT:
	case MT5735_TX_EPT_SRC_RX_RST:
	case MT5735_TX_EPT_SRC_CEP_TIMEOUT:
		di->ept_type &= ~MT5735_TX_EPT_SRC_CEP_TIMEOUT;
		power_event_notify(POWER_NT_WLTX,
			POWER_NE_WLTX_CEP_TIMEOUT, NULL);
		break;
	case MT5735_TX_EPT_SRC_FOD:
		di->ept_type &= ~MT5735_TX_EPT_SRC_FOD;
		power_event_notify(POWER_NT_WLTX,
			POWER_NE_WLTX_TX_FOD, NULL);
		break;
	default:
		break;
	}
}

static int mt5735_tx_clear_irq(u32 itr)
{
	int ret;
	u32 reg_val = MT5735_TX_CMD_CLEAR_INT;

	ret = mt5735_write_block(MT5735_TX_IRQ_CLR_ADDR, (u8 *)&itr,
		MT5735_TX_IRQ_CLR_LEN);
	ret += mt5735_write_block(MT5735_TX_CMD_ADDR, (u8 *)&reg_val,
		MT5735_TX_CMD_LEN);
	if (ret) {
		hwlog_err("clear_irq: write failed\n");
		return ret;
	}

	return 0;
}

static void mt5735_tx_ask_pkt_handler(struct mt5735_dev_info *di)
{
	if (di->irq_val & MT5735_TX_IRQ_SS_PKG_RCVD) {
		di->irq_val &= ~MT5735_TX_IRQ_SS_PKG_RCVD;
		if (di->g_val.qi_hdl && di->g_val.qi_hdl->hdl_qi_ask_pkt)
			di->g_val.qi_hdl->hdl_qi_ask_pkt();
	}

	if (di->irq_val & MT5735_TX_IRQ_ID_PKT_RCVD) {
		di->irq_val &= ~MT5735_TX_IRQ_ID_PKT_RCVD;
		if (di->g_val.qi_hdl && di->g_val.qi_hdl->hdl_qi_ask_pkt)
			di->g_val.qi_hdl->hdl_qi_ask_pkt();
	}

	if (di->irq_val & MT5735_TX_IRQ_CFG_PKT_RCVD) {
		di->irq_val &= ~MT5735_TX_IRQ_CFG_PKT_RCVD;
		if (di->g_val.qi_hdl && di->g_val.qi_hdl->hdl_qi_ask_pkt)
			di->g_val.qi_hdl->hdl_qi_ask_pkt();
		power_event_notify(POWER_NT_WLTX,
			POWER_NE_WLTX_GET_CFG, NULL);
	}

	if (di->irq_val & MT5735_TX_IRQ_PP_PKT_RCVD) {
		di->irq_val &= ~MT5735_TX_IRQ_PP_PKT_RCVD;
		if (di->g_val.qi_hdl && di->g_val.qi_hdl->hdl_non_qi_ask_pkt)
			di->g_val.qi_hdl->hdl_non_qi_ask_pkt();
	}
}

static void mt5735_tx_show_irq(u32 intr)
{
	u32 i;

	for (i = 0; i < ARRAY_SIZE(g_mt5735_tx_irq_name); i++) {
		if (intr & BIT(i))
			hwlog_info("[tx_irq] %s\n", g_mt5735_tx_irq_name[i]);
	}
}

static int mt5735_tx_get_interrupt(u32 *intr)
{
	int ret;

	ret = mt5735_read_block(MT5735_TX_IRQ_ADDR, (u8 *)intr,
		MT5735_TX_IRQ_LEN);
	if (ret)
		return ret;

	hwlog_info("[get_interrupt] irq=0x%08x\n", *intr);
	mt5735_tx_show_irq(*intr);

	return 0;
}

static void mt5735_tx_mode_irq_recheck(struct mt5735_dev_info *di)
{
	int ret;
	u32 irq_val = 0;

	if (gpio_get_value(di->gpio_int))
		return;

	hwlog_info("[tx_mode_irq_recheck] gpio_int low, re-check irq\n");
	ret = mt5735_tx_get_interrupt(&irq_val);
	if (ret)
		return;

	mt5735_tx_clear_irq(MT5735_TX_IRQ_CLR_ALL);
}

void mt5735_tx_mode_irq_handler(struct mt5735_dev_info *di)
{
	int ret;

	if (!di)
		return;

	ret = mt5735_tx_get_interrupt(&di->irq_val);
	if (ret) {
		hwlog_err("irq_handler: get irq failed, clear\n");
		mt5735_tx_clear_irq(MT5735_TX_IRQ_CLR_ALL);
		goto rechk_irq;
	}

	mt5735_tx_clear_irq(di->irq_val);

	mt5735_tx_ask_pkt_handler(di);

	if (di->irq_val & MT5735_TX_IRQ_START_PING) {
		di->irq_val &= ~MT5735_TX_IRQ_START_PING;
		power_event_notify(POWER_NT_WLTX,
			POWER_NE_WLTX_PING_RX, NULL);
	}
	if (di->irq_val & MT5735_TX_IRQ_EPT_PKT_RCVD) {
		di->irq_val &= ~MT5735_TX_IRQ_EPT_PKT_RCVD;
		mt5735_tx_ept_handler(di);
	}
	if (di->irq_val & MT5735_TX_IRQ_DPING_RCVD) {
		di->irq_val &= ~MT5735_TX_IRQ_DPING_RCVD;
		power_event_notify(POWER_NT_WLTX,
			POWER_NE_WLTX_RCV_DPING, NULL);
	}
	if (di->irq_val & MT5735_TX_IRQ_RPP_TIMEOUT) {
		di->irq_val &= ~MT5735_TX_IRQ_RPP_TIMEOUT;
		power_event_notify(POWER_NT_WLTX,
			POWER_NE_WLTX_RP_DM_TIMEOUT, NULL);
	}
	if (di->irq_val & MT5735_TX_IRQ_FOD_DET) {
		di->irq_val &= ~MT5735_TX_IRQ_FOD_DET;
		power_event_notify(POWER_NT_WLTX,
			POWER_NE_WLTX_TX_FOD, NULL);
	}

rechk_irq:
	mt5735_tx_mode_irq_recheck(di);
}

static struct wireless_tx_device_ops g_mt5735_tx_ops = {
	.rx_enable              = mt5735_chip_enable,
	.rx_sleep_enable        = mt5735_sleep_enable,
	.chip_reset             = mt5735_chip_reset,
	.enable_tx_mode         = mt5735_tx_enable_tx_mode,
	.tx_chip_init           = mt5735_tx_chip_init,
	.tx_stop_config         = mt5735_tx_stop_config,
	.check_fwupdate         = mt5735_fw_sram_update,
	.get_tx_iin             = mt5735_tx_get_iin,
	.get_tx_vrect           = mt5735_tx_get_vrect,
	.get_tx_vin             = mt5735_tx_get_vin,
	.get_chip_temp          = mt5735_tx_get_temp,
	.get_tx_fop             = mt5735_tx_get_fop,
	.set_tx_max_fop         = mt5735_tx_set_max_fop,
	.get_tx_max_fop         = mt5735_tx_get_max_fop,
	.set_tx_min_fop         = mt5735_tx_set_min_fop,
	.get_tx_min_fop         = mt5735_tx_get_min_fop,
	.set_tx_ping_frequency  = mt5735_tx_set_ping_frequency,
	.get_tx_ping_frequency  = mt5735_tx_get_ping_frequency,
	.set_tx_ping_interval   = mt5735_tx_set_ping_interval,
	.get_tx_ping_interval   = mt5735_tx_get_ping_interval,
	.check_rx_disconnect    = mt5735_tx_check_rx_disconnect,
	.in_tx_mode             = mt5735_tx_is_tx_mode,
	.in_rx_mode             = mt5735_tx_is_rx_mode,
	.set_tx_open_flag       = mt5735_tx_set_tx_open_flag,
	.set_tx_ilimit          = mt5735_tx_set_ilimit,
	.set_tx_fod_coef        = mt5735_tx_set_fod_coef,
	.set_rp_dm_timeout_val  = mt5735_tx_set_rp_dm_timeout_val,
	.activate_tx_chip       = mt5735_tx_activate_chip,
	.set_bridge             = mt5735_tx_set_bridge,
};

static struct wlps_tx_ops g_mt5735_txps_ops = {
	.tx_vset                = mt5735_tx_mode_vset,
};

int mt5735_tx_ops_register(void)
{
	return wireless_tx_ops_register(&g_mt5735_tx_ops);
}

int mt5735_tx_ps_ops_register(void)
{
	int ret;
	u32 tx_ps_ctrl_src = 0;

	ret = power_dts_read_u32_compatible(power_dts_tag(HWLOG_TAG),
		"huawei,wireless_ps", "tx_ps_ctrl_src", &tx_ps_ctrl_src, 0);
	if (ret)
		return ret;

	if (tx_ps_ctrl_src == WLPS_TX_SRC_TX_CHIP)
		return wlps_tx_ops_register(&g_mt5735_txps_ops);

	return 0;
}
