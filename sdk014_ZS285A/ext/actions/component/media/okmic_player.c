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
#include "ringbuff_stream.h"
#include "media_mem.h"
#ifdef CONFIG_DVFS
#include <dvfs.h>
#endif
#include <app_manager.h>

static media_player_okmic_ctx_s media_player_okmic_ctx;

media_player_okmic_ctx_s *get_okmic_player_ctx(void)
{
	return &media_player_okmic_ctx;
}
static io_stream_t _okmic_create_inputstream(void)
{
	int ret = 0;
	io_stream_t input_stream = NULL;

	input_stream = ringbuff_stream_create_ext(
			media_mem_get_cache_pool(INPUT_PLAYBACK, AUDIO_STREAM_OKMIC),
			media_mem_get_cache_pool_size(INPUT_PLAYBACK, AUDIO_STREAM_OKMIC));
	if (!input_stream) {
		goto exit;
	}

	ret = stream_open(input_stream, MODE_IN_OUT);
	if (ret) {
		stream_destroy(input_stream);
		input_stream = NULL;
		goto exit;
	}

exit:
	SYS_LOG_INF(" %p\n", input_stream);
	return	input_stream;
}


io_stream_t okmic_player_create_attached_stream(int in_stream_type)
{
	int ret = 0;
	io_stream_t out_stream = NULL;

	out_stream = ringbuff_stream_create_ext(
			media_mem_get_cache_pool(OUTPUT_PCM, in_stream_type),
			media_mem_get_cache_pool_size(OUTPUT_PCM, in_stream_type));
	if (!out_stream) {
		goto exit;
	}

	ret = stream_open(out_stream, MODE_IN_OUT);
	if (ret) {
		stream_destroy(out_stream);
		out_stream = NULL;
		goto exit;
	}
exit:
	SYS_LOG_INF(" %p\n", out_stream);
	return	out_stream;
}

static u8_t _okmic_get_attach_stream_type(u8_t attach_stream_type)
{
	if (attach_stream_type)
		return attach_stream_type;
	else {
		if (strncmp(app_manager_get_current_app(), "btmusic", strlen("btmusic")) == 0)
			return AUDIO_STREAM_MUSIC;
		else if (strncmp(app_manager_get_current_app(), "sd_mplayer", strlen("sd_mplayer")) == 0
			|| strncmp(app_manager_get_current_app(), "uhost_mplayer", strlen("uhost_mplayer")) == 0
			|| strncmp(app_manager_get_current_app(), "nor_mplayer", strlen("nor_mplayer")) == 0
			|| strncmp(app_manager_get_current_app(), "loop_player", strlen("loop_player")) == 0)
			return AUDIO_STREAM_LOCAL_MUSIC;
		else if (strncmp(app_manager_get_current_app(), "btcall", strlen("btcall")) == 0)
			return AUDIO_STREAM_VOICE;
		else if (strncmp(app_manager_get_current_app(), "linein", strlen("linein")) == 0)
			return AUDIO_STREAM_LINEIN;
		else if (strncmp(app_manager_get_current_app(), "usound", strlen("usound")) == 0)
			return AUDIO_STREAM_USOUND;
		else if (strncmp(app_manager_get_current_app(), "i2srx_in", strlen("i2srx_in")) == 0)
			return AUDIO_STREAM_I2SRX_IN;
		else if (strncmp(app_manager_get_current_app(), "spdif_in", strlen("spdif_in")) == 0)
			return AUDIO_STREAM_SPDIF_IN;
		else if (strncmp(app_manager_get_current_app(), "fm", strlen("fm")) == 0)
			return AUDIO_STREAM_FM;
		else {
			SYS_LOG_ERR("invalid type\n");
			return 0;
		}
	}
}
void okmic_player_start_play(void)
{
	media_init_param_t init_param;
	io_stream_t input_stream = NULL;

	media_player_okmic_ctx_s  *okmic_ctx= get_okmic_player_ctx();

	okmic_ctx->attach_stream_type = _okmic_get_attach_stream_type(okmic_ctx->attach_stream_type);

//#ifdef CONFIG_PLAYTTS
//	tts_manager_wait_finished(false);
//#endif

	memset(&init_param, 0, sizeof(media_init_param_t));

	input_stream = _okmic_create_inputstream();

	init_param.type = MEDIA_SRV_TYPE_PLAYBACK_AND_CAPTURE;
	init_param.stream_type = AUDIO_STREAM_OKMIC;
	init_param.efx_stream_type = init_param.stream_type;
	init_param.attach_stream_type = okmic_ctx->attach_stream_type;
	init_param.format = PCM_TYPE;
	init_param.input_stream = input_stream;
	init_param.output_stream = NULL;
	init_param.event_notify_handle = NULL;
	init_param.dumpable = 1;

	init_param.capture_format = PCM_TYPE;
	init_param.capture_input_stream = NULL;
	init_param.capture_output_stream = input_stream;
	init_param.support_tws = 1;
	init_param.channels = 2;
	init_param.sample_rate = 16;
	init_param.capture_channels = 2;
	init_param.capture_sample_rate_input = 16;
	init_param.capture_sample_rate_output = 16;

	okmic_ctx->player = media_player_open(&init_param);
	if (!okmic_ctx->player) {
		SYS_LOG_ERR("player open failed\n");
		goto err_exit;
	}

	okmic_ctx->input_stream = init_param.input_stream;

	media_player_fade_in(okmic_ctx->player, 60);

	media_player_play(okmic_ctx->player);


	okmic_ctx->sample_rate = init_param.sample_rate;
	okmic_ctx->attach_stream = NULL;
	okmic_ctx->upload_stream = NULL;
	okmic_ctx->input_stream = init_param.input_stream;
	okmic_ctx->capture_format = init_param.capture_format;
	okmic_ctx->play_format = init_param.format;
	okmic_ctx->stream_type = init_param.stream_type;
	okmic_ctx->play_channels = init_param.channels;
	okmic_ctx->capture_channels = init_param.capture_channels;

	SYS_LOG_INF("player open sucessed %p ", okmic_ctx->player);
	return;

err_exit:
	if (input_stream) {
		stream_close(input_stream);
		stream_destroy(input_stream);
	}
}

void okmic_player_stop_play(void)
{
	media_player_okmic_ctx_s  *okmic_ctx= get_okmic_player_ctx();

	if (!okmic_ctx || !okmic_ctx->player)
		return;

	media_player_fade_out(okmic_ctx->player, 60);

	/** reserve time to fade out*/
	os_sleep(60);

	if (okmic_ctx->input_stream)
		stream_close(okmic_ctx->input_stream);

	media_player_stop(okmic_ctx->player);
	media_player_close(okmic_ctx->player);

	okmic_ctx->player = NULL;
	if (okmic_ctx->input_stream) {
		stream_destroy(okmic_ctx->input_stream);
		okmic_ctx->input_stream = NULL;
	}

	SYS_LOG_INF("stopped\n");
}

void okmic_player_restart(io_stream_t attach_stream, io_stream_t upload_stream,u8_t attach_channels, uint8_t upload_format, uint16_t sample_rate, uint8_t attach_stream_type)
{
	media_player_okmic_ctx_s  *okmic_ctx= get_okmic_player_ctx();

	SYS_LOG_INF("enter!");

	if (!okmic_ctx->player)
		return;

	if (upload_format == 0) {
		upload_format = PCM_TYPE;
	}

	media_player_stop(okmic_ctx->player);
	media_player_close(okmic_ctx->player);

	if (okmic_ctx->attach_stream) {
		stream_destroy(okmic_ctx->attach_stream);
		okmic_ctx->attach_stream = NULL;
	}

	okmic_ctx->player = NULL;

	media_init_param_t init_param;
	memset(&init_param, 0, sizeof(media_init_param_t));

	init_param.type = MEDIA_SRV_TYPE_PLAYBACK_AND_CAPTURE;
	init_param.stream_type = AUDIO_STREAM_OKMIC;
	init_param.efx_stream_type = AUDIO_STREAM_OKMIC;
	init_param.attach_stream_type = _okmic_get_attach_stream_type(attach_stream_type);
	init_param.format = PCM_TYPE;
	init_param.input_stream = okmic_ctx->input_stream;
	init_param.output_stream = NULL;
	init_param.event_notify_handle = NULL;
	init_param.dumpable = 1;

	init_param.capture_format = upload_format;
	init_param.capture_input_stream = NULL;
	init_param.capture_output_stream = okmic_ctx->input_stream;
	init_param.capture_attach_stream = attach_stream;
	init_param.capture_attach_channels = attach_channels;
	init_param.sample_rate = sample_rate;
	init_param.capture_sample_rate_input = sample_rate;
	init_param.capture_sample_rate_output = sample_rate;
	okmic_ctx->attach_stream = attach_stream;
	okmic_ctx->attach_stream_type = attach_stream_type;

	if (upload_format == MSBC_TYPE) {
		init_param.channels = 1;
		init_param.capture_channels = 1;
		init_param.format = PCM_TYPE;
		init_param.capture_output_stream = upload_stream;
		init_param.capture_output_pcm_stream = okmic_ctx->input_stream;
	} else {
		init_param.channels = 2;
		init_param.capture_channels = 2;
		init_param.capture_output_pcm_stream = NULL;
	}

	okmic_ctx->player = media_player_open(&init_param);
	if (!okmic_ctx->player) {
		SYS_LOG_ERR("okmic player reset err!");
	}

	media_player_play(okmic_ctx->player);
	return;
}

