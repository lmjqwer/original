/*
 * Copyright (c) 2019 Actions Semiconductor Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file media player interface
 */
#define SYS_LOG_DOMAIN "media"
#include <os_common_api.h>
#include <mem_manager.h>
#include <msg_manager.h>
#include <srv_manager.h>
#include <sys_wakelock.h>
#include <bt_manager.h>
#include <string.h>
#include <misc/byteorder.h>
#include "audio_policy.h"
#include "audio_system.h"
#include "media_player.h"
#include "media_service.h"
#include <property_manager.h>
#include <sdfs.h>
#include <media_effect_param.h>
#ifdef CONFIG_DVFS
#include <dvfs.h>
#endif

void *media_resample_create(u8_t in_channels, u8_t out_channels,
		u8_t samplerate_in, u8_t samplerate_out, u8_t stream_type,
		io_stream_t input_stream, io_stream_t out_stream)
{
	struct acts_ringbuf *res_in = stream_get_ringbuffer(input_stream);

	if (!res_in) {
		SYS_LOG_ERR("resample only support ringbuffer_stream");
		return NULL;
	}
	
	return resample_service_init(in_channels, out_channels,
			samplerate_in, samplerate_out, stream_type,
			res_in, out_stream);

}

void media_resample_triggle_process(void *handle)
{
	resample_service_stream_notify(handle);
}

void media_resample_destory(void *handle)
{
	resample_service_exit(handle);
}

