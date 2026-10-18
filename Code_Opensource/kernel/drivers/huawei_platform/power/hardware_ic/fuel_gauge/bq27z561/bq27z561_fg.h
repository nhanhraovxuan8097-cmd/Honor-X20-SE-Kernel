/*
 * bq27z561_fg.h
 *
 * coul with bq27z561 driver
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

#ifndef _BQ27Z561_FG_H_
#define _BQ27Z561_FG_H_

#define FG_ERR_PARA_NULL                 1
#define FG_ERR_PARA_WRONG                2
#define FG_ERR_MISMATCH                  3
#define FG_ERR_NO_SPACE                  4
#define FG_ERR_I2C_R                     5
#define FG_ERR_I2C_W                     6
#define FG_ERR_I2C_WR                    7
#define INVALID_REG_ADDR                 0xFF
#define FG_ROM_MODE_I2C_ADDR_CFG         0x16
#define FG_ROM_MODE_I2C_ADDR             0x0B
#define FG_FLASH_MODE_I2C_ADDR           0x55
#define FG_FLAGS_FD                      BIT(4)
#define FG_FLAGS_FC                      BIT(5)
#define FG_FLAGS_DSG                     BIT(6)
#define FG_FLAGS_RCA                     BIT(9)
#define FG_IO_CONFIG_VAL                 0x24
#define FG_MAC_WR_MAX                    32
#define FG_MAX_BUFFER_LEN                128
#define FG_MAC_ADDR_LEN                  2
#define FG_MAC_TRAN_BUFF                 ((FG_MAC_WR_MAX) + 8)
#define FG_PARA_INVALID_VER              0xFF
#define FG_FULL_CAPACITY                 100
#define FG_CAPACITY_TH                   7
#define FG_SW_MODE_DELAY_TIME            4000
#define FG_WRITE_MAC_DELAY_TIME          100

#define BYTE_LEN                         1
#define MSG_LEN                          2
#define DWORD_LEN                        4
#define MAC_DATA_LEN                     11
#define I2C_RETRY_CNT                    3
#define ABNORMAL_BATT_TEMPERATURE_LOW    (-400)
#define ABNORMAL_BATT_TEMPERATURE_HIGH   800
#define CMD_MAX_DATA_SIZE                32
#define BQ27Z561_WORD_LEN                2
#define I2C_BLK_SIZE                     32
#define I2C_BLK_SIZE_HALF                16
#define BQ27Z561_DELAY_TIME              5
#define BQ27Z561_DELAY_CALI_TIME         0xE8

#define BQ27Z561_SOC_DELTA_TH_REG        0x6f
#define BQ27Z561_SOC_DELTA_TH_VAL        100
#define BQ27Z561_VOLT_LOW_SET_REG        0x66
#define BQ27Z561_VOLT_LOW_SET_VAL        2600
#define BQ27Z561_VOLT_OVER_VAL           5000
#define BQ27Z561_VOLT_LOW_CLR_REG        0x68
#define BQ27Z561_VOLT_LOW_CLR_OFFSET     100
#define BQ27Z561_VOLT_LOW_CLR_VAL        ((BQ27Z561_VOLT_LOW_SET_VAL) + 100)
#define BQ27Z561_INT_STATUS_REG          0x6E

#define FLOAT_DEFAULT_EXPONENTIAL        0x7f
#define FLOAT_BIT_NUMS                   64
#define FLOAT_FRACTION_SIZE              23
#define FLOAT_POINT_BIT                  2

enum bq27z561_fg_reg_idx {
	BQ_FG_REG_CTRL = 0,
	BQ_FG_REG_TEMP, /* Battery Temperature */
	BQ_FG_REG_VOLT, /* Battery Voltage */
	BQ_FG_REG_I, /* Battery Current */
	BQ_FG_REG_AI, /* Average Current */
	BQ_FG_REG_BATT_STATUS, /* BatteryStatus */
	BQ_FG_REG_TTE, /* Time to Empty */
	BQ_FG_REG_TTF, /* Time to Full */
	BQ_FG_REG_FCC, /* Full Charge Capacity */
	BQ_FG_REG_RM, /* Remaining Capacity */
	BQ_FG_REG_CC, /* Cycle Count */
	BQ_FG_REG_SOC, /* Relative State of Charge */
	BQ_FG_REG_SOH, /* State of Health */
	BQ_FG_REG_DC, /* Design Capacity */
	BQ_FG_REG_ALT_MAC, /* AltManufactureAccess */
	BQ_FG_REG_MAC_CHKSUM, /* MACChecksum */
	NUM_REGS,
};

enum bq27z561_fg_mac_cmd {
	FG_MAC_CMD_CTRL_STATUS = 0x0000,
	FG_MAC_CMD_DEV_TYPE = 0x0001,
	FG_MAC_CMD_FW_VER = 0x0002,
	FG_MAC_CMD_HW_VER = 0x0003,
	FG_MAC_CMD_IF_SIG = 0x0004,
	FG_MAC_CMD_DF_SIG = 0x0005,
	FG_MAC_CMD_CHEM_ID = 0x0006,
	FG_MAC_CMD_GAUGING = 0x0021,
	FG_MAC_CMD_SEAL = 0x0030,
	FG_MAC_CMD_DEV_RESET = 0x0041,
	FG_MAC_CMD_OP_STATUS = 0x0054,
	FG_MAC_CMD_CHARGING_STATUS = 0x0055,
	FG_MAC_CMD_GAUGING_STATUS = 0x0056,
	FG_MAC_CMD_MANU_STATUS = 0x0057,
	FG_MAC_CMD_MANU_INFO = 0x0070,
	FG_MAC_CMD_IT_STATUS1 = 0x0073,
	FG_MAC_CMD_IT_STATUS2 = 0x0074,
	FG_MAC_CMD_IT_STATUS3 = 0x0075,
	FG_MAC_CMD_SOC_CFG = 0x007A,
	FG_MAC_CMD_CELL_GAIN = 0x4000,
	FG_MAC_CMD_CC_GAIN = 0x4006,
	FG_MAC_CMD_CAP_GAIN = 0x400A,
	FG_MAC_CMD_MANU_INFO_DIR = 0x4041,
	FG_MAC_CMD_RA_TABLE = 0x40C0,
	FG_MAC_CMD_QMAX_CELL = 0x4146,
	FG_MAC_CMD_UPDATE_STATUS = 0x414A,
	FG_MAC_CMD_IO_CONFIG = 0x4484,
	FG_MAC_CMD_FC_SET_VOL_THD = 0x44B6,
	FG_MAC_CMD_LOW_TEMP_CHG_VOL = 0x44ED,
	FG_MAC_CMD_STD_TEMP_CHG_VOL = 0x44F5,
	FG_MAC_CMD_HIGH_TEMP_CHG_VOL = 0x4505,
	FG_MAC_CMD_REC_TEMP_CHG_VOL = 0x450D,
	FG_MAC_CMD_CHG_TERM_CUR = 0x4525,
	FG_MAC_CMD_CHG_TERM_VOL = 0x4529,
};

enum {
	SEAL_STATE_RSVED,
	SEAL_STATE_UNSEALED,
	SEAL_STATE_SEALED,
	SEAL_STATE_FA,
};

enum bq_fg_device {
	BQ27Z561,
};

enum {
	CMD_INVALID = 0,
	CMD_R, /* Read */
	CMD_W, /* Write */
	CMD_C, /* Compare */
	CMD_X, /* Delay */
};

/* single precision float */
struct spf {
	uint32_t fraction:23; /* Data Bit */
	uint32_t exponent:8; /* Exponential Bit */
	uint32_t sign:1; /* Sign Bit */
};

/* float element positon */
struct fep {
	int dec_point_pos;
	int first_one_pos;
};

static const unsigned char *device2str[] = {
	"bq27z561",
};

static u8 bq27z561_regs[NUM_REGS] = {
	0x00, /* CONTROL */
	0x06, /* TEMP */
	0x08, /* VOLT */
	0x0C, /* CURRENT */
	0x14, /* AVG CURRENT */
	0x0A, /* FLAGS */
	0x16, /* Time to empty */
	0x18, /* Time to full */
	0x12, /* Full charge capacity */
	0x10, /* Remaining Capacity */
	0x2A, /* CycleCount */
	0x2C, /* State of Charge */
	0x2E, /* State of Health */
	0x3C, /* Design Capacity */
	0x3E, /* AltManufacturerAccess */
	0x60, /* MACChecksum */
};

struct bq27z561_fg_display_data {
	int temp;
	int vbat;
	int ibat;
	int avg_ibat;
	int rm;
	int soc;
	int fcc;
	int qmax;
};

struct fg_batt_profile {
	const unsigned char *bqfs_image;
	u16 array_size;
	u8 version;
};

struct bqfs_cmd_info {
	u8 cmd_type;
	u8 addr;
	u8 reg;
	union {
		u8 bytes[CMD_MAX_DATA_SIZE + 1];
		u16 delay;
	} data;
	u8 data_len;
	u8 line_num;
};

struct bq27z561_device_info {
	struct device *dev;
	struct i2c_client *client;
	struct delayed_work batt_para_check_work;
	struct delayed_work batt_para_monitor_work;
	struct wakeup_source wakelock;
	struct mutex rd_mutex;
	struct mutex mac_mutex;
	atomic_t pm_suspend;
	atomic_t is_update;
	u32 gpio;
	int irq;
	u8 chip;
	int batt_version;
	u8 regs[NUM_REGS];
	u8 *bqfs_image_data;
	int fg_para_version;
	int count;
	bool batt_fc;
	bool batt_fd;
	bool batt_dsg;
	bool batt_rca; /* remaining capacity alarm */
	int seal_state; /* 0 - Full Access, 1 - Unsealed, 2 - Sealed */
	int batt_tte;
	int batt_soc;
	int last_batt_soc;
	int batt_fcc; /* Full charge capacity */
	int batt_rm; /* Remaining capacity */
	int batt_dc; /* Design Capacity */
	int batt_volt;
	int batt_temp;
	int batt_curr;
	int batt_status;
	int batt_qmax;
	int control_status;
	int op_status;
	int charing_status;
	int gauging_status;
	int manu_status;
	int batt_curr_avg;
	int batt_cyclecnt;
	int batt_mode;
};

#endif /* _BQ27Z561_FG_H_ */
