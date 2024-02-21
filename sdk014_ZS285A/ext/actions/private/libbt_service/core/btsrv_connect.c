/*
 * Copyright (c) 2016 Actions Semi Co., Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief bt connect/disconnect service
 */

#define SYS_LOG_DOMAIN "btsrv_connect"

#include <logging/sys_log.h>

#include <string.h>
#include <kernel.h>
#include <misc/slist.h>
#include <mem_manager.h>
#include <nvram_config.h>
#include <property_manager.h>
#include <thread_timer.h>
#include <bluetooth/host_interface.h>

#include "btsrv_inner.h"

#define AUTOCONN_START_TIME						(100)	/* 100ms */
#define AUTOCONN_GET_NAME_TIME					(200)	/* 200ms */
#define AUTOCONN_MASTER_DELAY					(2000)	/* 1000ms */
#define AUTOCONN_DELAY_TIME_MASK				(5000)	/* 5000ms */
#define MONITOR_A2DP_TIMES						(8)
#define MONITOR_AVRCP_TIMES						(6)
#define MONITOR_AVRCP_CONNECT_INTERVAL			(3)
#define MONITOR_PRIFILE_INTERVEL				(1000)	/* 1000ms */

#define BT_SUPERVISION_TIMEOUT					(8000)		/* 8000*0.625ms = 5000ms */

enum {
	AUTOCONN_STATE_IDLE,
	AUTOCONN_STATE_DELAY_CONNECTING,
	AUTOCONN_STATE_BASE_CONNECTING,
	AUTOCONN_STATE_BASE_CONNECTED,
	AUTOCONN_STATE_PROFILE_CONNETING,
};

enum {
	AUTOCONN_PROFILE_IDLE,
	AUTOCONN_PROFILE_HFP_CONNECTING,
	AUTOCONN_PROFILE_A2DP_CONNECTING,
	AUTOCONN_PROFILE_AVRCP_CONNECTING,
	AUTOCONN_PROFILE_HID_CONNECTING,
	AUTOCONN_PROFILE_CONNECTING_MAX,
};

enum {
	AUTOCONN_RECONNECT_CLEAR_ALL,
	AUTOCONN_RECONNECT_CLEAR_PHONE,
	AUTOCONN_RECONNECT_CLEAR_TWS,
};

enum {
	SWITCH_SBC_STATE_IDLE,
	SWITCH_SBC_STATE_DISCONNECTING_A2DP,
	SWITCH_SBC_STATE_CONNECTING_A2DP,
};

struct auto_conn_t {
	bd_address_t addr;
	u8_t addr_valid:1;
	u8_t tws_role:3;
	u8_t a2dp:1;
	u8_t avrcp:1;
	u8_t hfp:1;
	u8_t hfp_first:1;
	u8_t hid:1;
	u8_t strategy;
	u8_t base_try;
	u8_t profile_try;
	u8_t curr_connect_profile;
	u8_t state;
	u16_t base_interval;
	u16_t profile_interval;
};

struct profile_conn_t {
	bd_address_t addr;
	u8_t valid:1;
	u8_t avrcp_times:4;
	u8_t a2dp_times:4;
};

struct btsrv_connect_priv {
	struct autoconn_info nvram_reconn_info[BTSRV_SAVE_AUTOCONN_NUM];	/* Reconnect info save in nvram */
	struct auto_conn_t auto_conn[BTSRV_SAVE_AUTOCONN_NUM];				/* btsvr connect use for doing reconnect bt */
	struct thread_timer auto_conn_timer;
	u8_t connecting_index:3;
	u8_t auto_connect_running:1;
	u8_t clear_list_disconnecting:1;
	u8_t clear_list_mode:3;

	/* Monitor connect profile, connect by phone */
	struct profile_conn_t monitor_conn[BTSRV_SAVE_AUTOCONN_NUM];
	struct thread_timer monitor_conn_timer;
	u8_t monitor_timer_running:1;
	u8_t curr_req_performance:1;
	u8_t reconnect_req_high_performance:1;
};

static void btsrv_proc_link_change(u8_t *mac, u8_t type);
static struct btsrv_connect_priv *p_connect;
static struct btsrv_connect_priv p_btsrv_connect;

static void btsrv_update_performance_req(void)
{
	u8_t need_high;
	bool rdm_need_high_performance = btsrv_rdm_need_high_performance();

	if (rdm_need_high_performance || p_connect->reconnect_req_high_performance) {
		need_high = 1;
	} else {
		need_high = 0;
	}

	if (need_high && !p_connect->curr_req_performance) {
		p_connect->curr_req_performance = 1;
		SYS_LOG_INF("BTSRV_REQ_HIGH_PERFORMANCE\n");
		btsrv_adapter_callback(BTSRV_REQ_HIGH_PERFORMANCE, NULL);
	} else if (!need_high && p_connect->curr_req_performance) {
		p_connect->curr_req_performance = 0;
		SYS_LOG_INF("BTSRV_RELEASE_HIGH_PERFORMANCE\n");
		btsrv_adapter_callback(BTSRV_RELEASE_HIGH_PERFORMANCE, NULL);
	}
}

static void btsrv_update_nvram_auto_conn_info(void)
{
	btsrv_property_set(CFG_AUTOCONN_INFO, (void *)p_connect->nvram_reconn_info,
				sizeof(p_connect->nvram_reconn_info));
}

int btsrv_connect_get_auto_reconnect_info(struct autoconn_info *info, u8_t max_cnt)
{
	int dev_cnt = 0, read_cnt, i;

	read_cnt = (max_cnt > BTSRV_SAVE_AUTOCONN_NUM) ? BTSRV_SAVE_AUTOCONN_NUM : max_cnt;
	memcpy((char *)info, (char *)p_connect->nvram_reconn_info, (sizeof(struct autoconn_info)*read_cnt));
	for (i = 0; i < read_cnt; i++) {
		if (info[i].addr_valid) {
			dev_cnt++;
		}
	}

	return dev_cnt;
}

void btsrv_connect_set_auto_reconnect_info(struct autoconn_info *info, u8_t max_cnt)
{
	int write_cnt;

	write_cnt = (max_cnt > BTSRV_SAVE_AUTOCONN_NUM) ? BTSRV_SAVE_AUTOCONN_NUM : max_cnt;
	memcpy((char *)p_connect->nvram_reconn_info, (char *)info, (sizeof(struct autoconn_info)*write_cnt));
	btsrv_update_nvram_auto_conn_info();
}

void btsrv_autoconn_info_update(void)
{
	struct autoconn_info *info, *tmpInfo;
	int connected_cnt, i, j, have_tws = 0;

	info = mem_malloc(sizeof(struct autoconn_info)*BTSRV_SAVE_AUTOCONN_NUM*2);
	if (!info) {
		SYS_LOG_ERR("info_update failed");
		goto update_exit;
	}

	memset(info, 0, (sizeof(struct autoconn_info)*BTSRV_SAVE_AUTOCONN_NUM*2));
	connected_cnt = btsrv_rdm_get_autoconn_dev(info, BTSRV_SAVE_AUTOCONN_NUM);
	if (connected_cnt == 0) {
		goto update_exit;
	}

	/* Only save one tws device info */
	for (i = 0; i < connected_cnt; i++) {
		if (info[i].tws_role != BTSRV_TWS_NONE) {
			have_tws = 1;
		}
	}

	tmpInfo = info;
	tmpInfo += BTSRV_SAVE_AUTOCONN_NUM;
	btsrv_connect_get_auto_reconnect_info(tmpInfo, BTSRV_SAVE_AUTOCONN_NUM);
	for (i = 0; i < BTSRV_SAVE_AUTOCONN_NUM; i++) {
		if (tmpInfo[i].addr_valid) {
			for (j = 0; j < connected_cnt; j++) {
				if (!memcmp(tmpInfo[i].addr.val, info[j].addr.val, sizeof(bd_address_t))) {
					info[j].a2dp |= tmpInfo[i].a2dp;
					info[j].avrcp |= tmpInfo[i].avrcp;
					info[j].hfp |= tmpInfo[i].hfp;
					info[j].hid |= tmpInfo[i].hid;
					tmpInfo[i].addr_valid = 0;
					break;
				}
			}

			if (have_tws && tmpInfo[i].tws_role != BTSRV_TWS_NONE) {
				continue;
			}

			if ((j == connected_cnt) && (connected_cnt < BTSRV_SAVE_AUTOCONN_NUM)) {
				memcpy(&info[connected_cnt], &tmpInfo[i], sizeof(struct autoconn_info));
				connected_cnt++;
			}
		}
	}

	btsrv_connect_set_auto_reconnect_info(info, BTSRV_SAVE_AUTOCONN_NUM);

update_exit:
	if (info) {
		mem_free(info);
	}
}

static void btsrv_autoconn_info_clear(void)
{
	for (int i = 0; i < BTSRV_SAVE_AUTOCONN_NUM; i++) {
		if ((p_connect->nvram_reconn_info[i].tws_role != BTSRV_TWS_NONE && p_connect->clear_list_mode == BTSRV_DEVICE_TWS)
			|| (p_connect->nvram_reconn_info[i].tws_role == BTSRV_TWS_NONE && p_connect->clear_list_mode == BTSRV_DEVICE_PHONE)
			|| (p_connect->clear_list_mode == BTSRV_DEVICE_ALL)) {
	#ifdef CONFIG_USER_BT_CLEAR_LINKKEY
			if(p_connect->clear_list_mode != BTSRV_DEVICE_TWS)
				btif_br_clean_linkkey();
	#endif
			memset(&p_connect->nvram_reconn_info[i], 0, sizeof(struct autoconn_info));
		}
	}
	p_connect->clear_list_mode = 0;
	btsrv_update_nvram_auto_conn_info();
}

static void btsrv_connect_auto_connection_stop(void)
{
	int i;

	SYS_LOG_INF("auto_connection_stop");
	for (i = 0; i < BTSRV_SAVE_AUTOCONN_NUM; i++) {
		if (p_connect->auto_conn[i].addr_valid &&
			(p_connect->auto_conn[i].state == AUTOCONN_STATE_BASE_CONNECTING)) {
			btsrv_adapter_check_cancal_connect(&p_connect->auto_conn[i].addr);
		}
	}

	memset(&p_connect->auto_conn, 0, sizeof(p_connect->auto_conn));
	thread_timer_stop(&p_connect->auto_conn_timer);
	btsrv_scan_update_mode(false);
	p_connect->reconnect_req_high_performance = 0;
	btsrv_update_performance_req();
}

static void btsrv_connect_auto_connection_restart(s32_t duration, s32_t period)
{
	SYS_LOG_INF("connection_restart %d, %d", duration, period);
	thread_timer_start(&p_connect->auto_conn_timer, duration, period);
	p_connect->reconnect_req_high_performance = 1;
	btsrv_update_performance_req();
}

static void btsrv_connect_monitor_profile_stop(void)
{
	SYS_LOG_INF("monitor_profile_stop");
	thread_timer_stop(&p_connect->monitor_conn_timer);
}

static void btsrv_connect_monitor_profile_start(s32_t duration, s32_t period)
{
	if (thread_timer_is_running(&p_connect->monitor_conn_timer)) {
		return;
	}

	SYS_LOG_INF("monitor_profile_start %d, %d", duration, period);
	thread_timer_start(&p_connect->monitor_conn_timer, duration, period);
}

static struct auto_conn_t *btsrv_autoconn_find_wait_reconn(void)
{
	u8_t i, start_index, end_index;
	struct auto_conn_t *auto_conn = NULL;

	start_index = p_connect->connecting_index;
	end_index = BTSRV_SAVE_AUTOCONN_NUM;

try_again:
	for (i = start_index; i < end_index; i++) {
		if (p_connect->auto_conn[i].addr_valid) {
			auto_conn = &p_connect->auto_conn[i];
			SYS_LOG_INF("wait_reconn %d, %d, %d, %d%d%d%d", i, auto_conn->state, auto_conn->tws_role,
						auto_conn->a2dp, auto_conn->avrcp, auto_conn->hfp, auto_conn->hid);
			p_connect->connecting_index = i;
			break;
		}
	}

	if ((auto_conn == NULL) && (start_index != 0)) {
		start_index = 0;
		end_index = p_connect->connecting_index;
		goto try_again;
	}

	return auto_conn;
}

static void btsrv_update_autoconn_state(u8_t *addr, u8_t event)
{
	u8_t i, master;
	u8_t index = BTSRV_SAVE_AUTOCONN_NUM;

	for (i = 0; i < BTSRV_SAVE_AUTOCONN_NUM; i++) {
		if (p_connect->auto_conn[i].addr_valid &&
			!memcmp(p_connect->auto_conn[i].addr.val, addr, sizeof(bt_addr_t))) {
			index = i;
			break;
		}
	}

	if (index == BTSRV_SAVE_AUTOCONN_NUM) {
		return;
	}

	switch (event) {
	case BTSRV_LINK_BASE_CONNECTED:
		if (p_connect->auto_conn[index].state < AUTOCONN_STATE_BASE_CONNECTED) {
			p_connect->auto_conn[index].state = AUTOCONN_STATE_BASE_CONNECTED;
		}

		if (index == p_connect->connecting_index) {
			/* After base connected, wait get name finish to trigger next step, set get name time */
			btsrv_connect_auto_connection_restart(AUTOCONN_GET_NAME_TIME, 0);
		}
		break;

	case BTSRV_LINK_BASE_DISCONNECTED:
		p_connect->auto_conn[index].addr_valid = 0;
		p_connect->auto_conn[index].state = AUTOCONN_STATE_IDLE;
		master = (p_connect->auto_conn[index].tws_role == BTSRV_TWS_MASTER) ? 1 : 0;
		if (index == p_connect->connecting_index) {
			p_connect->connecting_index++;
			p_connect->connecting_index %= BTSRV_SAVE_AUTOCONN_NUM;
			/* As master connect to slave, but disconnect by slave, means slave want to as master,
			 * let some time for slave reconnect master.
			 */
			btsrv_connect_auto_connection_restart((master ? AUTOCONN_MASTER_DELAY : AUTOCONN_START_TIME), 0);
		}
		break;

	case BTSRV_LINK_BASE_GET_NAME:
		if (p_connect->auto_conn[index].state < AUTOCONN_STATE_PROFILE_CONNETING) {
			p_connect->auto_conn[index].state = AUTOCONN_STATE_PROFILE_CONNETING;
		}

		if (index == p_connect->connecting_index) {
			btsrv_connect_auto_connection_restart(AUTOCONN_START_TIME, 0);
		}
		break;

	case BTSRV_LINK_BASE_CONNECTED_TIMEOUT:
	case BTSRV_LINK_BASE_CONNECTED_FAILED:
		if (p_connect->auto_conn[index].state == AUTOCONN_STATE_BASE_CONNECTING &&
			index == p_connect->connecting_index) {
			p_connect->auto_conn[index].base_try--;
			p_connect->auto_conn[index].state = AUTOCONN_STATE_IDLE;
			if (p_connect->auto_conn[index].base_try == 0) {
				p_connect->auto_conn[index].addr_valid = 0;
			}

			if (p_connect->auto_conn[index].base_try == 0 ||
				p_connect->auto_conn[index].strategy == BTSRV_AUTOCONN_ALTERNATE) {
				/* Try next device */
				p_connect->connecting_index++;
				p_connect->connecting_index %= BTSRV_SAVE_AUTOCONN_NUM;
			}

			/* Connect failed event, just let timeout do next process */
			if (event == BTSRV_LINK_BASE_CONNECTED_TIMEOUT) {
				btsrv_connect_auto_connection_restart(AUTOCONN_START_TIME, 0);
			}
		}
		break;

	case BTSRV_LINK_HFP_CONNECTED:
	case BTSRV_LINK_A2DP_CONNECTED:
	case BTSRV_LINK_AVRCP_CONNECTED:
	case BTSRV_LINK_HID_CONNECTED:
		if (p_connect->auto_conn[index].state == AUTOCONN_STATE_PROFILE_CONNETING &&
			index == p_connect->connecting_index) {
			p_connect->auto_conn[index].profile_try++;
			btsrv_connect_auto_connection_restart(AUTOCONN_START_TIME, 0);
		}
		break;

	case BTSRV_LINK_HFP_DISCONNECTED:
	case BTSRV_LINK_A2DP_DISCONNECTED:
	case BTSRV_LINK_AVRCP_DISCONNECTED:
	case BTSRV_LINK_HID_DISCONNECTED:
		/* What todo!!! */
		break;
	default:
		break;
	}
}

static void btsrv_autoconn_check_clear_auto_info(u8_t clear_type)
{
	u8_t i;
	char addr[BT_ADDR_STR_LEN];

	for (i = 0; i < BTSRV_SAVE_AUTOCONN_NUM; i++) {
		if (p_connect->auto_conn[i].addr_valid) {
			if ((clear_type == AUTOCONN_RECONNECT_CLEAR_PHONE) &&
				(p_connect->auto_conn[i].tws_role != BTSRV_TWS_NONE)) {
				continue;
			} else if ((clear_type == AUTOCONN_RECONNECT_CLEAR_TWS) &&
				(p_connect->auto_conn[i].tws_role == BTSRV_TWS_NONE)) {
				continue;
			}

			if (btsrv_rdm_find_conn_by_addr(&p_connect->auto_conn[i].addr) == NULL) {
				/* Not tws slave or one of the two connect phone */
				hostif_bt_addr_to_str((const bt_addr_t *)&p_connect->auto_conn[i].addr, addr, BT_ADDR_STR_LEN);
				SYS_LOG_INF("clear_auto_info %s", addr);
				memset(&p_connect->auto_conn[i], 0, sizeof(struct auto_conn_t));
			}
		}
	}
}

static void btsrv_autoconn_idle_proc(void)
{
	char addr[BT_ADDR_STR_LEN];
	struct auto_conn_t *auto_conn;
	s32_t next_time = 0;
	u8_t index;

	if ((btsrv_rdm_get_dev_role() == BTSRV_TWS_SLAVE) ||
		(btsrv_rdm_get_connected_dev(NULL, NULL) == btsrv_max_conn_num())) {
		btsrv_autoconn_check_clear_auto_info(AUTOCONN_RECONNECT_CLEAR_ALL);
	}

	if (btsrv_rdm_get_dev_role() == BTSRV_TWS_MASTER) {
		btsrv_autoconn_check_clear_auto_info(AUTOCONN_RECONNECT_CLEAR_TWS);
	}

	if ((btsrv_rdm_get_dev_role() == BTSRV_TWS_NONE) &&
		(btsrv_rdm_get_connected_dev(NULL, NULL) == btsrv_max_phone_num())) {
		btsrv_autoconn_check_clear_auto_info(AUTOCONN_RECONNECT_CLEAR_PHONE);
	}

	auto_conn = btsrv_autoconn_find_wait_reconn();
	if (auto_conn == NULL) {
		SYS_LOG_INF("auto connect finished");
		btsrv_connect_auto_connection_stop();
		return;
	}

	index = p_connect->connecting_index;
	if (btsrv_rdm_find_conn_by_addr(&auto_conn->addr) == NULL) {
		hostif_bt_addr_to_str((const bt_addr_t *)&auto_conn->addr, addr, BT_ADDR_STR_LEN);
		SYS_LOG_INF("auto_connect: %s tws_role %d, try %d", addr, auto_conn->tws_role, auto_conn->base_try);

		if (auto_conn->tws_role == BTSRV_TWS_NONE) {
			p_connect->auto_conn[index].state = AUTOCONN_STATE_BASE_CONNECTING;
			btsrv_scan_update_mode(true);
			btsrv_adapter_connect(&auto_conn->addr);
			next_time = auto_conn->base_interval;
		} else {
			p_connect->auto_conn[index].state = AUTOCONN_STATE_DELAY_CONNECTING;
			btsrv_scan_update_mode(true);
			next_time = bt_rand32_get()%AUTOCONN_DELAY_TIME_MASK;
			SYS_LOG_INF("Delay time %d", next_time);
		}
	} else {
		SYS_LOG_INF("idle_proc state %d", p_connect->auto_conn[index].state);
		if (p_connect->auto_conn[index].state < AUTOCONN_STATE_BASE_CONNECTED) {
			p_connect->auto_conn[index].state = AUTOCONN_STATE_BASE_CONNECTED;
		}
		btsrv_scan_update_mode(true);
		next_time = AUTOCONN_START_TIME;
	}

	btsrv_connect_auto_connection_restart(next_time, 0);
}

static void btsrv_autoconn_delay_connecting_proc(void)
{
	struct auto_conn_t *auto_conn = &p_connect->auto_conn[p_connect->connecting_index];
	s32_t next_time = 0;

	if (!auto_conn->addr_valid) {
		auto_conn->state = AUTOCONN_STATE_IDLE;
		btsrv_connect_auto_connection_restart(AUTOCONN_START_TIME, 0);
		return;
	}

	if (btsrv_rdm_find_conn_by_addr(&auto_conn->addr) == NULL) {
		auto_conn->state = AUTOCONN_STATE_BASE_CONNECTING;
		btsrv_scan_update_mode(true);
	#ifdef CONFIG_SUPPORT_TWS
		/* Base connect do reconnect, no need tws module try more times, just set try_times to 0 */
		btsrv_tws_connect_to((bt_addr_t *)&auto_conn->addr, 0, auto_conn->tws_role);
	#endif
		next_time = auto_conn->base_interval;
	} else {
		SYS_LOG_INF("connecting_proc state %d", auto_conn->state);
		if (auto_conn->state < AUTOCONN_STATE_BASE_CONNECTED) {
			auto_conn->state = AUTOCONN_STATE_BASE_CONNECTED;
		}
		btsrv_scan_update_mode(true);
		next_time = AUTOCONN_START_TIME;
	}

	btsrv_connect_auto_connection_restart(next_time, 0);
}

static void btsrv_autoconn_base_connecting_proc(void)
{
	/* Base connect timeout */
	btsrv_proc_link_change(p_connect->auto_conn[p_connect->connecting_index].addr.val,
								BTSRV_LINK_BASE_CONNECTED_TIMEOUT);
}

static void btsrv_autoconn_base_connected_proc(void)
{
	u8_t index = p_connect->connecting_index;

	/* In base connected state, process get name,
	 * after get name, trigger enter AUTOCONN_STATE_PROFILE_CONNETING
	 */

	p_connect->auto_conn[index].base_try--;
	if (p_connect->auto_conn[index].base_try == 0) {
		p_connect->auto_conn[index].addr_valid = 0;
		p_connect->auto_conn[index].state = AUTOCONN_STATE_IDLE;
		p_connect->connecting_index++;
		p_connect->connecting_index %= BTSRV_SAVE_AUTOCONN_NUM;
		btsrv_connect_auto_connection_restart(AUTOCONN_START_TIME, 0);
	} else {
		btsrv_connect_auto_connection_restart(p_connect->auto_conn[index].base_interval, 0);
	}
}

static int btsrv_autoconn_check_connect_profile(struct bt_conn *conn, struct auto_conn_t *auto_conn)
{
	int ret, i;
	u8_t next_connect_profile;
	bool hfp_connected = btsrv_rdm_is_hfp_connected(conn);
	bool a2dp_connected = btsrv_rdm_is_a2dp_connected(conn);
	bool avrcp_connected = btsrv_rdm_is_avrcp_connected(conn);
	bool hid_connected = btsrv_rdm_is_hid_connected(conn);

	if ((auto_conn->hfp && !hfp_connected) ||
		(auto_conn->a2dp && !a2dp_connected) ||
		(auto_conn->avrcp && !avrcp_connected)||
		(auto_conn->hid && !hid_connected)) {
		/* Still have profile to connect */
		ret = 0;
	} else {
		/* Try other device */
		ret = 1;
		goto exit_check;
	}

	if (auto_conn->tws_role != BTSRV_TWS_NONE) {
		/* TWS a2dp/avrcp connect by master in base connected callback,
		 * here just check a2dp/avrcp is connected and try other device.
		 */
		goto exit_check;
	}

	SYS_LOG_INF("connect_profile %d%d, %d%d, %d%d, %d%d %d%d", auto_conn->curr_connect_profile, auto_conn->hfp_first,
			auto_conn->hfp, hfp_connected, auto_conn->a2dp, a2dp_connected, auto_conn->avrcp, avrcp_connected
			, auto_conn->hid, hid_connected);

	if (auto_conn->curr_connect_profile == AUTOCONN_PROFILE_IDLE) {
		if (auto_conn->hfp_first) {
			next_connect_profile = AUTOCONN_PROFILE_HFP_CONNECTING;
		} else {
			next_connect_profile = AUTOCONN_PROFILE_A2DP_CONNECTING;
		}
	} else {
		next_connect_profile = auto_conn->curr_connect_profile + 1;
	}

	for (i = (AUTOCONN_PROFILE_IDLE+1); i < AUTOCONN_PROFILE_CONNECTING_MAX; i++) {
		if (next_connect_profile >= AUTOCONN_PROFILE_CONNECTING_MAX) {
			next_connect_profile = AUTOCONN_PROFILE_HFP_CONNECTING;
		}

		if ((next_connect_profile == AUTOCONN_PROFILE_HFP_CONNECTING) &&
			auto_conn->hfp && !hfp_connected) {
			break;
		} else if ((next_connect_profile == AUTOCONN_PROFILE_A2DP_CONNECTING) &&
					auto_conn->a2dp && !a2dp_connected) {
			break;
		} else if ((next_connect_profile == AUTOCONN_PROFILE_AVRCP_CONNECTING) &&
					auto_conn->avrcp && !avrcp_connected) {
			break;
		}else if ((next_connect_profile == AUTOCONN_PROFILE_HID_CONNECTING) &&
					auto_conn->hid && !hid_connected) {
			break;
		}

		next_connect_profile++;
	}

	if (next_connect_profile == AUTOCONN_PROFILE_HFP_CONNECTING) {
		btsrv_hfp_connect(conn);
	} else if (next_connect_profile == AUTOCONN_PROFILE_A2DP_CONNECTING) {
		btsrv_a2dp_connect(conn, BT_A2DP_CH_SINK);
	} else if (next_connect_profile == AUTOCONN_PROFILE_AVRCP_CONNECTING) {
		btsrv_avrcp_connect(conn);
	}else if (next_connect_profile == AUTOCONN_PROFILE_HID_CONNECTING) {
		btsrv_hid_connect(conn);
	}

	auto_conn->curr_connect_profile = next_connect_profile;
exit_check:
	return ret;
}

static void btsrv_autoconn_profile_connecting_proc(void)
{
	u8_t index = p_connect->connecting_index;
	struct auto_conn_t *auto_conn = &p_connect->auto_conn[index];
	struct bt_conn *conn;

	conn = btsrv_rdm_find_conn_by_addr(&auto_conn->addr);
	if (conn == NULL) {
		SYS_LOG_ERR("connecting_proc need to fix!!!");
		return;
	}

	if (auto_conn->profile_try == 0) {
		SYS_LOG_WRN("Failed to connect %d(%d), %d(%d), %d(%d), %d(%d)",
					auto_conn->hfp, btsrv_rdm_is_hfp_connected(conn),
					auto_conn->a2dp, btsrv_rdm_is_a2dp_connected(conn),
					auto_conn->avrcp, btsrv_rdm_is_avrcp_connected(conn),
					auto_conn->hid, btsrv_rdm_is_hid_connected(conn));
		/* I have linkkey but phone clear linkkey, when do reconnect,
		 * phone will arise one connect notify, but phone do nothing,
		 * then profile will not connect, need active do disconnect.
		 * Better TODO: host check this case, notify upper layer
		 *                          clear linkkey and auto connect info.
		 */
		if (!btsrv_rdm_is_hfp_connected(conn) &&
			!btsrv_rdm_is_a2dp_connected(conn) &&
			!btsrv_rdm_is_avrcp_connected(conn) &&
			!btsrv_rdm_is_spp_connected(conn)  &&
			!btsrv_rdm_is_hid_connected(conn)) {
			btsrv_adapter_disconnect(conn);
		}
		goto try_other_dev;
	}
	auto_conn->profile_try--;

	if (btsrv_autoconn_check_connect_profile(conn, auto_conn)) {
		goto try_other_dev;
	}

	btsrv_connect_auto_connection_restart(auto_conn->profile_interval, 0);
	return;

try_other_dev:
	p_connect->auto_conn[index].addr_valid = 0;
	p_connect->auto_conn[index].state = AUTOCONN_STATE_IDLE;
	p_connect->connecting_index++;
	p_connect->connecting_index %= BTSRV_SAVE_AUTOCONN_NUM;
	btsrv_connect_auto_connection_restart(AUTOCONN_START_TIME, 0);
}

static void btsrv_autoconn_timer_handler(struct thread_timer *ttimer, void *expiry_fn_arg)
{
	u8_t state, index;

	p_connect->auto_connect_running = 1;
	index = p_connect->connecting_index;
	if (p_connect->auto_conn[index].addr_valid) {
		state = p_connect->auto_conn[index].state;
	} else {
		state = AUTOCONN_STATE_IDLE;
	}
	SYS_LOG_INF("autoconn index %d, state: %d", index, state);

	switch (state) {
	case AUTOCONN_STATE_IDLE:
		btsrv_autoconn_idle_proc();
		break;
	case AUTOCONN_STATE_DELAY_CONNECTING:
		btsrv_autoconn_delay_connecting_proc();
		break;
	case AUTOCONN_STATE_BASE_CONNECTING:
		btsrv_autoconn_base_connecting_proc();
		break;
	case AUTOCONN_STATE_BASE_CONNECTED:
		btsrv_autoconn_base_connected_proc();
		break;
	case AUTOCONN_STATE_PROFILE_CONNETING:
		btsrv_autoconn_profile_connecting_proc();
		break;
	default:
		btsrv_connect_auto_connection_stop();
		break;
	}

	p_connect->auto_connect_running = 0;
}

static void btsrv_monitor_timer_handler(struct thread_timer *ttimer, void *expiry_fn_arg)
{
	int i, stop_timer = 1;
	struct bt_conn *conn;

	p_connect->monitor_timer_running = 1;

	for (i = 0; i < BTSRV_SAVE_AUTOCONN_NUM; i++) {
		if (p_connect->monitor_conn[i].valid) {
			conn = btsrv_rdm_find_conn_by_addr(&p_connect->monitor_conn[i].addr);
			if (conn == NULL) {
				p_connect->monitor_conn[i].valid = 0;
				continue;
			}

			/* Now only monitor avrcp after a2dp connected,
			 * some phone active connect a2dp but not connect avrcp
			 */
			if (btsrv_rdm_is_a2dp_connected(conn)) {
				if (btsrv_rdm_is_avrcp_connected(conn)) {
					p_connect->monitor_conn[i].valid = 0;
					continue;
				} else {
					if (p_connect->monitor_conn[i].avrcp_times) {
						p_connect->monitor_conn[i].avrcp_times--;
					}

					if ((p_connect->monitor_conn[i].avrcp_times%MONITOR_AVRCP_CONNECT_INTERVAL) == 0) {
						SYS_LOG_INF("Do avrcp connect");
						btsrv_avrcp_connect(conn);
					}

					if (p_connect->monitor_conn[i].avrcp_times == 0) {
						p_connect->monitor_conn[i].valid = 0;
						continue;
					}
				}
			} else {
				/* Just find hongmi2A not do a2dp connect when connect from phone.
				 * if need add this active connect ???
				 */
				if (p_connect->monitor_conn[i].a2dp_times) {
					p_connect->monitor_conn[i].a2dp_times--;
					if (p_connect->monitor_conn[i].a2dp_times == 0) {
						SYS_LOG_INF("Phone not do a2dp connect");
						btsrv_a2dp_connect(conn, BT_A2DP_CH_SINK);
					}
				}
			}

			stop_timer = 0;
		}
	}

	p_connect->monitor_timer_running = 0;
	if (stop_timer) {
		btsrv_connect_monitor_profile_stop();
	}
}

static void btsrv_add_monitor(struct bt_conn *conn)
{
	int i, index = BTSRV_SAVE_AUTOCONN_NUM;
	bd_address_t *addr = (bd_address_t *)GET_CONN_BT_ADDR(conn);

	if (btsrv_is_pts_test()) {
		return;
	}

	for (i = 0; i < BTSRV_SAVE_AUTOCONN_NUM; i++) {
		if (p_connect->auto_conn[i].addr_valid &&
			!memcmp(p_connect->auto_conn[i].addr.val, addr->val, sizeof(bd_address_t))) {
			/* Reconnect device, not need monitor profile connect */
			return;
		}
	}

	for (i = 0; i < BTSRV_SAVE_AUTOCONN_NUM; i++) {
		if (!p_connect->monitor_conn[i].valid && index == BTSRV_SAVE_AUTOCONN_NUM) {
			index = i;
		}

		if (p_connect->monitor_conn[i].valid &&
			!memcmp(p_connect->monitor_conn[i].addr.val, addr->val, sizeof(bd_address_t))) {
			/* Already in monitor */
			return;
		}
	}

	if (index == BTSRV_SAVE_AUTOCONN_NUM) {
		return;
	}

	SYS_LOG_INF("Add profile monitor");

	memcpy(p_connect->monitor_conn[index].addr.val, addr->val, sizeof(bd_address_t));
	p_connect->monitor_conn[index].valid = 1;
	p_connect->monitor_conn[index].a2dp_times = MONITOR_A2DP_TIMES;
	p_connect->monitor_conn[index].avrcp_times = MONITOR_AVRCP_TIMES;
	btsrv_connect_monitor_profile_start(MONITOR_PRIFILE_INTERVEL, MONITOR_PRIFILE_INTERVEL);
}

static void btsrv_update_monitor(u8_t *addr, u8_t type)
{
	int i, index = BTSRV_SAVE_AUTOCONN_NUM;

	for (i = 0; i < BTSRV_SAVE_AUTOCONN_NUM; i++) {
		if (p_connect->monitor_conn[i].valid &&
			!memcmp(p_connect->monitor_conn[i].addr.val, addr, sizeof(bd_address_t))) {
			index = i;
			break;
		}
	}

	if (index == BTSRV_SAVE_AUTOCONN_NUM) {
		return;
	}

	/* Current only monitor avrcp */
	switch (type) {
	case BTSRV_LINK_AVRCP_CONNECTED:
	case BTSRV_LINK_BASE_DISCONNECTED:
		p_connect->monitor_conn[index].valid = 0;
		break;
	}
}

static void btsrv_proc_link_change(u8_t *mac, u8_t type)
{
	u8_t need_update = 0;

	switch (type) {
	case BTSRV_LINK_BASE_GET_NAME:
	case BTSRV_LINK_BASE_CONNECTED_TIMEOUT:
		btsrv_update_autoconn_state(mac, type);
		break;

	case BTSRV_LINK_BASE_CONNECTED:
		btsrv_update_autoconn_state(mac, type);
		btsrv_scan_update_mode(true);
		break;

	case BTSRV_LINK_BASE_CONNECTED_FAILED:
	case BTSRV_LINK_BASE_DISCONNECTED:
	case BTSRV_LINK_HFP_DISCONNECTED:
	case BTSRV_LINK_A2DP_DISCONNECTED:
	case BTSRV_LINK_AVRCP_DISCONNECTED:
	case BTSRV_LINK_HID_DISCONNECTED:
		btsrv_rdm_remove_dev(mac);
		btsrv_update_autoconn_state(mac, type);
		btsrv_update_monitor(mac, type);
		btsrv_scan_update_mode(false);
		if ((type == BTSRV_LINK_HFP_DISCONNECTED) ||
			(type == BTSRV_LINK_A2DP_DISCONNECTED)||
			(type == BTSRV_LINK_HID_DISCONNECTED)) {
			need_update = 1;
		}
		break;

	case BTSRV_LINK_HFP_CONNECTED:
	case BTSRV_LINK_A2DP_CONNECTED:
	case BTSRV_LINK_AVRCP_CONNECTED:
	case BTSRV_LINK_HID_CONNECTED:
		btsrv_update_autoconn_state(mac, type);
		btsrv_update_monitor(mac, type);
		need_update = 1;
		break;

	case BTSRV_LINK_SPP_DISCONNECTED:
	case BTSRV_LINK_PBAP_DISCONNECTED:
		btsrv_rdm_remove_dev(mac);
		btsrv_scan_update_mode(false);
		break;

	case BTSRV_LINK_SPP_CONNECTED:
	case BTSRV_LINK_PBAP_CONNECTED:
	default:
		break;
	}

	if (need_update) {
		btsrv_autoconn_info_update();
	}

	btsrv_update_performance_req();
}

static void btsrv_notify_link_event(struct bt_conn *base_conn, u8_t event, u8_t param)
{
	struct bt_link_cb_param cb_param;

	memset(&cb_param, 0, sizeof(struct bt_link_cb_param));
	cb_param.link_event = event;
	cb_param.addr = (bd_address_t *)GET_CONN_BT_ADDR(base_conn);

	switch (event) {
	case BT_LINK_EV_GET_NAME:
		cb_param.name = btsrv_rdm_get_dev_name(base_conn);
		cb_param.is_tws = param ? 1 : 0;
		break;
	case BT_LINK_EV_ACL_DISCONNECTED:
		cb_param.reason = param;
		break;
	default:
		break;
	}

	btsrv_adapter_callback(BTSRV_LINK_EVENT, &cb_param);
}

static int btsrv_connect_connected(struct bt_conn *base_conn)
{
	int ret = 0;
	bd_address_t *addr = (bd_address_t *)GET_CONN_BT_ADDR(base_conn);

	ret = btsrv_rdm_add_dev(base_conn);
	if (ret < 0) {
		SYS_LOG_ERR("Should not run to here!!!!");
		return ret;
	}

	btsrv_proc_link_change(addr->val, BTSRV_LINK_BASE_CONNECTED);
	btsrv_notify_link_event(base_conn, BT_LINK_EV_ACL_CONNECTED, 0);
	return ret;
}

static void btsrv_connect_security_changed(struct bt_conn *base_conn)
{
	if (btsrv_rdm_get_conn_role(base_conn) == BTSRV_TWS_NONE &&
		!btsrv_rdm_is_security_changed(base_conn)) {
		btsrv_rdm_set_security_changed(base_conn);
		btsrv_add_monitor(base_conn);
	}
}

static int btsrv_connect_disconnected(struct bt_conn *base_conn, u8_t reason)
{
	int role;
	struct bt_disconnect_reason bt_disparam;
	bd_address_t *addr = (bd_address_t *)GET_CONN_BT_ADDR(base_conn);

	role = btsrv_rdm_get_conn_role(base_conn);
	if (role == BTSRV_TWS_NONE) {
		/* Connected, but still not change to tws pending or tws role,
		 * receive disconnect, need notify tws exit connecting state.
		 */
		btsrv_event_notify_malloc(MSG_BTSRV_TWS, MSG_BTSRV_TWS_DISCONNECTED_ADDR, (u8_t *)addr, sizeof(bd_address_t), 0);
	} else if (role > BTSRV_TWS_NONE) {
		btsrv_event_notify_ext(MSG_BTSRV_TWS, MSG_BTSRV_TWS_DISCONNECTED, base_conn, reason);
	}

	memcpy(&bt_disparam.addr, addr, sizeof(bd_address_t));
	bt_disparam.reason = reason;
	bt_disparam.tws_role = role;
	btsrv_event_notify_malloc(MSG_BTSRV_BASE, MSG_BTSRV_DISCONNECTED_REASON, (u8_t *)&bt_disparam, sizeof(bt_disparam), 0);

	btsrv_notify_link_event(base_conn, BT_LINK_EV_ACL_DISCONNECTED, reason);
	btsrv_rdm_base_disconnected(base_conn);
	btsrv_proc_link_change(addr->val, BTSRV_LINK_BASE_DISCONNECTED);

	if (p_connect->clear_list_disconnecting) {
		if (btsrv_rdm_get_connected_dev_cnt_by_type(p_connect->clear_list_mode) == 0) {
			p_connect->clear_list_disconnecting = 0;
			btsrv_event_notify(MSG_BTSRV_CONNECT, MGS_BTSRV_CLEAR_AUTO_INFO, NULL);
			SYS_LOG_INF("clear list finish %d",p_connect->clear_list_mode);
		}
	}

	return 0;
}

/* Be careful, it can work well when connect avrcp conflict with phone ??? */
static void btsrv_connect_quick_connect_avrcp(struct bt_conn *base_conn)
{
	int i;
	bd_address_t *addr = (bd_address_t *)GET_CONN_BT_ADDR(base_conn);

	if (btsrv_rdm_is_avrcp_connected(base_conn)) {
		return;
	}

	for (i = 0; i < BTSRV_SAVE_AUTOCONN_NUM; i++) {
		if (p_connect->auto_conn[i].addr_valid &&
			!memcmp(p_connect->auto_conn[i].addr.val, addr->val, sizeof(bd_address_t))) {
			/* Reconnect device, reconnect will process avrcp connect */
			return;
		}
	}

	SYS_LOG_INF("quick connect avrcp\n");
	btsrv_avrcp_connect(base_conn);
}

static int btsrv_connect_a2dp_connected(struct bt_conn *base_conn)
{
	bd_address_t *addr = (bd_address_t *)GET_CONN_BT_ADDR(base_conn);

	btsrv_rdm_set_a2dp_connected(base_conn, true);
	btsrv_notify_link_event(base_conn, BT_LINK_EV_A2DP_CONNECTED, 0);
	btsrv_proc_link_change(addr->val, BTSRV_LINK_A2DP_CONNECTED);

	/* Same phone not connect avrcp when close/open bluetooth media */
	if (btsrv_rdm_get_conn_role(base_conn) == BTSRV_TWS_NONE) {
		btsrv_connect_quick_connect_avrcp(base_conn);
		btsrv_add_monitor(base_conn);
	}
	return 0;
}

static int btsrv_connect_a2dp_disconnected(struct bt_conn *base_conn, bool need_re_connect)
{
	bd_address_t *addr = (bd_address_t *)GET_CONN_BT_ADDR(base_conn);

	btsrv_rdm_a2dp_actived(base_conn, 0);
	btsrv_rdm_set_a2dp_connected(base_conn, false);
	btsrv_notify_link_event(base_conn, BT_LINK_EV_A2DP_DISCONNECTED, 0);
	btsrv_proc_link_change(addr->val, BTSRV_LINK_A2DP_DISCONNECTED);

	if (need_re_connect) {
		/* Wait todo: reconnect profile, if phone active disconnect, why need reconnect? */
	}
	return 0;
}

static int btsrv_connect_avrcp_connected(struct bt_conn *base_conn)
{
	bd_address_t *addr = (bd_address_t *)GET_CONN_BT_ADDR(base_conn);

	btsrv_rdm_set_avrcp_connected(base_conn, true);
	btsrv_notify_link_event(base_conn, BT_LINK_EV_AVRCP_CONNECTED, 0);
	btsrv_proc_link_change(addr->val, BTSRV_LINK_AVRCP_CONNECTED);
	return 0;
}

static int btsrv_connect_avrcp_disconnected(struct bt_conn *base_conn, bool need_re_connect)
{
	bd_address_t *addr = (bd_address_t *)GET_CONN_BT_ADDR(base_conn);

	btsrv_rdm_set_avrcp_connected(base_conn, false);
	btsrv_notify_link_event(base_conn, BT_LINK_EV_AVRCP_DISCONNECTED, 0);
	btsrv_proc_link_change(addr->val, BTSRV_LINK_AVRCP_DISCONNECTED);

	if (need_re_connect) {
		/* Wait todo: reconnect profile, if phone active disconnect, why need reconnect? */
	}

	return 0;
}

static int btsrv_connect_hfp_connected(struct bt_conn *base_conn)
{
	bd_address_t *addr = (bd_address_t *)GET_CONN_BT_ADDR(base_conn);

	btsrv_rdm_set_hfp_connected(base_conn, true);
	btsrv_notify_link_event(base_conn, BT_LINK_EV_HF_CONNECTED, 0);
	btsrv_proc_link_change(addr->val, BTSRV_LINK_HFP_CONNECTED);
	return 0;
}

static int btsrv_connect_hfp_disconnected(struct bt_conn *base_conn, bool need_re_connect)
{
	bd_address_t *addr = (bd_address_t *)GET_CONN_BT_ADDR(base_conn);

	btsrv_rdm_set_hfp_connected(base_conn, false);
	btsrv_notify_link_event(base_conn, BT_LINK_EV_HF_DISCONNECTED, 0);
	btsrv_proc_link_change(addr->val, BTSRV_LINK_HFP_DISCONNECTED);

	if (need_re_connect) {
		/* Wait todo: reconnect profile, if phone active disconnect, why need reconnect? */
	}

	return 0;
}

static int btsrv_connect_spp_connected(struct bt_conn *base_conn)
{
	bd_address_t *addr = (bd_address_t *)GET_CONN_BT_ADDR(base_conn);

	btsrv_rdm_set_spp_connected(base_conn, true);
	btsrv_notify_link_event(base_conn, BT_LINK_EV_SPP_CONNECTED, 0);
	btsrv_proc_link_change(addr->val, BTSRV_LINK_SPP_CONNECTED);
	return 0;
}

static int btsrv_connect_spp_disconnected(struct bt_conn *base_conn)
{
	bd_address_t *addr = (bd_address_t *)GET_CONN_BT_ADDR(base_conn);

	btsrv_rdm_set_spp_connected(base_conn, false);
	btsrv_notify_link_event(base_conn, BT_LINK_EV_SPP_DISCONNECTED, 0);
	btsrv_proc_link_change(addr->val, BTSRV_LINK_SPP_DISCONNECTED);
	return 0;
}

static int btsrv_connect_pbap_connected(struct bt_conn *base_conn)
{
	bd_address_t *addr = (bd_address_t *)GET_CONN_BT_ADDR(base_conn);

	btsrv_rdm_set_pbap_connected(base_conn, true);
	btsrv_proc_link_change(addr->val, BTSRV_LINK_PBAP_CONNECTED);
	return 0;
}

static int btsrv_connect_pbap_disconnected(struct bt_conn *base_conn)
{
	bd_address_t *addr = (bd_address_t *)GET_CONN_BT_ADDR(base_conn);

	btsrv_rdm_set_pbap_connected(base_conn, false);
	btsrv_proc_link_change(addr->val, BTSRV_LINK_PBAP_DISCONNECTED);
	return 0;
}

static int btsrv_connect_map_connected(struct bt_conn *base_conn)
{
	bd_address_t *addr = (bd_address_t *)GET_CONN_BT_ADDR(base_conn);

	btsrv_rdm_set_map_connected(base_conn, true);
	btsrv_proc_link_change(addr->val, BTSRV_LINK_MAP_CONNECTED);
	return 0;
}

static int btsrv_connect_map_disconnected(struct bt_conn *base_conn)
{
	bd_address_t *addr = (bd_address_t *)GET_CONN_BT_ADDR(base_conn);

	btsrv_rdm_set_map_connected(base_conn, false);
	btsrv_proc_link_change(addr->val, BTSRV_LINK_MAP_DISCONNECTED);
	return 0;
}

static int btsrv_connect_hid_connected(struct bt_conn *base_conn)
{
	bd_address_t *addr = (bd_address_t *)GET_CONN_BT_ADDR(base_conn);

	btsrv_rdm_set_hid_connected(base_conn, true);
	btsrv_notify_link_event(base_conn, BT_LINK_EV_HID_CONNECTED, 0);
	btsrv_proc_link_change(addr->val, BTSRV_LINK_HID_CONNECTED);
	return 0;
}

static int btsrv_connect_hid_disconnected(struct bt_conn *base_conn)
{
	bd_address_t *addr = (bd_address_t *)GET_CONN_BT_ADDR(base_conn);

	btsrv_rdm_set_hid_connected(base_conn, false);
	btsrv_notify_link_event(base_conn, BT_LINK_EV_HID_DISCONNECTED, 0);
	btsrv_proc_link_change(addr->val, BTSRV_LINK_HID_DISCONNECTED);
	return 0;
}

static int btsrv_connect_hid_unplug(struct bt_conn *base_conn)
{
	int i;
	struct autoconn_info *tmpInfo;
	bd_address_t *addr = (bd_address_t *)GET_CONN_BT_ADDR(base_conn);
	
	tmpInfo = mem_malloc(sizeof(struct autoconn_info)*BTSRV_SAVE_AUTOCONN_NUM);
	if (!tmpInfo) {
		SYS_LOG_ERR("malloc failed");
		return -ENOMEM;
	}
	
	btsrv_connect_get_auto_reconnect_info(tmpInfo, BTSRV_SAVE_AUTOCONN_NUM);
	for (i = 0; i < BTSRV_SAVE_AUTOCONN_NUM; i++) {
		if (tmpInfo[i].addr_valid) {
			if (!memcmp(tmpInfo[i].addr.val, addr->val, sizeof(bd_address_t))) {
				if(tmpInfo[i].hid && !tmpInfo[i].a2dp && !tmpInfo[i].hfp
					&& !tmpInfo[i].avrcp){
					tmpInfo[i].addr_valid = 0;
					SYS_LOG_INF("clear info");
				}
				break;
			}
		}
	}

	btsrv_connect_set_auto_reconnect_info(tmpInfo,BTSRV_SAVE_AUTOCONN_NUM);
	if (tmpInfo) {
		mem_free(tmpInfo);
	}
	return 0;
}

static void btsrv_controler_role_discovery(struct bt_conn *conn)
{
	u8_t role;

	if (hostif_bt_conn_role_discovery(conn, &role)) {
		SYS_LOG_ERR("Failed to discovery role");
		return;
	}

	btsrv_rdm_set_controler_role(conn, role);

	if (role == CONTROLER_ROLE_MASTER) {
		hostif_bt_conn_set_supervision_timeout(conn, BT_SUPERVISION_TIMEOUT);
	}
}

void btsrv_connect_set_phone_controler_role(bd_address_t *bd, u8_t role)
{
	struct bt_conn *conn = btsrv_rdm_find_conn_by_addr(bd);
	u8_t dev_role, dev_exp_role;

	if (conn == NULL) {
		return;
	}

	dev_exp_role = (role == CONTROLER_ROLE_MASTER) ? CONTROLER_ROLE_SLAVE : CONTROLER_ROLE_MASTER;
	btsrv_rdm_get_controler_role(conn, &dev_role);

	if ((btsrv_rdm_get_conn_role(conn) == BTSRV_TWS_NONE) && (dev_role != dev_exp_role)) {
		/* Controler request do role swith after security */
		SYS_LOG_INF("Do role(%d) switch", dev_exp_role);
		hostif_bt_conn_switch_role(conn, dev_exp_role);
	}
}

static bool btsrv_check_connectable(u8_t role)
{
	bool connectable = true;
	u8_t dev_role = btsrv_rdm_get_dev_role();
	u8_t connected_cnt = btsrv_rdm_get_connected_dev(NULL, NULL);

	SYS_LOG_INF("check_connectable:%d,%d,%d,%d,%d",
		role, dev_role, connected_cnt, btsrv_max_conn_num(), btsrv_max_phone_num());

	if (connected_cnt > btsrv_max_conn_num()) {
		/* Already connect max conn */
		connectable = false;
	} else if ((dev_role == BTSRV_TWS_NONE) && (role == BTSRV_TWS_NONE) &&
				(connected_cnt > btsrv_max_phone_num())) {
		/* Already connect max phone number, can't connect any phone */
		connectable = false;
	} else if (dev_role != BTSRV_TWS_NONE && role != BTSRV_TWS_NONE) {
		/* Already connect as tws device, Can't connect another tws device */
		connectable = false;
	}

	return connectable;
}

static void btsrv_get_name_finish(void *info, u8_t role)
{
	struct bt_conn *conn;
	struct btsrv_addr_name mac_addr_info;

	memcpy(&mac_addr_info, info, sizeof(mac_addr_info));

	conn = btsrv_rdm_find_conn_by_addr(&mac_addr_info.mac);
	if (conn == NULL) {
		SYS_LOG_ERR("Can't find conn for addr");
		return;
	}

	if (!btsrv_check_connectable(role)) {
		SYS_LOG_INF("Disconnect role:%d", role);
		btsrv_adapter_disconnect(conn);
		return;
	}

	btsrv_rdm_set_dev_name(conn, mac_addr_info.name);
	btsrv_notify_link_event(conn, BT_LINK_EV_GET_NAME, role);
	btsrv_proc_link_change(mac_addr_info.mac.val, BTSRV_LINK_BASE_GET_NAME);
	if (role != BTSRV_TWS_NONE) {
		btsrv_event_notify(MSG_BTSRV_TWS, MSG_BTSRV_GET_NAME_FINISH, conn);
	}
}

static void connected_dev_cb_do_disconnect(struct bt_conn *base_conn, u8_t tws_dev, void *cb_param)
{
	int disconnect_mode = (int)cb_param;

	if (disconnect_mode == BTSRV_DISCONNECT_ALL_MODE) {
		if (tws_dev) {
			btsrv_event_notify_malloc(MSG_BTSRV_TWS, MSG_BTSRV_TWS_DISCONNECT, (u8_t *)GET_CONN_BT_ADDR(base_conn), sizeof(bd_address_t), 0);
		} else {
			btsrv_adapter_disconnect(base_conn);
		}
	} else if (disconnect_mode == BTSRV_DISCONNECT_PHONE_MODE) {
		if (!tws_dev) {
			btsrv_adapter_disconnect(base_conn);
		}
	} else if (disconnect_mode == BTSRV_DISCONNECT_TWS_MODE) {
		if (tws_dev) {
			btsrv_event_notify_malloc(MSG_BTSRV_TWS, MSG_BTSRV_TWS_DISCONNECT, (u8_t *)GET_CONN_BT_ADDR(base_conn), sizeof(bd_address_t), 0);
		}
	}
}

static void btsrv_connect_disconnect_device(int disconnect_mode)
{
	SYS_LOG_INF("disconnect mode %d", disconnect_mode);
	btsrv_rdm_get_connected_dev(connected_dev_cb_do_disconnect, (void *)disconnect_mode);
}

static void btsrv_connect_clear_list(int mode)
{
	int count;
	int disconnec_mode = 0;
	btsrv_connect_auto_connection_stop();
#ifdef CONFIG_SUPPORT_TWS
	btsrv_tws_cancal_auto_connect();
#endif

	if (mode == BTSRV_DEVICE_ALL) {
		disconnec_mode = BTSRV_DISCONNECT_ALL_MODE;
	} else if (mode == BTSRV_DEVICE_PHONE) {
		disconnec_mode = BTSRV_DISCONNECT_PHONE_MODE;
	} else if (mode == BTSRV_DEVICE_TWS) {
		disconnec_mode = BTSRV_DISCONNECT_TWS_MODE;
	}

	count = btsrv_rdm_get_connected_dev(connected_dev_cb_do_disconnect, (void *)disconnec_mode);
	if (count == 0) {
		btsrv_scan_update_mode(true);
		btsrv_event_notify(MSG_BTSRV_CONNECT, MGS_BTSRV_CLEAR_AUTO_INFO, NULL);
		SYS_LOG_INF("clear list finish");
	} else {
		p_connect->clear_list_disconnecting = 1;
		p_connect->clear_list_mode = mode;
	}
}

static void btsrv_auto_connect_proc(struct bt_set_autoconn *param)
{
	int i, idle_pos = BTSRV_SAVE_AUTOCONN_NUM, need_start_timer = 0;

	if (btsrv_rdm_find_conn_by_addr(&param->addr)) {
		SYS_LOG_INF("Already connected");
		return;
	}

	for (i = 0; i < BTSRV_SAVE_AUTOCONN_NUM; i++) {
		if (!p_connect->auto_conn[i].addr_valid &&
			idle_pos == BTSRV_SAVE_AUTOCONN_NUM) {
			idle_pos = i;
		}

		if (p_connect->auto_conn[i].addr_valid &&
			!memcmp(param->addr.val, p_connect->auto_conn[i].addr.val, sizeof(bd_address_t))) {
			SYS_LOG_INF("Device already in reconnect list");
			return;
		}
	}

	if (idle_pos == BTSRV_SAVE_AUTOCONN_NUM) {
		SYS_LOG_ERR("Not more position for reconnect device");
		return;
	}

	for (i = 0; i < BTSRV_SAVE_AUTOCONN_NUM; i++) {
		if (p_connect->nvram_reconn_info[i].addr_valid &&
			!memcmp(p_connect->nvram_reconn_info[i].addr.val, param->addr.val, sizeof(bd_address_t))) {
			memcpy(p_connect->auto_conn[idle_pos].addr.val, param->addr.val, sizeof(bd_address_t));
			p_connect->auto_conn[idle_pos].addr_valid = p_connect->nvram_reconn_info[i].addr_valid;
			p_connect->auto_conn[idle_pos].tws_role = p_connect->nvram_reconn_info[i].tws_role;
			p_connect->auto_conn[idle_pos].a2dp = p_connect->nvram_reconn_info[i].a2dp;
			p_connect->auto_conn[idle_pos].hfp = p_connect->nvram_reconn_info[i].hfp;
			p_connect->auto_conn[idle_pos].avrcp = p_connect->nvram_reconn_info[i].avrcp;
			p_connect->auto_conn[idle_pos].hid = p_connect->nvram_reconn_info[i].hid;
			p_connect->auto_conn[idle_pos].hfp_first = p_connect->nvram_reconn_info[i].hfp_first;
			p_connect->auto_conn[idle_pos].strategy = param->strategy;
			p_connect->auto_conn[idle_pos].base_try = param->base_try;
			p_connect->auto_conn[idle_pos].profile_try = param->profile_try;
			p_connect->auto_conn[idle_pos].curr_connect_profile = AUTOCONN_PROFILE_IDLE;
			p_connect->auto_conn[idle_pos].state = AUTOCONN_STATE_IDLE;
			p_connect->auto_conn[idle_pos].base_interval = param->base_interval;
			p_connect->auto_conn[idle_pos].profile_interval = param->profile_interval;
			need_start_timer = 1;
			break;
		}
	}

	if (i == BTSRV_SAVE_AUTOCONN_NUM) {
		SYS_LOG_ERR("Device not in AUTOCONN_INFO_NVRAM");
	}

	/* Only need start timer when is stop */
	if (need_start_timer && (!thread_timer_is_running(&p_connect->auto_conn_timer))) {
		p_connect->connecting_index = idle_pos;
		thread_timer_start(&p_connect->auto_conn_timer, AUTOCONN_START_TIME, 0);
		p_connect->reconnect_req_high_performance = 1;
		btsrv_update_performance_req();
	}
}

static void btsrv_connect_check_switch_sbc(void)
{
#ifdef CONFIG_SUPPORT_TWS
	u8_t codec = BT_A2DP_SBC;
	struct bt_conn *conn = btsrv_rdm_a2dp_get_actived();

	if ((btsrv_rdm_get_dev_role() != BTSRV_TWS_MASTER) ||
		btsrv_tws_check_support_feature(TWS_FEATURE_A2DP_AAC)) {
		return;
	}

	/* As tws master, tws can't use AAC */
	if (conn == NULL) {
		btsrv_a2dp_halt_aac_endpoint(true);
		return;
	}

	btsrv_rdm_a2dp_get_codec_info(conn, &codec, NULL, NULL);
	if (codec == BT_A2DP_MPEG2) {
		SYS_LOG_INF("Need switch to SBC");
		btsrv_rdm_set_switch_sbc_state(conn, SWITCH_SBC_STATE_DISCONNECTING_A2DP);
		if (btsrv_a2dp_disconnect(conn)) {
			btsrv_rdm_set_switch_sbc_state(conn, SWITCH_SBC_STATE_IDLE);
		}
	}
#endif
}

static void btsrv_connect_proc_switch_sbc_state(struct bt_conn *conn, u8_t cmd)
{
#ifdef CONFIG_SUPPORT_TWS
	if (btsrv_rdm_get_conn_role(conn) != BTSRV_TWS_NONE) {
		return;
	}

	switch (btsrv_rdm_get_switch_sbc_state(conn)) {
	case SWITCH_SBC_STATE_IDLE:
		break;
	case SWITCH_SBC_STATE_DISCONNECTING_A2DP:
		if (cmd == MSG_BTSRV_A2DP_DISCONNECTED) {
			if (btsrv_rdm_get_dev_role() == BTSRV_TWS_MASTER) {
				btsrv_a2dp_halt_aac_endpoint(true);
			}
			SYS_LOG_INF("Switch connect SBC");
			btsrv_rdm_set_switch_sbc_state(conn, SWITCH_SBC_STATE_CONNECTING_A2DP);
			btsrv_a2dp_connect(conn, BT_A2DP_CH_SINK);
		} else {
			SYS_LOG_WRN("Unexpect case!\n");
		}
		break;
	case SWITCH_SBC_STATE_CONNECTING_A2DP:
		SYS_LOG_INF("Switch SBC ok");
		btsrv_rdm_set_switch_sbc_state(conn, SWITCH_SBC_STATE_IDLE);
		break;
	}
#endif
}

#ifdef	CONFIG_WAIT_L2CAP_CONNECT_CONFRIM
#define BT_L2CAP_PSM_AVDTP 				0x0019
#define BT_L2CAP_PSM_AVCTP_CONTROL		0x0017
#define BT_L2CAP_PSM_RFCOMM				0x0003

void btsrv_connect_proc_conn_rsp_result(struct btsrv_conn_rsp_t *result)
{
	u8_t index = p_connect->connecting_index;
	struct auto_conn_t *auto_conn = &p_connect->auto_conn[index];
	struct bt_conn *conn;
	u8_t need_update;
	u32_t update_time;

	conn = btsrv_rdm_find_conn_by_addr(&auto_conn->addr);
	if (conn == NULL) {
		return;
	}

	if (conn != result->conn) {
		return;
	}

	if ((auto_conn->curr_connect_profile == AUTOCONN_PROFILE_HFP_CONNECTING) &&
		(result->psm == BT_L2CAP_PSM_RFCOMM)) {
		need_update = 1;
	} else if ((auto_conn->curr_connect_profile == AUTOCONN_PROFILE_A2DP_CONNECTING) &&
		(result->psm == BT_L2CAP_PSM_AVDTP)) {
		need_update = 1;
	} else if ((auto_conn->curr_connect_profile == AUTOCONN_PROFILE_AVRCP_CONNECTING) &&
		(result->psm == BT_L2CAP_PSM_AVCTP_CONTROL)) {
		need_update = 1;
	} else {
		need_update = 0;
	}

	if (need_update) {
		update_time = result->waittime ? result->waittime : auto_conn->profile_interval;
		SYS_LOG_INF("conn_rsp_result 0x%x, %d", result->psm, update_time);
		if (update_time == 0xFFFFFFFF) {
			if (auto_conn->curr_connect_profile == AUTOCONN_PROFILE_HFP_CONNECTING) {
				auto_conn->hfp = 0;
			} else if (auto_conn->curr_connect_profile == AUTOCONN_PROFILE_A2DP_CONNECTING) {
				auto_conn->a2dp = 0;
				auto_conn->avrcp = 0;
			} else if (auto_conn->curr_connect_profile == AUTOCONN_PROFILE_AVRCP_CONNECTING) {
				auto_conn->avrcp = 0;
			}
			btsrv_connect_auto_connection_restart(update_time, auto_conn->profile_interval);
		} else {
			btsrv_connect_auto_connection_restart(update_time, 0);
		}
	}
}
#endif

int btsrv_connect_process(struct app_msg *msg)
{
	int ret = 0;

	switch (_btsrv_get_msg_param_cmd(msg)) {
	case MSG_BTSRV_CONNECTED:
		SYS_LOG_INF("MSG_BTSRV_CONNECTED");
		btsrv_connect_connected(msg->ptr);
		btsrv_controler_role_discovery(msg->ptr);
		btsrv_adapter_callback(BTSRV_RELEASE_HIGH_PERFORMANCE, NULL);
		break;
	case MSG_BTSRV_CONNECTED_FAILED:
		SYS_LOG_INF("MSG_BTSRV_CONNECTED_FAILED");
		btsrv_proc_link_change(msg->ptr, BTSRV_LINK_BASE_CONNECTED_FAILED);
		btsrv_event_notify_malloc(MSG_BTSRV_TWS, _btsrv_get_msg_param_cmd(msg), msg->ptr, sizeof(bd_address_t), 0);
		break;
	case MSG_BTSRV_SECURITY_CHANGED:
		btsrv_connect_security_changed(msg->ptr);
		break;
	case MSG_BTSRV_ROLE_CHANGE:
		btsrv_rdm_set_controler_role(msg->ptr, _btsrv_get_msg_param_reserve(msg));
		if (_btsrv_get_msg_param_reserve(msg) == CONTROLER_ROLE_MASTER) {
			hostif_bt_conn_set_supervision_timeout(msg->ptr, BT_SUPERVISION_TIMEOUT);
		}
		btsrv_event_notify_ext(MSG_BTSRV_TWS, MSG_BTSRV_ROLE_CHANGE, msg->ptr, _btsrv_get_msg_param_reserve(msg));
		break;
	case MSG_BTSRV_DISCONNECTED:
		SYS_LOG_INF("MSG_BTSRV_DISCONNECTED");
		btsrv_connect_disconnected(msg->ptr, _btsrv_get_msg_param_reserve(msg));
		break;
	case MSG_BTSRV_A2DP_CONNECTED:
		SYS_LOG_INF("MSG_BTSRV_A2DP_CONNECTED");
		btsrv_connect_a2dp_connected(msg->ptr);
		if (btsrv_rdm_get_conn_role(msg->ptr) >= BTSRV_TWS_PENDING) {
			btsrv_event_notify(MSG_BTSRV_TWS, _btsrv_get_msg_param_cmd(msg), msg->ptr);
		} else {
			/* Phone a2dp may in connecting when receive MSG_BTSRV_A2DP_CHECK_SWITCH_SBC
			 * check agine when phone a2dp connected.
			 */
			btsrv_connect_check_switch_sbc();
		}

		btsrv_event_notify(MSG_BTSRV_A2DP, _btsrv_get_msg_param_cmd(msg), msg->ptr);
		btsrv_connect_proc_switch_sbc_state(msg->ptr, MSG_BTSRV_A2DP_CONNECTED);
		break;
	case MSG_BTSRV_A2DP_DISCONNECTED:
		SYS_LOG_INF("MSG_BTSRV_A2DP_DISCONNECTED");
		if (btsrv_rdm_get_conn_role(msg->ptr) >= BTSRV_TWS_PENDING) {
			btsrv_event_notify(MSG_BTSRV_TWS, _btsrv_get_msg_param_cmd(msg), msg->ptr);
			btsrv_a2dp_halt_aac_endpoint(false);
		}

		btsrv_connect_a2dp_disconnected(msg->ptr, true);
		btsrv_connect_proc_switch_sbc_state(msg->ptr, MSG_BTSRV_A2DP_DISCONNECTED);
		break;
	case MSG_BTSRV_AVRCP_CONNECTED:
		SYS_LOG_INF("MSG_BTSRV_AVRCP_CONNECTED");
		btsrv_connect_avrcp_connected(msg->ptr);
		btsrv_event_notify(MSG_BTSRV_AVRCP, _btsrv_get_msg_param_cmd(msg), msg->ptr);
		break;
	case MSG_BTSRV_AVRCP_DISCONNECTED:
		SYS_LOG_INF("MSG_BTSRV_AVRCP_DISCONNECTED");
		btsrv_connect_avrcp_disconnected(msg->ptr, true);
		btsrv_event_notify(MSG_BTSRV_AVRCP, _btsrv_get_msg_param_cmd(msg), msg->ptr);
		break;
	case MSG_BTSRV_HID_CONNECTED:
		SYS_LOG_INF("MSG_BTSRV_HID_CONNECTED");
		btsrv_connect_hid_connected(msg->ptr);
		btsrv_event_notify(MSG_BTSRV_HID, _btsrv_get_msg_param_cmd(msg), msg->ptr);
		break;
	case MSG_BTSRV_HID_DISCONNECTED:
		SYS_LOG_INF("MSG_BTSRV_HID_DISCONNECTED");
		btsrv_connect_hid_disconnected(msg->ptr);
		btsrv_event_notify(MSG_BTSRV_HID, _btsrv_get_msg_param_cmd(msg), msg->ptr);
		break;
	case MSG_BTSRV_HID_UNPLUG:
		SYS_LOG_INF("MSG_BTSRV_HID_UNPLUG");
		btsrv_connect_hid_unplug(msg->ptr);
		break;
	case MSG_BTSRV_HFP_AG_CONNECTED:
		SYS_LOG_INF("MSG_BTSRV_HFP_AG_CONNECTED");
		btsrv_rdm_set_hfp_role(msg->ptr,BTSRV_HFP_ROLE_AG);
		btsrv_connect_hfp_connected(msg->ptr);
		if (btsrv_rdm_get_conn_role(msg->ptr) >= BTSRV_TWS_PENDING) {
			btsrv_event_notify(MSG_BTSRV_TWS, _btsrv_get_msg_param_cmd(msg), msg->ptr);
		}
		btsrv_event_notify(MSG_BTSRV_HFP_AG, _btsrv_get_msg_param_cmd(msg), msg->ptr);
		break;
	case MSG_BTSRV_HFP_CONNECTED:
		SYS_LOG_INF("MSG_BTSRV_HFP_CONNECTED");
		btsrv_rdm_set_hfp_role(msg->ptr,BTSRV_HFP_ROLE_HF);
		btsrv_connect_hfp_connected(msg->ptr);
		if (btsrv_rdm_get_conn_role(msg->ptr) >= BTSRV_TWS_PENDING) {
			btsrv_event_notify(MSG_BTSRV_TWS, _btsrv_get_msg_param_cmd(msg), msg->ptr);
		}
		btsrv_event_notify(MSG_BTSRV_HFP, _btsrv_get_msg_param_cmd(msg), msg->ptr);
		break;
	case MSG_BTSRV_HFP_AG_DISCONNECTED:
		btsrv_rdm_set_hfp_role(msg->ptr,BTSRV_HFP_ROLE_HF);
	case MSG_BTSRV_HFP_DISCONNECTED:
		SYS_LOG_INF("MSG_BTSRV_HFP_DISCONNECTED");
		/* Why need check btsrv_rdm_is_hfp_connected ?? */
		if (btsrv_rdm_is_hfp_connected(msg->ptr)) {
			btsrv_connect_hfp_disconnected(msg->ptr, true);
		}
		if (btsrv_rdm_get_conn_role(msg->ptr) >= BTSRV_TWS_PENDING) {
			btsrv_event_notify(MSG_BTSRV_TWS, _btsrv_get_msg_param_cmd(msg), msg->ptr);
		}
		break;
	case MSG_BTSRV_SPP_CONNECTED:
		btsrv_connect_spp_connected(msg->ptr);
		break;
	case MSG_BTSRV_SPP_DISCONNECTED:
		btsrv_connect_spp_disconnected(msg->ptr);
		break;
	case MSG_BTSRV_PBAP_CONNECTED:
		btsrv_connect_pbap_connected(msg->ptr);
		break;
	case MSG_BTSRV_PBAP_DISCONNECTED:
		btsrv_connect_pbap_disconnected(msg->ptr);
		break;
	case MSG_BTSRV_MAP_CONNECTED:
		btsrv_connect_map_connected(msg->ptr);
		break;
	case MSG_BTSRV_MAP_DISCONNECTED:
		btsrv_connect_map_disconnected(msg->ptr);
		break;
	case MSG_BTSRV_GET_NAME_FINISH:
		btsrv_get_name_finish(msg->ptr, _btsrv_get_msg_param_reserve(msg));
		break;
	case MSG_BTSRV_CLEAR_LIST_CMD:
		btsrv_connect_clear_list((int)(_btsrv_get_msg_param_ptr(msg)));
		break;
	case MGS_BTSRV_CLEAR_AUTO_INFO:
		btsrv_autoconn_info_clear();
		break;
	case MSG_BTSRV_AUTO_RECONNECT:
		btsrv_auto_connect_proc(msg->ptr);
		break;
	case MSG_BTSRV_AUTO_RECONNECT_STOP:
		btsrv_connect_auto_connection_stop();
#ifdef CONFIG_SUPPORT_TWS
		btsrv_tws_cancal_auto_connect();
#endif
		break;
	case MSG_BTSRV_DISCONNECT_DEVICE:
		btsrv_connect_disconnect_device((int)(_btsrv_get_msg_param_ptr(msg)));
		break;
	case MSG_BTSRV_A2DP_CHECK_SWITCH_SBC:
		btsrv_connect_check_switch_sbc();
		break;
#ifdef	CONFIG_WAIT_L2CAP_CONNECT_CONFRIM
	case MSG_BTSRV_CONN_RSP_RESULT:
		btsrv_connect_proc_conn_rsp_result(msg->ptr);
		break;
#endif
	default:
		break;
	}

	return ret;
}

bool btsrv_autoconn_is_reconnecting(void)
{
	u8_t index = p_connect->connecting_index;

	if (!p_connect->auto_conn[index].addr_valid) {
		return false;
	}

	return (p_connect->auto_conn[index].state == AUTOCONN_STATE_IDLE ||
			p_connect->auto_conn[index].state == AUTOCONN_STATE_DELAY_CONNECTING) ? false : true;
}

bool btsrv_autoconn_is_runing(void)
{
	return p_connect->reconnect_req_high_performance ? true : false;
}

void btsrv_connect_tws_role_confirm(void)
{
	/* Already as tws slave, stop autoconnect to phone  and disconnect connected phone */
	if (btsrv_rdm_get_dev_role() == BTSRV_TWS_SLAVE) {
		btsrv_connect_auto_connection_stop();
		btsrv_connect_disconnect_device(BTSRV_DISCONNECT_PHONE_MODE);
	}
}

int btsrv_connect_init(void)
{
	p_connect = &p_btsrv_connect;

	memset(p_connect, 0, sizeof(struct btsrv_connect_priv));
	thread_timer_init(&p_connect->auto_conn_timer, btsrv_autoconn_timer_handler, NULL);
	thread_timer_init(&p_connect->monitor_conn_timer, btsrv_monitor_timer_handler, NULL);

	btsrv_property_get(CFG_AUTOCONN_INFO, (char *)p_connect->nvram_reconn_info,
						(sizeof(p_connect->nvram_reconn_info)));
	return 0;
}

void btsrv_connect_deinit(void)
{
	if (p_connect == NULL) {
		return;
	}

	while (p_connect->auto_connect_running) {
		os_sleep(10);
	}

	if (thread_timer_is_running(&p_connect->auto_conn_timer)) {
		thread_timer_stop(&p_connect->auto_conn_timer);
		p_connect->reconnect_req_high_performance = 0;
		btsrv_update_performance_req();
	}

	while (p_connect->monitor_timer_running) {
		os_sleep(10);
	}

	if (thread_timer_is_running(&p_connect->monitor_conn_timer)) {
		thread_timer_stop(&p_connect->monitor_conn_timer);
	}

	p_connect = NULL;
}

void btsrv_connect_dump_info(void)
{
	char addr_str[BT_ADDR_STR_LEN];
	int i;
	struct auto_conn_t *auto_conn;
	struct autoconn_info *info;

	if (p_connect == NULL) {
		SYS_LOG_INF("Auto reconnect not init");
		return;
	}

	SYS_LOG_INF("index: %d\n", p_connect->connecting_index);
	for (i = 0; i < BTSRV_SAVE_AUTOCONN_NUM; i++) {
		auto_conn = &p_connect->auto_conn[i];
		if (auto_conn->addr_valid) {
			hostif_bt_addr_to_str((const bt_addr_t *)&auto_conn->addr, addr_str, BT_ADDR_STR_LEN);
			printk("Dev index %d, state %d, mac: %s\n", i, auto_conn->state, addr_str);
			printk("a2dp %d, avrcp %d, hfp %d hid %d\n", auto_conn->a2dp, auto_conn->avrcp, auto_conn->hfp, auto_conn->hid);
			printk("tws_role %d, strategy %d\n", auto_conn->tws_role, auto_conn->strategy);
			printk("base_try %d, profile_try %d\n", auto_conn->base_try, auto_conn->profile_try);
			printk("base_interval %d, profile_interval %d\n", auto_conn->base_interval, auto_conn->profile_interval);
			printk("\n");
		}
	}

	printk("nvram_reconn_info\n");
	for (i = 0; i < BTSRV_SAVE_AUTOCONN_NUM; i++) {
		info = &p_connect->nvram_reconn_info[i];
		if (info->addr_valid) {
			hostif_bt_addr_to_str((const bt_addr_t *)&info->addr, addr_str, BT_ADDR_STR_LEN);
			printk("(%d)%s tws_role %d, profile %d, %d, %d, %d, %d\n", i, addr_str, info->tws_role,
					info->hfp, info->a2dp, info->avrcp, info->hid, info->hfp_first);
		}
	}
	printk("\n");
}
