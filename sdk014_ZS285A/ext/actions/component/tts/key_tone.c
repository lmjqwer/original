/*
 * Copyright (c) 2019 Actions Semiconductor Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file key tone
 */
#define SYS_LOG_DOMAIN "keytone"
#include <os_common_api.h>
#include <audio_system.h>
#include <media_player.h>
#include <buffer_stream.h>
#include <file_stream.h>
#include <app_manager.h>
#include <mem_manager.h>
#include <tts_manager.h>
#include <audio_track.h>
#include <ringbuff_stream.h>
#include <sdfs.h>

typedef enum
{
	KEY_TONE_MODE_MIX = 1,
	KEY_TONE_MODE_SINGLE,
} KEY_TONE_MODE;

struct key_tone_manager_t {
	io_stream_t tone_stream;
	u8_t key_tone_cnt;
	u8_t key_tone_mode;
	void *resample_handle;
	struct audio_track_t *keytone_track;
	os_delayed_work play_work;
	os_delayed_work stop_work;
};

static struct key_tone_manager_t key_tone_manager;

static void tone_instream_read_notify(void *observer, int readoff, int writeoff,
			int total_size, unsigned char *buf, int num, stream_notify_type type)
{
	struct key_tone_manager_t *manager = &key_tone_manager;
	if (readoff == writeoff) {
		os_delayed_work_submit(&manager->stop_work, 10);
	}
}

static void tone_outstream_read_notify(void *observer, int readoff, int writeoff,
			int total_size, unsigned char *buf, int num, stream_notify_type type)
{
	struct key_tone_manager_t *manager = &key_tone_manager;

	if (acts_ringbuf_length(stream_get_ringbuffer(manager->tone_stream)) > 0) {
		media_resample_triggle_process(manager->resample_handle);
	} else {
		os_delayed_work_submit(&manager->stop_work, 10);
	}
}


static io_stream_t _key_tone_create_stream(u8_t *key_tone_file)
{
	int ret = 0;
	io_stream_t input_stream = NULL;
	struct buffer_t buffer;

	if (sd_fmap(key_tone_file, (void **)&buffer.base, &buffer.length) != 0) {
		goto exit;
	}

	input_stream = ringbuff_stream_create_ext(buffer.base, buffer.length);
	if (!input_stream) {
		goto exit;
	}

	ret = stream_open(input_stream, MODE_IN_OUT);
	if (ret) {
		stream_destroy(input_stream);
		input_stream = NULL;
		goto exit;
	}

	stream_write(input_stream, NULL, buffer.length);

exit:
	SYS_LOG_INF("%p\n", input_stream);
	return	input_stream;
}

static void _keytone_manager_play_work(os_work *work)
{
	struct key_tone_manager_t *manager = &key_tone_manager;
	struct audio_track_t *keytone_track;
	s32_t tone_time = 0;

	audio_system_mutex_lock();

	keytone_track = audio_system_get_track();

	io_stream_t tone_stream = _key_tone_create_stream("keytone.pcm");
	if (!tone_stream) {
		goto exit;
	}
	manager->tone_stream = 	tone_stream;
	tone_time = stream_get_length(manager->tone_stream) / 2 / 8;

	/**audio_track already open*/
	if (keytone_track) {
		if (audio_system_get_output_sample_rate() == 0) {
			audio_track_set_mix_stream(keytone_track, tone_stream, 8, 1, AUDIO_STREAM_TTS);
			stream_set_observer(tone_stream, manager, tone_instream_read_notify, STREAM_NOTIFY_READ);
			manager->key_tone_mode = KEY_TONE_MODE_MIX;
		}
	} else {
		int sample_rate = audio_system_get_output_sample_rate();

		if (sample_rate != 0 && sample_rate != 8) {
			keytone_track = audio_track_create(AUDIO_STREAM_TTS, sample_rate,
										 AUDIO_FORMAT_PCM_16_BIT, AUDIO_MODE_MONO, NULL,
										NULL, manager);
			if (!keytone_track) {
				goto exit;
			}

			manager->resample_handle = media_resample_create(1, 2, 8, sample_rate,
										AUDIO_STREAM_TTS, manager->tone_stream,
										audio_track_get_stream(keytone_track));
		} else {
			keytone_track = audio_track_create(AUDIO_STREAM_TTS, 8,
										 AUDIO_FORMAT_PCM_16_BIT, AUDIO_MODE_MONO,
										 stream_get_ringbuffer(manager->tone_stream),
										NULL, manager);

			if (!keytone_track) {
				goto exit;
			}
		}
		stream_set_observer(audio_track_get_stream(keytone_track), manager, tone_outstream_read_notify, STREAM_NOTIFY_READ);
		audio_track_start(keytone_track);
		manager->key_tone_mode = KEY_TONE_MODE_SINGLE;
	}

	manager->keytone_track = keytone_track;

exit:
	audio_system_mutex_unlock();

	os_delayed_work_submit(&manager->stop_work, tone_time + 30);
}

static void _keytone_manager_stop_work(os_work *work)
{
	struct key_tone_manager_t *manager = &key_tone_manager;
	audio_system_mutex_lock();

	if (manager->key_tone_cnt == 0) {
		audio_system_mutex_unlock();
		return ;
	}

	if (manager->keytone_track) {
		if(manager->tone_stream){
			stream_close(manager->tone_stream);
			stream_destroy(manager->tone_stream);
			audio_track_set_mix_stream(manager->keytone_track, NULL, 8, 1, AUDIO_STREAM_TTS);
			manager->tone_stream = NULL;
		}

		if (manager->key_tone_mode == KEY_TONE_MODE_SINGLE) {
			audio_track_flush(manager->keytone_track);
			audio_track_stop(manager->keytone_track);
			audio_track_destory(manager->keytone_track);
			manager->keytone_track = NULL;
		}
	}

	if (manager->resample_handle) {
		media_resample_destory(manager->resample_handle);
		manager->resample_handle = NULL;
	}
	manager->keytone_track = NULL;
	manager->key_tone_cnt = 0;


	audio_system_mutex_unlock();
}

int key_tone_play(void)
{
	struct key_tone_manager_t *manager = &key_tone_manager;
	SYS_IRQ_FLAGS flags;

	sys_irq_lock(&flags);

	if (!manager->key_tone_cnt) {
		manager->key_tone_cnt = 1;
		os_delayed_work_submit(&manager->play_work, OS_NO_WAIT);
	}

	sys_irq_unlock(&flags);
	return 0;
}

int key_tone_manager_init(void)
{
	memset(&key_tone_manager, 0, sizeof(struct key_tone_manager_t));

	os_delayed_work_init(&key_tone_manager.play_work, _keytone_manager_play_work);
	os_delayed_work_init(&key_tone_manager.stop_work, _keytone_manager_stop_work);

	return 0;
}

