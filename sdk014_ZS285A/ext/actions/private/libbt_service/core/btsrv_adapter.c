/*
 * Copyright (c) 2016 Actions Semi Co., Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief bt service adapter interface
 */
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <zephyr.h>
#include <arch/cpu.h>
#include <misc/byteorder.h>
#include <logging/sys_log.h>
#include <misc/util.h>

#include <device.h>
#include <init.h>
#include <uart.h>

#include <bluetooth/host_interface.h>

#include <mem_manager.h>
#include <property_manager.h>

#include "btsrv_inner.h"

#define CHECK_WAIT_DISCONNECT_INTERVAL		(100)	/* 100ms */

struct btsrv_info_t *btsrv_info;
static struct btsrv_info_t btsrv_btinfo;
static btsrv_discover_result_cb btsrv_discover_cb;

static u8_t btsrv_adapter_check_role(bt_addr_t *addr, u8_t *name)
{
	int i;
	u8_t role = BTSRV_TWS_NONE;
	struct autoconn_info info[BTSRV_SAVE_AUTOCONN_NUM];
	struct btsrv_check_device_role_s param;

	memset(info, 0, sizeof(info));
	btsrv_connect_get_auto_reconnect_info(info, BTSRV_SAVE_AUTOCONN_NUM);
	for (i = 0; i < BTSRV_SAVE_AUTOCONN_NUM; i++) {
		if (info[i].addr_valid && !memcmp(&info[i].addr, addr, sizeof(bd_address_t))) {
			role = (info[i].tws_role == BTSRV_TWS_NONE) ? BTSRV_TWS_NONE : BTSRV_TWS_PENDING;
			break;
		}
	}

	if ((i == BTSRV_SAVE_AUTOCONN_NUM) || (role == BTSRV_TWS_NONE)) {
		memcpy(&param.addr, addr, sizeof(bd_address_t));
		param.len = strlen(name);
		param.name = name;
		if (btsrv_adapter_callback(BTSRV_CHECK_NEW_DEVICE_ROLE, (void *)&param)) {
			role = BTSRV_TWS_PENDING;
		}
	}

	return role;
}

static void _btsrv_adapter_connected_remote_name_cb(bt_addr_t *addr, u8_t *name)
{
	u8_t role = BTSRV_TWS_NONE;
	char addr_str[BT_ADDR_STR_LEN];
	struct btsrv_addr_name info;
	u32_t name_len;

	hostif_bt_addr_to_str(addr, addr_str, BT_ADDR_STR_LEN);
	role = btsrv_adapter_check_role(addr, name);
	SYS_LOG_INF("name_cb %s %s %d", name, addr_str, role);

	memset(&info, 0, sizeof(info));
	memcpy(info.mac.val, addr->val, sizeof(bd_address_t));
	name_len = min(CONFIG_MAX_BT_NAME_LEN, strlen(name));
	memcpy(info.name, name, name_len);
	btsrv_event_notify_malloc(MSG_BTSRV_CONNECT, MSG_BTSRV_GET_NAME_FINISH, (u8_t *)&info, sizeof(info), role);
}

static bool _btsrv_adapter_connect_req_cb(bt_addr_t *peer, u8_t type)
{
	int i;
	u8_t role = BTSRV_TWS_NONE;
	struct autoconn_info info[BTSRV_SAVE_AUTOCONN_NUM];
	struct bt_link_cb_param param;

	if (type == BT_CONN_TYPE_BR) {
		memset(info, 0, sizeof(info));
		btsrv_connect_get_auto_reconnect_info(info, BTSRV_SAVE_AUTOCONN_NUM);
		for (i = 0; i < BTSRV_SAVE_AUTOCONN_NUM; i++) {
			if (info[i].addr_valid && !memcmp(&info[i].addr, peer, sizeof(bd_address_t))) {
				role = info[i].tws_role;
				break;
			}
		}

		memset(&param, 0, sizeof(param));
		param.link_event = BT_LINK_EV_ACL_CONNECT_REQ;
		param.addr = (bd_address_t *)peer;
		param.new_dev = (i == BTSRV_SAVE_AUTOCONN_NUM) ? 1 : 0;
		param.is_tws = (role == BTSRV_TWS_NONE) ? 0 : 1;
		param.is_acl_link = 1;
		if (btsrv_adapter_callback(BTSRV_LINK_EVENT, &param)) {
			return false;
		} else {
			return true;
		}
	} else if (type == BT_CONN_TYPE_SCO)  {
		memset(&param, 0, sizeof(param));
		param.link_event = BT_LINK_EV_ACL_CONNECT_REQ;
		param.addr = (bd_address_t *)peer;
		param.new_dev = 1;
		param.is_tws = 0;
		param.is_acl_link = 0;
		if (btsrv_adapter_callback(BTSRV_LINK_EVENT, &param)) {
			return false;
		} else {
			return true;
		}
	} else {
		return true;
	}
}

static void _btsrv_adapter_connected_cb(struct bt_conn *conn, u8_t err)
{
	char addr_str[BT_ADDR_STR_LEN];
	bt_addr_t *bt_addr = NULL;

	if (!conn || hostif_bt_conn_get_type(conn) != BT_CONN_TYPE_BR) {
		return;
	}

	bt_addr = (bt_addr_t *)GET_CONN_BT_ADDR(conn);
	hostif_bt_addr_to_str(bt_addr, addr_str, BT_ADDR_STR_LEN);
	SYS_LOG_INF("connected_cb %s, 0x%x", addr_str, err);

	if (err) {
		btsrv_event_notify_malloc(MSG_BTSRV_CONNECT, MSG_BTSRV_CONNECTED_FAILED, bt_addr->val, sizeof(bd_address_t), 0);
	} else {
		/* In MSG_BTSRV_CONNECTED req high performance is too late,
		 * request in stack rx thread, can't block in callback.
		 * and do release after process MSG_BTSRV_CONNECTED message.
		 */
		btsrv_adapter_callback(BTSRV_REQ_HIGH_PERFORMANCE, NULL);

		btsrv_event_notify(MSG_BTSRV_CONNECT, MSG_BTSRV_CONNECTED, conn);
		hostif_bt_remote_name_request(bt_addr, _btsrv_adapter_connected_remote_name_cb);
	}
}

static void _btsrv_adapter_disconnected_cb(struct bt_conn *conn, u8_t reason)
{
	if (!conn || hostif_bt_conn_get_type(conn) != BT_CONN_TYPE_BR) {
		return;
	}

	SYS_LOG_INF("disconnected_cb reason 0x%x", reason);
	btsrv_event_notify_ext(MSG_BTSRV_CONNECT, MSG_BTSRV_DISCONNECTED, conn, reason);
}

#if defined(CONFIG_BT_SMP) || defined(CONFIG_BT_BREDR)
static void _btsrv_adapter_identity_resolved_cb(struct bt_conn *conn, const bt_addr_le_t *rpa,
			      const bt_addr_le_t *identity)
{
	char addr_identity[BT_ADDR_LE_STR_LEN];
	char addr_rpa[BT_ADDR_LE_STR_LEN];

	hostif_bt_addr_le_to_str(identity, addr_identity, sizeof(addr_identity));
	hostif_bt_addr_le_to_str(rpa, addr_rpa, sizeof(addr_rpa));

	SYS_LOG_INF("resolved_cb %s -> %s", addr_rpa, addr_identity);
}

static void _btsrv_adapter_security_changed_cb(struct bt_conn *conn, bt_security_t level)
{
	char addr[BT_ADDR_STR_LEN];

	if (!conn || hostif_bt_conn_get_type(conn) != BT_CONN_TYPE_BR) {
		return;
	}

	hostif_bt_addr_to_str((const bt_addr_t *)GET_CONN_BT_ADDR(conn), addr, BT_ADDR_STR_LEN);
	SYS_LOG_INF("security_cb %s %d", addr, level);
	btsrv_event_notify(MSG_BTSRV_CONNECT, MSG_BTSRV_SECURITY_CHANGED, conn);
}

static void _btsrv_adapter_role_change_cb(struct bt_conn *conn, u8_t role)
{
	char addr[BT_ADDR_STR_LEN];

	if (!conn || hostif_bt_conn_get_type(conn) != BT_CONN_TYPE_BR) {
		return;
	}

	hostif_bt_addr_to_str((const bt_addr_t *)GET_CONN_BT_ADDR(conn), addr, BT_ADDR_STR_LEN);
	SYS_LOG_INF("role_change_cb %s %d", addr, role);
	btsrv_event_notify_ext(MSG_BTSRV_CONNECT, MSG_BTSRV_ROLE_CHANGE, conn, role);
}
#endif

static void _btsrv_connectionless_data_cb(struct bt_conn *conn, u8_t *data, u16_t len)
{
#ifdef CONFIG_SUPPORT_TWS
	if (btsrv_rdm_get_dev_role() != BTSRV_TWS_NONE) {
		btsrv_tws_connectionless_data_cb(conn, data, len);
	}
#endif
}

static struct bt_conn_cb conn_callbacks = {
	.connect_req = _btsrv_adapter_connect_req_cb,
	.connected = _btsrv_adapter_connected_cb,
	.disconnected = _btsrv_adapter_disconnected_cb,
#if defined(CONFIG_BT_SMP) || defined(CONFIG_BT_BREDR)
	.identity_resolved = _btsrv_adapter_identity_resolved_cb,
	.security_changed = _btsrv_adapter_security_changed_cb,
	.role_change = _btsrv_adapter_role_change_cb,
#endif
	.rx_connectionless_data = _btsrv_connectionless_data_cb,
};

#ifdef	CONFIG_WAIT_L2CAP_CONNECT_CONFRIM
static void btsrv_br_conn_rsp_result_cb(struct bt_conn *conn, u16_t psm, u32_t waittime)
{
	struct btsrv_conn_rsp_t result;

	result.conn = conn;
	result.psm = psm;
	result.waittime = waittime;

	btsrv_event_notify_malloc(MSG_BTSRV_CONNECT, MSG_BTSRV_CONN_RSP_RESULT, (char *)&result, sizeof(result), 0);
}
#endif

static void _btsrv_adapter_ready(int err)
{
	if (err) {
		SYS_LOG_ERR("Bt init failed %d", err);
		return;
	}

	SYS_LOG_DBG("Bt init");
	hostif_bt_conn_cb_register(&conn_callbacks);
#ifdef	CONFIG_WAIT_L2CAP_CONNECT_CONFRIM
	hostif_bt_l2cap_reg_br_conn_rsp_cb(btsrv_br_conn_rsp_result_cb);
#endif
	bt_service_set_bt_ready();
	btsrv_event_notify(MSG_BTSRV_BASE, MSG_BTSRV_READY, NULL);
}

static void btsrv_adapter_start_wait_disconnect_timer(void)
{
	if (thread_timer_is_running(&btsrv_info->wait_disconnect_timer)) {
		return;
	}

	SYS_LOG_INF("start_wait_disconnect_timer");
	thread_timer_start(&btsrv_info->wait_disconnect_timer, CHECK_WAIT_DISCONNECT_INTERVAL, CHECK_WAIT_DISCONNECT_INTERVAL);
}

static void btsrv_adapter_stop_wait_disconnect_timer(void)
{
	SYS_LOG_INF("stop_wait_disconnect_timer");
	thread_timer_stop(&btsrv_info->wait_disconnect_timer);
}

static void connected_dev_cb_check_wait_disconnect(struct bt_conn *base_conn, u8_t tws_dev, void *cb_param)
{
	int err;
	int *wait_diconnect_cnt = cb_param;

	if (btsrv_rdm_is_wait_to_diconnect(base_conn)) {
		if (hostif_bt_conn_is_in_sniff(base_conn) == false) {
			hostif_bt_conn_ref(base_conn);
			err = hostif_bt_conn_disconnect(base_conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
			if (err) {
				SYS_LOG_ERR("Disconnect failed %d", err);
			} else {
				btsrv_rdm_set_wait_to_diconnect(base_conn, false);
			}
			hostif_bt_conn_unref(base_conn);
		} else {
			(*wait_diconnect_cnt)++;
		}
	}
}

static void btsrv_adapter_wait_disconnect_timer_handler(struct thread_timer *ttimer, void *expiry_fn_arg)
{
	int wait_diconnect_cnt = 0;

	btsrv_rdm_get_connected_dev(connected_dev_cb_check_wait_disconnect, &wait_diconnect_cnt);
	if (wait_diconnect_cnt == 0) {
		btsrv_adapter_stop_wait_disconnect_timer();
	}
}

struct btsrv_info_t *btsrv_adapter_init(btsrv_callback cb)
{
	int err;
	char addr_str[BT_ADDR_STR_LEN];
	u8_t i, value;

	memset(&btsrv_btinfo, 0, sizeof(struct btsrv_info_t));

	btsrv_info = &btsrv_btinfo;
	btsrv_info->callback = cb;
	btsrv_info->allow_sco_connect = 1;
	thread_timer_init(&btsrv_info->wait_disconnect_timer, btsrv_adapter_wait_disconnect_timer_handler, NULL);

	btsrv_storage_init();
	btsrv_rdm_init();
	btsrv_connect_init();
	btsrv_scan_init();
	btsrv_link_adjust_init();

	err = hostif_bt_enable(_btsrv_adapter_ready);

	if (err) {
		SYS_LOG_INF("Bt init failed %d", err);
		goto err;
	}

	if (btsrv_property_get(CFG_BT_NAME, btsrv_info->device_name, sizeof(btsrv_info->device_name)) <= 0) {
		SYS_LOG_WRN("failed to get bt name");
	} else {
		SYS_LOG_INF("bt name: %s", (char *)btsrv_info->device_name);
	}

	if (property_get_byte_array(CFG_BT_MAC, btsrv_info->device_addr, sizeof(btsrv_info->device_addr), NULL)) {
		SYS_LOG_WRN("failed to get BT_MAC");
	} else {
		/* Like stack, low mac address save in low memory address */
		for (i=0; i<3; i++) {
			value = btsrv_info->device_addr[i];
			btsrv_info->device_addr[i] = btsrv_info->device_addr[5 -i];
			btsrv_info->device_addr[5 -i] = value;
		}

		memset(addr_str, 0, sizeof(addr_str));
		hostif_bt_addr_to_str((bt_addr_t *)btsrv_info->device_addr, addr_str, BT_ADDR_STR_LEN);
		SYS_LOG_INF("BT_MAC: %s", addr_str);
	}

	return btsrv_info;

err:
	return NULL;
}

int btsrv_adapter_set_config_info(void *param)
{
	int ret = -EIO;

	if (btsrv_info) {
		memcpy(&btsrv_info->cfg, param, sizeof(btsrv_info->cfg));
		SYS_LOG_INF("btsrv config info: %d, %d, %d", btsrv_info->cfg.max_conn_num,
						btsrv_info->cfg.max_phone_num, btsrv_info->cfg.pts_test_mode);
		ret = 0;
	}

	return ret;
}

static void btsrv_adapter_discovery_result(struct bt_br_discovery_result *result)
{
	struct btsrv_discover_result cb_result;

	if (!btsrv_discover_cb) {
		return;
	}

	memset(&cb_result, 0, sizeof(cb_result));
	if (result) {
		memcpy(&cb_result.addr, &result->addr, sizeof(bd_address_t));
		cb_result.rssi = result->rssi;
		if (result->name) {
			cb_result.name = result->name;
			cb_result.len = result->len;
			memcpy(cb_result.device_id, result->device_id, sizeof(cb_result.device_id));
		}
	} else {
		cb_result.discover_finish = 1;
	}

	btsrv_discover_cb(&cb_result);
	if (cb_result.discover_finish) {
		btsrv_discover_cb = NULL;
	}
}

int btsrv_adapter_start_discover(struct btsrv_discover_param *param)
{
	int ret;
	struct bt_br_discovery_param discovery_param;

	btsrv_discover_cb = param->cb;
	discovery_param.length = param->length;
	discovery_param.num_responses = param->num_responses;
	discovery_param.liac = false;
	ret = hostif_bt_br_discovery_start((const struct bt_br_discovery_param *)&discovery_param,
										btsrv_adapter_discovery_result);
	if (ret) {
		btsrv_discover_cb = NULL;
	}

	return ret;
}

int btsrv_adapter_stop_discover(void)
{
	hostif_bt_br_discovery_stop();
	return 0;
}

int btsrv_adapter_connect(bd_address_t *addr)
{
	struct bt_conn *conn = hostif_bt_conn_create_br((const bt_addr_t *)addr, BT_BR_CONN_PARAM_DEFAULT);

	if (!conn) {
		SYS_LOG_ERR("Connection failed");
	} else {
		SYS_LOG_INF("Connection pending");
		/* unref connection obj in advance as app user */
		hostif_bt_conn_unref(conn);
	}

	return 0;
}

int btsrv_adapter_check_cancal_connect(bd_address_t *addr)
{
	int err;
	struct bt_conn *conn;

	conn = hostif_bt_conn_br_acl_connecting((const bt_addr_t *)addr);
	if (conn) {
		/* In connecting, hostif_bt_conn_disconnect will cancal connect */
		err = hostif_bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
		hostif_bt_conn_unref(conn);
		SYS_LOG_INF("Cancal connect %d", err);
	}

	return 0;
}

int btsrv_adapter_disconnect(struct bt_conn *conn)
{
	int err = 0;

	hostif_bt_conn_force_exit_sniff(conn);

	if (hostif_bt_conn_is_in_sniff(conn)) {
		btsrv_rdm_set_wait_to_diconnect(conn, true);
		btsrv_adapter_start_wait_disconnect_timer();
	} else {
		hostif_bt_conn_ref(conn);

		err = hostif_bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
		if (err) {
			SYS_LOG_ERR("Disconnect failed %d", err);
		}

		hostif_bt_conn_unref(conn);
	}

	return err;
}

void btsrv_adapter_run(void)
{
	btsrv_info->running = 1;
}

void btsrv_adapter_allow_sco_connect(bool allow)
{
	btsrv_info->allow_sco_connect = allow;
}

int btsrv_adapter_stop(void)
{
	btsrv_adapter_stop_wait_disconnect_timer();
	btsrv_info->running = 0;
	/* TODO: Call btsrv connect to disconnect all connection,
	 * other deinit must wait all disconnect finish.
	 */

	/* Wait TODO:  */
	btsrv_link_adjust_deinit();
	btsrv_scan_deinit();
	btsrv_a2dp_deinit();
	btsrv_avrcp_deinit();
	btsrv_connect_deinit();
	btsrv_rdm_deinit();
	btsrv_storage_deinit();
	hostif_bt_disable();
	btsrv_info = NULL;
	return 0;
}

int btsrv_adapter_callback(btsrv_event_e event, void *param)
{
	if (btsrv_info && btsrv_info->callback) {
		return btsrv_info->callback(event, param);
	}

	return 0;
}

static void btsrv_active_disconnect(bd_address_t *addr)
{
	struct bt_conn *conn;

	conn = btsrv_rdm_find_conn_by_addr(addr);
	if (!conn) {
		SYS_LOG_INF("Device not connected!\n");
		return;
	}

	btsrv_adapter_disconnect(conn);
}

int btsrv_adapter_process(struct app_msg *msg)
{
	int ret = 0;

	switch (_btsrv_get_msg_param_cmd(msg)) {
	case MSG_BTSRV_READY:
		btsrv_adapter_callback(BTSRV_READY, NULL);
		break;
	case MSG_BTSRV_REQ_FLUSH_NVRAM:
		btsrv_adapter_callback(BTSRV_REQ_FLUSH_PROPERTY, msg->ptr);
		break;
	case MSG_BTSRV_DISCONNECTED_REASON:
		btsrv_adapter_callback(BTSRV_DISCONNECTED_REASON, msg->ptr);
		break;
	case MSG_BTSRV_SET_DEFAULT_SCAN_PARAM:
	case MSG_BTSRV_SET_ENHANCE_SCAN_PARAM:
		btsrv_scan_set_param(msg->ptr, (_btsrv_get_msg_param_cmd(msg) == MSG_BTSRV_SET_ENHANCE_SCAN_PARAM));
		break;
	case MSG_BTSRV_SET_DISCOVERABLE:
		if (_btsrv_get_msg_param_value(msg)) {
			btsrv_scan_set_user_discoverable(true, false);
		} else {
			btsrv_scan_set_user_discoverable(false, true);
		}
		break;
	case MSG_BTSRV_SET_CONNECTABLE:
		if (_btsrv_get_msg_param_value(msg)) {
			btsrv_scan_set_user_connectable(true, false);
		} else {
			btsrv_scan_set_user_connectable(false, true);
		}
		break;
	case MSG_BTSRV_ALLOW_SCO_CONNECT:
		btsrv_adapter_allow_sco_connect(_btsrv_get_msg_param_ptr(msg));
		break;
	case MSG_BTSRV_CONNECT_TO:
		btsrv_adapter_connect(msg->ptr);
		break;
	case MSG_BTSRV_DISCONNECT:
		btsrv_active_disconnect(msg->ptr);
		break;
	case MSG_BTSRV_DID_REGISTER:
		hostif_bt_did_register_sdp(msg->ptr);
		break;
	case MSG_BTSRV_DUMP_INFO:
		btsrv_rdm_dump_info();
		btsrv_connect_dump_info();
		btsrv_scan_dump_info();
#ifdef CONFIG_SUPPORT_TWS
		btsrv_tws_dump_info();
#endif
		break;
	default:
		break;
	}

	return ret;
}