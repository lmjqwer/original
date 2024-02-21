/*
 * Copyright (c) 2019 Actions Semiconductor Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief bt call media
 */
#include <msg_manager.h>
#include <thread_timer.h>
#include <media_player.h>
#include <audio_system.h>
#include <audio_policy.h>
#include <zephyr.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ringbuff_stream.h>
#include "ui_manager.h"
#include "media_mem.h"
#include "btcall.h"
#include "tts_manager.h"

static io_stream_t _bt_call_sco_create_inputstream(void)
{
	int ret = 0;
	io_stream_t input_stream = NULL;

	input_stream = ringbuff_stream_create_ext(
			media_mem_get_cache_pool(INPUT_PLAYBACK, AUDIO_STREAM_VOICE),
			media_mem_get_cache_pool_size(INPUT_PLAYBACK, AUDIO_STREAM_VOICE));

	if(!input_stream) {
		goto exit; 
	}

	ret = stream_open(input_stream, MODE_IN_OUT | MODE_READ_BLOCK | MODE_BLOCK_TIMEOUT);
	if (ret) {
		stream_destroy(input_stream);
		input_stream = NULL;
		goto exit;
	}

exit:
	SYS_LOG_INF(" %p ",input_stream);
	return 	input_stream;
}

void bt_call_start_play(void)
{
	media_init_param_t init_param;
	uint8_t codec_id = bt_manager_sco_get_codecid();
	uint8_t sample_rate = bt_manager_sco_get_sample_rate();
	struct btcall_app_t *btcall = btcall_get_app();

#ifdef CONFIG_PLAYTTS
	tts_manager_wait_finished(true);
	tts_manager_lock();
#endif

	if (btcall->player) {
		bt_call_stop_play();
		SYS_LOG_INF("already open\n");
	}

	memset(&init_param, 0, sizeof(media_init_param_t));
	init_param.type = MEDIA_SRV_TYPE_PLAYBACK_AND_CAPTURE;

	if (!btcall->upload_stream_outer) {
		btcall->upload_stream = sco_upload_stream_create(codec_id);
		if (!btcall->upload_stream) {
			SYS_LOG_INF("stream create failed");
			goto err_exit;
		}

		if (stream_open(btcall->upload_stream, MODE_OUT)) {
			stream_destroy(btcall->upload_stream);
			btcall->upload_stream = NULL;
			SYS_LOG_INF("stream open failed ");
			goto err_exit;
		}
	}

	if (codec_id == MSBC_TYPE) {
		init_param.capture_bit_rate = 26;
	}

	SYS_LOG_INF("codec_id %d sample rate: %d",codec_id, sample_rate);

	btcall->bt_stream = _bt_call_sco_create_inputstream();
	init_param.stream_type = AUDIO_STREAM_VOICE;
	init_param.efx_stream_type = AUDIO_STREAM_VOICE;
	init_param.format = codec_id;
	init_param.sample_rate = sample_rate;
	init_param.input_stream = btcall->bt_stream;
	init_param.output_stream = NULL;
	init_param.event_notify_handle = NULL;
	init_param.capture_format = codec_id;
	init_param.capture_sample_rate_input = sample_rate;
	init_param.capture_sample_rate_output = sample_rate;
	init_param.capture_input_stream = NULL;
	init_param.capture_output_stream = btcall->upload_stream;
	init_param.dumpable = true;

	if (audio_policy_get_out_audio_mode(init_param.stream_type) == AUDIO_MODE_STEREO) {
		init_param.channels = 2;
	} else {
		init_param.channels = 1;
	}

	init_param.capture_channels = 1;

	btcall->player = media_player_open(&init_param);
	if (!btcall->player) {
		goto err_exit;
	}

	bt_manager_set_stream(STREAM_TYPE_SCO, btcall->bt_stream);

	audio_system_mute_microphone(btcall->mic_mute);

	media_player_fade_in(btcall->player, 60);

	media_player_play(btcall->player);

	bt_manager_hfp_sync_vol_to_remote(audio_system_get_stream_volume(AUDIO_STREAM_VOICE));

	SYS_LOG_INF("open sucessed %p ",btcall->player);
#ifdef CONFIG_SEG_LED_MANAGER
	if (bt_manager_sco_get_codecid() == MSBC_TYPE
		&& bt_manager_hfp_get_status() != BT_STATUS_SIRI) {
		seg_led_display_string(SLED_NUMBER2, "-", true);
	}
#endif

	return;

err_exit:
	if (btcall->upload_stream && !btcall->upload_stream_outer) {
		stream_close(btcall->upload_stream);
		stream_destroy(btcall->upload_stream);
		btcall->upload_stream = NULL;
	}
#ifdef CONFIG_PLAYTTS
	tts_manager_unlock();
#endif

	SYS_LOG_ERR("open failed \n");
	return;
}

void bt_call_stop_play(void)
{
	struct btcall_app_t *btcall = btcall_get_app();

	if (!btcall->player) {
		/**avoid noise when hang up btcall */
		os_sleep(100);
		return;
	}

	media_player_fade_out(btcall->player, 100);

	/** reserve time to fade out*/
	os_sleep(100);

	bt_manager_set_stream(STREAM_TYPE_SCO, NULL);


	if (btcall->bt_stream) {
		stream_close(btcall->bt_stream);
	}

	media_player_stop(btcall->player);

	media_player_close(btcall->player);

	if (btcall->upload_stream && !btcall->upload_stream_outer) {
		stream_close(btcall->upload_stream);
		stream_destroy(btcall->upload_stream);
		btcall->upload_stream = NULL;
	}

	if (btcall->bt_stream) {
		stream_destroy(btcall->bt_stream);
		btcall->bt_stream = NULL;
	}

	SYS_LOG_INF(" %p ok \n",btcall->player);

	btcall->player = NULL;

#ifdef CONFIG_PLAYTTS
	tts_manager_unlock();
#endif

}

