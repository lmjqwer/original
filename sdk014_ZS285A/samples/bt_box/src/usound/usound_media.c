/*
 * Copyright (c) 2019 Actions Semiconductor Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief usound media
 */

#include "usound.h"
#include "tts_manager.h"
#include "buffer_stream.h"
#include "ringbuff_stream.h"
#include "media_mem.h"

int usound_get_status(void)
{
	struct usound_app_t *usound = usound_get_app();

	if (!usound)
		return USOUND_STATUS_NULL;

	if (usound->playing)
		return USOUND_STATUS_PLAYING;
	else
		return USOUND_STATUS_PAUSED;
}
#ifdef CONFIG_USOUND_SPEAKER
static io_stream_t _usound_create_inputstream(void)
{
	int ret = 0;
	io_stream_t input_stream = NULL;

	input_stream = ringbuff_stream_create_ext(
			media_mem_get_cache_pool(INPUT_PLAYBACK, AUDIO_STREAM_USOUND),
			media_mem_get_cache_pool_size(INPUT_PLAYBACK, AUDIO_STREAM_USOUND));
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
#endif

#ifdef CONFIG_USOUND_MIC
static io_stream_t _usound_create_uploadstream(void)
{
	int ret = 0;
	io_stream_t upload_stream = NULL;

	upload_stream = usb_audio_upload_stream_create();
	if (!upload_stream) {
		goto exit;
	}

	ret = stream_open(upload_stream, MODE_IN_OUT);
	if (ret) {
		stream_destroy(upload_stream);
		upload_stream = NULL;
		goto exit;
	}

exit:
	SYS_LOG_INF(" %p\n", upload_stream);
	return	upload_stream;
}
#endif

void usound_start_play(void)
{
	struct usound_app_t *usound = usound_get_app();
	int volume = 0;
	media_init_param_t init_param;
#ifdef CONFIG_USOUND_SPEAKER
	io_stream_t input_stream = NULL;
#endif

#ifdef CONFIG_USOUND_MIC
	io_stream_t upload_stream = NULL;
#endif

	if (!usound)
		return;

#ifdef CONFIG_PLAYTTS
	tts_manager_wait_finished(false);
#endif

	if (usound->player) {
		SYS_LOG_INF(" already open\n");
		return;
	}

	memset(&init_param, 0, sizeof(media_init_param_t));

#ifdef CONFIG_USOUND_SPEAKER
	input_stream = _usound_create_inputstream();
	if (!input_stream) {
		goto err_exit;
	}
#endif

#ifdef CONFIG_USOUND_MIC
	upload_stream = _usound_create_uploadstream();
	if (!upload_stream) {
		goto err_exit;
	}
#endif
	init_param.stream_type = AUDIO_STREAM_USOUND;
	init_param.efx_stream_type = AUDIO_STREAM_USOUND;
#ifdef CONFIG_USOUND_SPEAKER
	init_param.format = PCM_TYPE;
	init_param.input_stream = input_stream;
	init_param.output_stream = NULL;
	init_param.event_notify_handle = NULL;
	init_param.channels = 2;
	init_param.sample_rate = 48;
#ifdef CONFIG_TWS
	/**usound not support tws only for pair to 281b*/
	if (bt_manager_tws_check_is_woodpecker()) {
		init_param.support_tws = 1;
	} else {
		init_param.support_tws = 0;
	}
#endif
#endif

#ifdef CONFIG_USOUND_MEETING_MODE
	init_param.support_tws = 0;
#endif

#ifdef CONFIG_USOUND_MIC
	init_param.capture_format = PCM_TYPE;
	init_param.capture_input_stream = NULL;
	init_param.capture_output_stream = upload_stream;
	init_param.capture_channels = 1;

#ifdef CONFIG_USOUND_MEETING_MODE
	init_param.capture_sample_rate_input = 16;
	init_param.capture_sample_rate_output = 16;
#else
	init_param.capture_sample_rate_input = 48;
	init_param.capture_sample_rate_output = 48;
#endif

	usound->usound_upload_stream = upload_stream;
#endif

#if defined(CONFIG_USOUND_SPEAKER) && defined(CONFIG_USOUND_MIC)
	init_param.type = MEDIA_SRV_TYPE_PLAYBACK_AND_CAPTURE;
#elif defined(CONFIG_USOUND_SPEAKER)
	init_param.type = MEDIA_SRV_TYPE_PLAYBACK;
#elif defined(CONFIG_USOUND_MIC)
	init_param.type = MEDIA_SRV_TYPE_CAPTURE;
#endif
	init_param.dumpable = true;

	usound->player = media_player_open(&init_param);
	if (!usound->player) {
		SYS_LOG_ERR("open failed\n");
		goto err_exit;
	}

	usound->usound_stream = init_param.input_stream;

	usb_audio_set_stream(usound->usound_stream);

	media_player_fade_in(usound->player, 60);

	media_player_play(usound->player);

	volume = audio_system_get_stream_volume(AUDIO_STREAM_USOUND);
	
	media_player_set_volume(usound->player, volume, volume);

	SYS_LOG_INF("sucessed %p ", usound->player);
	return;
err_exit:
#ifdef CONFIG_USOUND_SPEAKER
	if (input_stream) {
		stream_close(input_stream);
		stream_destroy(input_stream);
	}
#endif
#ifdef CONFIG_USOUND_MIC
	if (upload_stream) {
		stream_close(upload_stream);
		stream_destroy(upload_stream);
	}
#endif
	SYS_LOG_INF("failed\n");
}

void usound_stop_play(void)
{
	struct usound_app_t *usound = usound_get_app();

	if (!usound)
		return;

	if (!usound->player)
		return;
#ifdef CONFIG_USOUND_SPEAKER
	media_player_fade_out(usound->player, 60);

	/** reserve time to fade out*/
	os_sleep(60);

	if (usound->usound_stream) {
		usb_audio_set_stream(NULL);
		stream_close(usound->usound_stream);
	}
#endif

#ifdef CONFIG_USOUND_MIC
	if (usound->usound_upload_stream)
		stream_close(usound->usound_upload_stream);
#endif

	media_player_stop(usound->player);

	media_player_close(usound->player);

	SYS_LOG_INF(" %p ok\n", usound->player);

	usound->player = NULL;

#ifdef CONFIG_USOUND_SPEAKER
	if (usound->usound_stream) {
		stream_destroy(usound->usound_stream);
		usound->usound_stream = NULL;
	}
#endif

#ifdef CONFIG_USOUND_MIC
	if (usound->usound_upload_stream) {
		stream_destroy(usound->usound_upload_stream);
		usound->usound_upload_stream = NULL;
	}
#endif

}

