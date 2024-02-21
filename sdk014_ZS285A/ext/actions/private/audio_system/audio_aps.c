/*
 * Copyright (c) 2016 Actions Semi Co., Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief audio stream.
 */

#include <os_common_api.h>
#include <mem_manager.h>
#include <msg_manager.h>
#include <audio_hal.h>
#include <acts_cache.h>
#include <audio_system.h>
#include <audio_track.h>
#include <media_type.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stream.h>
#include <btservice_api.h>

#define SYS_LOG_NO_NEWLINE
#ifdef SYS_LOG_DOMAIN
#undef SYS_LOG_DOMAIN
#endif
#define SYS_LOG_DOMAIN "audio_aps"
#include <logging/sys_log.h>

#define AUDIO_APS_ADJUST_INTERVAL            30

static aps_monitor_info_t aps_monitor;

aps_monitor_info_t *audio_aps_monitor_get_instance(void)
{
	return &aps_monitor;
}

extern void media_resample_set_mode(aps_resample_mode_e state);

void audio_aps_monitor_set_aps(void *audio_handle, u8_t status, int level)
{
	aps_monitor_info_t *handle = audio_aps_monitor_get_instance();
#ifndef CONFIG_SF_APS
	u8_t aps_mode = APS_LEVEL_AUDIOPLL;
#endif
	u8_t set_aps = false;

	if (status & APS_OPR_SET) {
		handle->dest_level = level;
	}

	if (status & APS_OPR_FAST_SET) {
		handle->dest_level = level;
		handle->current_level = level;
		set_aps = true;
	}

	if (handle->current_level > handle->dest_level) {
		handle->current_level--;
		set_aps = true;
	}

	if (handle->current_level < handle->dest_level) {
		handle->current_level++;
		set_aps = true;
	}

	if (set_aps) {
		SYS_LOG_DBG("adjust mode %d: %d\n", aps_mode, handle->current_level);
	#ifdef CONFIG_SF_APS
		if (handle->current_level > APS_LEVEL_5) {
			media_resample_set_mode(APS_RESAMPLE_MODE_DOWN);
		} else if (handle->current_level < APS_LEVEL_5) {
			media_resample_set_mode(APS_RESAMPLE_MODE_UP);
		} else {
			media_resample_set_mode(APS_RESAMPLE_MODE_DEFAULT);
		}
	#else
		hal_aout_channel_set_aps(audio_handle, handle->current_level, aps_mode);
	#endif
	}
}

void audio_aps_monitor_normal(aps_monitor_info_t *handle, int stream_length, uint8_t aps_max_level, uint8_t aps_min_level, uint8_t aps_level)
{
	void *audio_handle = handle->audio_track->audio_handle;
	u16_t mid_threshold = 0;
	u16_t diff_threshold = 0;

	if (!handle->need_aps) {
		return;
	}


	diff_threshold = (handle->aps_increase_water_mark - handle->aps_reduce_water_mark);
	mid_threshold = handle->aps_increase_water_mark - (diff_threshold / 2);

	switch (handle->aps_status) {
	case APS_STATUS_DEFAULT:
		if (stream_length > handle->aps_increase_water_mark) {
			SYS_LOG_DBG("inc aps\n");
			audio_aps_monitor_set_aps(audio_handle, APS_OPR_SET, aps_max_level);
			handle->aps_status = APS_STATUS_INC;
		} else if (stream_length < handle->aps_reduce_water_mark) {
			SYS_LOG_DBG("fast dec aps\n");
			audio_aps_monitor_set_aps(audio_handle, APS_OPR_SET, aps_min_level);
			handle->aps_status = APS_STATUS_DEC;
		} else {
			audio_aps_monitor_set_aps(audio_handle, APS_OPR_ADJUST, 0);
		}
		break;

	case APS_STATUS_INC:
		if (stream_length < handle->aps_reduce_water_mark) {
			SYS_LOG_DBG("fast dec aps\n");
			audio_aps_monitor_set_aps(audio_handle, APS_OPR_SET, aps_min_level);
			handle->aps_status = APS_STATUS_DEC;
		} else if (stream_length <= mid_threshold) {
			SYS_LOG_DBG("default aps\n");
			audio_aps_monitor_set_aps(audio_handle, APS_OPR_SET, aps_level);
			handle->aps_status = APS_STATUS_DEFAULT;
		} else {
			audio_aps_monitor_set_aps(audio_handle, APS_OPR_ADJUST, 0);
		}
	break;

	case APS_STATUS_DEC:
		if (stream_length > handle->aps_increase_water_mark) {
			SYS_LOG_DBG("fast inc aps\n");
			audio_aps_monitor_set_aps(audio_handle, APS_OPR_SET,  aps_max_level);
			handle->aps_status = APS_STATUS_INC;
		} else if (stream_length >= mid_threshold) {
			SYS_LOG_DBG("default aps\n");
			audio_aps_monitor_set_aps(audio_handle, APS_OPR_SET, aps_level);
			handle->aps_status = APS_STATUS_DEFAULT;
		} else {
			audio_aps_monitor_set_aps(audio_handle, APS_OPR_ADJUST, 0);
		}
		break;
	}
}

extern void audio_aps_monitor_slave(aps_monitor_info_t *handle, int stream_length, u8_t aps_max_level, u8_t aps_min_level, u8_t slave_aps_level);
extern void audio_aps_monitor_master(aps_monitor_info_t *handle, int stream_length, u8_t aps_max_level, u8_t aps_min_level, u8_t slave_aps_level);

/* Run interval DATA_PROCESS_PERIOD =  OS_MSEC(4) */
void audio_aps_monitor(int pcm_time)
{
	aps_monitor_info_t *handle = audio_aps_monitor_get_instance();
	static u32_t s_time;

	/* Adjust aps every AUDIO_APS_ADJUST_INTERVAL */
	if ((k_uptime_get_32() - s_time) < handle->duration) {
		return;
	}

	//printk("input %d ms\n", pcm_time);

	s_time = k_uptime_get_32();
	handle->cache_pcm_time = pcm_time;
	if (handle->role == BTSRV_TWS_NONE) {
		audio_aps_monitor_normal(handle, pcm_time, handle->aps_max_level, handle->aps_min_level, handle->aps_default_level);
	} else if (handle->role == BTSRV_TWS_MASTER) {
		audio_aps_monitor_master(handle, pcm_time, handle->aps_max_level, handle->aps_min_level, handle->aps_default_level);
	} else if (handle->role == BTSRV_TWS_SLAVE) {
		audio_aps_monitor_slave(handle, pcm_time, handle->aps_max_level, handle->aps_min_level, handle->current_level);
	}
}

void audio_aps_monitor_init(int stream_type, int efx_stream_type, void *tws_observer, struct audio_track_t *audio_track)
{
	aps_monitor_info_t *handle = audio_aps_monitor_get_instance();

	memset(handle, 0, sizeof(aps_monitor_info_t));
	handle->audio_track = audio_track;
	handle->aps_status = APS_STATUS_DEFAULT;
	handle->aps_min_level = APS_LEVEL_3;
	handle->aps_max_level = APS_LEVEL_7;
	handle->aps_default_level = APS_LEVEL_5;

	/* Default water mark: no aps adjustment */
	handle->aps_increase_water_mark = UINT16_MAX;
	handle->aps_reduce_water_mark = 0;
	handle->need_aps = 1;
	handle->aps_increase_water_mark = audio_policy_get_increase_threshold(stream_type, efx_stream_type);
	handle->aps_reduce_water_mark = audio_policy_get_reduce_threshold(stream_type, efx_stream_type);
	handle->duration = AUDIO_APS_ADJUST_INTERVAL;

	switch (stream_type) {
	/**this type not need aps */
	case AUDIO_STREAM_MIC_IN:
	case AUDIO_STREAM_FM:
	case AUDIO_STREAM_LINEIN:
	case AUDIO_STREAM_TTS:
	case AUDIO_STREAM_LOCAL_MUSIC:
		handle->need_aps = 0;
	/**this type need aps */
	case AUDIO_STREAM_MUSIC:
	case AUDIO_STREAM_USOUND:
	case AUDIO_STREAM_I2SRX_IN:
	case AUDIO_STREAM_SPDIF_IN:
		handle->duration = 6;
		if (tws_observer) {
			handle->need_aps = 1;
			audio_aps_monitor_tws_init(tws_observer);
			audio_track_set_waitto_start(audio_track, true);
		}
		break;
	case AUDIO_STREAM_VOICE:
		handle->duration = 8;
	default:
		break;
	}

	handle->current_level = handle->aps_default_level;
	handle->dest_level = handle->current_level;

}

void audio_aps_notify_decode_err(u16_t err_cnt)
{
	audio_aps_tws_notify_decode_err(err_cnt);
}

void audio_aps_notify_sample_rate_change(u32_t sample_rate)
{
	audio_aps_tws_notify_sample_rate_change(sample_rate);
}

void audio_aps_monitor_deinit(int format, void *tws_observer, struct audio_track_t *audio_track)
{
	/* Be consistent with audio_aps_monitor_init */
	switch (format) {
		case ACT_TYPE:
		case MSBC_TYPE:
		case CVSD_TYPE:
			break;
		case SBC_TYPE:
		case AAC_TYPE:
		default:
			if (tws_observer) {
				audio_aps_monitor_tws_deinit(tws_observer);
			}
			break;
	}
}

