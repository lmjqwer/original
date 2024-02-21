/*
 * Copyright (c) 2017 Actions Semi Co., Ltd.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief btsrvice a2dp
 */

#include <errno.h>
#include <zephyr/types.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <misc/printk.h>
#include <misc/byteorder.h>
#include <zephyr.h>

#include <bluetooth/host_interface.h>

#include <mem_manager.h>
#include "btsrv_inner.h"
static btsrv_a2dp_callback a2dp_user_callback;
static struct bt_a2dp_endpoint a2dp_sbc_endpoint[CONFIG_MAX_A2DP_ENDPOINT];
static struct bt_a2dp_endpoint a2dp_aac_endpoint[CONFIG_MAX_A2DP_ENDPOINT];
static u8_t a2dp_register_aac_num;

#if 0
static const u8_t a2dp_sbc_codec[] = {
	BT_A2DP_AUDIO << 4,
	BT_A2DP_SBC,
	0xFF,	/* (SNK mandatory)44100, 48000, mono, dual channel, stereo, join stereo */
			/* (SNK optional) 16000, 32000 */
	0xFF,	/* (SNK mandatory) Block length: 4/8/12/16, subbands:4/8, Allocation Method: SNR, Londness */
	0x02,	/* min bitpool */
	0x35	/* max bitpool */
};

static const u8_t a2dp_aac_codec[] = {
	BT_A2DP_AUDIO << 4,
	BT_A2DP_MPEG2,
	0xF0,	/* MPEG2 AAC LC, MPEG4 AAC LC, MPEG AAC LTP, MPEG4 AAC Scalable */
	0x01,	/* Sampling Frequecy 44100 */
	0x8F,	/* Sampling Frequecy 48000, channels 1, channels 2 */
	0xFF,	/* VBR, bit rate */
	0xFF,	/* bit rate */
	0xFF	/* bit rate */
};
#endif

#define A2DP_AAC_FREQ_NUM		12
const static u8_t a2dp_aac_freq_table[A2DP_AAC_FREQ_NUM] = {96, 88, 64, 48, 44, 32, 24, 22, 16, 12, 11, 8};

static u8_t btsrv_convert_sample_rate(struct bt_a2dp_media_codec *codec)
{
	u8_t sample_rate = 44, i;
	u16_t freq;

	if (codec->head.codec_type == BT_A2DP_SBC) {
		switch (codec->sbc.freq) {
		case BT_A2DP_SBC_48000:
			sample_rate = 48;
			break;
		case BT_A2DP_SBC_44100:
			sample_rate = 44;
			break;
		case BT_A2DP_SBC_32000:
			sample_rate = 32;
			break;
		case BT_A2DP_SBC_16000:
			sample_rate = 16;
			break;
		}
	} else if (codec->head.codec_type == BT_A2DP_MPEG2) {
		freq = (codec->aac.freq0 << 4) | codec->aac.freq1;
		for (i = 0; i < A2DP_AAC_FREQ_NUM; i++) {
			if (freq & (BT_A2DP_AAC_96000 << i)) {
				sample_rate = a2dp_aac_freq_table[i];
				break;
			}
		}
	}

	return sample_rate;
}

static void _btsrv_a2dp_connect_cb(struct bt_conn *conn)
{
	btsrv_event_notify(MSG_BTSRV_CONNECT, MSG_BTSRV_A2DP_CONNECTED, conn);
}

static void _btsrv_a2dp_disconnected_cb(struct bt_conn *conn)
{
	/* TODO: Disconnected process order: btsrv_tws->btsrv_a2dp->btsrv_connect */
	btsrv_event_notify(MSG_BTSRV_A2DP, MSG_BTSRV_A2DP_DISCONNECTED, conn);
}

#ifdef CONFIG_DEBUG_DATA_RATE
#define TEST_RECORD_NUM			10
static u32_t pre_rx_time;
static u8_t test_index;
static u16_t test_rx_record[TEST_RECORD_NUM][2];

static inline void _btsrv_a2dp_debug_date_rate(u16_t len)
{
	static int start_time_stamp;
	static int received_data;
	u32_t curr_time;

	if (!start_time_stamp) {
		received_data = 0;
		start_time_stamp = k_cycle_get_32();
	}
	received_data += len;
	if ((k_cycle_get_32() - start_time_stamp) > 5000000 * 24) {
		if (start_time_stamp != 0) {
			SYS_LOG_INF("a2dp data rate: %d bytes/s\n", received_data / 5);
		}
		received_data = 0;
		start_time_stamp = k_cycle_get_32();
	}

	curr_time = os_uptime_get_32();
	test_rx_record[test_index][0] = curr_time - pre_rx_time;
	test_rx_record[test_index][1] = len;
	test_index++;
	if (test_index >= TEST_RECORD_NUM) {
		test_index = 0;
	}
	pre_rx_time = curr_time;
}

void debug_dump_media_rx_state(void)
{
	static u32_t pre_dump_time;
	u32_t curr_time;
	u16_t time_cnt = 0, len_cnt = 0;
	int flag, i;

	curr_time = os_uptime_get_32();
	if ((curr_time - pre_dump_time) < 500) {
		return;
	}
	pre_dump_time = curr_time;

	flag = btsrv_set_negative_prio();

	printk("diff_time data_len\n");
	for (i = test_index; i < TEST_RECORD_NUM; i++) {
		time_cnt += test_rx_record[i][0];
		len_cnt += test_rx_record[i][1];
		printk("%d(%d)\t %d(%d)\n", test_rx_record[i][0], time_cnt, test_rx_record[i][1], len_cnt);
	}

	for (i = 0; i < test_index; i++) {
		time_cnt += test_rx_record[i][0];
		len_cnt += test_rx_record[i][1];
		printk("%d(%d)\t %d(%d)\n", test_rx_record[i][0], time_cnt, test_rx_record[i][1], len_cnt);
	}

	printk("%d(%d)\n", curr_time - pre_rx_time, time_cnt + (curr_time - pre_rx_time));
	btsrv_revert_prio(flag);
}
#else
void debug_dump_media_rx_state(void)
{
}
#endif

/** this callback dircty call to app, will in bt stack context */
static void _btsrv_a2dp_media_handler_cb(struct bt_conn *conn, u8_t *data, u16_t len)
{
	u8_t head_len, format, sample_rate, cp_type;

	if (btsrv_rdm_get_a2dp_pending_ahead_start(conn)) {
		btsrv_a2dp_media_state_change(conn, BT_A2DP_MEDIA_STATE_START);
	}

#ifdef CONFIG_SUPPORT_TWS
	if (btsrv_tws_protocol_data_cb(conn, data, len)) {
		return;
	}
#endif

	/* Must get info after btsrv_tws_protocol_data_cb,
	 * tws slave update format in btsrv_tws_protocol_data_cb
	 */
	btsrv_rdm_a2dp_get_codec_info(conn, &format, &sample_rate, &cp_type);

	switch (format) {
	case BT_A2DP_SBC:
	case BT_A2DP_MPEG2:
		if (!(btsrv_rdm_a2dp_get_actived() == conn ||
			btsrv_rdm_get_dev_role() == BTSRV_TWS_SLAVE)) {
			SYS_LOG_DBG("return for master role %d\n", btsrv_rdm_get_dev_role());
			break;
		}

		if (format == BT_A2DP_SBC) {
			head_len = AVDTP_SBC_HEADER_LEN;
		} else {
			head_len = AVDTP_AAC_HEADER_LEN;
		}

		if (cp_type == BT_AVDTP_AV_CP_TYPE_SCMS_T) {
			head_len++;
		}

#ifdef CONFIG_DEBUG_DATA_RATE
		_btsrv_a2dp_debug_date_rate(len - head_len);
#endif

#ifdef CONFIG_SUPPORT_TWS
		if (btsrv_rdm_get_dev_role() == BTSRV_TWS_SLAVE &&
			btsrv_tws_get_state() == TWS_STATE_READY_PLAY) {
			if (btsrv_tws_sink_parse_a2dp_sync_info(data, MEDIA_RTP_HEAD_LEN, data + head_len, len - head_len)) {
				break;
			}
		}
#endif
		a2dp_user_callback(BTSRV_A2DP_DATA_INDICATED, data + head_len, len - head_len);
		break;
	default:
		SYS_LOG_INF("A2dp not support type: 0x%x, 0x%x, %d\n", data[0], data[1], format);
		break;
	}

}

static int _btsrv_a2dp_media_state_req_cb(struct bt_conn *conn, u8_t state)
{
	int cmd = -1;
	u8_t send_notify = 0;
	u16_t delay_report;

	switch (state) {
	case BT_A2DP_MEDIA_STATE_OPEN:
		cmd = MSG_BTSRV_A2DP_MEDIA_STATE_OPEN;
		delay_report = 0;
		a2dp_user_callback(BTSRV_A2DP_GET_INIT_DELAY_REPORT, &delay_report, sizeof(delay_report));
		if (delay_report) {
			hostif_bt_a2dp_send_delay_report(conn, delay_report);
		}
		break;
	case BT_A2DP_MEDIA_STATE_START:
		cmd = MSG_BTSRV_A2DP_MEDIA_STATE_START;
		btsrv_rdm_a2dp_actived(conn, 1);
		btsrv_rdm_set_a2dp_pending_ahead_start(conn, 0);
		break;
	case BT_A2DP_MEDIA_STATE_CLOSE:
		cmd = MSG_BTSRV_A2DP_MEDIA_STATE_CLOSE;
		btsrv_rdm_a2dp_actived(conn, 0);
		btsrv_rdm_set_a2dp_pending_ahead_start(conn, 0);
		break;
	case BT_A2DP_MEDIA_STATE_SUSPEND:
		cmd = MSG_BTSRV_A2DP_MEDIA_STATE_SUSPEND;
		btsrv_rdm_a2dp_actived(conn, 0);
		btsrv_rdm_set_a2dp_pending_ahead_start(conn, 0);
		break;
	case BT_A2DP_MEDIA_STATE_PENDING_AHEAD_START:
		btsrv_rdm_set_a2dp_pending_ahead_start(conn, 1);
		break;
	}

	if (cmd > 0) {
		if (btsrv_rdm_a2dp_get_actived() == conn) {
			/* Phone state req */
			send_notify = 1;
		} else if (btsrv_rdm_a2dp_get_actived() &&
			(btsrv_rdm_get_dev_role() == BTSRV_TWS_MASTER)) {
			/* Tws source_restart state req,
			 * only notify when have a2dp active device.
			 */
			send_notify = 1;
		} else if (btsrv_rdm_get_dev_role() == BTSRV_TWS_SLAVE) {
			/* Tws mater send to slave state req */
			send_notify = 1;
		}

		if ((btsrv_rdm_a2dp_get_actived() == conn || btsrv_rdm_get_dev_role() == BTSRV_TWS_SLAVE) &&
			(cmd == MSG_BTSRV_A2DP_MEDIA_STATE_START)) {
			u8_t format, sample_rate;
			u8_t codec_info[2];

			btsrv_rdm_a2dp_get_codec_info(conn, &format, &sample_rate, NULL);
			codec_info[0] = format;
			codec_info[1] = sample_rate;

			a2dp_user_callback(BTSRV_A2DP_CODEC_INFO, codec_info, sizeof(codec_info));
		}

		if (send_notify) {
			btsrv_event_notify(MSG_BTSRV_A2DP, cmd, NULL);
		}
	}

	return 0;
}

static void _btsrv_a2dp_seted_codec_cb(struct bt_conn *conn, struct bt_a2dp_media_codec *codec, u8_t cp_type)
{
	u8_t codec_info[2];

	codec_info[0] = codec->head.codec_type;
	codec_info[1] = btsrv_convert_sample_rate(codec);
	SYS_LOG_INF("media %d, codec %d, freq %d, cp %d\n", codec->head.media_type, codec_info[0], codec_info[1], cp_type);
	btsrv_rdm_a2dp_set_codec_info(conn, codec_info[0], codec_info[1], cp_type);
}

static const struct bt_a2dp_app_cb btsrv_a2dp_cb = {
	.connected = _btsrv_a2dp_connect_cb,
	.disconnected = _btsrv_a2dp_disconnected_cb,
	.media_handler = _btsrv_a2dp_media_handler_cb,
	.media_state_req = _btsrv_a2dp_media_state_req_cb,
	.seted_codec = _btsrv_a2dp_seted_codec_cb,
};

int btsrv_a2dp_media_state_change(struct bt_conn *conn, u8_t state)
{
	return _btsrv_a2dp_media_state_req_cb(conn, state);
}

static void _btsrv_a2dp_connected(struct bt_conn *conn)
{
	char addr_str[BT_ADDR_STR_LEN];
	bd_address_t *addr = GET_CONN_BT_ADDR(conn);

	hostif_bt_addr_to_str((const bt_addr_t *)addr, addr_str, BT_ADDR_STR_LEN);
	SYS_LOG_INF("A2dp connected:%p addr %s\n", conn, addr_str);
	a2dp_user_callback(BTSRV_A2DP_CONNECTED, NULL, 0);
}

static void _btsrv_a2dp_disconnected(struct bt_conn *conn)
{
	char addr_str[BT_ADDR_STR_LEN];
	bd_address_t *addr = GET_CONN_BT_ADDR(conn);

	hostif_bt_addr_to_str((const bt_addr_t *)addr, addr_str, BT_ADDR_STR_LEN);
	SYS_LOG_INF("A2dp disconnected:%p addr %s\n", conn, addr_str);
	a2dp_user_callback(BTSRV_A2DP_DISCONNECTED, NULL, 0);
}

static void _btsrv_a2dp_actived_dev_changed(struct bt_conn *conn)
{
	u8_t format, sample_rate;
	u8_t codec_info[2];

#ifdef CONFIG_BT_DOUBLE_PHONE_PREEMPTION_MODE
	struct bt_conn *second_conn;

	second_conn = btsrv_rdm_a2dp_get_second_dev();
	if (second_conn && btsrv_rdm_is_a2dp_stream_open(second_conn)) {
		btsrv_avrcp_preemption_pause(second_conn);
	} else if (second_conn && !btsrv_rdm_is_a2dp_stream_open(second_conn)) {
		if (btsrv_rdm_is_a2dp_stream_open(conn)) {
			btsrv_avrcp_resume_preemption_play(conn);
		}
	}
#endif

	a2dp_user_callback(BTSRV_A2DP_STREAM_SUSPEND, NULL, 0);

	btsrv_rdm_a2dp_get_codec_info(conn, &format, &sample_rate, NULL);
	codec_info[0] = format;
	codec_info[1] = sample_rate;

	a2dp_user_callback(BTSRV_A2DP_CODEC_INFO, codec_info, sizeof(codec_info));
	a2dp_user_callback(BTSRV_A2DP_STREAM_STARED, NULL, 0);
}

static void _btsrv_a2dp_check_state(void)
{
	struct bt_conn *conn = btsrv_rdm_a2dp_get_actived();
	struct bt_conn *second_conn = btsrv_rdm_a2dp_get_second_dev();

	if (btsrv_rdm_is_actived_a2dp_stream_open()) {
		SYS_LOG_INF("a2dp trigger start\n");
		u8_t format, sample_rate;
		u8_t codec_info[2];

		btsrv_rdm_a2dp_get_codec_info(conn, &format, &sample_rate, NULL);
		codec_info[0] = format;
		codec_info[1] = sample_rate;

		a2dp_user_callback(BTSRV_A2DP_CODEC_INFO, codec_info, sizeof(codec_info));
		a2dp_user_callback(BTSRV_A2DP_STREAM_STARED, NULL, 0);
	} else if (second_conn && btsrv_rdm_is_a2dp_stream_open(second_conn)) {
		/* active dev may not update during call,so after call exit,
		 * we should change active dev if current active dev is not
		 * stream opened.
		 */
		btsrv_rdm_a2dp_actived(second_conn, 1);
		SYS_LOG_INF("change active dev to another\n");
	}
}

static void pts_send_delay_report(struct bt_conn *base_conn, u8_t tws_dev, void *cb_param)
{
	u32_t delay_time = (u32_t)cb_param;

	hostif_bt_a2dp_send_delay_report(base_conn, (u16_t)delay_time);
}

static void btsrv_a2dp_send_delay_report(u16_t delay_time)
{
	struct bt_conn *conn = btsrv_rdm_a2dp_get_actived();
	u32_t cb_param = delay_time;

	if (btsrv_is_pts_test() && conn == NULL) {
		btsrv_rdm_get_connected_dev(pts_send_delay_report, (void *)cb_param);
		return;
	}

	if (conn) {
		hostif_bt_a2dp_send_delay_report(conn, delay_time);
	}
}

int btsrv_a2dp_init(struct btsrv_a2dp_start_param *param)
{
	int ret = 0;
	int i = 0, max_register;

	max_register = min(CONFIG_MAX_A2DP_ENDPOINT, param->sbc_endpoint_num);
	for (i = 0; i < max_register; i++) {
		a2dp_sbc_endpoint[i].info.codec = (struct bt_a2dp_media_codec *)param->sbc_codec;
		a2dp_sbc_endpoint[i].info.a2dp_cp_scms_t = param->a2dp_cp_scms_t;
		a2dp_sbc_endpoint[i].info.a2dp_delay_report = param->a2dp_delay_report;
		ret |= hostif_bt_a2dp_register_endpoint(&a2dp_sbc_endpoint[i], BT_A2DP_AUDIO, BT_A2DP_EP_SINK);
	}

	max_register = min(CONFIG_MAX_A2DP_ENDPOINT, param->aac_endpoint_num);
	a2dp_register_aac_num = max_register;
	for (i = 0; i < max_register; i++) {
		a2dp_aac_endpoint[i].info.codec = (struct bt_a2dp_media_codec *)param->aac_codec;
		a2dp_aac_endpoint[i].info.a2dp_cp_scms_t = param->a2dp_cp_scms_t;
		a2dp_aac_endpoint[i].info.a2dp_delay_report = param->a2dp_delay_report;
		ret |= hostif_bt_a2dp_register_endpoint(&a2dp_aac_endpoint[i], BT_A2DP_AUDIO, BT_A2DP_EP_SINK);
	}

	if (ret) {
		SYS_LOG_ERR("bt br-a2dp-register failed\n");
		goto err;
	}

	a2dp_user_callback = param->cb;
	hostif_bt_a2dp_register_cb((struct bt_a2dp_app_cb *)&btsrv_a2dp_cb);

	return 0;
err:
	return -1;
}

int btsrv_a2dp_deinit(void)
{
	hostif_bt_a2dp_register_cb(NULL);
	a2dp_user_callback = NULL;
	return 0;
}

int btsrv_a2dp_disconnect(struct bt_conn *conn)
{
	if (!conn) {
		SYS_LOG_ERR("conn is NULL\n");
		return -EINVAL;
	}

	hostif_bt_a2dp_disconnect(conn);
	SYS_LOG_INF("a2dp_disconnect\n");
	return 0;
}

int btsrv_a2dp_connect(struct bt_conn *conn, u8_t role)
{
	if (!hostif_bt_a2dp_connect(conn, role)) {
		SYS_LOG_INF("Connect a2dp\n");
	} else {
		SYS_LOG_ERR("Connect a2dp failed\n");
	}

	return 0;
}

void btsrv_a2dp_halt_aac_endpoint(bool halt)
{
	int ret, i;

	for (i = 0; i < a2dp_register_aac_num; i++) {
		ret = hostif_bt_a2dp_halt_endpoint(&a2dp_aac_endpoint[i], halt);
		SYS_LOG_INF("%s AAC endpoint %d\n", (halt ? "Halt" : "Resume"), ret);
	}
}

int btsrv_a2dp_process(struct app_msg *msg)
{
	struct bt_conn *conn;

	switch (_btsrv_get_msg_param_cmd(msg)) {
	case MSG_BTSRV_A2DP_START:
		SYS_LOG_INF("MSG_BTSRV_A2DP_START\n");
		btsrv_a2dp_init(msg->ptr);
		break;
	case MSG_BTSRV_A2DP_STOP:
		SYS_LOG_INF("MSG_BTSRV_A2DP_STOP\n");
		btsrv_a2dp_deinit();
		break;
	case MSG_BTSRV_A2DP_CONNECT_TO:
		SYS_LOG_INF("MSG_BTSRV_A2DP_CONNECT_TO\n");
		conn = btsrv_rdm_find_conn_by_addr(msg->ptr);
		if (conn) {
			btsrv_a2dp_connect(conn, (_btsrv_get_msg_param_reserve(msg) ? BT_A2DP_CH_SOURCE : BT_A2DP_CH_SINK));
		}
		break;
	case MSG_BTSRV_A2DP_DISCONNECT:
		SYS_LOG_INF("MSG_BTSRV_A2DP_DISCONNECT\n");
		conn = btsrv_rdm_find_conn_by_addr(msg->ptr);
		if (conn) {
			btsrv_a2dp_disconnect(conn);
		}
		break;
	case MSG_BTSRV_A2DP_CONNECTED:
		_btsrv_a2dp_connected(msg->ptr);
		break;
	case MSG_BTSRV_A2DP_DISCONNECTED:
		SYS_LOG_INF("MSG_BTSRV_A2DP_DISCONNECTED\n");
		if (btsrv_rdm_a2dp_get_actived() == (struct bt_conn *)(msg->ptr) ||
			btsrv_rdm_get_conn_role(msg->ptr) == BTSRV_TWS_SLAVE) {
			/* Device with media start, without media suspend/close buf direct
			 * a2dp disconnect, need send media close to upper layer to stop music play.
			 */
			a2dp_user_callback(BTSRV_A2DP_STREAM_CLOSED, NULL, 0);
		}
		_btsrv_a2dp_disconnected(msg->ptr);
		btsrv_event_notify(MSG_BTSRV_CONNECT, _btsrv_get_msg_param_cmd(msg), msg->ptr);
		break;
	case MSG_BTSRV_A2DP_MEDIA_STATE_OPEN:
		SYS_LOG_INF("BT_A2DP_MEDIA_STATE_OPEN\n");
		a2dp_user_callback(BTSRV_A2DP_STREAM_OPENED, NULL, 0);
		break;
	case MSG_BTSRV_A2DP_MEDIA_STATE_START:
		SYS_LOG_DBG("A2DP_MEDIA_STATE_START\n");
		a2dp_user_callback(BTSRV_A2DP_STREAM_STARED, NULL, 0);
		break;
	case MSG_BTSRV_A2DP_MEDIA_STATE_CLOSE:
		SYS_LOG_DBG("A2DP_MEDIA_STATE_CLOSE\n");
		a2dp_user_callback(BTSRV_A2DP_STREAM_CLOSED, NULL, 0);
		break;
	case MSG_BTSRV_A2DP_MEDIA_STATE_SUSPEND:
		SYS_LOG_DBG("A2DP_MEDIA_STATE_SUSPEND\n");
		a2dp_user_callback(BTSRV_A2DP_STREAM_SUSPEND, NULL, 0);
		break;
	case MSG_BTSRV_A2DP_ACTIVED_DEV_CHANGED:
		_btsrv_a2dp_actived_dev_changed(msg->ptr);
		break;
	case MSG_BTSRV_A2DP_CHECK_STATE:
		_btsrv_a2dp_check_state();
		break;
	case MSG_BTSRV_A2DP_SEND_DELAY_REPORT:
		btsrv_a2dp_send_delay_report((u16_t)_btsrv_get_msg_param_value(msg));
		break;
	default:
		break;
	}
	return 0;
}
