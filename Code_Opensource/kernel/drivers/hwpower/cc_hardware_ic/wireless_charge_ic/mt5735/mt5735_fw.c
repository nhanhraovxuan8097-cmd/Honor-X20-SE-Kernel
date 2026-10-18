/*
 * mt5735_fw.c
 *
 * mt5735 mtp, sram driver
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
#include "mt5735_mtp.h"

#define HWLOG_TAG wireless_mt5735_fw
HWLOG_REGIST();

static int mt5735_get_major_fw_version(u16 *fw)
{
	return mt5735_read_word(MT5735_MTP_MAJOR_ADDR, fw);
}

static int mt5735_get_minor_fw_version(u16 *fw)
{
	return mt5735_read_word(MT5735_MTP_MINOR_ADDR, fw);
}

static int mt5735_mtp_version_check(void)
{
	int ret;
	u16 major_fw_ver = 0;
	u16 minor_fw_ver = 0;

	ret = mt5735_get_major_fw_version(&major_fw_ver);
	if (ret) {
		hwlog_err("get major fw_ver fail\n");
		return ret;
	}
	hwlog_info("major_fw=0x%04x\n", major_fw_ver);

	ret = mt5735_get_minor_fw_version(&minor_fw_ver);
	if (ret) {
		hwlog_err("get minor fw_ver fail\n");
		return ret;
	}
	hwlog_info("minor_fw=0x%04x\n", minor_fw_ver);

	if ((major_fw_ver != MT5735_MTP_MAJOR_VER) ||
		(minor_fw_ver != MT5735_MTP_MINOR_VER))
		return -WLC_ERR_MISMATCH;

	return 0;
}

static int mt5735_mtp_pwr_cycle_chip(void)
{
	int ret;

	ret = mt5735_write_byte(0x5808, 0x95); /* unlock system registers */
	ret += mt5735_write_byte(0x5800, 0x01); /* set HS clock */
	ret += mt5735_write_byte(0x5244, 0x57); /* set AHB clock */
	ret += mt5735_write_byte(0x5200, 0x20); /* configure 1us pulse */
	ret += mt5735_write_byte(0x5218, 0xFF); /* configure 500ns pulse */
	ret += mt5735_write_byte(0x5219, 0x0F); /* remove MTP protection */
	ret += mt5735_write_byte(0x5208, 0x08); /* reset M0 */
	msleep(50); /* delay 50ms for mtp enable */
	if (ret) {
		hwlog_err("enable mtp fail\n");
		return ret;
	}

	return 0;
}

static int mt5735_mtp_load_bootloader(struct mt5735_dev_info *di)
{
	int ret;
	int remaining = ARRAY_SIZE(g_mt5735_bootloader);
	int size_to_wr;
	int wr_already = 0;
	u16 chip_id = 0;
	u16 addr = MT5735_BTLOADR_DATA_ADDR;
	u8 wr_buff[MT5735_PAGE_SIZE] = { 0 };

	hwlog_info("bootloader_size=%d\n", remaining);
	while (remaining > 0) {
		size_to_wr = remaining > MT5735_PAGE_SIZE ?
			MT5735_PAGE_SIZE : remaining;
		memcpy(wr_buff, g_mt5735_bootloader + wr_already, size_to_wr);
		ret = mt5735_write_block(addr, wr_buff, size_to_wr);
		if (ret) {
			hwlog_err("wr fail, addr=0x%08x\n", addr);
			return ret;
		}
		addr += size_to_wr;
		wr_already += size_to_wr;
		remaining -= size_to_wr;
	}

	ret = mt5735_write_byte(0x5200, 0x80); /* run M0 */
	if (ret) {
		hwlog_err("remap to RAM fail\n");
		return ret;
	}
	msleep(50); /* 50ms delay for soft program */
	ret = mt5735_read_word(MT5735_MTP_CHIP_ID_ADDR, &chip_id);
	if (ret || (chip_id != MT5735_CHIP_ID)) {
		hwlog_err("ret = %d, chip_id = %u\n", ret, chip_id);
		return ret;
	}
	hwlog_info("load bootloader succ\n");

	return 0;
}

static int mt5735_mtp_status_check(u16 expect_status)
{
	int i;
	int ret;
	u16 status = 0;

	/* wait for 500ms for mtp/crc check */
	for (i = 0; i < 50; i++) {
		power_usleep(DT_USLEEP_10MS);
		ret = mt5735_read_word(MT5735_MTP_STRT_ADDR, &status);
		if (ret) {
			hwlog_err("read 0x0000 fail\n");
			return ret;
		}
		if (status & expect_status)
			break;
	}

	if (status != expect_status) {
		hwlog_err("mtp status check fail, status = 0x%x\n", status);
		return -WLC_ERR_CHECK_FAIL;
	}

	return 0;
}

static int mt5735_mtp_crc_check(struct mt5735_dev_info *di)
{
	int ret;

	ret = mt5735_mtp_load_bootloader(di);
	if (ret) {
		hwlog_err("load bootloader fail\n");
		return ret;
	}

	/* write mtp start address */
	ret = mt5735_write_word(0x0002, 0x0000);
	/* write 16-bit MTP data size */
	ret += mt5735_write_word(0x0004, (u16)ARRAY_SIZE(g_mt5735_mtp));
	/* write the 16-bit CRC */
	ret += mt5735_write_word(0x0006, (u16)MT5735_MTP_CRC);
	/* start MTP data CRC-16 check */
	ret += mt5735_write_word(MT5735_MTP_STRT_ADDR, 0x0040);
	ret += mt5735_mtp_status_check(MT5735_CRC_WR_OK);
	if (ret) {
		hwlog_err("mtp crc check fail\n");
		return ret;
	}

	return 0;
}

static int mt5735_check_mtp_match(void)
{
	int ret;
	struct mt5735_dev_info *di = NULL;

	mt5735_get_dev_info(&di);
	if (!di)
		return -WLC_ERR_PARA_NULL;

	if (di->mtp_status == MT5735_MTP_STATUS_GOOD)
		return 0;

	wlps_control(WLPS_RX_EXT_PWR, WLPS_CTRL_ON);
	msleep(100); /* delay for power on */

	ret = mt5735_mtp_version_check();
	if (ret) {
		hwlog_err("version check fail\n");
		goto check_fail;
	}

	ret = mt5735_mtp_pwr_cycle_chip();
	if (ret) {
		hwlog_err("power cycle chip fail\n");
		goto check_fail;
	}

	ret = mt5735_mtp_crc_check(di);
	if (ret) {
		hwlog_err("crc check fail\n");
		goto check_fail;
	}

	di->mtp_status = MT5735_MTP_STATUS_GOOD;
	wlps_control(WLPS_RX_EXT_PWR, WLPS_CTRL_OFF);
	return 0;

check_fail:
	di->mtp_status = MT5735_MTP_STATUS_BAD;
	wlps_control(WLPS_RX_EXT_PWR, WLPS_CTRL_OFF);
	return -WLC_ERR_CHECK_FAIL;
}

static int mt5735_mtp_program_check(void)
{
	int ret;

	 /* start programming */
	ret = mt5735_write_byte(MT5735_MTP_STRT_ADDR, 0x10);
	if (ret) {
		hwlog_err("start programming fail\n");
		return ret;
	}
	ret = mt5735_mtp_status_check(MT5735_MTP_WR_OK);
	if (ret) {
		hwlog_err("check mtp status fail\n");
		return ret;
	}

	return 0;
}

static int mt5735_mtp_load_fw(struct mt5735_dev_info *di)
{
	int i;
	int ret;
	u16 addr = MT5735_MTP_STRT_ADDR;
	int offset = 0;
	int remaining = ARRAY_SIZE(g_mt5735_mtp);
	int wr_size;
	struct mt5735_pgm_str pgm_str;

	hwlog_info("mtp_size=%d\n", remaining);
	while (remaining > 0) {
		memset(&pgm_str, 0, sizeof(struct mt5735_pgm_str));
		wr_size = remaining > MT5735_PAGE_SIZE ?
			MT5735_PAGE_SIZE : remaining;
		pgm_str.addr = addr;
		/* 16 bytes aligned */
		pgm_str.code_len = (wr_size + 15) / 16 * 16;
		memcpy(pgm_str.fw, g_mt5735_mtp + offset, wr_size);
		pgm_str.chk_sum = addr + pgm_str.code_len;
		for (i = 0; i < wr_size; i++)
			pgm_str.chk_sum += pgm_str.fw[i];
		 /* write 8-bytes header + fw, 16 bytes aligned */
		ret = mt5735_write_block(MT5735_MTP_STRT_ADDR,
			(u8 *)&pgm_str, (pgm_str.code_len + 8 + 15) / 16 * 16);
		if (ret) {
			hwlog_err("load fw wr fail\n");
			return ret;
		}
		ret = mt5735_mtp_program_check();
		if (ret) {
			hwlog_err("load fw check fail\n");
			return ret;
		}
		addr += wr_size;
		offset += wr_size;
		remaining -= wr_size;
	}

	return 0;
}

static int mt5735_fw_program_mtp(struct mt5735_dev_info *di)
{
	int ret;

	mt5735_disable_irq_nosync();
	wlps_control(WLPS_PROC_OTP_PWR, WLPS_CTRL_ON);
	mt5735_chip_enable(RX_EN_ENABLE);
	msleep(100); /* delay for power on */

	ret = mt5735_mtp_pwr_cycle_chip();
	if (ret) {
		hwlog_err("power cycle chip fail\n");
		goto exit;
	}
	ret = mt5735_mtp_load_bootloader(di);
	if (ret) {
		hwlog_err("load bootloader fail\n");
		goto exit;
	}
	ret = mt5735_mtp_load_fw(di);
	if (ret) {
		hwlog_err("load fw fail\n");
		goto exit;
	}
	wlps_control(WLPS_PROC_OTP_PWR, WLPS_CTRL_OFF);
	power_usleep(DT_USLEEP_10MS);
	wlps_control(WLPS_PROC_OTP_PWR, WLPS_CTRL_ON);
	power_usleep(DT_USLEEP_10MS);
	ret = mt5735_mtp_pwr_cycle_chip();
	if (ret) {
		hwlog_err("program mtp power cycle chip fail\n");
		goto exit;
	}
	ret = mt5735_mtp_crc_check(di);
	if (ret) {
		hwlog_err("program mtp crc check fail\n");
		goto exit;
	}

	wlps_control(WLPS_PROC_OTP_PWR, WLPS_CTRL_OFF);
	mt5735_enable_irq();
	hwlog_info("program mtp succ\n");
	return 0;

exit:
	mt5735_enable_irq();
	hwlog_err("program mtp fail\n");
	wlps_control(WLPS_PROC_OTP_PWR, WLPS_CTRL_OFF);
	return ret;
}

static int mt5735_fw_rx_program_mtp(int proc_type)
{
	int ret;
	struct mt5735_dev_info *di = NULL;

	mt5735_get_dev_info(&di);
	if (!di)
		return -WLC_ERR_PARA_NULL;

	hwlog_info("program_mtp: type=%d\n", proc_type);
	if (!di->g_val.mtp_chk_complete)
		return -WLC_ERR_I2C_WR;
	di->g_val.mtp_chk_complete = false;
	ret = mt5735_fw_program_mtp(di);
	if (!ret)
		hwlog_info("[rx_program_mtp] succ\n");
	di->g_val.mtp_chk_complete = true;

	return ret;
}

static int mt5735_fw_check_mtp(void)
{
	if (mt5735_check_mtp_match())
		goto check_fail;

	wlps_control(WLPS_RX_EXT_PWR, WLPS_CTRL_OFF);
	return 0;

check_fail:
	wlps_control(WLPS_RX_EXT_PWR, WLPS_CTRL_OFF);
	return -WLC_ERR_CHECK_FAIL;
}

int mt5735_fw_sram_update(enum wireless_mode sram_mode)
{
	int ret;
	u16 minor_fw_ver = 0;

	ret = mt5735_get_minor_fw_version(&minor_fw_ver);
	if (ret) {
		hwlog_err("get minor fw_ver fail\n");
		return ret;
	}

	return 0;
}

static int mt5735_fw_is_mtp_exist(void)
{
	int ret;
	struct mt5735_dev_info *di = NULL;

	mt5735_get_dev_info(&di);
	if (!di)
		return WIRELESS_FW_NON_PROGRAMED;

	di->g_val.mtp_chk_complete = false;
	ret = mt5735_fw_check_mtp();
	if (!ret) {
		di->g_val.mtp_chk_complete = true;
		return WIRELESS_FW_PROGRAMED;
	}
	di->g_val.mtp_chk_complete = true;

	return WIRELESS_FW_NON_PROGRAMED;
}

void mt5735_fw_mtp_check_work(struct work_struct *work)
{
	int i;
	int ret;
	struct mt5735_dev_info *di = NULL;

	mt5735_get_dev_info(&di);
	if (!di)
		return;

	di->g_val.mtp_chk_complete = false;
	ret = mt5735_fw_check_mtp();
	if (!ret) {
		(void)mt5735_chip_reset();
		hwlog_info("[mtp_check_work] succ\n");
		goto exit;
	}

	/* program for 3 times until it's ok */
	for (i = 0; i < 3; i++) {
		ret = mt5735_fw_program_mtp(di);
		if (ret)
			continue;
		hwlog_info("mtp_check_work: update mtp succ, cnt=%d\n", i + 1);
		goto exit;
	}
	hwlog_err("mtp_check_work: update mtp failed\n");

exit:
	di->g_val.mtp_chk_complete = true;
}

static struct wireless_fw_ops g_mt5735_fw_ops = {
	.program_fw  = mt5735_fw_rx_program_mtp,
	.is_fw_exist = mt5735_fw_is_mtp_exist,
	.check_fw    = mt5735_fw_check_mtp,
};

int mt5735_fw_ops_register(void)
{
	return wireless_fw_ops_register(&g_mt5735_fw_ops);
}
