/*
 * Copyright (c) 2017 Actions Semi Co., Ltd.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief bt enging sco upload stream
 */
#include <os_common_api.h>
#include <errno.h>
#include <zephyr/types.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <misc/printk.h>
#include <misc/byteorder.h>
#include <zephyr.h>
#include <audio_policy.h>

#include <acts_ringbuf.h>

#include <bluetooth/host_interface.h>
#include <mem_manager.h>
#include <media_mem.h>
#include <stream.h>
#include <media_type.h>
#include <audio_system.h>
#include "btsrv_inner.h"

//#define SCO_SEND_USED_WORKQUEUE                  1

#define SCO_CVSD_TX_PAYLOAD_LEN                 (120)
#define SCO_MSBC_TX_PAYLOAD_LEN                 (58)

/** cace info ,used for cache stream */
typedef struct {
#ifdef SCO_SEND_USED_WORKQUEUE
	os_delayed_work send_work;
	struct acts_ringbuf *cache_buff;
#endif
	u16_t codec_type;
	u16_t payload_size;
	u8_t *payload;
	u16_t payload_offset;
	u8_t msbc_seq_num;
	u8_t stop_flag;
	u8_t drop_cnt;
} sco_upload_info_t;

static u8_t _sco_upload_get_msbc_seq_num(sco_upload_info_t *info)
{
	static const u8_t msbc_head_flag[4] = {0x08, 0x38, 0xc8, 0xf8};

	info->msbc_seq_num++;
	info->msbc_seq_num = (info->msbc_seq_num & 0x03);

	return msbc_head_flag[info->msbc_seq_num];
}
#ifdef SCO_SEND_USED_WORKQUEUE
static void sco_upload_send_work(os_work *work)
{
	sco_upload_info_t *info =  CONTAINER_OF(work, sco_upload_info_t, send_work);
	struct bt_conn *conn = btsrv_sco_get_conn();
	u8_t *p_buf = NULL;
	int offset = 0;
	int payload_len = 0;
	int ret = 0;

	if (!info || !conn || info->stop_flag)
		return;

	p_buf = info->payload;

	while (acts_ringbuf_length(info->cache_buff) >= info->payload_size) {
		offset = 0;

		/**if msbc , we add msbc seq num */
		if (info->codec_type == MSBC_TYPE) {
			p_buf[offset++] = 0x01;
			p_buf[offset++] = _sco_upload_get_msbc_seq_num(info);
		}

		payload_len = info->payload_size + offset;

		ret = acts_ringbuf_get(info->cache_buff, &p_buf[offset], info->payload_size - 1);
		if (ret != info->payload_size - 1) {
			SYS_LOG_WRN("want read %d(%d) bytes\n", info->payload_size, ret);
			break;
		}

		ret = hostif_bt_conn_send_sco_data(conn, p_buf, payload_len);
		if (ret) {
			SYS_LOG_WRN("sco send failed ret %d\n", ret);
		}
	}

	os_delayed_work_submit(&info->send_work, 10);
}
#endif
int sco_upload_stream_open(io_stream_t handle, stream_mode mode)
{
	sco_upload_info_t *info = (sco_upload_info_t *)handle->data;

	if (!info)
		return -EACCES;

	handle->mode = mode;
	info->stop_flag = 0;
#ifdef SCO_SEND_USED_WORKQUEUE
	os_delayed_work_submit(&info->send_work, 10);
#endif
	return 0;
}

#ifndef SCO_SEND_USED_WORKQUEUE
static int sco_upload_stream_send_direct(io_stream_t handle, unsigned char *buf, int len)
{
	struct bt_conn *conn = btsrv_sco_get_conn();
	sco_upload_info_t *info = (sco_upload_info_t *)handle->data;
	int ret = 0;
	u8_t *p_buf = info->payload;

	if (!info)
		return -EACCES;

	if (system_check_low_latencey_mode()) {
		if(info->drop_cnt < 3) {
			info->drop_cnt++;
			return len;
		}
	}
	if (info->codec_type == MSBC_TYPE) {
		p_buf[info->payload_offset++] = 0x01;
		p_buf[info->payload_offset++] = _sco_upload_get_msbc_seq_num(info);

		memcpy(&p_buf[info->payload_offset], buf, len);
		info->payload_offset += len;

		/**pending data*/
		p_buf[info->payload_offset++] = 0;

		ret = hostif_bt_conn_send_sco_data(conn, p_buf, info->payload_offset);
		if (ret) {
			SYS_LOG_WRN("sco send failed ret %d\n", ret);
		}
		info->payload_offset = 0;
	} else {
		if(info->payload_size >= info->payload_offset + len){
			memcpy(&p_buf[info->payload_offset], buf, len);
			info->payload_offset += len;
		} else {
			SYS_LOG_WRN("buff overflow %d %d\n", info->payload_size,len);
		}

		if(info->payload_offset == info->payload_size) {
			ret = hostif_bt_conn_send_sco_data(conn, p_buf, info->payload_offset);
			if (ret) {
				SYS_LOG_WRN("sco send failed ret %d\n", ret);
			}
			info->payload_offset = 0;
		}
	}

	return len;
}
#endif

int sco_upload_stream_write(io_stream_t handle, unsigned char *buf, int len)
{
	sco_upload_info_t *info = (sco_upload_info_t *)handle->data;
	int ret = 0;

	if (!info)
		return -EACCES;

#ifdef SCO_SEND_USED_WORKQUEUE
	ret = acts_ringbuf_put(info->cache_buff, buf, len);

	if (ret != len) {
		SYS_LOG_WRN("want write %d(%d) bytes\n", len, ret);
	}
#else
	ret = sco_upload_stream_send_direct(handle, buf, len);
#endif

	return ret;
}

int sco_upload_stream_tell(io_stream_t handle)
{
	sco_upload_info_t *info = (sco_upload_info_t *)handle->data;

	if (!info)
		return -EACCES;

#ifdef SCO_SEND_USED_WORKQUEUE
	return acts_ringbuf_space(info->cache_buff);
#else
	return media_mem_get_cache_pool_size(OUTPUT_SCO, AUDIO_STREAM_VOICE);
#endif
}

int sco_upload_stream_close(io_stream_t handle)
{
	sco_upload_info_t *info = (sco_upload_info_t *)handle->data;
	int res = 0;

	if (!info)
		return -EACCES;

	info->stop_flag = 1;

#ifdef SCO_SEND_USED_WORKQUEUE
	k_delayed_work_cancel_sync(&info->send_work, K_FOREVER);
#endif

	return res;
}

int sco_upload_stream_destroy(io_stream_t handle)
{
	int res = 0;
	sco_upload_info_t *info = (sco_upload_info_t *)handle->data;

	if (!info)
		return -EACCES;

#ifdef SCO_SEND_USED_WORKQUEUE
	if (info->cache_buff) {
		mem_free(info->cache_buff);
		info->cache_buff = NULL;
	}
#endif

	mem_free(info);
	handle->data = NULL;

	return res;
}
static int sco_upload_stream_get_space(io_stream_t handle)
{
	sco_upload_info_t *info = (sco_upload_info_t *)handle->data;

	if (!info)
		return -EACCES;
#ifdef SCO_SEND_USED_WORKQUEUE
	return acts_ringbuf_space(info->cache_buff);
#else
	return media_mem_get_cache_pool_size(OUTPUT_SCO, AUDIO_STREAM_VOICE);
#endif
}

static int sco_upload_stream_get_length(io_stream_t handle)
{
	sco_upload_info_t *info = (sco_upload_info_t *)handle->data;

	if (!info)
		return -EACCES;
#ifdef SCO_SEND_USED_WORKQUEUE
	return acts_ringbuf_length(info->cache_buff);
#else
	return 0;
#endif
}

int sco_upload_stream_init(io_stream_t handle, void *param)
{
	int res = 0;
	sco_upload_info_t *info = NULL;
	u8_t hfp_codec_id = *(u8_t *)param;

	info = mem_malloc(sizeof(sco_upload_info_t));
	if (!info) {
		SYS_LOG_ERR("cache stream info malloc failed\n");
		return -ENOMEM;
	}

#ifdef SCO_SEND_USED_WORKQUEUE
	info->cache_buff = mem_malloc(sizeof(struct acts_ringbuf));
	if (!info->cache_buff) {
		res = -ENOMEM;
		goto err_exit;
	}

	acts_ringbuf_init(info->cache_buff,
				media_mem_get_cache_pool(OUTPUT_SCO, AUDIO_STREAM_VOICE),
				media_mem_get_cache_pool_size(OUTPUT_SCO, AUDIO_STREAM_VOICE));
#endif

	info->codec_type = hfp_codec_id;
	info->msbc_seq_num = 0;

	if (info->codec_type == CVSD_TYPE) {
		info->payload_size = SCO_CVSD_TX_PAYLOAD_LEN;
	} else {
		info->payload_size = SCO_MSBC_TX_PAYLOAD_LEN;
	}

	info->payload = media_mem_get_cache_pool(TX_SCO, AUDIO_STREAM_VOICE);
	info->payload_offset = 0;
	info->drop_cnt = 0;

#ifdef SCO_SEND_USED_WORKQUEUE
	os_delayed_work_init(&info->send_work, sco_upload_send_work);
#endif

	handle->data = info;

	return 0;
#ifdef SCO_SEND_USED_WORKQUEUE
err_exit:
#endif
	if (info)
		mem_free(info);
	return res;
}

const stream_ops_t sco_upload_stream_ops = {
	.init = sco_upload_stream_init,
	.open = sco_upload_stream_open,
	.read = NULL,
	.seek = NULL,
	.tell = sco_upload_stream_tell,
	.get_length = sco_upload_stream_get_length,
	.get_space = sco_upload_stream_get_space,
	.write = sco_upload_stream_write,
	.close = sco_upload_stream_close,
	.destroy = sco_upload_stream_destroy,
};

io_stream_t sco_upload_stream_create(u8_t hfp_codec_id)
{
	return stream_create(&sco_upload_stream_ops, &hfp_codec_id);
}
