/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2019-2020. All rights reserved.
 * Description: This module is to collect IP package parameters
 * Author: songqiubin songqiubin@huawei.com
 * Create: 2019-09-18
 */

#ifndef _FI_H
#define _FI_H

#include <linux/if.h>
#include <linux/netfilter_ipv4.h>
#include <linux/netfilter_ipv6.h>
#include <linux/spinlock_types.h>
#include <linux/types.h>
#include "fi_flow.h"
#include "fi_utils.h"
#include "emcom/emcom_xengine.h"

#define FI_LAUNCH_APP_MAX		8
#define FI_TIMER_INTERVAL		2 /* 500ms timer */
#define FI_NL_SKB_QUEUE_MAXLEN	128
#define FI_DEV_NUM			4
#define FI_WLAN0_IFNAME	emcom_xengine_get_network_iface_name(EMCOM_XENGINE_NET_WLAN0)
#define FI_CELL0_IFNAME	emcom_xengine_get_network_iface_name(EMCOM_XENGINE_NET_CELL0)
#define FI_WLAN1_IFNAME	emcom_xengine_get_network_iface_name(EMCOM_XENGINE_NET_WLAN1)
#define FI_CELL1_IFNAME	emcom_xengine_get_network_iface_name(EMCOM_XENGINE_NET_CELL1)

#define FI_MIN_DELTA_US 5000u
#define FI_MIN_BURST_DURATION 2000u
#define FI_MIN_BURST_SIZE (10 * 1024)
#define FI_RTT_SHIFT 3
#define FI_VER "V3.00 20200920"

typedef enum {
	FI_DEV_WLAN,
	FI_DEV_CELL,
}fi_dev_type;

typedef enum {
	FI_SET_NL_PID_CMD,
	FI_COLLECT_START_CMD,
	FI_COLLECT_STATUS_UPDATE_CMD,
	FI_COLLECT_INFO_UPDATE_CMD,
	FI_COLLECT_FLOW_CTRL_MAP_UPDATE_CMD,
	CMD_NUM,
} fi_cfg_type;

typedef enum {
	FI_PKT_MSG_RPT,
	FI_PERIODIC_MSG_RPT,
	FI_QOS_MSG_RPT,
	FI_IFACE_MSG_RPT,
} fi_msg_type;

struct fi_app_info_table {
	struct fi_app_info_node	app[FI_LAUNCH_APP_MAX];
	atomic_t			free_cnt;
};

struct fi_ctx {
	struct mutex		nf_mutex;
	bool				nf_exist;
	struct timer_list	tm;
	bool				running;
	bool	iface_qos_rpt_enable;
	struct sock *nlfd;
	uint32_t nl_pid;
	struct sk_buff_head skb_queue;
	struct semaphore netlink_sync_sema;
	struct task_struct *netlink_task;
};


struct fi_iface_msg {
	char dev[IFNAMSIZ];
	uint32_t rcv_bytes;
};


struct fi_cfg_head{
	unsigned short		type;
	unsigned short		len;
};

extern struct fi_ctx g_fi_ctx;
extern void fi_cfg_process(const struct fi_cfg_head *cfg);
extern void fi_init(struct sock *nlfd);
extern void fi_deinit(void);
void fi_bw_calc(struct fi_flow_node *flow);
void fi_rtt_update(struct fi_flow_node *flow);

#endif /* _FI_H */
