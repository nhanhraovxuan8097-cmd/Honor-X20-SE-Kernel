/*
 * fingerprint.h
 *
 * fingerprint driver
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
#ifndef FINGERPRINT_H
#define FINGERPRINT_H

#include <linux/mutex.h>
#include <linux/cdev.h>
#include <linux/pm_wakeup.h>

#define SUCCESS           0
#define FAIL              -1

#define EVENT_HOLD        28
#define EVENT_CLICK       174
#define EVENT_DCLICK      111
#define EVENT_UP          103
#define EVENT_DOWN        108
#define EVENT_LEFT        105
#define EVENT_RIGHT       106

#define FP_MAX_SENSOR_ID_LEN        16
#define FP_MAX_CHIP_INFO_LEN        50
#define FP_DEFAULT_INFO_LEN         3
#define FP_RETURN_SUCCESS           0
#define MAX_MODULE_ID_LEN           64

/* NAVIGATION_ADJUST_NOREVERSE: default */
/* adapting to the rear fingerprint sensor module */
#define NAVIGATION_ADJUST_NOREVERSE 0
/* NAVIGATION_ADJUST_NOREVERSE: adapt to the front fingerprint sensor module */
#define NAVIGATION_ADJUST_REVERSE   1
#define NAVIGATION_ADJUST_NOTURN    0
#define NAVIGATION_ADJUST_TURN90    90
#define NAVIGATION_ADJUST_TURN180   180
#define NAVIGATION_ADJUST_TURN270   270

#define FP_RESET_RETRIES  4
#define FP_RESET_LOW_US   15000
#define FP_RESET_HIGH1_US 10000
#define FP_RESET_HIGH2_US 1250

#define FPC_TTW_HOLD_TIME 3000

#define FP_DEV_NAME   "fingerprint"
#define FP_CLASS_NAME "fpsensor"

/* sharemem for tee */
#define MTK_SIP_KERNEL_FP_CONF_TO_TEE_ADDR_AARCH32 0x820002C5
#define MTK_SIP_KERNEL_FP_CONF_TO_TEE_ADDR_AARCH64 0xC20002C5
#define FP_CHECK_NUM 0x66BB
#define OFFSET_8     8
#define OFFSET_16    16

/* define magic number */
#define FP_IOC_MAGIC 'f'

#define MODULE_ID_STRING_LENGTH 64
#define TEMP_BUFFER_LENGTH      8
#define IRQ_COUNT_MAX_VALUE     1000
/* define commands */
#define FP_IOC_CMD_ENABLE_IRQ          _IO(FP_IOC_MAGIC, 1)
#define FP_IOC_CMD_DISABLE_IRQ         _IO(FP_IOC_MAGIC, 2)
#define FP_IOC_CMD_SEND_UEVENT         _IO(FP_IOC_MAGIC, 3)
#define FP_IOC_CMD_GET_IRQ_STATUS      _IO(FP_IOC_MAGIC, 4)
#define FP_IOC_CMD_SET_WAKELOCK_STATUS _IO(FP_IOC_MAGIC, 5)
#define FP_IOC_CMD_SEND_SENSORID       _IO(FP_IOC_MAGIC, 6)
#define FP_IOC_CMD_SET_IPC_WAKELOCKS   _IO(FP_IOC_MAGIC, 7)
#define FP_IOC_CMD_SET_POWEROFF        _IO(FP_IOC_MAGIC, 10)
#define FP_IOC_CMD_ENABLE_SPI_CLK      _IO(FP_IOC_MAGIC, 11)
#define FP_IOC_CMD_DISABLE_SPI_CLK     _IO(FP_IOC_MAGIC, 12)
#define  FP_IOC_CMD_SET_POWERON        _IO(FP_IOC_MAGIC, 14)
#define  FP_IOC_CMD_CHECK_HBM_STATUS  _IO(FP_IOC_MAGIC, 15)
#define  FP_IOC_CMD_RESET_HBM_STATUS  _IO(FP_IOC_MAGIC, 16)
#define  FP_IOC_CMD_SEND_SENSORID_UD  _IO(FP_IOC_MAGIC, 17)
#define  FP_IOC_CMD_GET_BIGDATA       _IO(FP_IOC_MAGIC, 18)
#define  FP_IOC_CMD_NOTIFY_DISPLAY_FP_DOWN_UD _IO(FP_IOC_MAGIC, 19)
#define  FP_IOC_CMD_WAIT_FINGER_UP  _IO(FP_IOC_MAGIC, 20)
#define  FP_IOC_CMD_IDENTIFY_EXIT   _IO(FP_IOC_MAGIC, 21)
#define  FP_IOC_CMD_GET_FINGER_STATUS   _IO(FP_IOC_MAGIC, 22)

enum hbm_status {
	HBM_ON = 0,
	HBM_NONE,
};

enum fp_gpio_pin_level {
	FP_GPIO_LOW_LEVEL,
	FP_GPIO_HIGH_LEVEL,
};

enum fprint_pin {
	FINGERPRINT_RST_PIN = 0,
	FINGERPRINT_SPI_CS_PIN,
	FINGERPRINT_SPI_MO_PIN,
	FINGERPRINT_SPI_MI_PIN,
	FINGERPRINT_SPI_CK_PIN,
	FINGERPRINT_POWER_EN_PIN,
};

enum fingerprint_state {
	FP_UNINIT = 0,
	FP_LCD_UNBLANK = 1,
	FP_LCD_POWEROFF = 2,
	FP_STATE_UNDEFINE = 255,
};

enum fp_poweroff_scheme {
	FP_POWEROFF_SCHEME_ONE = 1,
	FP_POWEROFF_SCHEME_TWO = 2,
};

struct fingerprint_bigdata {
	int lcd_charge_time;
	int lcd_on_time;
	int cpu_wakeup_time;
};

struct fp_data {
	struct device *dev;
	struct spi_device *spi;
	struct cdev cdev;
	struct class *class;
	struct device *device;
	dev_t devno;
	struct platform_device *pf_dev;

	struct wakeup_source ttw_wl;
	int irq_gpio;
	int irq;
	int btb_det_gpio;
	int btb_det_irq;
	int rst_gpio;
	int avdd_en_gpio;
	int vdd_en_gpio;
	int poweroff_scheme;
	int module_vendor_info;
	int navigation_adjust1;
	int navigation_adjust2;
	unsigned int snr_stat;
	unsigned int nav_stat;
	struct input_dev *input_dev;
	int irq_num;
	int event_type;
	int event_code;
	unsigned int spi_clk_counter;
	struct mutex mutex_lock_irq_switch;
	struct mutex mutex_lock_clk;
	struct mutex lock;
	bool wakeup_enabled;
	bool read_image_flag;
	unsigned int sensor_id;
	unsigned int sensor_id_ud;
	unsigned int autotest_input;
	char module_id[MAX_MODULE_ID_LEN];
	char module_id_ud[MAX_MODULE_ID_LEN];
	struct delayed_work dwork;
	struct regulator *vdd;
	struct regulator *avdd;
	struct pinctrl *pctrl;
	struct pinctrl_state *pins_idle;

	struct pinctrl *pinctrl;
	struct pinctrl_state *pins_default;
	struct pinctrl_state *eint_as_int;
	struct pinctrl_state *fp_rst_low;
	struct pinctrl_state *fp_rst_high;
	struct pinctrl_state *fp_cs_high;
	struct pinctrl_state *fp_cs_low;
	struct pinctrl_state *fp_mo_low;
	struct pinctrl_state *fp_mo_high;
	struct pinctrl_state *fp_mi_low;
	struct pinctrl_state *fp_mi_high;
	struct pinctrl_state *fp_ck_low;
	struct pinctrl_state *fp_ck_high;
	struct pinctrl_state *fp_power_high;
	struct pinctrl_state *fp_power_low;
	struct pinctrl_state *fp_btb_det;

#if defined(CONFIG_FB)
	struct notifier_block fb_notify;
#endif
	atomic_t state;
	int hbm_status;
	wait_queue_head_t hbm_queue;
	wait_queue_head_t wait_finger_up_queue;
	struct fingerprint_bigdata fingerprint_bigdata;
	unsigned int custom_timing_scheme;
	unsigned int use_tp_irq;
	int tp_event;
	unsigned int product_id;
};

enum fp_custom_timing_scheme {
	FP_CUSTOM_TIMING_SCHEME_ZERO = 0,
	FP_CUSTOM_TIMING_SCHEME_ONE = 1,
	FP_CUSTOM_TIMING_SCHEME_TWO = 2,
	FP_CUSTOM_TIMING_SCHEME_THREE = 3,
	FP_CUSTOM_TIMING_SCHEME_FOUR = 4,
};

enum fp_irq_source {
	USE_SELF_IRQ = 0,
	USE_TP_IRQ,
};

struct fp_sensor_info {
	unsigned int sensor_id;
	char sensor_name[FP_MAX_SENSOR_ID_LEN];
};

bool get_pd_charge_flag(void);
int fingerprint_gpio_reset(struct fp_data *fingerprint);

#endif /* FINGERPRINT_H */
