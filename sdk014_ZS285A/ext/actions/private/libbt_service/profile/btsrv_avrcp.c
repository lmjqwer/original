/*
 * Copyright (c) 2017 Actions Semi Co., Ltd.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief btsrvice avrcp
 */
#include <errno.h>
#include <zephyr/types.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <misc/printk.h>
#include <misc/byteorder.h>
#include <zephyr.h>
#include <thread_timer.h>

#include <bluetooth/host_interface.h>

#include <mem_manager.h>
#include "btservice_api.h"
#include "btsrv_inner.h"

#define AVRCP_CTRL_PASS_CHECK_INTERVAL			5		/* 5ms */
#define AVRCP_WAIT_ACCEPT_PUSH_TIMEOUT			1000	/* 1000ms */
#define AVRCP_CTRL_PASS_DELAY_RELEASE_TIME		30		/* 30ms */
#define AVRCP_CHECK_PAUSE_INTERVAL				100		/* 100ms */
#define AVRCP_PAUSE_SWITCH_TIME					500		/* 500ms */
#define AVRCP_SWITCH_MUSIC_PAUSE_TIME			300		/* 300ms */

enum {
	AVRCP_PASS_STATE_IDLE,
	AVRCP_PASS_STATE_WAIT_ACCEPT_PUSH,
	AVRCP_PASS_STATE_WAIT_DELAY_RELEASE,
	AVRCP_PASS_STATE_CONTINUE_START,
};

struct avrcp_play_status_t {
	struct bt_conn *conn;
	u8_t play_state;
	u32_t song_len;
	u32_t song_pos;
};

static btsrv_avrcp_callback_t *avrcp_user_callback;
static struct thread_timer sync_volume_timer;
static struct thread_timer ctrl_pass_timer;
static struct thread_timer check_puase_timer;
static void btsrv_avrcp_check_puase_timer_start_stop(bool start);
static void btsrv_avrcp_check_puase_handler(struct thread_timer *ttimer, void *expiry_fn_arg);
static OS_MUTEX_DEFINE(ctrl_pass_mutex);

static void _btsrv_avrcp_ctrl_connected_cb(struct bt_conn *conn)
{
	btsrv_event_notify(MSG_BTSRV_CONNECT, MSG_BTSRV_AVRCP_CONNECTED, conn);
}

static void _btsrv_avrcp_ctrl_disconnected_cb(struct bt_conn *conn)
{
	/* TODO: Disconnected process order: btsrv_tws->btsrv_avrcp->btsrv_connect */
	btsrv_event_notify(MSG_BTSRV_CONNECT, MSG_BTSRV_AVRCP_DISCONNECTED, conn);
}

static void _btsrv_avrcp_ctrl_event_notify_cb(struct bt_conn *conn, u8_t event_id, u8_t status)
{
	int cmd = -1;
	u32_t start_time = 0;
	u32_t delay_time = btsrv_volume_sync_delay_ms();

	if (event_id != BT_AVRCP_EVENT_PLAYBACK_STATUS_CHANGED &&
		event_id != BT_AVRCP_EVENT_VOLUME_CHANGED &&
		event_id != BT_AVRCP_EVENT_TRACK_CHANGED) {
		SYS_LOG_WRN("avrcp is NULL or event_id %d not care\n", event_id);
		return;
	}

	if (event_id == BT_AVRCP_EVENT_VOLUME_CHANGED &&
		btsrv_rdm_a2dp_get_actived() == conn) {

		if (!btsrv_rdm_is_a2dp_stream_open(conn)) {
			avrcp_user_callback->set_volume_cb(conn, status);
			return;
		} else {
			btsrv_rdm_get_a2dp_start_time(conn, &start_time);
			if (start_time && (os_uptime_get_32() - start_time) > delay_time
				 && !thread_timer_is_running(&sync_volume_timer))
				avrcp_user_callback->set_volume_cb(conn, status);
			return;
		}

	}else if (event_id == BT_AVRCP_EVENT_TRACK_CHANGED){
		cmd = MSG_BTSRV_AVRCP_PLAYBACK_TRACK_CHANGE;
	}else if(event_id == BT_AVRCP_EVENT_PLAYBACK_STATUS_CHANGED){
	switch (status) {
		case BT_AVRCP_PLAYBACK_STATUS_STOPPED:
			cmd = MSG_BTSRV_AVRCP_PLAYBACK_STATUS_STOPPED;
			break;
		case BT_AVRCP_PLAYBACK_STATUS_PLAYING:
			cmd = MSG_BTSRV_AVRCP_PLAYBACK_STATUS_PLAYING;
			break;
		case BT_AVRCP_PLAYBACK_STATUS_PAUSED:
			cmd = MSG_BTSRV_AVRCP_PLAYBACK_STATUS_PAUSED;
			break;
		case BT_AVRCP_PLAYBACK_STATUS_FWD_SEEK:
			cmd = MSG_BTSRV_AVRCP_PLAYBACK_STATUS_FWD_SEEK;
			break;
		case BT_AVRCP_PLAYBACK_STATUS_REV_SEEK:
			cmd = MSG_BTSRV_AVRCP_PLAYBACK_STATUS_REV_SEEK;
			break;
		case BT_AVRCP_PLAYBACK_STATUS_ERROR:
			cmd = MSG_BTSRV_AVRCP_PLAYBACK_STATUS_ERROR;
			break;
		}
	}

	if (cmd > 0) {
		btsrv_event_notify(MSG_BTSRV_AVRCP, cmd, conn);
	}
}

static void _btsrv_avrcp_ctrl_get_volume_cb(struct bt_conn *conn, u8_t *volume)
{
	avrcp_user_callback->get_volume_cb(conn, volume);
}

static void btsrv_avrcp_ctrl_rsp_pass_proc(struct bt_conn *conn, u8_t op_id, u8_t state)
{
	struct btsrv_rdm_avrcp_pass_info *info;

	if (state != BT_AVRCP_RSP_STATE_PASS_THROUGH_PUSHED) {
		return;
	}

	info = btsrv_rdm_avrcp_get_pass_info(conn);
	if (!info) {
		return;
	}

	os_mutex_lock(&ctrl_pass_mutex, OS_FOREVER);
	if (info->op_id == op_id && info->pass_state == AVRCP_PASS_STATE_WAIT_ACCEPT_PUSH) {
		info->pass_state = AVRCP_PASS_STATE_WAIT_DELAY_RELEASE;
		info->op_time = os_uptime_get_32();
	}
	os_mutex_unlock(&ctrl_pass_mutex);
}

static void _btsrv_avrcp_ctrl_pass_ctrl_cb(struct bt_conn *conn, u8_t op_id, u8_t state)
{
	u8_t cmd;

	SYS_LOG_INF("conn 0x%x op_id 0x%x state 0x%x\n", hostif_bt_conn_get_acl_handle(conn), op_id, state);

	if (state == BT_AVRCP_RSP_STATE_PASS_THROUGH_PUSHED ||
		state == BT_AVRCP_RSP_STATE_PASS_THROUGH_RELEASED) {
		btsrv_avrcp_ctrl_rsp_pass_proc(conn, op_id, state);
	} else {
		if (btsrv_rdm_a2dp_get_actived() == conn) {
			switch(op_id){
			case AVRCP_OPERATION_ID_PLAY:
				cmd = BTSRV_AVRCP_CMD_PLAY;
				break;
			case AVRCP_OPERATION_ID_PAUSE:
				cmd = BTSRV_AVRCP_CMD_PAUSE;
				break;
			case AVRCP_OPERATION_ID_VOLUME_UP:
				cmd = BTSRV_AVRCP_CMD_VOLUMEUP;
				break;
			case AVRCP_OPERATION_ID_VOLUME_DOWN:
				cmd = BTSRV_AVRCP_CMD_VOLUMEDOWN;
				break;
			case AVRCP_OPERATION_ID_MUTE:
				cmd = BTSRV_AVRCP_CMD_MUTE;
				break;
			default:
				SYS_LOG_ERR("op_id %d not support\n", op_id);
				return;
			}
			avrcp_user_callback->pass_ctrl_cb(conn, cmd,state);
		}
	}
}

static void _btsrv_avrcp_ctrl_get_play_status(struct bt_conn *conn, u32_t *song_len,
									u32_t *song_pos, u8_t *play_state)
{
	struct avrcp_play_status_t param;

	param.conn = conn;
	param.play_state = *play_state;
	param.song_len = *song_len;
	param.song_pos = *song_pos;

	btsrv_event_notify_malloc(MSG_BTSRV_AVRCP, MSG_BTSRV_AVRCP_GET_PLAY_STATUS, (u8_t *)&param, sizeof(param), 0);
}

static void _btsrv_avrcp_ctrl_update_id3_info_cb(struct bt_conn *conn, struct id3_info * info)
{
	if (btsrv_rdm_a2dp_get_actived() == conn){
		avrcp_user_callback->event_cb(BTSRV_AVRCP_UPDATE_ID3_INFO,info);
	}
}

static const struct bt_avrcp_app_cb btsrv_avrcp_ctrl_cb = {
	.connected = _btsrv_avrcp_ctrl_connected_cb,
	.disconnected = _btsrv_avrcp_ctrl_disconnected_cb,
	.notify = _btsrv_avrcp_ctrl_event_notify_cb,
	.pass_ctrl = _btsrv_avrcp_ctrl_pass_ctrl_cb,
	.get_play_status = _btsrv_avrcp_ctrl_get_play_status,
	.get_volume = _btsrv_avrcp_ctrl_get_volume_cb,
	.update_id3_info = _btsrv_avrcp_ctrl_update_id3_info_cb,
};

static void btsrv_avrcp_ctrl_pass_timer_start_stop(bool start)
{
	if (start) {
		if (!thread_timer_is_running(&ctrl_pass_timer)) {
			SYS_LOG_INF("Start ctrl_pass_timer\n");
			thread_timer_start(&ctrl_pass_timer, AVRCP_CTRL_PASS_CHECK_INTERVAL, AVRCP_CTRL_PASS_CHECK_INTERVAL);
		}
	} else {
		if (thread_timer_is_running(&ctrl_pass_timer)) {
			SYS_LOG_INF("Stop ctrl_pass_timer\n");
			thread_timer_stop(&ctrl_pass_timer);
		}
	}
}

static void connected_dev_cb_check_ctrl_pass(struct bt_conn *base_conn, u8_t tws_dev, void *cb_param)
{
	int *need_timer = cb_param;
	struct btsrv_rdm_avrcp_pass_info *info;
	u32_t time, check_timeout = 0;

	if (tws_dev) {
		return;
	}

	info = btsrv_rdm_avrcp_get_pass_info(base_conn);
	if (info->op_id == 0 ||
		info->pass_state == AVRCP_PASS_STATE_IDLE ||
		info->pass_state == AVRCP_PASS_STATE_CONTINUE_START) {
		return;
	}

	*need_timer = 1;
	time = os_uptime_get_32();

	if (info->pass_state == AVRCP_PASS_STATE_WAIT_ACCEPT_PUSH) {
		check_timeout = AVRCP_WAIT_ACCEPT_PUSH_TIMEOUT;
	} else if (info->pass_state == AVRCP_PASS_STATE_WAIT_DELAY_RELEASE) {
		check_timeout = AVRCP_CTRL_PASS_DELAY_RELEASE_TIME;
	}

	if ((time - info->op_time) > check_timeout) {
		hostif_bt_avrcp_ct_pass_through_cmd(base_conn, info->op_id, false);
		info->pass_state = AVRCP_PASS_STATE_IDLE;
		info->op_id = 0;
	}
}

static void btsrv_avrcp_ctrl_pass_timer_handler(struct thread_timer *ttimer, void *expiry_fn_arg)
{
	int need_timer = 0;

	os_mutex_lock(&ctrl_pass_mutex, OS_FOREVER);
	btsrv_rdm_get_connected_dev(connected_dev_cb_check_ctrl_pass, &need_timer);
	os_mutex_unlock(&ctrl_pass_mutex);

	if (!need_timer) {
		btsrv_avrcp_ctrl_pass_timer_start_stop(false);
	}
}
/*监控调整音量的定时器，通过连接的蓝牙获取音量*/
static void btsrv_avrcp_sync_volume_timer_handler(struct thread_timer *ttimer, void *expiry_fn_arg)
{
	u8_t volume;
	struct bt_conn *avrcp_conn = btsrv_rdm_a2dp_get_actived();
    printk("enter timer\n");
	avrcp_user_callback->get_volume_cb(avrcp_conn, &volume);
	SYS_LOG_INF("conn %p, volume %d\n", avrcp_conn, volume);
    printk("conn %p, volume %d\n", avrcp_conn, volume);
	if (!btsrv_is_pts_test()) {
		hostif_bt_avrcp_tg_notify_change(avrcp_conn, volume);
	}
}

int btsrv_avrcp_init(btsrv_avrcp_callback_t *cb)
{
	hostif_bt_avrcp_cttg_register_cb((struct bt_avrcp_app_cb *)&btsrv_avrcp_ctrl_cb);
	avrcp_user_callback = cb;

	thread_timer_init(&sync_volume_timer, btsrv_avrcp_sync_volume_timer_handler, NULL);
	thread_timer_init(&ctrl_pass_timer, btsrv_avrcp_ctrl_pass_timer_handler, NULL);
	thread_timer_init(&check_puase_timer, btsrv_avrcp_check_puase_handler, NULL);

	return 0;
}

int btsrv_avrcp_deinit(void)
{
	btsrv_avrcp_check_puase_timer_start_stop(false);
	btsrv_avrcp_ctrl_pass_timer_start_stop(false);
	if (thread_timer_is_running(&sync_volume_timer)) {
		thread_timer_stop(&sync_volume_timer);
	}
	return 0;
}

int btsrv_avrcp_disconnect(struct bt_conn *conn)
{
	if (conn && btsrv_rdm_is_avrcp_connected(conn)) {
		SYS_LOG_INF("avrcp_disconnect\n");
		hostif_bt_avrcp_cttg_disconnect(conn);
	}
	return 0;
}

int btsrv_avrcp_connect(struct bt_conn *conn)
{
	int ret = 0;

	if (!hostif_bt_avrcp_cttg_connect(conn)) {
		SYS_LOG_INF("Connect avrcp\n");
		ret = 0;
	} else {
		SYS_LOG_ERR("Connect avrcp failed\n");
		ret = -1;
	}

	return ret;
}

int btsrv_avrcp_sync_vol(void)
{
	u32_t start_time = 0, diff;
	u32_t delay_time = btsrv_volume_sync_delay_ms();

	if (thread_timer_is_running(&sync_volume_timer))
		return -1;

	struct bt_conn *avrcp_conn = btsrv_rdm_a2dp_get_actived();

	btsrv_rdm_get_a2dp_start_time(avrcp_conn, &start_time);
	diff = os_uptime_get_32() - start_time;

	if (diff >= delay_time)
		btsrv_avrcp_sync_volume_timer_handler(NULL, NULL);
	else
		thread_timer_start(&sync_volume_timer, delay_time - diff, 0);

	return 0;
}

static int btsrv_avrcp_ct_pass_through_cmd(struct bt_conn *conn,
										u8_t opid, bool continue_cmd, bool start)
{
	int ret = 0;
	struct btsrv_rdm_avrcp_pass_info *info;

	info = btsrv_rdm_avrcp_get_pass_info(conn);
	if (!info) {
		return -EIO;
	}

	os_mutex_lock(&ctrl_pass_mutex, OS_FOREVER);

	if (continue_cmd) {
		if ((start && info->pass_state != AVRCP_PASS_STATE_IDLE) ||
			(!start && info->pass_state != AVRCP_PASS_STATE_CONTINUE_START)) {
			SYS_LOG_INF("Pass busy %d op_id 0x%x\n", info->pass_state, info->op_id);
			ret = -EBUSY;
			goto pass_exit;
		}

		if (start) {
			info->pass_state = AVRCP_PASS_STATE_CONTINUE_START;
			info->op_id = opid;
		} else {
			info->pass_state = AVRCP_PASS_STATE_IDLE;
			info->op_id = 0;
		}

		ret = hostif_bt_avrcp_ct_pass_through_continue_cmd(conn, opid, start);
	} else {
		if (info->pass_state != AVRCP_PASS_STATE_IDLE) {
			SYS_LOG_INF("Pass busy %d op_id 0x%x\n", info->pass_state, info->op_id);
			ret = -EBUSY;
			goto pass_exit;
		}

		info->pass_state = AVRCP_PASS_STATE_WAIT_ACCEPT_PUSH;
		info->op_id = opid;
		info->op_time = os_uptime_get_32();
		btsrv_avrcp_ctrl_pass_timer_start_stop(true);

		ret = hostif_bt_avrcp_ct_pass_through_cmd(conn, opid, true);
	}

pass_exit:
	os_mutex_unlock(&ctrl_pass_mutex);
	return ret;
}

static int  _btsrv_avrcp_controller_process(btsrv_avrcp_cmd_e cmd)
{
	int status = 0;
	struct bt_conn *avrcp_conn = btsrv_rdm_a2dp_get_actived();

	switch (cmd) {
	case BTSRV_AVRCP_CMD_PLAY:
	status = btsrv_avrcp_ct_pass_through_cmd(
				avrcp_conn, AVRCP_OPERATION_ID_PLAY, false, false);
	break;
	case BTSRV_AVRCP_CMD_PAUSE:
	status = btsrv_avrcp_ct_pass_through_cmd(
				avrcp_conn, AVRCP_OPERATION_ID_PAUSE, false, false);
	break;
	case BTSRV_AVRCP_CMD_STOP:
	status = btsrv_avrcp_ct_pass_through_cmd(
				avrcp_conn, AVRCP_OPERATION_ID_STOP, false, false);
	break;
	case BTSRV_AVRCP_CMD_FORWARD:
	status = btsrv_avrcp_ct_pass_through_cmd(
				avrcp_conn, AVRCP_OPERATION_ID_FORWARD, false, false);
	break;
	case BTSRV_AVRCP_CMD_BACKWARD:
	status = btsrv_avrcp_ct_pass_through_cmd(
				avrcp_conn, AVRCP_OPERATION_ID_BACKWARD, false, false);
	break;
	case BTSRV_AVRCP_CMD_VOLUMEUP:
	status = btsrv_avrcp_ct_pass_through_cmd(
				avrcp_conn, AVRCP_OPERATION_ID_VOLUME_UP, false, false);
	break;
	case BTSRV_AVRCP_CMD_VOLUMEDOWN:
	status = btsrv_avrcp_ct_pass_through_cmd(
				avrcp_conn, AVRCP_OPERATION_ID_VOLUME_DOWN, false, false);
	break;
	case BTSRV_AVRCP_CMD_MUTE:
	status = btsrv_avrcp_ct_pass_through_cmd(
				avrcp_conn, AVRCP_OPERATION_ID_MUTE, false, false);
	break;
	case BTSRV_AVRCP_CMD_FAST_FORWARD_START:
	status = btsrv_avrcp_ct_pass_through_cmd(
				avrcp_conn, AVRCP_OPERATION_ID_FAST_FORWARD, true, true);
	break;
	case BTSRV_AVRCP_CMD_FAST_FORWARD_STOP:
	status = btsrv_avrcp_ct_pass_through_cmd(
				avrcp_conn, AVRCP_OPERATION_ID_FAST_FORWARD, true, false);
	break;
	case BTSRV_AVRCP_CMD_FAST_BACKWARD_START:
	status = btsrv_avrcp_ct_pass_through_cmd(
				avrcp_conn, AVRCP_OPERATION_ID_REWIND, true, true);
	break;
	case BTSRV_AVRCP_CMD_FAST_BACKWARD_STOP:
	status = btsrv_avrcp_ct_pass_through_cmd(
				avrcp_conn, AVRCP_OPERATION_ID_REWIND, true, false);
	break;
	case BTSRV_AVRCP_CMD_REPEAT_SINGLE:
	case BTSRV_AVRCP_CMD_REPEAT_ALL_TRACK:
	case BTSRV_AVRCP_CMD_REPEAT_OFF:
	case BTSRV_AVRCP_CMD_SHUFFLE_ON:
	case BTSRV_AVRCP_CMD_SHUFFLE_OFF:
	default:
	SYS_LOG_ERR("cmd 0x%02x not support\n", cmd);
	return -EINVAL;
	}

	if (status < 0) {
		SYS_LOG_ERR("0x%02x failed, status 0x%02x avrcp->avrcp_conn %p\n",
					cmd, status, avrcp_conn);
	} else {
		SYS_LOG_INF("0x%02x ok, avrcp->avrcp_conn %p\n",
					cmd, avrcp_conn);
	}

	return status;
}

static int  _btsrv_avrcp_get_id3_info()
{
	int status = 0;
	struct bt_conn *avrcp_conn = btsrv_rdm_a2dp_get_actived();
	if(!avrcp_conn)
		return -EINVAL;
	status = hostif_bt_avrcp_ct_get_id3_info(avrcp_conn);

	return status;
}

static int btsrv_avrcp_set_absolute_volume(u32_t param)
{
	struct bt_conn *avrcp_conn = btsrv_rdm_a2dp_get_actived();

	if(!avrcp_conn) {
		return -EIO;
	}

	return hostif_bt_avrcp_ct_set_absolute_volume(avrcp_conn, param);
}

static void btsrv_avrcp_check_puase_timer_start_stop(bool start)
{
	if (start) {
		if (!thread_timer_is_running(&check_puase_timer)) {
			SYS_LOG_INF("Start check_puase_timer\n");
			thread_timer_start(&check_puase_timer, AVRCP_CHECK_PAUSE_INTERVAL, AVRCP_CHECK_PAUSE_INTERVAL);
		}
	} else {
		if (thread_timer_is_running(&check_puase_timer)) {
			SYS_LOG_INF("Stop check_puase_timer\n");
			thread_timer_stop(&check_puase_timer);
		}
	}
}

static void btsrv_avrcp_check_puase_handler(struct thread_timer *ttimer, void *expiry_fn_arg)
{
	u8_t need_timer = 1, status;
	u32_t puase_time, curr_time;
	struct bt_conn *active_conn = btsrv_rdm_a2dp_get_actived();
	struct bt_conn *second_conn = btsrv_rdm_a2dp_get_second_dev();

	if (!second_conn) {
		need_timer = 0;
		goto exit_check;
	}

	if (!btsrv_rdm_is_a2dp_stream_open(second_conn) ||
		(btsrv_rdm_avrcp_get_play_status(second_conn) != BTSRV_AVRCP_PLAYSTATUS_PLAYING)) {
		need_timer = 0;
		goto exit_check;
	}

	status = btsrv_rdm_avrcp_get_play_status(active_conn);
	if (status == BTSRV_AVRCP_PLAYSTATUS_PAUSEED) {
		curr_time = os_uptime_get_32();
		puase_time = btsrv_rdm_avrcp_get_pause_time(active_conn);
		if ((curr_time - puase_time) > AVRCP_PAUSE_SWITCH_TIME) {
			SYS_LOG_INF("avrcp pause 0x%x switch 0x%x\n", hostif_bt_conn_get_acl_handle(active_conn),
						hostif_bt_conn_get_acl_handle(second_conn));
			btsrv_rdm_a2dp_actived(second_conn, 1);
			need_timer = 0;
		}
	} else {
		need_timer = 0;
	}

exit_check:
	if (!need_timer) {
		btsrv_avrcp_check_puase_timer_start_stop(false);
	}
}

void btsrv_avrcp_preemption_pause(struct bt_conn *conn)
{
#ifdef CONFIG_BT_DOUBLE_PHONE_PREEMPTION_MODE
	if (btsrv_rdm_avrcp_get_play_status(conn) == BTSRV_AVRCP_PLAYSTATUS_PLAYING) {
		SYS_LOG_INF("preemption pause 0x%x\n", hostif_bt_conn_get_acl_handle(conn));
		btsrv_avrcp_ct_pass_through_cmd(conn, AVRCP_OPERATION_ID_PAUSE, false, false);
	}
#endif
}

void btsrv_avrcp_resume_preemption_play(struct bt_conn *conn)
{
#ifdef CONFIG_BT_DOUBLE_PHONE_PREEMPTION_MODE
	if (btsrv_rdm_avrcp_get_play_status(conn) == BTSRV_AVRCP_PLAYSTATUS_PAUSEED) {
		SYS_LOG_INF("resume preemption play 0x%x\n", hostif_bt_conn_get_acl_handle(conn));
		btsrv_avrcp_ct_pass_through_cmd(conn, AVRCP_OPERATION_ID_PLAY, false, false);
	}
#endif
}

static void btsrv_avrcp_state_change(struct bt_conn *conn, u8_t state)
{
#ifdef CONFIG_BT_DOUBLE_PHONE_PREEMPTION_MODE
	u8_t new_state;
	u32_t puase_time, curr_time;
	struct bt_conn *active_conn = btsrv_rdm_a2dp_get_actived();
	struct bt_conn *second_conn = btsrv_rdm_a2dp_get_second_dev();

	puase_time = btsrv_rdm_avrcp_get_pause_time(conn);		/* Get time before set state */
	new_state = btsrv_rdm_avrcp_set_play_status(conn, state);
	SYS_LOG_INF("avrcp 0x%x state %d new_state %d\n", hostif_bt_conn_get_acl_handle(conn), state, new_state);

	if (!second_conn) {
		return;
	}

	if (new_state == BTSRV_AVRCP_PLAYSTATUS_PLAYING) {
		if (active_conn == conn) {
			return;
		}

		curr_time = os_uptime_get_32();
		if ((curr_time - puase_time) > AVRCP_SWITCH_MUSIC_PAUSE_TIME) {
			btsrv_rdm_a2dp_actived(conn, 1);
		}
	} else if (new_state == BTSRV_AVRCP_PLAYSTATUS_PAUSEED) {
		if (active_conn == conn) {
			btsrv_avrcp_check_puase_timer_start_stop(true);
		}
	}
#endif
}

int btsrv_avrcp_get_play_status(void)
{
	struct bt_conn *avrcp_conn = btsrv_rdm_a2dp_get_actived();

	if(!avrcp_conn) {
		return -EIO;
	}

	return hostif_bt_avrcp_ct_get_play_status(avrcp_conn);
}

static void btsrv_avrcp_proc_get_play_status(struct avrcp_play_status_t *param)
{
	SYS_LOG_INF("play_state %d len %d pos %d", param->play_state, param->song_len, param->song_pos);
}

int btsrv_avrcp_process(struct app_msg *msg)
{
	struct bt_conn *conn;

	switch (_btsrv_get_msg_param_cmd(msg)) {
	case MSG_BTSRV_AVRCP_START:
		SYS_LOG_INF("MSG_BTSRV_AVRCP_START\n");
		btsrv_avrcp_init(msg->ptr);
		break;
	case MSG_BTSRV_AVRCP_STOP:
		SYS_LOG_INF("MSG_BTSRV_AVRCP_STOP\n");
		btsrv_avrcp_deinit();
		break;
	case MSG_BTSRV_AVRCP_CONNECT_TO:
		SYS_LOG_INF("MSG_BTSRV_AVRCP_CONNECT_TO\n");
		conn = btsrv_rdm_find_conn_by_addr(msg->ptr);
		if (conn) {
			btsrv_avrcp_connect(conn);
		}
		break;
	case MSG_BTSRV_AVRCP_DISCONNECT:
		SYS_LOG_INF("MSG_BTSRV_AVRCP_DISCONNECT\n");
		conn = btsrv_rdm_find_conn_by_addr(msg->ptr);
		if (conn) {
			btsrv_avrcp_disconnect(conn);
		}
		break;

	case MSG_BTSRV_AVRCP_SEND_CMD:
		SYS_LOG_INF("MSG_BTSRV_AVRCP_SEND_CMD %d\n", msg->value);
		_btsrv_avrcp_controller_process(msg->value);
		break;
	case MSG_BTSRV_AVRCP_GET_ID3_INFO:
		SYS_LOG_INF("MSG_BTSRV_AVRCP_GET_ID3_INFO\n");
		_btsrv_avrcp_get_id3_info();
		break;
	case MSG_BTSRV_AVRCP_SET_ABSOLUTE_VOLUME:
		SYS_LOG_INF("MSG_BTSRV_AVRCP_SET_ABSOLUTE_VOLUME 0x%x\n", msg->value);
		btsrv_avrcp_set_absolute_volume((u32_t)msg->value);
		break;
	case MSG_BTSRV_AVRCP_CONNECTED:
		avrcp_user_callback->event_cb(BTSRV_AVRCP_CONNECTED, NULL);
		break;
	case MSG_BTSRV_AVRCP_DISCONNECTED:
		avrcp_user_callback->event_cb(BTSRV_AVRCP_DISCONNECTED, NULL);
		break;
	case MSG_BTSRV_AVRCP_PLAYBACK_STATUS_STOPPED:
		SYS_LOG_INF("MSG_BTSRV_AVRCP_PLAYBACK_STATUS_STOPPED");
		btsrv_avrcp_state_change(msg->ptr, BTSRV_AVRCP_PLAYSTATUS_STOPPED);
		if (btsrv_rdm_a2dp_get_actived() == msg->ptr) {
			avrcp_user_callback->event_cb(BTSRV_AVRCP_STOPED, NULL);
		}
		break;
	case MSG_BTSRV_AVRCP_PLAYBACK_STATUS_PLAYING:
		SYS_LOG_INF("MSG_BTSRV_AVRCP_PLAYBACK_STATUS_PLAYING\n");
		btsrv_avrcp_state_change(msg->ptr, BTSRV_AVRCP_PLAYSTATUS_PLAYING);
		if (btsrv_rdm_a2dp_get_actived() == msg->ptr) {
			avrcp_user_callback->event_cb(BTSRV_AVRCP_PLAYING, NULL);
		}
		break;
	case MSG_BTSRV_AVRCP_PLAYBACK_STATUS_PAUSED:
		SYS_LOG_INF("MSG_BTSRV_AVRCP_PLAYBACK_STATUS_PAUSED\n");
		btsrv_avrcp_state_change(msg->ptr, BTSRV_AVRCP_PLAYSTATUS_PAUSEED);
		if (btsrv_rdm_a2dp_get_actived() == msg->ptr) {
			avrcp_user_callback->event_cb(BTSRV_AVRCP_PAUSED, NULL);
		}
		break;
	case MSG_BTSRV_AVRCP_PLAYBACK_STATUS_FWD_SEEK:
		SYS_LOG_DBG("MSG_BTSRV_AVRCP_PLAYBACK_STATUS_FWD_SEEK\n");
		break;
	case MSG_BTSRV_AVRCP_PLAYBACK_STATUS_REV_SEEK:
		SYS_LOG_DBG("MSG_BTSRV_AVRCP_PLAYBACK_STATUS_REV_SEEK\n");
		break;
	case MSG_BTSRV_AVRCP_PLAYBACK_STATUS_ERROR:
		SYS_LOG_DBG("MSG_BTSRV_AVRCP_PLAYBACK_STATUS_ERROR\n");
		break;
	case MSG_BTSRV_AVRCP_PLAYBACK_TRACK_CHANGE:
		SYS_LOG_INF("MSG_BTSRV_AVRCP_PLAYBACK_TRACK_CHANGE\n");
		if (btsrv_rdm_a2dp_get_actived() == msg->ptr) {
			avrcp_user_callback->event_cb(BTSRV_AVRCP_TRACK_CHANGE, NULL);
		}
		break;
	case MSG_BTSRV_AVRCP_SYNC_VOLUME:
		SYS_LOG_DBG("MSG_BTSRV_AVRCP_SYNC_VOLUME\n");
		if (btsrv_rdm_get_dev_role() != BTSRV_TWS_SLAVE) {
			btsrv_avrcp_sync_vol();
		}
		break;
	case MSG_BTSRV_AVRCP_GET_PLAY_STATUS:
		btsrv_avrcp_proc_get_play_status(msg->ptr);
		break;
	default:
		break;
	}

	return 0;
}
