/*
 * Copyright © 2021, ConvenientPower
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/debugfs.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/notifier.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/param.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/printk.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/delay.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/uaccess.h>

#include <linux/miscdevice.h>
#include <linux/kthread.h>
#include <linux/kernel.h>

#include <linux/init.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/pm_wakeup.h>
#include <linux/of_irq.h>
#include <linux/pinctrl/consumer.h>
#include <linux/regmap.h>
#include <securec.h>

#include <mt-plat/charger_class.h>
#include <mt-plat/charger_type.h>
#include <mt-plat/mtk_battery.h>
#include <mt-plat/mtk_boot.h>

#include <cps_wls_charger.h>

/*
 * add for hardwareinfo
 */
#ifdef CONFIG_HQ_HARDWARE_INFO
#include <linux/hardware_info.h>
#endif
#define BOOTLOADER_FILE_NAME "/data/misc/cps/bootloader.hex"
#define FIRMWARE_FILE_NAME "/data/misc/cps/firmware.hex"

#define CPS_WLS_L_CHRG_DRV_NAME "cps-wls-charger-L"
#define CPS_WLS_H_CHRG_DRV_NAME "cps-wls-charger-h"

#define CPS_WLS_CHRG_PSY_NAME "wireless"
#define CPS_PRINTF_BUF_SIZE 32

static char g_cps_printf_buf[CPS_PRINTF_BUF_SIZE];
static struct cps_wls_chrg_chip *g_chip = NULL;
static struct cps_wls_chrg_chip *g_chip_h = NULL;

typedef enum {
	CPS_TX_REG_OCP_TH,
	CPS_TX_REG_UVP_TH,
	CPS_TX_REG_OVP_TH,
	CPS_TX_REG_FOP_MIN,
	CPS_TX_REG_FOP_MAX,
	CPS_TX_REG_PING_FREQ,
	CPS_TX_REG_PING_DUTY,
	CPS_TX_REG_PING_OCP_TH,
	CPS_TX_REG_PING_TIME,
	CPS_TX_REG_PING_INTERVAL,
	CPS_TX_REG_FOD_V0_TH,
	CPS_TX_REG_FOD_V1_TH,
	CPS_TX_REG_FOD_V2_TH,
	CPS_TX_REG_FUNC_EN,
	CPS_TX_REG_MAX_CUR,
	CPS_TX_REG_FOP_VAL,
	CPS_TX_REG_ADC_VIN,
	CPS_TX_REG_ADC_VRECT,
	CPS_TX_REG_ADC_I_IN,
	CPS_TX_REG_CE_VAL,
	CPS_TX_REG_RP_VAL,
	CPS_TX_REG_EPT_RSN,
	CPS_TX_REG_ADC_DIE_TEMP,
	CPS_TX_REG_EPT_CODE,
	CPS_TX_REG_MAX
} cps_tx_reg_e;

typedef enum {
	CPS_COMM_REG_CHIP_ID,
	CPS_COMM_REG_FW_VER,
	CPS_COMM_REG_SYS_MODE,
	CPS_COMM_REG_INT_EN,
	CPS_COMM_REG_INT_FLAG,
	CPS_COMM_REG_INT_CLR,
	CPS_COMM_REG_CMD,
	CPS_COMM_REG_MAX
} cps_comm_reg_e;

typedef struct {
	uint16_t reg_name;
	uint16_t reg_bytes_len;
	uint32_t reg_addr;
} cps_reg_s;

cps_reg_s cps_comm_reg[CPS_COMM_REG_MAX] = {
	/* reg name; bytes number; reg address */
	{ CPS_COMM_REG_CHIP_ID,         4,          0x1D00 },
	{ CPS_COMM_REG_FW_VER,          4,          0x1D10 },
	{ CPS_COMM_REG_SYS_MODE,        1,          0x1D14 },
	{ CPS_COMM_REG_INT_EN,          2,          0x1D40 },
	{ CPS_COMM_REG_INT_FLAG,        2,          0x1D42 },
	{ CPS_COMM_REG_INT_CLR,         2,          0x1D44 },
	{ CPS_COMM_REG_CMD,             2,          0x1D46 },
};

cps_reg_s cps_tx_reg[CPS_TX_REG_MAX] = {
	/* reg name; bytes number; reg address */
	{ CPS_TX_REG_OCP_TH,            2,          0x1E42 },
	{ CPS_TX_REG_UVP_TH,            2,          0x1E44 },
	{ CPS_TX_REG_OVP_TH,            2,          0x1E46 },
	{ CPS_TX_REG_FOP_MIN,           1,          0x1E48 },
	{ CPS_TX_REG_FOP_MAX,           1,          0x1E49 },
	{ CPS_TX_REG_PING_FREQ,         1,          0x1E4A },
	{ CPS_TX_REG_PING_DUTY,         1,          0x1E4B },
	{ CPS_TX_REG_PING_OCP_TH,       2,          0x1E4E },
	{ CPS_TX_REG_PING_TIME,         2,          0x1E52 },
	{ CPS_TX_REG_PING_INTERVAL,     2,          0x1E54 },
	{ CPS_TX_REG_FOD_V0_TH,         2,          0x1E58 },
	{ CPS_TX_REG_FOD_V1_TH,         2,          0x1E5A },
	{ CPS_TX_REG_FOD_V2_TH,         2,          0x1E5C },
	{ CPS_TX_REG_FUNC_EN,           1,          0x1E5E },
	{ CPS_TX_REG_MAX_CUR,           2,          0x1E60 },
	{ CPS_TX_REG_FOP_VAL,           2,          0x1E84 },
	{ CPS_TX_REG_ADC_VIN,           2,          0x1E86 },
	{ CPS_TX_REG_ADC_VRECT,         2,          0x1E88 },
	{ CPS_TX_REG_ADC_I_IN,          2,          0x1E8A },
	{ CPS_TX_REG_CE_VAL,            1,          0x1E8D },
	{ CPS_TX_REG_RP_VAL,            1,          0x1E8E },
	{ CPS_TX_REG_EPT_RSN,           2,          0x1E90 },
	{ CPS_TX_REG_ADC_DIE_TEMP,      2,          0x1E94 },
	{ CPS_TX_REG_EPT_CODE,          1,          0x1E96 },
};

static const struct regmap_config cps4035H_regmap_config = {
	.reg_bits = 16,
	.val_bits = 16,
};

static const struct regmap_config cps4035L_regmap_config = {
	.reg_bits = 16,
	.val_bits = 8,
};

static int cps_wls_l_read_reg(int reg)
{
	int ret = 0;
	int value = 0;

	mutex_lock(&g_chip->i2c_lock);
	ret = regmap_read(g_chip->regmap, reg, &value);
	mutex_unlock(&g_chip->i2c_lock);
	if (ret < 0) {
		cps_wls_log(CPS_LOG_ERR, "[%s] i2c read error!\n", __func__);
		return CPS_WLS_FAIL;
	}

	return value;
}

/*
 * Write register operation
 * If the write register fails, an error message will be printed.
 * If the write succeeds, no attention is needed, \
 * so there is no need to process the return value \
 * Except for some special flow operations
 */
static int cps_wls_h_write_reg(int reg, int value)
{
	int ret = 0;
	int tmp = 0;
	int reg_data = 0;

	tmp = ((value & 0xff) << 8) | ((value >> 8) & 0xff);
	mutex_lock(&g_chip_h->i2c_lock);
	ret = regmap_write(g_chip_h->regmap, reg, tmp);
	mutex_unlock(&g_chip_h->i2c_lock);
	if (ret < 0) {
		reg_data = cps_wls_l_read_reg(reg);
		cps_wls_log(CPS_LOG_ERR, "[%s] i2c write error! reg_addr:0x%x, reg_data:0x%x\n",
			__func__, reg, reg_data);
		return CPS_WLS_FAIL;
	}

	return CPS_WLS_SUCCESS;
}

static int cps_wls_l_write_reg(int reg, int value)
{
	int ret = 0;
	int reg_data = 0;

	mutex_lock(&g_chip->i2c_lock);
	ret = regmap_write(g_chip->regmap, reg, value);
	mutex_unlock(&g_chip->i2c_lock);
	if (ret < 0) {
		reg_data = cps_wls_l_read_reg(reg);
		cps_wls_log(CPS_LOG_ERR, "[%s] i2c write reg error! addr:0x%x, data:0x%x\n",
			__func__, reg, reg_data);
		return CPS_WLS_FAIL;
	}

	return CPS_WLS_SUCCESS;
}

static int cps_wls_write_reg(int reg, int value, int byte_len)
{
	int i = 0;
	int tmp = 0;

	for (i = 0; i < byte_len; i++) {
		tmp = (value >> (i * 8)) & 0xff;
		if (cps_wls_l_write_reg((reg & 0xffff) + i, tmp) == CPS_WLS_FAIL)
			return CPS_WLS_FAIL;
	}

	return CPS_WLS_SUCCESS;
}

static int cps_wls_read_reg(int reg, int byte_len)
{
	int i = 0;
	int tmp = 0;
	int read_date = 0;

	for (i = 0; i < byte_len; i++) {
		tmp = cps_wls_l_read_reg((reg & 0xffff) + i);
		if (tmp == CPS_WLS_FAIL)
			return CPS_WLS_FAIL;
		read_date |= (tmp << (8 * i));
	}

	return read_date;
}

static int cps_wls_program_sram(int addr, u8 *date, int len)
{
	int ret = 0;

	/* set wirte mode :byte write mode */
	if (cps_wls_h_write_reg(REG_WRITE_MODE, PROGRAM_WRITE_MODE) == CPS_WLS_FAIL) {
		cps_wls_log(CPS_LOG_ERR, "[%s] i2c write error, set write mode error!\n",
			__func__);
		return CPS_WLS_FAIL;
	}

	/* set high 16 bit addr */
	if (cps_wls_h_write_reg(REG_HIGH_ADDR, (addr >> 16) & 0xffff) == CPS_WLS_FAIL) {
		cps_wls_log(CPS_LOG_ERR, "[%s] i2c write error,set addr error!\n", __func__);
		return CPS_WLS_FAIL;
	}

	mutex_lock(&g_chip->i2c_lock);
	ret = regmap_raw_write(g_chip->regmap, addr & 0xffff, date, len);
	mutex_unlock(&g_chip->i2c_lock);
	if (ret < 0) {
		cps_wls_log(CPS_LOG_ERR, "[%s] i2c write error!\n", __func__);
		return CPS_WLS_FAIL;
	}

	return CPS_WLS_SUCCESS;
}

/*
 * If the write register fails, an error message will be printed.
 * If the write succeeds, no attention is needed, \
 * so there is no need to process the return value \
 * Except for some special flow operations
 */
static int cps_wls_write_word(int addr, int value)
{
	int ret = 0;
	u8 write_date[4] = {0};

	write_date[0] = value & 0xff;
	write_date[1] = (value >> 8) & 0xff;
	write_date[2] = (value >> 16) & 0xff;
	write_date[3] = (value >> 24) & 0xff;
	/* set high 16 bit addr */
	if (cps_wls_h_write_reg(REG_HIGH_ADDR, (addr >> 16) & 0xffff) == CPS_WLS_FAIL) {
		cps_wls_log(CPS_LOG_ERR, "[%s] i2c write error, ste addr error!\n", __func__);
		return CPS_WLS_FAIL;
	}
	mutex_lock(&g_chip->i2c_lock);
	ret = regmap_raw_write(g_chip->regmap, addr & 0xffff, write_date, 4);
	mutex_unlock(&g_chip->i2c_lock);
	if (ret < 0) {
		cps_wls_log(CPS_LOG_ERR, "[%s] i2c write error ret = %d!\n", __func__, ret);
		return CPS_WLS_FAIL;
	}

	return CPS_WLS_SUCCESS;
}

static int cps_wls_read_word(int addr)
{
	int ret = 0;
	u8 read_date[4] = {0};

	mutex_lock(&g_chip->i2c_lock);
	/* set high 16 bit addr */
	if (cps_wls_h_write_reg(REG_HIGH_ADDR, (addr >> 16) & 0xffff) == CPS_WLS_FAIL) {
		cps_wls_log(CPS_LOG_ERR, "[%s] i2c read error, set addr error!\n", __func__);
		return CPS_WLS_FAIL;
	}

	ret = regmap_raw_read(g_chip->regmap, addr & 0xffff, read_date, 4);
	mutex_unlock(&g_chip->i2c_lock);
	if (ret < 0) {
		cps_wls_log(CPS_LOG_ERR, "[%s] i2c read error!\n", __func__);
		return CPS_WLS_FAIL;
	}

	return *(int *)read_date;
}

/*
 * If the write register fails, an error message will be printed.
 * If the write succeeds, no attention is needed, \
 * so there is no need to process the return value \
 * Except for some special flow operations
 */
static int cps_wls_program_cmd_send(int cmd)
{
	return cps_wls_write_word(ADDR_CMD, cmd);
}

static int cps_wls_program_wait_cmd_done(void)
{
	int ret = 0;
	/* Timer step ms, timeout period */
	int wait_time_out = 50;

	while(1) {
		ret = cps_wls_read_word(ADDR_FLAG);
		if (ret == CPS_WLS_FAIL)
			return CPS_WLS_FAIL;
		wait_time_out--;
		msleep(1);
		if ((ret & 0xff) == PASS)
			break;
		if (wait_time_out < 0)
			return CPS_WLS_FAIL;
	}

	return CPS_WLS_SUCCESS;
}

static int cps_wls_fp_size(struct file *f)
{
	int error = -EBADF;
	struct kstat stat;

	error = vfs_getattr(&f->f_path, &stat,STATX_INO, AT_STATX_SYNC_AS_STAT);
	if (error == 0) {
		return stat.size;
	} else{
		pr_err("get file file stat error\n");
		return error;
	}
}

static int cps_wls_cps_file_read(char *filename, char **buf)
{
	struct file *fp = NULL;
	mm_segment_t fs;
	int size = 0;
	loff_t pos = 0;

	fp = filp_open(filename, O_RDONLY, 0);
	if (IS_ERR(fp)) {
		pr_err("open %s file error\n", filename);
		goto end;
	}

	fs = get_fs();
	set_fs(KERNEL_DS);
	size = cps_wls_fp_size(fp);
	if (size <= 0) {
		pr_err("load file:%s error\n", filename);
		goto error;
	}

	*buf = kzalloc(size + 1, GFP_KERNEL);
	if (vfs_read(fp, *buf, size, &pos) == -EFAULT)
		cps_wls_log(CPS_LOG_ERR, "[%s] vfs read fail!\n", __func__);

error:
	filp_close(fp, NULL);
	set_fs(fs);
end:

	return size;
}

static unsigned char cps_wls_chartoBcd(char iChar)
{
	unsigned char mBCD = 0;

	if (iChar >= '0' && iChar <= '9')
		mBCD = iChar - '0';
	else if (iChar >= 'A' && iChar <= 'F')
		mBCD = iChar - 'A' + 0x0a;
	else if (iChar >= 'a' && iChar <= 'f')
		mBCD = iChar - 'a' + 0x0a;

	return mBCD;
}

static unsigned char* cps_wls_file_parse(char *buf, int size, unsigned char *file,
	int *file_length)
{
	int i = 0;
	int j = 0;
	int file_index = 0;
	char temp = 0;

	if (!buf || !file)
		return NULL;
	for (i = 0; i < size; i++) {
		if (buf[i] == '\n' || buf[i] == ' ' || buf[i] == '\r') {
		  file_index = 0;
		  continue;
		} else {
			if (file_index == 1) {
				file_index++;
				file[j++] = (unsigned char)((cps_wls_chartoBcd(temp) << 4)
					+ cps_wls_chartoBcd(buf[i]));
			} else if (file_index == 0) {
				file_index++;
				temp = buf[i];
			}
		}

	}
	*file_length = j;

	return file;
}

static int cps_wls_bootloader_load(unsigned char *bootloader, int *bootloader_length)
{
	char *buf = NULL;
	int size = 0;

	size = cps_wls_cps_file_read(BOOTLOADER_FILE_NAME, &buf);
	if (size > 0) {
		if (bootloader == NULL) {
			kfree(buf);
			pr_err("file alloc error.\n");
			return -EINVAL;
		}

		if (cps_wls_file_parse(buf, size, bootloader, bootloader_length) == NULL) {
			kfree(buf);
			pr_err("file parse error\n");
			return -EINVAL;
		}
		kfree(buf);
	}

	return 0;
}

static int cps_wls_firmware_load(unsigned char *firmeware, int *firmeware_length)
{
	char *buf = NULL;
	int size = 0;

	size = cps_wls_cps_file_read(FIRMWARE_FILE_NAME, &buf);
	if (size > 0) {
		if (firmeware == NULL) {
			kfree(buf);
			pr_err("file alloc error.\n");
			return -EINVAL;
		}
		if (cps_wls_file_parse(buf, size, firmeware, firmeware_length) == NULL) {
			kfree(buf);
			pr_err("file parse error\n");
			return -EINVAL;
		}
		kfree(buf);
	}

	return 0;
}

static int cps_wls_update_firmware_first_part(unsigned char *firmware_buf, int firmware_length,
	unsigned char *bootloader_buf, int bootloader_length)
{
	int ret = 0;
	int result = -1;

	/* load bootloader */
	ret = cps_wls_bootloader_load(bootloader_buf, &bootloader_length);
	if (ret != 0) {
		cps_wls_log(CPS_LOG_ERR, "[%s] bootloader get error %d\n", __func__, ret);
		goto update_fail;
	}
	/* set password */
	if (cps_wls_h_write_reg(REG_PASSWORD, PASSWORD) == CPS_WLS_FAIL)
		goto update_fail;
	/* reset mcu */
	if (cps_wls_h_write_reg(REG_RESET_MCU, CPS_4035_RESET) == CPS_WLS_FAIL)
		goto update_fail;
	/* enable MTP write */
	if (cps_wls_write_word(0x40012334, 0x00) == CPS_WLS_FAIL)
		goto update_fail;
	/* program sram */
	if (cps_wls_program_sram(0x20000000, bootloader_buf, bootloader_length) == CPS_WLS_FAIL)
		goto update_fail;
	/* remap enable */
	if (cps_wls_write_word(0x400400A0, 0x000000ff) == CPS_WLS_FAIL)
		goto update_fail;
	/* trim disable */
	if (cps_wls_write_word(0x40040010, 0x00008000) == CPS_WLS_FAIL)
		goto update_fail;
	/* sys-restart */
	if (cps_wls_write_word(0x40040070, 0x61A00000) == CPS_WLS_FAIL)
		cps_wls_log(CPS_LOG_ERR, "[%s] system restart\n", __func__);

	msleep(100);
	/* set password */
	if (cps_wls_h_write_reg(REG_PASSWORD, PASSWORD) == CPS_WLS_FAIL)
		goto update_fail;
	if (cps_wls_h_write_reg(REG_WRITE_MODE, PROGRAM_WRITE_MODE) == CPS_WLS_FAIL)
		goto update_fail;

	/* cali bootloader code */
	if (cps_wls_program_cmd_send(CACL_CRC_TEST) == CPS_WLS_FAIL) {
		cps_wls_log(CPS_LOG_ERR, "[%s] cali bootloader code cmd send fail\n",
			__func__);
		goto update_fail;
	}
	result = cps_wls_program_wait_cmd_done();
	if (result != CPS_WLS_SUCCESS) {
		cps_wls_log(CPS_LOG_ERR, "[%s] bootloader crc fail\n", __func__);
		goto update_fail;
	}
	cps_wls_log(CPS_LOG_ERR, "[%s] load bootloader successful\n", __func__);

	/* LOAD firmware to MTP */
	if (memset_s(firmware_buf, 0x4800, 0, 0x4800) != EOK) {
		cps_wls_log(CPS_LOG_ERR, "[%s] LOAD firmware to MTP fail\n", __func__);
	}
	/* load bootloader */
	ret = cps_wls_firmware_load(firmware_buf, &firmware_length);
	if (ret != 0) {
		cps_wls_log(CPS_LOG_ERR, "[%s] firmware get error %d\n", __func__, ret);
		goto update_fail;
	}
	/* set start addr */
	if (cps_wls_write_word(ADDR_BUFFER0, 0x0000) == CPS_WLS_FAIL)
		goto update_fail;
	if (cps_wls_program_cmd_send(PGM_ADDR_SET) == CPS_WLS_FAIL) {
		cps_wls_log(CPS_LOG_ERR, "[%s] cmd send fail\n", __func__);
		goto update_fail;
	}
	result = cps_wls_program_wait_cmd_done();
	if (result != CPS_WLS_SUCCESS) {
		cps_wls_log(CPS_LOG_ERR, "[%s] cps wls program wait cmd done run fail\n", __func__);
		goto update_fail;
	}
	return 0;

update_fail:
	return 1;
}

static void cps_wls_update_firmware_write_hex(unsigned char *p)
{
	int k = 0;
	int buff0_flag = 0;
	int buff1_flag = 0;
	/* 18K data, byte write */
	int size_temp = (18 * 1024 / 4);

	/* goto program mode */
	if (cps_wls_write_word(0x40012120, 0x1250) == CPS_WLS_FAIL) {
		cps_wls_log(CPS_LOG_ERR, "[%s] goto program mode fail!\n",
			__func__);
		return;
	}
	if (cps_wls_write_word(0x40012ee8, 0xD148) == CPS_WLS_FAIL) {
		cps_wls_log(CPS_LOG_ERR, "[%s] goto program mode fail!\n",
			__func__);
		return;
	}
	for (k = 0; k < size_temp / CPS_PROGRAM_BUFFER_SIZE; k++) {
		if (buff0_flag == 0) {
			/* write buf0 */
			if (cps_wls_program_sram(ADDR_BUFFER0, p, CPS_PROGRAM_BUFFER_SIZE * 4)
				== CPS_WLS_FAIL) {
				cps_wls_log(CPS_LOG_ERR, "[%s] write buff0 fail\n",
					__func__);
				return;
			}
			p = p + CPS_PROGRAM_BUFFER_SIZE * 4;
			if (buff1_flag == 1) {
				/* wait finish */
				if (cps_wls_program_wait_cmd_done() != CPS_WLS_SUCCESS)
					cps_wls_log(CPS_LOG_ERR, "[%s] wait finish write buff0 fail\n",
						__func__);
				buff1_flag = 0;
			}
			/* write buff 0 CMD */
			if (cps_wls_program_cmd_send(PGM_BUFFER0) == CPS_WLS_FAIL) {
				cps_wls_log(CPS_LOG_ERR, "[%s] write buff 0 CMD fail\n",
					__func__);
			}
			buff0_flag = 1;
			continue;
		}
		if (buff1_flag == 0) {
			/* write buf1 */
			if (cps_wls_program_sram(ADDR_BUFFER1, p, CPS_PROGRAM_BUFFER_SIZE * 4)
				== CPS_WLS_FAIL) {
				cps_wls_log(CPS_LOG_ERR, "[%s] write buff1 fail\n",
					__func__);
				return;
			}
			p = p + CPS_PROGRAM_BUFFER_SIZE * 4;
			if (buff0_flag == 1) {
				/* wait finish */
				if (cps_wls_program_wait_cmd_done() != CPS_WLS_SUCCESS)
					cps_wls_log(CPS_LOG_ERR, "[%s] wait finish write buff1 fail\n",
						__func__);
				buff0_flag = 0;
			}

			/* write buff 0 CMD */
			if (cps_wls_program_cmd_send(PGM_BUFFER1) == CPS_WLS_FAIL) {
				cps_wls_log(CPS_LOG_ERR, "[%s] write buff 1 CMD fail\n",
					__func__);
			}
			buff1_flag = 1;
			continue;
		}
	}
	if (buff0_flag == 1) {
		/* wait finish */
		if (cps_wls_program_wait_cmd_done() != CPS_WLS_SUCCESS)
			cps_wls_log(CPS_LOG_ERR, "[%s] wait finish write buff0 fail\n", __func__);
		buff0_flag = 0;
	}
	if (buff1_flag == 1) {
		/* wait finish */
		if (cps_wls_program_wait_cmd_done() != CPS_WLS_SUCCESS)
			cps_wls_log(CPS_LOG_ERR, "[%s] wait finish write buff1 fail\n", __func__);
		buff1_flag = 0;
	}
}

static int cps_wls_update_firmware_crc_check(void)
{
	int result = -1;

	/* exit program mode */
	if (cps_wls_write_word(0x40012120, 0x0000) == CPS_WLS_FAIL) {
		cps_wls_log(CPS_LOG_ERR, "[%s] exit program mode fail\n", __func__);
		return result;
	}
	if (cps_wls_write_word(0x40012ee8, 0x0000) == CPS_WLS_FAIL) {
		cps_wls_log(CPS_LOG_ERR, "[%s] exit program mode fail\n", __func__);
		return result;
	}
	/* cali app crc */
	if (cps_wls_program_cmd_send(CACL_CRC_APP) == CPS_WLS_FAIL) {
		cps_wls_log(CPS_LOG_ERR, "[%s] cali app crc fail\n", __func__);
		return result;
	}
	result = cps_wls_program_wait_cmd_done();

	return result;
}

static void cps_wls_update_firmware_exit_program_mode(void)
{
	/* write mcu&trim flag, goto program mode */
	if (cps_wls_write_word(0x40012120, 0x1250) == CPS_WLS_FAIL)
		cps_wls_log(CPS_LOG_ERR, "[%s] write mcu&trim flag fail\n", __func__);
	if (cps_wls_write_word(0x40012ee8, 0xD148) == CPS_WLS_FAIL)
		cps_wls_log(CPS_LOG_ERR, "[%s] goto program mode fail\n", __func__);
	if (cps_wls_program_cmd_send(PGM_WR_FLAG) == CPS_WLS_FAIL) {
		cps_wls_log(CPS_LOG_ERR, "[%s] program_cmd_send fail\n", __func__);
	}
	if (cps_wls_program_wait_cmd_done() != CPS_WLS_SUCCESS)
		cps_wls_log(CPS_LOG_ERR, "[%s] program wait cmd done fail\n", __func__);
	/* exit program mode */
	if (cps_wls_write_word(0x40012120, 0x0000) == CPS_WLS_FAIL)
		cps_wls_log(CPS_LOG_ERR, "[%s] exit program mode fail\n", __func__);
	if (cps_wls_write_word(0x40012ee8, 0x0000) == CPS_WLS_FAIL)
		cps_wls_log(CPS_LOG_ERR, "[%s] exit program mode fail\n", __func__);
}

static int cps_wls_update_firmware(void)
{
	int ret = 0;
	int write_count = 0;
	int bootloader_length = 0;
	int firmware_length = 0;
	unsigned char *bootloader_buf = NULL;
	unsigned char *firmware_buf = NULL;
	unsigned char *p = NULL;
	int result = -1;

	/* 2K buffer */
	bootloader_buf = kzalloc(0x800, GFP_KERNEL);
	/* 18K buffer */
	firmware_buf = kzalloc(0x4800, GFP_KERNEL);
	ret = cps_wls_update_firmware_first_part(firmware_buf, firmware_length,
		bootloader_buf, bootloader_length);
	if(ret == 1)
		goto update_fail;
	/* set write buffer size defalt 64 word */
	if (cps_wls_write_word(ADDR_BUF_SIZE, CPS_PROGRAM_BUFFER_SIZE) == CPS_WLS_FAIL)
		goto update_fail;
	write_count = 0;

start_write_app_code:
	p = firmware_buf;
	write_count++;
	cps_wls_update_firmware_write_hex(p);

	result = cps_wls_update_firmware_crc_check();
	if(result != CPS_WLS_SUCCESS) {
		cps_wls_log(CPS_LOG_ERR, "[%s] TEST APP CRC fail", __func__);
		if(write_count < 3)
			goto start_write_app_code;
		else
			goto update_fail;
	}
	cps_wls_update_firmware_exit_program_mode();
	cps_wls_log(CPS_LOG_ERR, "[%s] Program successful\n", __func__);
	return CPS_WLS_SUCCESS;

update_fail:
	cps_wls_log(CPS_LOG_ERR, "[%s] update fail\n", __func__);

	return CPS_WLS_FAIL;
}

/*
 * CPS4035 system interface
 */
static int cps_wls_get_int_flag(void)
{
	cps_reg_s *cps_reg = NULL;

	cps_reg = (cps_reg_s*)(&cps_comm_reg[CPS_COMM_REG_INT_FLAG]);
	return cps_wls_read_reg((int)cps_reg->reg_addr, (int)cps_reg->reg_bytes_len);
}

static int cps_wls_set_int_clr(int value)
{
	cps_reg_s *cps_reg = NULL;

	cps_reg = (cps_reg_s*)(&cps_comm_reg[CPS_COMM_REG_INT_CLR]);
	return cps_wls_write_reg(cps_reg->reg_addr, value, (int)cps_reg->reg_bytes_len);
}

static int cps_wls_get_chip_id(void)
{
	cps_reg_s *cps_reg = NULL;

	cps_reg = (cps_reg_s*)(&cps_comm_reg[CPS_COMM_REG_CHIP_ID]);
	return cps_wls_read_reg(cps_reg->reg_addr, (int)cps_reg->reg_bytes_len);
}

static int cps_wls_get_sys_mode(void)
{
	cps_reg_s *cps_reg = NULL;

	cps_reg = (cps_reg_s*)(&cps_comm_reg[CPS_COMM_REG_SYS_MODE]);
	return cps_wls_read_reg((int)cps_reg->reg_addr, (int)cps_reg->reg_bytes_len);
}

/*
 * CPS4035 TX interface
 */
static int cps_wls_get_tx_i_in(void)
{
	cps_reg_s *cps_reg = NULL;

	cps_reg = (cps_reg_s*)(&cps_tx_reg[CPS_TX_REG_ADC_I_IN]);
	return cps_wls_read_reg((int)cps_reg->reg_addr, (int)cps_reg->reg_bytes_len);
}

static int cps_wls_get_tx_vin(void)
{
	cps_reg_s *cps_reg = NULL;

	cps_reg = (cps_reg_s*)(&cps_tx_reg[CPS_TX_REG_ADC_VIN]);
	return cps_wls_read_reg((int)cps_reg->reg_addr, (int)cps_reg->reg_bytes_len);
}

static int cps_wls_get_tx_vrect(void)
{
	cps_reg_s *cps_reg = NULL;

	cps_reg = (cps_reg_s*)(&cps_tx_reg[CPS_TX_REG_ADC_VRECT]);
	return cps_wls_read_reg((int)cps_reg->reg_addr, (int)cps_reg->reg_bytes_len);
}

static int cps_wls_set_int_enable(void)
{
	uint16_t int_en = 0;
	cps_reg_s *cps_reg = NULL;

	int_en = 0xFFFF;
	cps_reg = (cps_reg_s*)(&cps_comm_reg[CPS_COMM_REG_INT_EN]);

	if (cps_wls_write_reg((int)cps_reg->reg_addr, int_en,
		(int)cps_reg->reg_bytes_len) == CPS_WLS_FAIL)
		goto set_int_fail;
	return CPS_WLS_SUCCESS;

set_int_fail:
	return CPS_WLS_FAIL;
}

/*
 * Reserve interrupt logic processing
 */
static int cps_wls_tx_irq_handler(int int_flag)
{
	int rc = 0;
	return rc;
}

static irqreturn_t cps_wls_irq_handler(int irq, void *dev_id)
{
	int int_flag = 0;
	int int_clr = 0;

	cps_wls_log(CPS_LOG_DEBG, "[%s] IRQ triggered\n", __func__);
	mutex_lock(&g_chip->irq_lock);
	if (cps_wls_get_chip_id() != CPS_4035_CHIP_ID) {
		/* unlock i2c */
		if (cps_wls_h_write_reg(REG_PASSWORD, PASSWORD) == CPS_WLS_FAIL)
			cps_wls_log(CPS_LOG_DEBG, "[%s] CPS_I2C_UNLOCK ERROR!\n", __func__);
		if (cps_wls_h_write_reg(REG_HIGH_ADDR, HIGH_ADDR) == CPS_WLS_FAIL)
			cps_wls_log(CPS_LOG_DEBG, "[%s] REG HIGH ADDR ERROR!\n", __func__);
		if (cps_wls_h_write_reg(REG_WRITE_MODE, WRITE_MODE) == CPS_WLS_FAIL)
			cps_wls_log(CPS_LOG_DEBG, "[%s] REG WRITE MODE ERROR!\n", __func__);
		if (cps_wls_set_int_enable() == CPS_WLS_FAIL)
			cps_wls_log(CPS_LOG_DEBG, "[%s] cps wls set int enable error!\n", __func__);
		cps_wls_log(CPS_LOG_DEBG, "[%s] CPS_I2C_UNLOCK\n", __func__);
	}

	int_flag = cps_wls_get_int_flag();
	cps_wls_log(CPS_LOG_DEBG, ">>>>>int_flag = %x\n", int_flag);
	if (int_flag == CPS_WLS_FAIL) {
		cps_wls_log(CPS_LOG_ERR, "[%s] read wls irq reg failed\n", __func__);
		mutex_unlock(&g_chip->irq_lock);
		return IRQ_HANDLED;
	}

	int_clr = int_flag;
	int_flag = cps_wls_set_int_clr(int_flag);
	cps_wls_log(CPS_LOG_DEBG, ">>>>>int_flag = %x\n", int_flag);
	mutex_unlock(&g_chip->irq_lock);
	if (cps_wls_get_sys_mode() == SYS_MODE_TX) {
		cps_wls_tx_irq_handler(int_flag);
	}

	return IRQ_HANDLED;
}

static enum power_supply_property cps_wls_chrg_props[] = {
	POWER_SUPPLY_PROP_CURRENT_MAX,
	POWER_SUPPLY_PROP_CHARGING_ENABLED,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_ONLINE,
};

static int cps_wls_chrg_property_is_writeable(struct power_supply *psy,
	enum power_supply_property psp)
{
	switch (psp) {
		case POWER_SUPPLY_PROP_CURRENT_MAX:
		case POWER_SUPPLY_PROP_CHARGING_ENABLED:
			return 1;
		default:
			break;
	}

	return 0;
}

static int cps_wls_chrg_get_property(struct power_supply *psy, enum power_supply_property psp,
	union power_supply_propval *val)
{
	switch (psp) {
		case POWER_SUPPLY_PROP_ONLINE:
			val->intval = 0;
			break;
		default:
			return -EINVAL;
			break;
	}

	return 0;
}

static int cps_wls_chrg_set_property(struct power_supply *psy,
	enum power_supply_property psp, const union power_supply_propval *val)
{
	struct cps_wls_chrg_chip *g_chip = power_supply_get_drvdata(psy);
	cps_wls_log(CPS_LOG_DEBG, "[%s] psp = %d.\n", __func__, psp);
	g_chip->state = 1;
	return 0;
}

/*
 * Reserved cutover power interface
 */
static void cps_wls_charger_external_power_changed(struct power_supply *psy)
{
}

static ssize_t show_wls_switch(struct device *dev, struct device_attribute *attr, char *buf)
{
	buf = &g_cps_printf_buf[0];
	return sprintf_s(buf, sizeof(g_cps_printf_buf), "cps wls switch status:0x%x\n", *buf);
}

static ssize_t store_wls_switch(struct device *dev, struct device_attribute *attr,
	const char *buf, size_t count)
{
	int tmp = 0;

	tmp = simple_strtoul(buf, NULL, 0);
	if (tmp != 0) {
		gpio_set_value(g_chip->pen_en, 0);
		gpio_set_value(g_chip->pen_col_en, 1);
		gpio_set_value(g_chip->vbst_en, 1);
	} else {
		gpio_set_value(g_chip->pen_en, 0);
		gpio_set_value(g_chip->pen_col_en, 0);
		gpio_set_value(g_chip->vbst_en, 0);
	}
	cps_wls_log(CPS_LOG_DEBG, "[%s] p_en = %d, p_col_en = %d, vbst_en = %d, tmp = %d.\n",
		__func__, g_chip->pen_en, g_chip->pen_col_en, g_chip->vbst_en, tmp);

	return count;
}

static DEVICE_ATTR(wls_switch, 0664, show_wls_switch, store_wls_switch);

static ssize_t show_reg_addr(struct device *dev, struct device_attribute *attr, char *buf)
{
	buf = &g_cps_printf_buf[0];
	return sprintf_s(buf, sizeof(g_cps_printf_buf), "reg addr 0x%08x\n", g_chip->reg_addr);
}

static ssize_t store_reg_addr(struct device *dev, struct device_attribute *attr,
	const char *buf, size_t count)
{
	int tmp = 0;

	tmp = simple_strtoul(buf, NULL, 0);
	g_chip->reg_addr = tmp;

	return count;
}

static DEVICE_ATTR(reg_addr, 0664, show_reg_addr, store_reg_addr);

static ssize_t show_reg_data(struct device *dev, struct device_attribute *attr, char *buf)
{
	g_chip->reg_data = cps_wls_read_reg(g_chip->reg_addr, 4);
	buf = &g_cps_printf_buf[0];
	return sprintf_s(buf, sizeof(g_cps_printf_buf), "reg addr 0x%08x -> 0x%08x\n",
		g_chip->reg_addr, g_chip->reg_data);
}

static ssize_t store_reg_data(struct device *dev, struct device_attribute *attr,
	const char *buf, size_t count)
{
	int tmp = 0;

	tmp = simple_strtoul(buf, NULL, 0);
	g_chip->reg_data = tmp;
	if (cps_wls_write_reg(g_chip->reg_addr, g_chip->reg_data, 4) == CPS_WLS_FAIL)
		cps_wls_log(CPS_LOG_ERR, "[%s]stor run fail!\n", __func__);
	return count;
}

static DEVICE_ATTR(reg_data, 0664, show_reg_data, store_reg_data);

static ssize_t store_update_fw(struct device *dev, struct device_attribute *attr,
	const char *buf, size_t count)
{
	int tmp = 0;

	tmp = simple_strtoul(buf, NULL, 0);
	if (tmp != 0) {
		cps_wls_log(CPS_LOG_ERR, "[%s] start update fw\n", __func__);
		if (cps_wls_update_firmware() == CPS_WLS_FAIL)
			cps_wls_log(CPS_LOG_ERR, "[%s] cps wls update firmware fail\n", __func__);
	}

	return count;
}

static DEVICE_ATTR(update_fw, 0664, NULL, store_update_fw);

static ssize_t store_write_password(struct device *dev, struct device_attribute *attr,
	const char *buf, size_t count)
{
	int tmp = 0;

	tmp = simple_strtoul(buf, NULL, 0);
	if (tmp != 0) {
		cps_wls_log(CPS_LOG_DEBG, "[%s] write password\n", __func__);
		if (cps_wls_h_write_reg(REG_PASSWORD, PASSWORD) == CPS_WLS_FAIL)
			cps_wls_log(CPS_LOG_DEBG, "[%s] write reg password error!\n", __func__);
		if (cps_wls_h_write_reg(REG_HIGH_ADDR, HIGH_ADDR) == CPS_WLS_FAIL)
			cps_wls_log(CPS_LOG_DEBG, "[%s] write reg addr error!\n", __func__);
		if (cps_wls_h_write_reg(REG_WRITE_MODE, WRITE_MODE) == CPS_WLS_FAIL)
			cps_wls_log(CPS_LOG_DEBG, "[%s] write mode error!\n", __func__);
	}

	return count;
}

static DEVICE_ATTR(write_password, 0664, NULL, store_write_password);

static ssize_t show_tx_vin(struct device *dev, struct device_attribute *attr, char *buf)
{
	buf = &g_cps_printf_buf[0];
	return sprintf_s(buf, sizeof(g_cps_printf_buf), "tx vin : %d\n", cps_wls_get_tx_vin());
}

static DEVICE_ATTR(get_tx_vin, 0444, show_tx_vin, NULL);

static ssize_t show_tx_iin(struct device *dev, struct device_attribute *attr, char *buf)
{
	buf = &g_cps_printf_buf[0];
	return sprintf_s(buf, sizeof(g_cps_printf_buf), "tx iin : %d\n", cps_wls_get_tx_i_in());
}

static DEVICE_ATTR(get_tx_iin, 0444, show_tx_iin, NULL);

static ssize_t show_tx_vrect(struct device *dev, struct device_attribute *attr, char *buf)
{
	buf = &g_cps_printf_buf[0];
	return sprintf_s(buf, sizeof(g_cps_printf_buf), "tx vrect : %d\n", cps_wls_get_tx_vrect());
}

static DEVICE_ATTR(get_tx_vrect, 0444, show_tx_vrect, NULL);

static void cps_wls_create_device_node(struct device *dev)
{
	device_create_file(dev, &dev_attr_reg_addr);
	device_create_file(dev, &dev_attr_reg_data);
	/* program */
	device_create_file(dev, &dev_attr_update_fw);
	/* write password */
	device_create_file(dev, &dev_attr_write_password);
	/* TX */
	device_create_file(dev, &dev_attr_get_tx_vin);
	device_create_file(dev, &dev_attr_get_tx_iin);
	device_create_file(dev, &dev_attr_get_tx_vrect);
	device_create_file(dev, &dev_attr_wls_switch);
}

static int cps_wls_parse_dt(struct cps_wls_chrg_chip *g_chip)
{
	int ret = 0;

	struct device_node *node = g_chip->dev->of_node;
	if (!node) {
		cps_wls_log(CPS_LOG_ERR, "devices tree node missing \n");
		return -EINVAL;
	}

	g_chip->pen_int = of_get_named_gpio(node, "gpio_int", 0);
	g_chip->pen_en = of_get_named_gpio(node, "gpio_en", 0);
	g_chip->pen_col_en = of_get_named_gpio(node, "gpio_coil_en", 0);
	g_chip->vbst_en = of_get_named_gpio(node, "gpio_pwr_good", 0);

	cps_wls_log(CPS_LOG_DEBG, "[%s] p_int = %d, p_en = %d, p_col_en = %d, vbst_en = %d.\n",
		__func__, g_chip->pen_int, g_chip->pen_en, g_chip->pen_col_en, g_chip->vbst_en);

	if (g_chip->pen_int) {
		if (gpio_is_valid(g_chip->pen_int)) {
			ret = devm_gpio_request(g_chip->dev, g_chip->pen_int, "p_int");
			if (ret) {
				dev_err(g_chip->dev,
					"Failed GPIO request for p_int: %d err %d\n", g_chip->pen_int, ret);
				return ret;
			}
		} else {
			dev_err(g_chip->dev, "When DC is wired, DOK should be wired as well.\n");
			return -EINVAL;
		}
		 gpio_direction_input(g_chip->pen_int);
	}

	if (gpio_is_valid(g_chip->pen_en)) {
		ret = devm_gpio_request(g_chip->dev, g_chip->pen_en, "p_en");
		if (ret) {
			dev_err(g_chip->dev,
				"Failed GPIO request for p_en: %d err %d\n", g_chip->pen_en, ret);
			return ret;
		}

		gpio_direction_output(g_chip->pen_en, 0);
	}

	if (gpio_is_valid(g_chip->pen_col_en)) {
		ret = devm_gpio_request(g_chip->dev, g_chip->pen_col_en, "p_col_en");
		if (ret) {
			dev_err(g_chip->dev,
				"Failed GPIO request for p_col_en: %d err %d\n", g_chip->pen_col_en, ret);
			return ret;
		}
		 gpio_direction_output(g_chip->pen_col_en, 0);
	}

	if (gpio_is_valid(g_chip->vbst_en)) {
		ret = devm_gpio_request(g_chip->dev, g_chip->vbst_en, "vbst_en");
		if (ret) {
			dev_err(g_chip->dev,
				"Failed GPIO request for vbst_en: %d err %d\n", g_chip->vbst_en, ret);
			return ret;
		}
		 gpio_direction_output(g_chip->vbst_en, 0);
	}
	cps_wls_log(CPS_LOG_DEBG, "[%s] p_en = %d, p_col_en = %d, vbst_en = %d\n", __func__,
		g_chip->pen_en, g_chip->pen_col_en, g_chip->vbst_en);

	return ret;
}

static void cps_wls_lock_work_init(struct cps_wls_chrg_chip *g_chip)
{
	mutex_init(&g_chip->irq_lock);
	mutex_init(&g_chip->i2c_lock);
}

static void cps_wls_lock_destroy(struct cps_wls_chrg_chip *g_chip)
{
	mutex_destroy(&g_chip->irq_lock);
	mutex_destroy(&g_chip->i2c_lock);
}

static void cps_wls_free_gpio(struct cps_wls_chrg_chip *g_chip)
{
	if (gpio_is_valid(g_chip->pen_en))
		gpio_free(g_chip->pen_en);
	if (gpio_is_valid(g_chip->pen_int))
		gpio_free(g_chip->pen_int);
	if (gpio_is_valid(g_chip->pen_col_en))
		gpio_free(g_chip->pen_col_en);
	if (gpio_is_valid(g_chip->vbst_en))
		gpio_free(g_chip->vbst_en);
}

static int cps_wls_register_psy(struct cps_wls_chrg_chip *g_chip)
{
	struct power_supply_config cps_wls_psy_cfg = {};

	g_chip->wl_psd.name = CPS_WLS_CHRG_PSY_NAME;
	g_chip->wl_psd.type = POWER_SUPPLY_TYPE_UNKNOWN;
	g_chip->wl_psd.properties = cps_wls_chrg_props;
	g_chip->wl_psd.num_properties = 4;
	g_chip->wl_psd.get_property = cps_wls_chrg_get_property;
	g_chip->wl_psd.set_property = cps_wls_chrg_set_property;
	g_chip->wl_psd.property_is_writeable= cps_wls_chrg_property_is_writeable;
	g_chip->wl_psd.external_power_changed = cps_wls_charger_external_power_changed;

	cps_wls_psy_cfg.drv_data = g_chip;
	cps_wls_psy_cfg.of_node = g_chip->dev->of_node;
	g_chip->wl_psy = power_supply_register(g_chip->dev, &g_chip->wl_psd,
		&cps_wls_psy_cfg);
	if (IS_ERR(g_chip->wl_psy)) {
		return PTR_ERR(g_chip->wl_psy);
	}

	return CPS_WLS_SUCCESS;
}

static int cps_wls_chrg_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	int ret = 0;
	int chip_id = 0;

	cps_wls_log(CPS_LOG_ERR, "[%s] ---->start\n", __func__);
	g_chip = devm_kzalloc(&client->dev, sizeof(*g_chip), GFP_KERNEL);
	if (!g_chip) {
		cps_wls_log(CPS_LOG_ERR,"[%s] cps_debug: Unable to allocate memory\n", __func__);
		return -ENOMEM;
	}
	g_chip->client = client;
	g_chip->dev = &client->dev;
	g_chip->name = "cps_wls";
	g_chip->regmap = devm_regmap_init_i2c(client, &cps4035L_regmap_config);
	if (IS_ERR(g_chip->regmap)) {
		cps_wls_log(CPS_LOG_ERR, "[%s] Failed to allocate regmap!\n", __func__);
		devm_kfree(&client->dev, g_chip);
		return PTR_ERR(g_chip->regmap);
	}
	i2c_set_clientdata(client, g_chip);
	dev_set_drvdata(&(client->dev), g_chip);

	ret = cps_wls_parse_dt(g_chip);
	if (ret < 0) {
		cps_wls_log(CPS_LOG_ERR, "[%s] Couldn't parse DT nodes ret = %d\n", __func__, ret);
		goto free_source;
	}
	cps_wls_lock_work_init(g_chip);
	cps_wls_create_device_node(&(client->dev));
	cps_wls_log(CPS_LOG_DEBG, "[%s] wireless charger addr low probe successful!\n", __func__);
	return ret;

free_source:
	cps_wls_free_gpio(g_chip);
	cps_wls_lock_destroy(g_chip);
	cps_wls_log(CPS_LOG_ERR, "[%s] error: free resource.\n", __func__);

	return ret;
}

static void cps_wls_h_lock_work_init(struct cps_wls_chrg_chip *g_chip)
{
	mutex_init(&g_chip->i2c_lock);
}

static void cps_wls_h_lock_destroy(struct cps_wls_chrg_chip *g_chip)
{
	mutex_destroy(&g_chip->i2c_lock);
}

static int cps_wls_h_chrg_probe(struct i2c_client *client,
	const struct i2c_device_id *id)
{
	int ret = 0;

	cps_wls_log(CPS_LOG_ERR, "[%s] ---->start\n", __func__);
	g_chip_h = devm_kzalloc(&client->dev, sizeof(*g_chip_h), GFP_KERNEL);
	if (!g_chip_h) {
		cps_wls_log(CPS_LOG_ERR,"[%s] cps_debug: Unable to allocate memory\n", __func__);
		return -ENOMEM;
	}
	g_chip_h->client = client;
	g_chip_h->dev = &client->dev;
	g_chip_h->name = "cps_wls_h";
	g_chip_h->regmap = devm_regmap_init_i2c(client, &cps4035H_regmap_config);
	if (IS_ERR(g_chip_h->regmap)) {
		cps_wls_log(CPS_LOG_ERR, "[%s] Failed to allocate regmap!\n", __func__);
		devm_kfree(&client->dev, g_chip_h);
		return PTR_ERR(g_chip_h->regmap);
	}
	i2c_set_clientdata(client, g_chip_h);
	dev_set_drvdata(&(client->dev), g_chip_h);

	cps_wls_h_lock_work_init(g_chip_h);
	cps_wls_log(CPS_LOG_DEBG, "[%s] wireless charger addr high probe successful!\n", __func__);
	return ret;
}

static int cps_wls_h_chrg_remove(struct i2c_client *client)
{
	cps_wls_h_lock_destroy(g_chip_h);
	kfree(g_chip_h);
	return 0;
}

static int cps_wls_l_chrg_remove(struct i2c_client *client)
{
	kfree(g_chip);
	return 0;
}

static const struct of_device_id cps_wls_chrg_of_tbl_h[] = {
	{ .compatible = "cps, wls-charger-cps4035-H-pen", .data = NULL},
};

static const struct i2c_device_id cps_wls_charger_id_h[] = {
	{ "cps-wls-charger-h-pen", 0},
};

MODULE_DEVICE_TABLE(i2c, cps_wls_charger_id_h);

static struct i2c_driver cps_wls_charger_driver_h = {
	.driver = {
		.name = CPS_WLS_H_CHRG_DRV_NAME,
		.owner = THIS_MODULE,
		.of_match_table = cps_wls_chrg_of_tbl_h,
	},
	.probe = cps_wls_h_chrg_probe,
	.remove = cps_wls_h_chrg_remove,
	.id_table = cps_wls_charger_id_h,
};

static const struct i2c_device_id cps_wls_charger_id_l[] = {
	{ "cps-wls-charger-l-pen", 0},
};

static const struct of_device_id cps_wls_chrg_of_tbl_l[] = {
	{ .compatible = "cps, wls-charger-cps4035-L-pen", .data = NULL},
};

MODULE_DEVICE_TABLE(i2c, cps_wls_charger_id_l);

static struct i2c_driver cps_wls_charger_driver_l = {
	.driver = {
		.name = CPS_WLS_L_CHRG_DRV_NAME,
		.owner = THIS_MODULE,
		.of_match_table = cps_wls_chrg_of_tbl_l,
	},
	.probe = cps_wls_chrg_probe,
	.remove = cps_wls_l_chrg_remove,
	.id_table = cps_wls_charger_id_l,
};

static int __init cps_wls_driver_init(void)
{
	return (i2c_add_driver(&cps_wls_charger_driver_l) &
		i2c_add_driver(&cps_wls_charger_driver_h));
}

late_initcall(cps_wls_driver_init);

static void __exit cps_wls_driver_exit(void)
{
	i2c_del_driver(&cps_wls_charger_driver_l);
	i2c_del_driver(&cps_wls_charger_driver_h);
}

module_exit(cps_wls_driver_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Huawei Technologies Co., Ltd.");
MODULE_DESCRIPTION("cps4035 driver");
MODULE_ALIAS("i2c:cps_wls_charger");
