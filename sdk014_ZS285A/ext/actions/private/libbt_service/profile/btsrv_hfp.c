/*
 * Copyright (c) 2017 Actions Semi Co., Ltd.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief btsrvice hfp
 */
#include <errno.h>
#include <zephyr/types.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <misc/printk.h>
#include <misc/byteorder.h>
#include <zephyr.h>
#include <media/media_type.h>
#include <thread_timer.h>

#include <bluetooth/host_interface.h>

#include <mem_manager.h>
#include "btsrv_inner.h"

struct btsrv_hfp_context_info {
	btsrv_hfp_callback hfp_user_callback;
};

static int btsrv_hfp_hf_disable_nrec(struct bt_conn *br_conn);

static struct btsrv_hfp_context_info hfp_context;

static void _btsrv_hfp_connected_cb(struct bt_conn *conn)
{
	btsrv_event_notify(MSG_BTSRV_CONNECT, MSG_BTSRV_HFP_CONNECTED, conn);
}

static void _btsrv_hfp_disconnected_cb(struct bt_conn *conn)
{
	/* TODO: Disconnected process order: btsrv_tws(if need)->btsrv_hfp->btsrv_connect */
	btsrv_event_notify(MSG_BTSRV_HFP, MSG_BTSRV_HFP_DISCONNECTED, conn);
}

static void _btsrv_hfp_service_cb(struct bt_conn *conn, u32_t value)
{
	/* CIEV service
	 * 0: No Home/Roam network available.
	 * 1: Home/Roam network available.
	 */
	SYS_LOG_INF("hf service %d", value);
}

static void _btsrv_hfp_call_cb(struct bt_conn *conn, u32_t value)
{
	/* CIEV call
	 * 0: there are no calls in progress
	 * 1: at least one call is in progress
	 */
	SYS_LOG_INF("hf call %d", value);

	if (value == 0) {
		btsrv_event_notify_ext(MSG_BTSRV_HFP, MSG_BTSRV_HFP_SET_STATE, conn, BTSRV_HFP_STATE_CALL_EXITED);
		/* btsrv_hfp_set_status(conn, BTSRV_HFP_STATE_CALL_EXITED); */
	} else if (value == 1) {
		btsrv_event_notify_ext(MSG_BTSRV_HFP, MSG_BTSRV_HFP_SET_STATE, conn, BTSRV_HFP_STATE_CALL_ONGOING);
		/* btsrv_hfp_set_status(conn, BTSRV_HFP_STATE_CALL_ONGOING); */
	}
}

static void _btsrv_hfp_call_setup_cb(struct bt_conn *conn, u32_t value)
{
	/* CIEV call_setup
	 * 0: not currently in call set up
	 * 1: an incoming call process ongoing
	 * 2: an outgoing call set up is ongoing
	 * 3: remote party being alerted in an outgoing call
	 */
	SYS_LOG_INF("hf call_setup %d", value);

	switch (value) {
	case 0:
		btsrv_event_notify_ext(MSG_BTSRV_HFP, MSG_BTSRV_HFP_SET_STATE, conn, BTSRV_HFP_STATE_CALL_SETUP_EXITED);
		/* btsrv_hfp_set_status(conn, BTSRV_HFP_STATE_CALL_EXITED); */
		break;
	case 1:
		btsrv_event_notify_ext(MSG_BTSRV_HFP, MSG_BTSRV_HFP_SET_STATE, conn, BTSRV_HFP_STATE_CALL_INCOMING);
		/* btsrv_hfp_set_status(conn, BTSRV_HFP_STATE_CALL_INCOMING); */
		break;
	case 2:
		btsrv_event_notify_ext(MSG_BTSRV_HFP, MSG_BTSRV_HFP_SET_STATE, conn, BTSRV_HFP_STATE_CALL_OUTCOMING);
		/* btsrv_hfp_set_status(conn, BTSRV_HFP_STATE_CALL_OUTCOMING); */
		break;
	case 3:
		btsrv_event_notify_ext(MSG_BTSRV_HFP, MSG_BTSRV_HFP_SET_STATE, conn, BTSRV_HFP_STATE_CALL_ALERTED);
		/* btsrv_hfp_set_status(conn, BTSRV_HFP_STATE_CALL_ALERTED); */
		break;
	}
}

static void _btsrv_hfp_call_held_cb(struct bt_conn *conn, u32_t value)
{
	/* CIEV call_held
	 * 0: No calls held
	 * 1: Call is placed on hold or active/held calls swapped
	 * 2: Call on hold, no active call
	 */
	SYS_LOG_INF("hf call_held %d", value);

	switch (value) {
	case 0:
		btsrv_event_notify_ext(MSG_BTSRV_HFP, MSG_BTSRV_HFP_SET_STATE, conn, BTSRV_HFP_STATE_CALL_HELD_EXITED);
		break;
	case 1:
		btsrv_event_notify_ext(MSG_BTSRV_HFP, MSG_BTSRV_HFP_SET_STATE, conn, BTSRV_HFP_STATE_CALL_MULTIPARTY_HELD);
		break;
	case 2:
		btsrv_event_notify_ext(MSG_BTSRV_HFP, MSG_BTSRV_HFP_SET_STATE, conn, BTSRV_HFP_STATE_CALL_HELD);
		break;
	}
}

static void _btsrv_hfp_signal_cb(struct bt_conn *conn, u32_t value)
{
	SYS_LOG_INF("%u", value);
}

static void _btsrv_hfp_roam_cb(struct bt_conn *conn, u32_t value)
{
	SYS_LOG_INF("Roaming is %s active", value ? "" : "not");
}

static void _btsrv_hfp_battery_cb(struct bt_conn *conn, u32_t value)
{
	SYS_LOG_INF("Battery value: %u", value);
}

static void _btsrv_hfp_ring_cb(struct bt_conn *conn)
{
	SYS_LOG_INF("Incoming Call...");
}

static void _btsrv_hfp_bsir_cb(struct bt_conn *conn, u32_t value)
{
	SYS_LOG_INF("%s provide in-band ring tones", value ? "" : "not");
}

static void _btsrv_hfp_ccwa_cb(struct bt_conn *conn, u8_t *buf, u32_t value)
{
	SYS_LOG_INF("Call waiting phone number:%s, type:%d", buf, value);
}

static void _btsrv_hfp_clip_cb(struct bt_conn *conn, u8_t *buf, u32_t value)
{
	int buffer[12] = {0};
	int len = 0;

	SYS_LOG_INF("Calling line identification phone number:%s, type:%d", buf, value);
	buffer[0] = (int)conn;
	len = snprintf((char *)&buffer[1], sizeof(buffer) - 4, "%s", buf) + 4;
	btsrv_event_notify_malloc(MSG_BTSRV_HFP, MSG_BTSRV_HFP_PHONE_NUM, (u8_t *)buffer, len, 0);
}

static void _btsrv_hfp_bcs_cb(struct bt_conn *conn, u32_t codec_id)
{
	btsrv_event_notify_ext(MSG_BTSRV_HFP, MSG_BTSRV_HFP_CODEC_INFO, conn, codec_id);
	SYS_LOG_INF("codec_id %d\n", codec_id);
}

static void _btsrv_hfp_bvra_cb(struct bt_conn *conn, u32_t active)
{
	SYS_LOG_INF("Voice recognition is %s in the AG", active ? "enabled" : "disabled");
	btsrv_event_notify_ext(MSG_BTSRV_HFP, MSG_BTSRV_HFP_SIRI_STATE, conn, active);
}

static void _btsrv_hfp_vgs_cb(struct bt_conn *conn, u32_t value)
{
	SYS_LOG_INF("Gain of Speaker %d", value);
	btsrv_event_notify_ext(MSG_BTSRV_HFP, MSG_BTSRV_HFP_VOLUME_CHANGE, conn, value);
}

static void _btsrv_hfp_vgm_cb(struct bt_conn *conn, u32_t value)
{
	SYS_LOG_INF("Gain of Microphone %d", value);
}

static void _btsrv_hfp_btrh_cb(struct bt_conn *conn, u32_t value)
{
	/* CIEV btrh
	 * 0: Incoming call is put on hold in the AG
	 * 1: Held incoming call is accepted in the AG
	 * 2: Held incoming call is rejected in the AG
	 */
	SYS_LOG_INF("hf btrh %d", value);
}

static void _btsrv_hfp_cops_cb(struct bt_conn *conn, u32_t mode, u32_t format, u8_t *opt)
{
	SYS_LOG_INF("Cops mode %d, format: %d, operator: %s", mode, format, opt);
}

static void _btsrv_hfp_cnum_cb(struct bt_conn *conn, u8_t *buf, u32_t value)
{
	SYS_LOG_INF("Subscriber Number Information phone number:%s, type:%d", buf, value);
}

static void _btsrv_hfp_clcc_cb(struct bt_conn *conn, u32_t idx, u32_t dir, u32_t status, u32_t mode, u32_t mpty)
{
	SYS_LOG_INF("list current calls result code, idx: %d, dir: %d,"
				"status: %d, mode: %d, mpty: %d", idx, dir, status, mode, mpty);
}

static void _btsrv_hfp_cclk_cb(struct bt_conn *conn, u8_t *buf)
{
	u8_t buffer[30] = {0};
	int len = 0;

	SYS_LOG_INF("time of phone is :%s",buf);
	buffer[0] = (int)conn;
	memcpy(buffer,&conn,4);
	memcpy(&buffer[4],buf,strlen(buf));
	len = strlen(buf) + 4 +1;
	btsrv_event_notify_malloc(MSG_BTSRV_HFP, MSG_BTSRV_HFP_TIME_UPDATE, (u8_t *)buffer, len, 0);
}

static void _btsrv_hfp_cmd_complete_cb(struct bt_conn *conn,
				  struct bt_hfp_hf_cmd_complete *cmd)
{

}

static const struct bt_hfp_hf_cb btsrv_hf_cb = {
	.connected = _btsrv_hfp_connected_cb,
	.disconnected = _btsrv_hfp_disconnected_cb,
	.service = _btsrv_hfp_service_cb,
	.call = _btsrv_hfp_call_cb,
	.call_setup = _btsrv_hfp_call_setup_cb,
	.call_held = _btsrv_hfp_call_held_cb,
	.signal = _btsrv_hfp_signal_cb,
	.roam = _btsrv_hfp_roam_cb,
	.battery = _btsrv_hfp_battery_cb,
	.ring_indication = _btsrv_hfp_ring_cb,
	.bsir = _btsrv_hfp_bsir_cb,
	.ccwa = _btsrv_hfp_ccwa_cb,
	.clip = _btsrv_hfp_clip_cb,
	.bcs = _btsrv_hfp_bcs_cb,
	.bvra = _btsrv_hfp_bvra_cb,
	.vgs = _btsrv_hfp_vgs_cb,
	.vgm = _btsrv_hfp_vgm_cb,
	.btrh = _btsrv_hfp_btrh_cb,
	.cops = _btsrv_hfp_cops_cb,
	.cnum = _btsrv_hfp_cnum_cb,
	.clcc = _btsrv_hfp_clcc_cb,
	.cclk = _btsrv_hfp_cclk_cb,
	.cmd_complete_cb = _btsrv_hfp_cmd_complete_cb,
};

u8_t btsrv_hfp_get_codec_id(struct bt_conn *conn)
{
	struct bt_conn *base_conn = btsrv_rdm_get_base_conn_by_sco(conn);
	u8_t codec_id = BT_CODEC_ID_CVSD;
	u8_t sample_rate = 8;

	btsrv_rdm_hfp_get_codec_info(base_conn, &codec_id, &sample_rate);

	return codec_id;
}

static int _btsrv_hfp_update_codec_info(void)
{
	u8_t codec_info[2] = {BT_CODEC_ID_CVSD, 8};
	struct bt_conn *conn = btsrv_rdm_hfp_get_actived();
	if(!conn)
		return -EINVAL;
	
	btsrv_rdm_hfp_get_codec_info(conn, codec_info, &codec_info[1]);

	if (codec_info[0] == BT_CODEC_ID_CVSD) {
		codec_info[0] = CVSD_TYPE;
	} else if (codec_info[0] == BT_CODEC_ID_MSBC) {
		codec_info[0] = MSBC_TYPE;
	}

	hfp_context.hfp_user_callback(BTSRV_HFP_CODEC_INFO, codec_info, sizeof(codec_info));

	return 0;
}

static int _btsrv_hfp_tws_start_call(void)
{
#ifdef CONFIG_SUPPORT_TWS
	u8_t codec_info[2] = {BT_CODEC_ID_CVSD, 8};
	struct bt_conn *conn = btsrv_rdm_hfp_get_actived();
	if(btsrv_rdm_get_dev_role() == BTSRV_TWS_MASTER){	
		btsrv_rdm_hfp_get_codec_info(conn, codec_info, &codec_info[1]);

		btsrv_tws_hfp_start_callout(codec_info[0]);
	}
#endif
	return 0;
}

static void btsrv_hfp_call_status_cb(struct bt_conn *conn, btsrv_hfp_event_e event, btsrv_hfp_state state)
{
	struct bt_conn *second_conn = btsrv_rdm_hfp_get_second_dev();
	int other_dev_state = -1;

	if (!second_conn) {
		if (state == BTSRV_HFP_STATE_SCO_ESTABLISHED) {
			_btsrv_hfp_update_codec_info();
			hfp_context.hfp_user_callback(BTSRV_HFP_SCO, NULL, 0);
		}
		hfp_context.hfp_user_callback(event, NULL, 0);
	} else {
		if (conn == second_conn)
			other_dev_state = btsrv_rdm_hfp_get_state(btsrv_rdm_hfp_get_actived());
		else
			other_dev_state = btsrv_rdm_hfp_get_state(second_conn);
		switch (state) {
		case BTSRV_HFP_STATE_INIT:
		case BTSRV_HFP_STATE_LINKED:
			switch (other_dev_state) {
			case BTSRV_HFP_STATE_INIT:
			case BTSRV_HFP_STATE_LINKED:
				hfp_context.hfp_user_callback(event, NULL, 0);
				break;
			case BTSRV_HFP_STATE_CALL_INCOMING:
				hfp_context.hfp_user_callback(BTSRV_HFP_CALL_INCOMING, NULL, 0);
				event = BTSRV_HFP_CALL_INCOMING;
				break;
			case BTSRV_HFP_STATE_CALL_OUTCOMING:
				hfp_context.hfp_user_callback(BTSRV_HFP_CALL_OUTGOING, NULL, 0);
				event = BTSRV_HFP_CALL_OUTGOING;
				break;
			case BTSRV_HFP_STATE_CALL_ALERTED:
				hfp_context.hfp_user_callback(BTSRV_HFP_CALL_ALERTED, NULL, 0);
				event = BTSRV_HFP_CALL_ALERTED;
				break;
			case BTSRV_HFP_STATE_CALL_ONGOING:
				hfp_context.hfp_user_callback(BTSRV_HFP_CALL_ONGOING, NULL, 0);
				event = BTSRV_HFP_CALL_ONGOING;
				break;
			case BTSRV_HFP_STATE_SCO_ESTABLISHED:
				hfp_context.hfp_user_callback(BTSRV_HFP_SCO, NULL, 0);
				event = BTSRV_HFP_SCO;
				break;
			case BTSRV_HFP_STATE_CALL_3WAYIN:
				hfp_context.hfp_user_callback(BTSRV_HFP_CALL_3WAYIN, NULL, 0);
				event = BTSRV_HFP_CALL_3WAYIN;
				break;
			case BTSRV_HFP_STATE_CALL_MULTIPARTY:
				hfp_context.hfp_user_callback(BTSRV_HFP_CALL_MULTIPARTY, NULL, 0);
				event = BTSRV_HFP_CALL_MULTIPARTY;
				break;
			default:
				break;
			}
			break;
		case BTSRV_HFP_STATE_CALL_INCOMING:
		case BTSRV_HFP_STATE_CALL_OUTCOMING:
		case BTSRV_HFP_STATE_CALL_ALERTED:
			switch (other_dev_state) {
			case BTSRV_HFP_STATE_INIT:
			case BTSRV_HFP_STATE_LINKED:
				hfp_context.hfp_user_callback(event, NULL, 0);
				break;
			case BTSRV_HFP_STATE_CALL_INCOMING:
			case BTSRV_HFP_STATE_CALL_OUTCOMING:
			case BTSRV_HFP_STATE_CALL_ALERTED:
			case BTSRV_HFP_STATE_CALL_ONGOING:
			case BTSRV_HFP_STATE_SCO_ESTABLISHED: /* to do */
				hfp_context.hfp_user_callback(BTSRV_HFP_CALL_3WAYIN, NULL, 0);
				event = BTSRV_HFP_CALL_3WAYIN;
				break;
			case BTSRV_HFP_STATE_CALL_3WAYIN:
			case BTSRV_HFP_STATE_CALL_MULTIPARTY:
				hfp_context.hfp_user_callback(BTSRV_HFP_CALL_MULTIPARTY, NULL, 0);
				event = BTSRV_HFP_STATE_CALL_MULTIPARTY;
				break;
			default:
				break;
			}
			break;
		case BTSRV_HFP_STATE_CALL_ONGOING:
			switch (other_dev_state) {
			case BTSRV_HFP_STATE_INIT:
			case BTSRV_HFP_STATE_LINKED:
				hfp_context.hfp_user_callback(event, NULL, 0);
				break;
			case BTSRV_HFP_STATE_CALL_INCOMING:
			case BTSRV_HFP_STATE_CALL_OUTCOMING:
			case BTSRV_HFP_STATE_CALL_ALERTED:
				hfp_context.hfp_user_callback(BTSRV_HFP_CALL_3WAYIN, NULL, 0);
				event = BTSRV_HFP_CALL_3WAYIN;
				break;
			case BTSRV_HFP_STATE_CALL_ONGOING:
			case BTSRV_HFP_STATE_SCO_ESTABLISHED:	/* to do */
			case BTSRV_HFP_STATE_CALL_3WAYIN:
			case BTSRV_HFP_STATE_CALL_MULTIPARTY:
				hfp_context.hfp_user_callback(BTSRV_HFP_CALL_MULTIPARTY, NULL, 0);
				event = BTSRV_HFP_STATE_CALL_MULTIPARTY;
				break;
			default:
				break;
			}
			break;
		case BTSRV_HFP_STATE_SCO_ESTABLISHED:
			if (conn != second_conn) {
				_btsrv_hfp_update_codec_info();
				hfp_context.hfp_user_callback(BTSRV_HFP_SCO, NULL, 0);
				hfp_context.hfp_user_callback(event, NULL, 0);
			}
			break;
		case BTSRV_HFP_STATE_CALL_3WAYIN:
		case BTSRV_HFP_STATE_CALL_MULTIPARTY:
			hfp_context.hfp_user_callback(event, NULL, 0);
			break;
		default:
			break;
		}

	}

	SYS_LOG_INF("event %d dev state %d second dev state %d\n", event, state, other_dev_state);
}

int btsrv_hfp_set_status(struct bt_conn *conn, int state)
{
	int old_state = btsrv_rdm_hfp_get_state(conn);
	int new_state = -1;
	int event = -1;
	u8_t active_call, held_call, incoming_call, outgoing_call;
	int ret = btsrv_rdm_hfp_get_call_state(conn, &active_call, &held_call, &incoming_call, &outgoing_call);

	if (ret < 0)
		return -ESRCH;

	switch (state) {
	case BTSRV_HFP_STATE_CALL_EXITED:
		active_call = 0;
		held_call = 0;
		if (old_state > BTSRV_HFP_STATE_INIT) {
			if (!incoming_call && !outgoing_call) {
				new_state = BTSRV_HFP_STATE_LINKED;
				event = BTSRV_HFP_CALL_EXIT;
				if(btsrv_rdm_hfp_get_sco_state(conn) == BTSRV_SCO_STATE_PHONE)
					btsrv_rdm_hfp_set_sco_state(conn, BTSRV_SCO_STATE_INIT);
			} else if (incoming_call == 1) {
				new_state = BTSRV_HFP_STATE_CALL_INCOMING;
				event = BTSRV_HFP_CALL_INCOMING;
			} else if (outgoing_call == 1) {
				new_state = BTSRV_HFP_STATE_CALL_OUTCOMING;
				event = BTSRV_HFP_CALL_OUTGOING;
			}
		}
		break;
	case BTSRV_HFP_STATE_CALL_SETUP_EXITED:
		if (incoming_call > 0)
			incoming_call = 0;
		else if (outgoing_call > 0)
			outgoing_call = 0;
		if (old_state == BTSRV_HFP_STATE_CALL_INCOMING ||
			old_state == BTSRV_HFP_STATE_CALL_OUTCOMING ||
			old_state == BTSRV_HFP_STATE_CALL_ALERTED) {
			new_state = BTSRV_HFP_STATE_LINKED;
			event = BTSRV_HFP_CALL_EXIT;
			if(btsrv_rdm_hfp_get_sco_state(conn) == BTSRV_SCO_STATE_PHONE)
				btsrv_rdm_hfp_set_sco_state(conn, BTSRV_SCO_STATE_INIT);
		} else if (old_state == BTSRV_HFP_STATE_CALL_3WAYIN) {
			new_state = BTSRV_HFP_STATE_CALL_ONGOING;
			event = BTSRV_HFP_CALL_ONGOING;
		}
		break;
	case BTSRV_HFP_STATE_CALL_HELD_EXITED:
		held_call = 0;
		if (old_state == BTSRV_HFP_STATE_CALL_MULTIPARTY) {
			if ((incoming_call + outgoing_call > 0)
				&& active_call){
				new_state = BTSRV_HFP_STATE_CALL_3WAYIN;
				event = BTSRV_HFP_CALL_3WAYIN;
			} else if (incoming_call == 1) {
				new_state = BTSRV_HFP_STATE_CALL_INCOMING;
				event = BTSRV_HFP_CALL_INCOMING;
			} else if (outgoing_call == 1) {
				new_state = BTSRV_HFP_STATE_CALL_OUTCOMING;
				event = BTSRV_HFP_CALL_OUTGOING;
			} else {/* to do : held call --> active call */
				new_state = BTSRV_HFP_STATE_CALL_ONGOING;
				event = BTSRV_HFP_CALL_ONGOING;
				active_call = 1;
			}
		}
		break;
	case BTSRV_HFP_STATE_CALL_HELD:
		new_state = BTSRV_HFP_STATE_CALL_MULTIPARTY;
		event = BTSRV_HFP_CALL_MULTIPARTY;
		active_call = 0;
		held_call = 1;
		break;
	case BTSRV_HFP_STATE_CALL_MULTIPARTY_HELD:
		active_call = 1;
		held_call = 1;
		new_state = BTSRV_HFP_STATE_CALL_MULTIPARTY;
		event = BTSRV_HFP_CALL_MULTIPARTY;
		break;
	case BTSRV_HFP_STATE_CALL_INCOMING:
		incoming_call = 1;
		if (old_state == BTSRV_HFP_STATE_INIT ||
			old_state == BTSRV_HFP_STATE_LINKED ||
			old_state == BTSRV_HFP_STATE_SCO_ESTABLISHED) {
			new_state = BTSRV_HFP_STATE_CALL_INCOMING;
			event = BTSRV_HFP_CALL_INCOMING;
		} else if (old_state >= BTSRV_HFP_STATE_CALL_ONGOING && old_state <= BTSRV_HFP_STATE_CALL_3WAYIN) {
			new_state = BTSRV_HFP_STATE_CALL_3WAYIN;
			event = BTSRV_HFP_CALL_3WAYIN;
		}
		break;
	case BTSRV_HFP_STATE_CALL_OUTCOMING:
		outgoing_call = 1;
		if (old_state == BTSRV_HFP_STATE_INIT ||
			old_state == BTSRV_HFP_STATE_LINKED ||
			old_state == BTSRV_HFP_STATE_SCO_ESTABLISHED) {
			new_state = BTSRV_HFP_STATE_CALL_OUTCOMING;
			event = BTSRV_HFP_CALL_OUTGOING;
		} else if (old_state >= BTSRV_HFP_STATE_CALL_ONGOING && old_state <= BTSRV_HFP_STATE_CALL_3WAYIN) {
			new_state = BTSRV_HFP_STATE_CALL_3WAYIN;
			event = BTSRV_HFP_CALL_3WAYIN;
		}
		break;
	case BTSRV_HFP_STATE_CALL_ONGOING:
		active_call = 1;
		if (old_state == BTSRV_HFP_STATE_INIT ||
			old_state == BTSRV_HFP_STATE_LINKED ||
			old_state == BTSRV_HFP_STATE_CALL_INCOMING ||
			old_state == BTSRV_HFP_STATE_CALL_OUTCOMING ||
			old_state == BTSRV_HFP_STATE_CALL_ALERTED ||
			old_state == BTSRV_HFP_STATE_SCO_ESTABLISHED) {
			new_state = BTSRV_HFP_STATE_CALL_ONGOING;
			event = BTSRV_HFP_CALL_ONGOING;
		} else if (old_state == BTSRV_HFP_STATE_CALL_3WAYIN) {
			new_state = BTSRV_HFP_STATE_CALL_MULTIPARTY;
			event = BTSRV_HFP_CALL_MULTIPARTY;
		}
		break;
	case BTSRV_HFP_STATE_CALL_ALERTED:
		outgoing_call = 1;
		if (old_state == BTSRV_HFP_STATE_INIT ||
			old_state == BTSRV_HFP_STATE_LINKED ||
			old_state == BTSRV_HFP_STATE_CALL_OUTCOMING ||
			old_state == BTSRV_HFP_STATE_SCO_ESTABLISHED) {
			new_state = BTSRV_HFP_STATE_CALL_ALERTED;
			event = BTSRV_HFP_CALL_ALERTED;
		} else if (old_state >= BTSRV_HFP_STATE_CALL_ONGOING && old_state <= BTSRV_HFP_STATE_CALL_3WAYIN) {
			new_state = BTSRV_HFP_STATE_CALL_3WAYIN;
			event = BTSRV_HFP_CALL_3WAYIN;
		}
		break;
	case BTSRV_HFP_STATE_SCO_ESTABLISHED:
		if (old_state == BTSRV_HFP_STATE_INIT ||
			old_state == BTSRV_HFP_STATE_LINKED)
			new_state = BTSRV_HFP_STATE_SCO_ESTABLISHED;
		else{
			if (conn == btsrv_rdm_hfp_get_actived() || 
				conn == btsrv_rdm_get_tws_by_role(BTSRV_TWS_SLAVE)) {
				_btsrv_hfp_update_codec_info();
				hfp_context.hfp_user_callback(BTSRV_SCO_CONNECTED, NULL, 0);
			}
		}
		event = BTSRV_SCO_CONNECTED;
		btsrv_rdm_hfp_set_sco_state(conn, BTSRV_SCO_STATE_HFP);
		if(conn == btsrv_rdm_hfp_get_actived() && btsrv_rdm_get_dev_role() == BTSRV_TWS_MASTER){
			_btsrv_hfp_tws_start_call();
		}
		break;
	case BTSRV_HFP_STATE_SCO_RELEASED:
		if (old_state == BTSRV_HFP_STATE_SCO_ESTABLISHED) {
			new_state = BTSRV_HFP_STATE_LINKED;
			btsrv_rdm_hfp_set_sco_state(conn, BTSRV_SCO_STATE_INIT);
		} else {
			if (conn == btsrv_rdm_hfp_get_actived() ||  
				conn == btsrv_rdm_get_tws_by_role(BTSRV_TWS_SLAVE))
				hfp_context.hfp_user_callback(BTSRV_SCO_DISCONNECTED, NULL, 0);
			/* sco may be released after call exit,sco state must set to INIT */
			if (old_state != BTSRV_HFP_STATE_LINKED && old_state != BTSRV_HFP_STATE_INIT)
				btsrv_rdm_hfp_set_sco_state(conn, BTSRV_SCO_STATE_PHONE);
			else
				btsrv_rdm_hfp_set_sco_state(conn, BTSRV_SCO_STATE_INIT);
		}
		event = BTSRV_SCO_DISCONNECTED;
#ifdef CONFIG_SUPPORT_TWS
		if(conn == btsrv_rdm_hfp_get_actived() && btsrv_rdm_get_dev_role() == BTSRV_TWS_MASTER){
			btsrv_tws_hfp_stop_call();
		}
#endif
		break;
	}

	SYS_LOG_INF("old state %d new state %d state %d\n", old_state, new_state, state);
	SYS_LOG_INF("active %d hold %d coming %d\n", active_call, held_call,
		incoming_call + outgoing_call);

	if (new_state >= 0) {
		if ((new_state >= BTSRV_HFP_STATE_CALL_INCOMING &&
			new_state <= BTSRV_HFP_STATE_SCO_ESTABLISHED) &&
			conn == btsrv_rdm_a2dp_get_actived() &&
			btsrv_rdm_is_a2dp_stream_open(conn)) {
			btsrv_rdm_a2dp_actived_switch_lock(conn, 1);
			SYS_LOG_INF("conn %p a2dp switch lock\n", conn);
		} else if (new_state == BTSRV_HFP_STATE_LINKED &&
			conn == btsrv_rdm_a2dp_get_actived()) {
			btsrv_rdm_a2dp_actived_switch_lock(conn, 0);
			SYS_LOG_INF("conn %p a2dp switch unlock\n", conn);
		}
		if(conn == btsrv_rdm_hfp_get_actived() && old_state == BTSRV_HFP_STATE_CALL_INCOMING
			&& btsrv_rdm_hfp_get_notify_phone_num_state(conn)){
			hfp_context.hfp_user_callback(BTSRV_HFP_PHONE_NUM_STOP, NULL, 0);
			btsrv_rdm_hfp_set_notify_phone_num_state(conn,0);
		}
		btsrv_rdm_hfp_set_state(conn, new_state);
		btsrv_hfp_call_status_cb(conn, event, new_state);
	}
	btsrv_rdm_hfp_set_call_state(conn, active_call, held_call, incoming_call, outgoing_call);
	return 0;
}

int btsrv_hfp_get_status(void)
{
	struct bt_conn *conn = btsrv_rdm_hfp_get_actived();

	return btsrv_rdm_hfp_get_state(conn);
}

int btsrv_hfp_connect(struct bt_conn *conn)
{
	if (!hostif_bt_hfp_hf_connect(conn)) {
		SYS_LOG_INF("Connect hfp_conn:%p\n", conn);
	} else {
		SYS_LOG_ERR("cmd_bredr_hfp_connect failed\n");
	}

	return 0;
}

int btsrv_hfp_init(btsrv_hfp_callback cb)
{
	SYS_LOG_INF("cb %p", cb);
	memset(&hfp_context, 0, sizeof(struct btsrv_hfp_context_info));
	hostif_bt_hfp_hf_register_cb((struct bt_hfp_hf_cb *)&btsrv_hf_cb);
	hfp_context.hfp_user_callback = cb;

	return 0;
}

int btsrv_hfp_deinit(void)
{
	hostif_bt_hfp_hf_register_cb(NULL);
	return 0;
}

static int _btsrv_hfp_connected(struct bt_conn *conn)
{
	bd_address_t *addr = NULL;
	int state = btsrv_rdm_hfp_get_state(conn);

	addr = GET_CONN_BT_ADDR(conn);
	SYS_LOG_INF("hfp connected:%p addr %p\n", conn, addr);

	/**decode id may update during connect*/
	/* btsrv_rdm_hfp_set_codec_info(conn, BT_CODEC_ID_CVSD, 8); */
	if (state == BTSRV_HFP_STATE_INIT)
		btsrv_rdm_hfp_set_state(conn, BTSRV_HFP_STATE_LINKED);

	hfp_context.hfp_user_callback(BTSRV_HFP_CONNECTED, NULL, 0);
	btsrv_hfp_hf_disable_nrec(conn);
	return 0;
}

static int _btsrv_hfp_disconnected(struct bt_conn *conn)
{
	bd_address_t *addr = NULL;
	int old_state = btsrv_rdm_hfp_get_state(conn);

	addr = GET_CONN_BT_ADDR(conn);
	SYS_LOG_INF("hfp disconnected:%p addr %p\n", conn, addr);
	btsrv_rdm_hfp_set_state(conn, BTSRV_HFP_STATE_INIT);
	if(btsrv_rdm_hfp_get_sco_state(conn) == BTSRV_SCO_STATE_PHONE)
		btsrv_rdm_hfp_set_sco_state(conn, BTSRV_SCO_STATE_INIT);
	if (old_state > BTSRV_HFP_STATE_LINKED)
		btsrv_hfp_call_status_cb(conn, BTSRV_HFP_CALL_EXIT, BTSRV_HFP_STATE_INIT);
	if (!btsrv_rdm_hfp_get_second_dev())
		hfp_context.hfp_user_callback(BTSRV_HFP_DISCONNECTED, NULL, 0);

	return 0;
}

static int _btsrv_hfp_phone_num(struct bt_conn *conn, char *data)
{
	/* call back to uplayer , play tts deceide by up layer*/
	if (conn == btsrv_rdm_hfp_get_actived()) {
		hfp_context.hfp_user_callback(BTSRV_HFP_PHONE_NUM, data, strlen(data));
		btsrv_rdm_hfp_set_notify_phone_num_state(conn,1);
	}

	return 0;
}

static int _btsrv_hfp_codec_info(struct bt_conn *conn, u8_t codec_id)
{
	u8_t codec_info[2] = {0};

	if (codec_id == BT_CODEC_ID_CVSD) {
		codec_info[0] = CVSD_TYPE;
		codec_info[1] = 8;
	} else if (codec_id == BT_CODEC_ID_MSBC) {
		codec_info[0] = MSBC_TYPE;
		codec_info[1] = 16;
	}

	btsrv_rdm_hfp_set_codec_info(conn, codec_id, codec_info[1]);
	if (conn == btsrv_rdm_hfp_get_actived() || conn == btsrv_rdm_get_tws_by_role(BTSRV_TWS_SLAVE))
		hfp_context.hfp_user_callback(BTSRV_HFP_CODEC_INFO, codec_info, sizeof(codec_info));

	return 0;
}

static int _btsrv_hfp_volume_change(struct bt_conn *conn, u8_t volume)
{
	if (conn == btsrv_rdm_hfp_get_actived())
		hfp_context.hfp_user_callback(BTSRV_HFP_VOLUME_CHANGE, (u8_t *)&volume, sizeof(volume));

	return 0;
}

static int _btsrv_hfp_active_dev_change(struct bt_conn *conn)
{
	if (conn == btsrv_rdm_hfp_get_actived()) {
	#ifdef CONFIG_SUPPORT_TWS
		if(btsrv_rdm_get_dev_role() == BTSRV_TWS_MASTER){
			btsrv_tws_hfp_stop_call();
		}
	#endif
		hfp_context.hfp_user_callback(BTSRV_HFP_ACTIVE_DEV_CHANGE, NULL, 0);
		int state = btsrv_rdm_hfp_get_state(conn);
		int sco_state = btsrv_rdm_hfp_get_sco_state(conn);
		if(!btsrv_rdm_hfp_get_actived_sco()){
			if ((state >= BTSRV_HFP_STATE_CALL_ONGOING && state <= BTSRV_HFP_STATE_CALL_MULTIPARTY) ||
				(state >= BTSRV_HFP_STATE_CALL_INCOMING &&  state <= BTSRV_HFP_STATE_CALL_ALERTED &&
				sco_state == BTSRV_SCO_STATE_PHONE))
				if (btsrv_info->allow_sco_connect)
					hostif_bt_conn_create_sco(conn);
		}else{
			struct thread_timer *sco_disconnect_timer = btsrv_rdm_get_sco_disconnect_timer(conn);
			if(thread_timer_is_running(sco_disconnect_timer)){
				thread_timer_stop(sco_disconnect_timer);
			}
			if(sco_state != BTSRV_SCO_STATE_DISCONNECT){
				_btsrv_hfp_update_codec_info();
				_btsrv_hfp_tws_start_call();
				hfp_context.hfp_user_callback(BTSRV_SCO_CONNECTED, NULL, 0);
			}
		}
		struct bt_conn *second_conn = btsrv_rdm_hfp_get_second_dev();
		if(second_conn && btsrv_rdm_hfp_get_notify_phone_num_state(second_conn)){
			hfp_context.hfp_user_callback(BTSRV_HFP_PHONE_NUM_STOP, NULL, 0);
			btsrv_rdm_hfp_set_notify_phone_num_state(second_conn,0);
		}
	}

	return 0;
}

static int _btsrv_hfp_siri_state_change(struct bt_conn *conn, u8_t state)
{
	if (conn == btsrv_rdm_hfp_get_actived())
		hfp_context.hfp_user_callback(BTSRV_HFP_SIRI_STATE_CHANGE, (u8_t *)&state, sizeof(state));

	return 0;
}

static int _btsrv_hfp_time_update(struct bt_conn *conn, char *data)
{
	/* call back to uplayer , play tts deceide by up layer*/
	if (conn == btsrv_rdm_hfp_get_actived()) {
		hfp_context.hfp_user_callback(BTSRV_HFP_TIME_UPDATE, data, strlen(data));
	}

	return 0;
}

static int _btsrv_hfp_hf_switch_sound_source(void)
{
	struct bt_conn *sco_conn = btsrv_rdm_hfp_get_actived_sco();
	struct bt_conn *br_conn = btsrv_rdm_hfp_get_actived();
	struct bt_conn *second_conn = btsrv_rdm_hfp_get_second_dev();
	int state;
	if(second_conn)
		state = btsrv_rdm_hfp_get_state(second_conn);

	if (!br_conn)
		return -ESRCH;

	if (sco_conn) {
		if (second_conn && (state >= BTSRV_HFP_STATE_CALL_INCOMING
			&& state <= BTSRV_HFP_STATE_SCO_ESTABLISHED)){
			btsrv_rdm_hfp_actived(second_conn, 1, 1);
		}

		btsrv_sco_disconnect(sco_conn);
	} else {
		if (second_conn && (state >= BTSRV_HFP_STATE_CALL_INCOMING
			&& state <= BTSRV_HFP_STATE_SCO_ESTABLISHED)){
			btsrv_rdm_hfp_actived(second_conn, 1, 1);
			return 0;
		}
		/* phone which has ring tone will creat sco buring incoming
		 * so we don't creat sco during incoming except for BTSRV_SCO_STATE_PHONE state
		 * this state means sco has been created before.
		 */
		int sco_state = btsrv_rdm_hfp_get_sco_state(br_conn);
		int state = btsrv_rdm_hfp_get_state(br_conn);

		if ((state >= BTSRV_HFP_STATE_CALL_ONGOING && state <= BTSRV_HFP_STATE_CALL_MULTIPARTY) ||
			(state >= BTSRV_HFP_STATE_CALL_INCOMING &&  state <= BTSRV_HFP_STATE_CALL_ALERTED &&
			sco_state == BTSRV_SCO_STATE_PHONE)) {
			if (btsrv_info->allow_sco_connect)
				hostif_bt_conn_create_sco(br_conn);
		}
	}

	return 0;
}

static int _btsrv_hfp_hf_dial_number(u8_t *number)
{
	char at_command[32];
	struct bt_conn *br_conn = btsrv_rdm_hfp_get_actived();

	if (!br_conn)
		return -ESRCH;

	snprintf(at_command, sizeof(at_command), "ATD%s;\r", number);
	hostif_bt_hfp_hf_send_cmd(br_conn, BT_HFP_USER_AT_CMD, at_command);

	return 0;
}

static int _btsrv_hfp_hf_dial_last_number(void)
{
	char at_command[32];
	struct bt_conn *br_conn = btsrv_rdm_hfp_get_actived();

	if (!br_conn)
		return -ESRCH;

	int state = btsrv_rdm_hfp_get_state(br_conn);

	if (state != BTSRV_HFP_STATE_LINKED && state != BTSRV_HFP_STATE_CALL_ONGOING &&
		state != BTSRV_HFP_STATE_SCO_ESTABLISHED)
		return -EBUSY;

	snprintf(at_command, sizeof(at_command), "AT+BLDN\r");
	hostif_bt_hfp_hf_send_cmd(br_conn, BT_HFP_USER_AT_CMD, at_command);

	return 0;
}

static int _btsrv_hfp_hf_dial_memory(int location)
{
	char at_command[32];
	struct bt_conn *br_conn = btsrv_rdm_hfp_get_actived();

	if (!br_conn)
		return -ESRCH;

	snprintf(at_command, sizeof(at_command), "ATD>%u;\r", location);
	hostif_bt_hfp_hf_send_cmd(br_conn, BT_HFP_USER_AT_CMD, at_command);

	return 0;
}

static int _btsrv_hfp_hf_volume_control(u8_t type, u8_t volume)
{
	char at_command[32];
	struct bt_conn *br_conn = btsrv_rdm_hfp_get_actived();

	if (!br_conn)
		return -ESRCH;

	if (type == 0x01) {
		snprintf(at_command, sizeof(at_command), "AT+VGS=%u\r", volume);
	} else {
		snprintf(at_command, sizeof(at_command), "AT+VGM=%u\r", volume);
	}

	hostif_bt_hfp_hf_send_cmd(br_conn, BT_HFP_USER_AT_CMD, at_command);

	return 0;
}

static int _btsrv_hfp_hf_battery_report(u8_t mode, u8_t bat_val)
{
	char at_command[32];
	struct bt_conn *br_conn = btsrv_rdm_hfp_get_actived();

	if (!br_conn)
		return -ESRCH;

	if (mode == 0x01) {
		snprintf(at_command, sizeof(at_command), "AT+XAPL=0000-0000-0100,2\r");
	} else {
		snprintf(at_command, sizeof(at_command), "AT+IPHONEACCEV=1,1,%u\r", bat_val);
	}

	hostif_bt_hfp_hf_send_cmd(br_conn, BT_HFP_USER_AT_CMD, at_command);

	br_conn = btsrv_rdm_hfp_get_second_dev();

	if (br_conn) {
		hostif_bt_hfp_hf_send_cmd(br_conn, BT_HFP_USER_AT_CMD, at_command);
	}

	return 0;
}

static int btsrv_hfp_hf_disable_nrec(struct bt_conn *br_conn)
{
	if (!br_conn)
		return -ESRCH;

	hostif_bt_hfp_hf_send_cmd(br_conn, BT_HFP_USER_AT_CMD, "AT+NREC=0");
	return 0;
}

static int _btsrv_hfp_hf_accept_call(void)
{
	struct bt_conn *br_conn = btsrv_rdm_hfp_get_actived();

	if (!br_conn)
		return -ESRCH;

	int state = btsrv_rdm_hfp_get_state(br_conn);

	if (state == BTSRV_HFP_STATE_CALL_INCOMING) {
		hostif_bt_hfp_hf_send_cmd(br_conn, BT_HFP_USER_AT_CMD, "ATA");
	}

	return 0;
}

static int _btsrv_hfp_hf_reject_call(void)
{
	struct bt_conn *br_conn = btsrv_rdm_hfp_get_actived();

	if (!br_conn)
		return -ESRCH;

	int state = btsrv_rdm_hfp_get_state(br_conn);

	if (state == BTSRV_HFP_STATE_CALL_INCOMING) {
		hostif_bt_hfp_hf_send_cmd(br_conn, BT_HFP_USER_AT_CMD, "AT+CHUP");
	}

	return 0;
}

static int _btsrv_hfp_hf_hangup_call(void)
{
	struct bt_conn *br_conn = btsrv_rdm_hfp_get_actived();

	if (!br_conn)
		return -ESRCH;

	int state = btsrv_rdm_hfp_get_state(br_conn);

	if (state == BTSRV_HFP_STATE_CALL_ONGOING ||
		state == BTSRV_HFP_STATE_CALL_OUTCOMING ||
		state == BTSRV_HFP_STATE_CALL_ALERTED) {
		hostif_bt_hfp_hf_send_cmd(br_conn, BT_HFP_USER_AT_CMD, "AT+CHUP");
	}

	return 0;
}

static int _btsrv_hfp_hf_hangup_another_call(void)
{
	struct bt_conn *br_conn = btsrv_rdm_hfp_get_actived();

	if (!br_conn)
		return -ESRCH;

	int state = btsrv_rdm_hfp_get_state(br_conn);

	if (state == BTSRV_HFP_STATE_CALL_3WAYIN ||
		state == BTSRV_HFP_STATE_CALL_MULTIPARTY) {
		hostif_bt_hfp_hf_send_cmd(br_conn, BT_HFP_USER_AT_CMD, "AT+CHLD=0");
		return 0;
	}

	struct bt_conn *second_conn = btsrv_rdm_hfp_get_second_dev();

	if (!second_conn)
		return 0;

	int other_dev_state = btsrv_rdm_hfp_get_state(second_conn);

	if (state == BTSRV_HFP_STATE_CALL_INCOMING) {
		hostif_bt_hfp_hf_send_cmd(br_conn, BT_HFP_USER_AT_CMD, "ATA");
	}

	switch (other_dev_state) {
	case BTSRV_HFP_STATE_CALL_INCOMING:
	case BTSRV_HFP_STATE_CALL_OUTCOMING:
	case BTSRV_HFP_STATE_CALL_ALERTED:
	case BTSRV_HFP_STATE_CALL_ONGOING:
		hostif_bt_hfp_hf_send_cmd(second_conn, BT_HFP_USER_AT_CMD, "AT+CHUP");
		break;
	case BTSRV_HFP_STATE_CALL_3WAYIN:
	case BTSRV_HFP_STATE_CALL_MULTIPARTY:
		hostif_bt_hfp_hf_send_cmd(second_conn, BT_HFP_USER_AT_CMD, "AT+CHLD=0");
		break;
	}

	return 0;
}

static int _btsrv_hfp_hf_holdcur_answer_call(void)
{
	struct bt_conn *br_conn = btsrv_rdm_hfp_get_actived();

	if (!br_conn)
		return -ESRCH;

	int state = btsrv_rdm_hfp_get_state(br_conn);

	if (state == BTSRV_HFP_STATE_CALL_3WAYIN ||
		state == BTSRV_HFP_STATE_CALL_MULTIPARTY) {
		hostif_bt_hfp_hf_send_cmd(br_conn, BT_HFP_USER_AT_CMD, "AT+CHLD=2");
		return 0;
	}

	struct bt_conn *second_conn = btsrv_rdm_hfp_get_second_dev();

	if (!second_conn)
		return 0;

	int other_dev_state = btsrv_rdm_hfp_get_state(second_conn);

	if (state == BTSRV_HFP_STATE_CALL_INCOMING) {
		if (other_dev_state == BTSRV_HFP_STATE_CALL_INCOMING ||
			other_dev_state == BTSRV_HFP_STATE_CALL_OUTCOMING ||
			other_dev_state == BTSRV_HFP_STATE_CALL_ALERTED) {
			hostif_bt_hfp_hf_send_cmd(br_conn, BT_HFP_USER_AT_CMD, "ATA");
			return 0;
		}
	}

	if (state == BTSRV_HFP_STATE_CALL_OUTCOMING ||
		state == BTSRV_HFP_STATE_CALL_ALERTED) {
		if (other_dev_state == BTSRV_HFP_STATE_CALL_OUTCOMING ||
			other_dev_state == BTSRV_HFP_STATE_CALL_ALERTED) {
			hostif_bt_hfp_hf_send_cmd(br_conn, BT_HFP_USER_AT_CMD, "AT+CHUP");
			return 0;
		}
	}

	switch (state) {
	case BTSRV_HFP_STATE_SCO_ESTABLISHED:	/* to do for siri */
		break;
	}

	switch (other_dev_state) {
	case BTSRV_HFP_STATE_CALL_INCOMING:
		hostif_bt_hfp_hf_send_cmd(second_conn, BT_HFP_USER_AT_CMD, "ATA");
		break;
	case BTSRV_HFP_STATE_CALL_3WAYIN:
	case BTSRV_HFP_STATE_CALL_MULTIPARTY:
		hostif_bt_hfp_hf_send_cmd(second_conn, BT_HFP_USER_AT_CMD, "AT+CHLD=2");
		break;
	}

	btsrv_rdm_hfp_actived(second_conn, 1, 1);
	return 0;
}

static int _btsrv_hfp_hf_hangupcur_answer_call(void)
{
	struct bt_conn *br_conn = btsrv_rdm_hfp_get_actived();

	if (!br_conn)
		return -ESRCH;

	int state = btsrv_rdm_hfp_get_state(br_conn);

	if (state == BTSRV_HFP_STATE_CALL_3WAYIN ||
		state == BTSRV_HFP_STATE_CALL_MULTIPARTY) {
		hostif_bt_hfp_hf_send_cmd(br_conn, BT_HFP_USER_AT_CMD, "AT+CHLD=1");
		return 0;
	}

	struct bt_conn *second_conn = btsrv_rdm_hfp_get_second_dev();

	if (!second_conn)
		return 0;

	int other_dev_state = btsrv_rdm_hfp_get_state(second_conn);

	switch (state) {
	case BTSRV_HFP_STATE_CALL_INCOMING:
	case BTSRV_HFP_STATE_CALL_OUTCOMING:
	case BTSRV_HFP_STATE_CALL_ALERTED:
	case BTSRV_HFP_STATE_CALL_ONGOING:
		hostif_bt_hfp_hf_send_cmd(br_conn, BT_HFP_USER_AT_CMD, "AT+CHUP");
		break;
	case BTSRV_HFP_STATE_SCO_ESTABLISHED:	/* to do */
		break;
	}

	switch (other_dev_state) {
	case BTSRV_HFP_STATE_CALL_INCOMING:
		hostif_bt_hfp_hf_send_cmd(second_conn, BT_HFP_USER_AT_CMD, "ATA");
		break;
	case BTSRV_HFP_STATE_CALL_3WAYIN:
	case BTSRV_HFP_STATE_CALL_MULTIPARTY:
		hostif_bt_hfp_hf_send_cmd(second_conn, BT_HFP_USER_AT_CMD, "AT+CHLD=1");
		break;
	}
	btsrv_rdm_hfp_actived(second_conn, 1, 1);

	return 0;

}

static int _btsrv_hfp_hf_voice_recognition_start(void)
{
	char at_command[32];
	u8_t srir_state = 2;
	struct bt_conn *br_conn = btsrv_rdm_hfp_get_actived();

	if (!br_conn)
		return -ESRCH;

	int state = btsrv_rdm_hfp_get_state(br_conn);

	if (state >= BTSRV_HFP_STATE_CALL_INCOMING &&
		state <= BTSRV_HFP_STATE_SCO_ESTABLISHED)
		return -EBUSY;

	snprintf(at_command, sizeof(at_command), "AT+BVRA=1");

	hostif_bt_hfp_hf_send_cmd(br_conn, BT_HFP_USER_AT_CMD, at_command);

	hfp_context.hfp_user_callback(BTSRV_HFP_SIRI_STATE_CHANGE, (u8_t *)&srir_state, sizeof(srir_state));
	return 0;
}

static int _btsrv_hfp_hf_voice_recognition_stop(void)
{
	char at_command[32];
	struct bt_conn *br_conn = btsrv_rdm_hfp_get_actived();

	if (!br_conn)
		return -ESRCH;

	snprintf(at_command, sizeof(at_command), "AT+BVRA=0");

	hostif_bt_hfp_hf_send_cmd(br_conn, BT_HFP_USER_AT_CMD, at_command);

	return 0;
}

static int _btsrv_hfp_get_time()
{
	char at_command[32];
	struct bt_conn *br_conn = btsrv_rdm_hfp_get_actived();

	if (!br_conn)
		return -ESRCH;

	snprintf(at_command, sizeof(at_command), "AT+CCLK?");
	hostif_bt_hfp_hf_send_cmd(br_conn, BT_HFP_USER_AT_CMD, at_command);

	return 0;
}

static int _btsrv_hfp_hf_send_at_command(u8_t *command,u8_t active_call)
{
	if(active_call){
		struct bt_conn *br_conn = btsrv_rdm_hfp_get_actived();

		if (!br_conn)
			return -ESRCH;
		hostif_bt_hfp_hf_send_cmd(br_conn, BT_HFP_USER_AT_CMD, command);
	}else{
		struct bt_conn *second_conn = btsrv_rdm_hfp_get_second_dev();

		if (!second_conn)
			return -ESRCH;

		hostif_bt_hfp_hf_send_cmd(second_conn, BT_HFP_USER_AT_CMD, command);
	}
	return 0;
}

int btsrv_hfp_get_call_state(u8_t active_call)
{
	if(active_call){
		struct bt_conn *br_conn = btsrv_rdm_hfp_get_actived();

		if (!br_conn)
			return BTSRV_HFP_STATE_INIT;

		return btsrv_rdm_hfp_get_state(br_conn);
	}else{
		struct bt_conn *second_conn = btsrv_rdm_hfp_get_second_dev();

		if (!second_conn)
			return BTSRV_HFP_STATE_INIT;

		return btsrv_rdm_hfp_get_state(second_conn);
	}
}

int btsrv_hfp_process(struct app_msg *msg)
{
	struct bt_conn *param;
	if (_btsrv_get_msg_param_type(msg) != MSG_BTSRV_HFP) {
		return -ENOTSUP;
	}

	switch (_btsrv_get_msg_param_cmd(msg)) {
	case MSG_BTSRV_HFP_START:
		SYS_LOG_INF("MSG_BTSRV_HFP_START\n");
		btsrv_hfp_init(msg->ptr);
		break;
	case MSG_BTSRV_HFP_STOP:
		SYS_LOG_INF("MSG_BTSRV_HFP_STOP\n");
		btsrv_hfp_deinit();
		break;
	case MSG_BTSRV_HFP_CONNECTED:
		_btsrv_hfp_connected(msg->ptr);
		break;
	case MSG_BTSRV_HFP_DISCONNECTED:
		_btsrv_hfp_disconnected(msg->ptr);
		btsrv_event_notify(MSG_BTSRV_CONNECT, _btsrv_get_msg_param_cmd(msg), msg->ptr);
		break;
	case MSG_BTSRV_HFP_SET_STATE:
		SYS_LOG_INF("MSG_BTSRV_HFP_SET_STATE:\n");
		btsrv_hfp_set_status(msg->ptr, msg->reserve);
		break;
	case MSG_BTSRV_HFP_HF_DIAL_NUM:
		SYS_LOG_INF("MSG_BTSRV_HFP_HF_DIAL_NUM\n");
		_btsrv_hfp_hf_dial_number(msg->ptr);
		break;
	case MSG_BTSRV_HFP_HF_DIAL_LAST_NUM:
		SYS_LOG_INF("MSG_BTSRV_HFP_HF_DIAL_LAST_NUM\n");
		_btsrv_hfp_hf_dial_last_number();
		break;
	case MSG_BTSRV_HFP_HF_DIAL_MEMORY:
		SYS_LOG_INF("MSG_BTSRV_HFP_HF_DIAL_MEMORY\n");
		_btsrv_hfp_hf_dial_memory(msg->value);
		break;
	case MSG_BTSRV_HFP_HF_VOLUME_CONTROL:
		SYS_LOG_INF("MSG_BTSRV_HFP_HF_VOLUME_CONTROL\n");
		_btsrv_hfp_hf_volume_control((u8_t)(msg->value >> 16), (u8_t)msg->value);
		break;
	case MSG_BTSRV_HFP_HF_BATTERY_REPORT:
		SYS_LOG_INF("MSG_BTSRV_HFP_HF_BATTERY_REPORT\n");
		_btsrv_hfp_hf_battery_report((u8_t)(msg->value >> 16), (u8_t)msg->value);
		break;
	case MSG_BTSRV_HFP_HF_ACCEPT_CALL:
		SYS_LOG_INF("MSG_BTSRV_HFP_HF_ACCEPT_CALL\n");
		_btsrv_hfp_hf_accept_call();
		break;
	case MSG_BTSRV_HFP_HF_REJECT_CALL:
		SYS_LOG_INF("MSG_BTSRV_HFP_HF_REJECT_CALL\n");
		_btsrv_hfp_hf_reject_call();
		break;
	case MSG_BTSRV_HFP_HF_HANGUP_CALL:
		SYS_LOG_INF("MSG_BTSRV_HFP_HF_HANGUP_CALL\n");
		_btsrv_hfp_hf_hangup_call();
		break;
	case MSG_BTSRV_HFP_HF_HANGUP_ANOTHER_CALL:
		SYS_LOG_INF("MSG_BTSRV_HFP_HF_HANGUP_ANOTHER_CALL\n");
		_btsrv_hfp_hf_hangup_another_call();
		break;
	case MSG_BTSRV_HFP_HF_HOLDCUR_ANSWER_CALL:
		SYS_LOG_INF("MSG_BTSRV_HFP_HF_HOLDCUR_ANSWER_CALL\n");
		_btsrv_hfp_hf_holdcur_answer_call();
		break;
	case MSG_BTSRV_HFP_HF_HANGUPCUR_ANSWER_CALL:
		SYS_LOG_INF("MSG_BTSRV_HFP_HF_HANGUPCUR_ANSWER_CALL\n");
		_btsrv_hfp_hf_hangupcur_answer_call();
		break;
	case MSG_BTSRV_HFP_HF_VOICE_RECOGNITION_START:
		SYS_LOG_INF("MSG_BTSRV_HFP_HF_VOICE_RECOGNITION_START\n");
		_btsrv_hfp_hf_voice_recognition_start();
		break;
	case MSG_BTSRV_HFP_HF_VOICE_RECOGNITION_STOP:
		SYS_LOG_INF("MSG_BTSRV_HFP_HF_VOICE_RECOGNITION_STOP\n");
		_btsrv_hfp_hf_voice_recognition_stop();
		break;
	case MSG_BTSRV_HFP_HF_VOICE_SEND_AT_COMMAND:
		SYS_LOG_INF("MSG_BTSRV_HFP_HF_VOICE_SEND_AT_COMMAND\n");
		_btsrv_hfp_hf_send_at_command(msg->ptr, _btsrv_get_msg_param_reserve(msg));
		break;
	case MSG_BTSRV_HFP_SWITCH_SOUND_SOURCE:
		SYS_LOG_INF("MSG_BTSRV_HFP_SWITCH_SOUND_SOURCE\n");
		_btsrv_hfp_hf_switch_sound_source();
		break;
	case MSG_BTSRV_HFP_PHONE_NUM:
		SYS_LOG_INF("MSG_BTSRV_HFP_PHONE_NUM\n");
		param = (struct bt_conn *) *(int *)(msg->ptr);

		_btsrv_hfp_phone_num(param, (char *)((u32_t)msg->ptr+4));
		break;
	case MSG_BTSRV_HFP_CODEC_INFO:
		SYS_LOG_INF("MSG_BTSRV_HFP_CODEC_INFO\n");
		_btsrv_hfp_codec_info(msg->ptr, msg->reserve);
		break;
	case MSG_BTSRV_HFP_VOLUME_CHANGE:
		SYS_LOG_INF("MSG_BTSRV_HFP_VOLUME_CHANGE\n");
		_btsrv_hfp_volume_change(msg->ptr, msg->reserve);
		break;
	case MSG_BTSRV_HFP_ACTIVED_DEV_CHANGED:
		SYS_LOG_INF("MSG_BTSRV_HFP_ACTIVED_DEV_CHANGED\n");
		_btsrv_hfp_active_dev_change(msg->ptr);
		break;
	case MSG_BTSRV_HFP_SIRI_STATE:
		SYS_LOG_INF("MSG_BTSRV_HFP_SIRI_STATE\n");
		_btsrv_hfp_siri_state_change(msg->ptr, msg->reserve);
		break;
	case MSG_BTSRV_HFP_TIME_UPDATE:
		SYS_LOG_INF("MSG_BTSRV_HFP_TIME_UPDATE\n");
		param = (struct bt_conn *) *(int *)(msg->ptr);

		_btsrv_hfp_time_update(param, (char *)(msg->ptr)+4);
		break;
	case MSG_BTSRV_HFP_GET_TIME:
		SYS_LOG_INF("MSG_BTSRV_HFP_GET_TIME\n");
		_btsrv_hfp_get_time();
		break;
	}
	return 0;
}

