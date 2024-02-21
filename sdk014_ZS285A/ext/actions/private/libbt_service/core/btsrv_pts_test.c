/*
 * Copyright (c) 2016 Actions Semi Co., Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief bt service core interface
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
#include <property_manager.h>

#include <bluetooth/host_interface.h>
#include "btsrv_inner.h"

extern int bt_pts_conn_creat_add_sco_cmd(struct bt_conn *brle_conn);

int btsrv_pts_send_hfp_cmd(char *cmd)
{
	struct bt_conn *br_conn = btsrv_rdm_hfp_get_actived();

	if (br_conn == NULL) {
		return -EIO;
	}

	hostif_bt_hfp_hf_send_cmd(br_conn, BT_HFP_USER_AT_CMD, cmd);
	return 0;
}

int btsrv_pts_hfp_active_connect_sco(void)
{
	struct bt_conn *br_conn = btsrv_rdm_hfp_get_actived();

	if (br_conn == NULL) {
		return -EIO;
	}

	hostif_bt_conn_create_sco(br_conn);
	return 0;
}

int btsrv_pts_avrcp_get_play_status(void)
{
	struct bt_conn *br_conn = btsrv_rdm_avrcp_get_connected_dev();

	if (br_conn == NULL) {
		return -EIO;
	}

	hostif_bt_avrcp_ct_get_play_status(br_conn);
	return 0;
}

int btsrv_pts_avrcp_pass_through_cmd(u8_t opid)
{
	struct bt_conn *br_conn = btsrv_rdm_avrcp_get_connected_dev();

	if (br_conn == NULL) {
		return -EIO;
	}

	hostif_bt_avrcp_ct_pass_through_cmd(br_conn, opid, true);
	os_sleep(30);
	hostif_bt_avrcp_ct_pass_through_cmd(br_conn, opid, false);
	return 0;
}

int btsrv_pts_avrcp_notify_volume_change(u8_t volume)
{
	struct bt_conn *br_conn = btsrv_rdm_avrcp_get_connected_dev();

	if (br_conn == NULL) {
		return -EIO;
	}

	hostif_bt_avrcp_tg_notify_change(br_conn, volume);
	return 0;
}

int btsrv_pts_avrcp_reg_notify_volume_change(void)
{
	struct bt_conn *br_conn = btsrv_rdm_avrcp_get_connected_dev();

	if (br_conn == NULL) {
		return -EIO;
	}

	hostif_bt_pts_avrcp_ct_get_capabilities(br_conn);
	os_sleep(100);
	hostif_bt_pts_avrcp_ct_register_notification(br_conn);
	return 0;
}

static void auth_passkey_display(struct bt_conn *conn, unsigned int passkey)
{
}

static void auth_passkey_confirm(struct bt_conn *conn, unsigned int passkey)
{
	hostif_bt_conn_ssp_confirm_reply(conn);
}

static void auth_pincode_entry(struct bt_conn *conn, bool highsec)
{
}

static void auth_cancel(struct bt_conn *conn)
{
}

static void auth_pairing_confirm(struct bt_conn *conn)
{
}

static const struct bt_conn_auth_cb auth_cb_display_yes_no = {
	.passkey_display = auth_passkey_display,
	.passkey_entry = NULL,
	.passkey_confirm = auth_passkey_confirm,
	.pincode_entry = auth_pincode_entry,
	.cancel = auth_cancel,
	.pairing_confirm = auth_pairing_confirm,
};

int btsrv_pts_register_auth_cb(bool reg_auth)
{
	if (reg_auth) {
		hostif_bt_conn_auth_cb_register(&auth_cb_display_yes_no);
	} else {
		hostif_bt_conn_auth_cb_register(NULL);
	}

	return 0;
}
