/*
 * Copyright (c) 2016 Actions Semi Co., Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief bt srv api interface
 */

#include <os_common_api.h>
#include <mem_manager.h>
#include <btservice_api.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "btsrv_inner.h"


int btif_base_register_processer(void)
{
	int ret = 0;

	ret |= btsrv_register_msg_processer(MSG_BTSRV_BASE, &btsrv_adapter_process);
	ret |= btsrv_register_msg_processer(MSG_BTSRV_CONNECT, &btsrv_connect_process);
	return ret;
}

int btif_start(btsrv_callback cb, u32_t classOfDevice, u16_t *did)
{
	struct app_msg msg = {0};

	if (!srv_manager_check_service_is_actived(BLUETOOTH_SERVICE_NAME)) {
		if (srv_manager_active_service(BLUETOOTH_SERVICE_NAME)) {
			SYS_LOG_DBG("btif_start ok\n");
		} else {
			SYS_LOG_ERR("btif_start failed\n");
			return -ESRCH;
		}
	}
	msg.type = MSG_INIT_APP;
	msg.ptr = cb;

	if (classOfDevice) {
		hostif_bt_init_class(classOfDevice);
	}

	if (did) {
		hostif_bt_init_device_id(did);
	}

	return !send_async_msg(BLUETOOTH_SERVICE_NAME, &msg);
}

int btif_stop(void)
{
	int ret = 0;

	if (!srv_manager_check_service_is_actived(BLUETOOTH_SERVICE_NAME)) {
		SYS_LOG_ERR("btif_stop failed\n");
		ret = -ESRCH;
		goto exit;
	}

	if (!srv_manager_exit_service(BLUETOOTH_SERVICE_NAME)) {
		ret = -ETIMEDOUT;
		goto exit;
	}

	SYS_LOG_DBG("btif_stop success!\n");
exit:
	return ret;
}

int btif_base_set_config_info(void *param)
{
	int ret, flags;

	flags = btsrv_set_negative_prio();
	ret = btsrv_adapter_set_config_info(param);
	btsrv_revert_prio(flags);

	return ret;
}

int btif_br_start_discover(struct btsrv_discover_param *param)
{
	int ret, flags;

	flags = btsrv_set_negative_prio();
	ret = btsrv_adapter_start_discover(param);
	btsrv_revert_prio(flags);

	return ret;
}

int btif_br_stop_discover(void)
{
	int ret, flags;

	flags = btsrv_set_negative_prio();
	ret = btsrv_adapter_stop_discover();
	btsrv_revert_prio(flags);

	return ret;
}

/* Mac low address store in low memory address */
int btif_br_connect(bd_address_t *bd)
{
	return btsrv_function_call_malloc(MSG_BTSRV_BASE, MSG_BTSRV_CONNECT_TO, (void *)bd, sizeof(bd_address_t), 0);
}

int btif_br_disconnect(bd_address_t *bd)
{
	return btsrv_function_call_malloc(MSG_BTSRV_BASE, MSG_BTSRV_DISCONNECT, (void *)bd, sizeof(bd_address_t), 0);
}

int btif_br_set_scan_param(const struct bt_scan_parameter *param, bool enhance_param)
{
	int cmd;

	cmd = enhance_param ? MSG_BTSRV_SET_ENHANCE_SCAN_PARAM : MSG_BTSRV_SET_DEFAULT_SCAN_PARAM;
	return btsrv_function_call_malloc(MSG_BTSRV_BASE, cmd, (void *)param, sizeof(struct bt_scan_parameter), 0);
}

int btif_br_set_discoverable(bool enable)
{
	return btsrv_function_call(MSG_BTSRV_BASE, MSG_BTSRV_SET_DISCOVERABLE, (void *)enable);
}

int btif_br_set_connnectable(bool enable)
{
	return btsrv_function_call(MSG_BTSRV_BASE, MSG_BTSRV_SET_CONNECTABLE, (void *)enable);
}

int btif_br_get_auto_reconnect_info(struct autoconn_info *info, u8_t max_cnt)
{
	int ret, flags;

	flags = btsrv_set_negative_prio();
	ret = btsrv_connect_get_auto_reconnect_info(info, max_cnt);
	btsrv_revert_prio(flags);

	return ret;
}

void btif_br_set_auto_reconnect_info(struct autoconn_info *info, u8_t max_cnt)
{
	int flags;

	flags = btsrv_set_negative_prio();
	btsrv_connect_set_auto_reconnect_info(info, max_cnt);
	btsrv_revert_prio(flags);
}

int btif_br_auto_reconnect(struct bt_set_autoconn *param)
{
	return btsrv_function_call_malloc(MSG_BTSRV_CONNECT, MSG_BTSRV_AUTO_RECONNECT, (void *)param, sizeof(struct bt_set_autoconn), 0);
}

int btif_br_auto_reconnect_stop(void)
{
	return btsrv_function_call(MSG_BTSRV_CONNECT, MSG_BTSRV_AUTO_RECONNECT_STOP, NULL);
}

bool btif_br_is_auto_reconnect_runing(void)
{
	return btsrv_autoconn_is_runing();
}

int btif_br_clear_list(int mode)
{
	return btsrv_function_call(MSG_BTSRV_CONNECT, MSG_BTSRV_CLEAR_LIST_CMD, (void *)mode);
}

int btif_br_disconnect_device(int disconnect_mode)
{
	return btsrv_function_call(MSG_BTSRV_CONNECT, MSG_BTSRV_DISCONNECT_DEVICE, (void *)disconnect_mode);
}

int btif_br_allow_sco_connect(bool allowed)
{
	return btsrv_function_call(MSG_BTSRV_BASE, MSG_BTSRV_ALLOW_SCO_CONNECT, (void *)allowed);
}

/* Device include phone and tws device */
int btif_br_get_connected_device_num(void)
{
	int ret, flags;

	flags = btsrv_set_negative_prio();
	ret = btsrv_rdm_get_connected_dev(NULL, NULL);
	btsrv_revert_prio(flags);

	return ret;
}

int btif_br_get_dev_rdm_state(struct bt_dev_rdm_state *state)
{
	int ret, flags;

	flags = btsrv_set_negative_prio();
	ret = btsrv_rdm_get_dev_state(state);
	btsrv_revert_prio(flags);

	return ret;
}

void btif_br_set_phone_controler_role(bd_address_t *bd, u8_t role)
{
	if (role != CONTROLER_ROLE_MASTER && role != CONTROLER_ROLE_SLAVE) {
		return;
	}

	btsrv_connect_set_phone_controler_role(bd, role);
}

int btif_br_get_linkkey(struct bt_linkkey_info *info, u8_t cnt)
{
	int ret, flags;

	flags = btsrv_set_negative_prio();
	ret = btsrv_storage_get_linkkey(info, cnt);
	btsrv_revert_prio(flags);

	return ret;
}

int btif_br_update_linkkey(struct bt_linkkey_info *info, u8_t cnt)
{
	int ret, flags;

	flags = btsrv_set_negative_prio();
	ret = btsrv_storage_update_linkkey(info, cnt);
	btsrv_revert_prio(flags);

	return ret;
}

int btif_br_write_ori_linkkey(bd_address_t *addr, u8_t *link_key)
{
	int ret, flags;

	flags = btsrv_set_negative_prio();
	ret = btsrv_storage_write_ori_linkkey(addr, link_key);
	btsrv_revert_prio(flags);

	return ret;
}

void btif_br_clean_linkkey(void)
{
	int flags;

	flags = btsrv_set_negative_prio();
	btsrv_storage_clean_linkkey();
	btsrv_revert_prio(flags);
}

int btif_br_read_rssi(s8_t *rssi)
{
	int ret, flags;
	struct bt_conn *conn;

	flags = btsrv_set_negative_prio();

	/* Not the best way, just easy modify for sony */
	conn = btsrv_rdm_a2dp_get_actived();
	if (conn == NULL) {
		ret = -EIO;
	} else {
		ret = hostif_bt_conn_read_rssi(conn, rssi);
	}

	btsrv_revert_prio(flags);
	return ret;
}

int btif_did_register_sdp(u8_t *data, u32_t len)
{
	return btsrv_function_call_malloc(MSG_BTSRV_BASE, MSG_BTSRV_DID_REGISTER, data,len,0);
}

int btif_set_afh_host_channel(u8_t *channel)
{
	return hostif_bt_set_afh_host_channel(channel);
}

int btif_dump_brsrv_info(void)
{
	return btsrv_function_call(MSG_BTSRV_BASE, MSG_BTSRV_DUMP_INFO, NULL);
}
