/*
 * Copyright (c) 2016 Actions Semi Co., Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief audio track.
*/

#include <os_common_api.h>
#include <mem_manager.h>
#include <msg_manager.h>
#include <audio_track.h>
#include <audio_device.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <media_mem.h>
#include <assert.h>
#include <ringbuff_stream.h>
#include <arithmetic.h>

#define SYS_LOG_NO_NEWLINE
#ifdef SYS_LOG_DOMAIN
#undef SYS_LOG_DOMAIN
#endif
#define SYS_LOG_DOMAIN "audio track"
#include <logging/sys_log.h>

#define FADE_IN_TIME_MS (60)
#define FADE_OUT_TIME_MS (100)

static u8_t reload_pcm_buff[1024];

extern int stream_read_pcm(asin_pcm_t *aspcm, io_stream_t stream, int max_samples, int debug_space);
extern void *media_resample_open(u8_t channels, u8_t samplerate_in,
		u8_t samplerate_out, int *samples_in, int *samples_out, u8_t stream_type, int cnt);
extern int media_resample_process(void *handle, u8_t channels, void *output_buf[2],
		void *input_buf[2], int input_samples);
extern void media_resample_close(void *handle);

extern void *media_fade_open(u8_t sample_rate, u8_t channels, u8_t is_interweaved);
extern void media_fade_close(void *handle);
extern int media_fade_process(void *handle, void *inout_buf[2], int samples);
extern int media_fade_in_set(void *handle, int fade_time_ms);
extern int media_fade_out_set(void *handle, int fade_time_ms);
extern int media_fade_out_is_finished(void *handle);

extern void *media_mix_open(u8_t sample_rate, u8_t channels, u8_t is_interweaved);
extern void media_mix_close(void *handle);
extern int media_mix_process(void *handle, void *inout_buf[2], void *mix_buf,
			     int samples);

static int _aduio_track_update_output_samples(struct audio_track_t *handle, u32_t length)
{
	SYS_IRQ_FLAGS flags;
	sys_irq_lock(&flags);

	handle->output_samples += (u32_t )(length / handle->frame_size);

	sys_irq_unlock(&flags);

	return handle->output_samples;
}

static void _audio_track_fadeout_procress(struct audio_track_t *handle, void *inout_buf, u32_t samples_cnt)
{
	short *pcm = (short *)inout_buf;
	int i;
	u8_t channels = handle->audio_mode == AUDIO_MODE_MONO ? 1 : 2;
	u32_t value[2] = {0, 0};

	for (i = 0; i < samples_cnt; i++) {
		value[0] = (int) (*(short *) pcm);
		//printk(" org:%x\t i:%d cnt:%d samples:%d\n", value[0], i, handle->fade_out_cnt, handle->fade_out_samples);
		value[0] = value[0] * handle->fade_out_cnt / handle->fade_out_samples;
		*(short *) (pcm) = (short )value[0];
		pcm++;

		if (channels > 1) {
			value[1] = (int) (*(short *)pcm);
			value[1] = value[1] * handle->fade_out_cnt / handle->fade_out_samples;
			*(short *) (pcm) = (short )value[1];
			pcm++;
		}

		if (handle->fade_out_cnt > 0)
			handle->fade_out_cnt--;
	}

}

static int _audio_track_data_mix(struct audio_track_t *handle, s16_t *src_buff, u16_t samples, s16_t *dest_buff)
{
	int ret = 0;
	u16_t mix_num = 0;
	u8_t track_channels = (handle->audio_mode != AUDIO_MODE_MONO) ? 2 : 1;
	asin_pcm_t mix_pcm = {
		.channels = handle->mix_channels,
		.sample_bits = 16,
		.pcm = { handle->res_in_buf[0], handle->res_in_buf[1], },
	};

	if (stream_get_length(handle->mix_stream) <= 0 && handle->res_remain_samples <= 0)
		return 0;

	if (track_channels > 1)
		samples /= 2;

	while (1) {
		u16_t mix_samples = min(samples, handle->res_remain_samples);
		s16_t *mix_buff[2] = {
			handle->res_out_buf[0] + handle->res_out_samples - handle->res_remain_samples,
			handle->res_out_buf[1] + handle->res_out_samples - handle->res_remain_samples,
		};

		/* 1) consume remain samples */
		if (handle->mix_handle && dest_buff == src_buff) {
			media_mix_process(handle->mix_handle, (void **)&dest_buff, mix_buff[0], mix_samples);
			dest_buff += track_channels * mix_samples;
			src_buff += track_channels * mix_samples;
		} else {
			if (track_channels > 1) {
				for (int i = 0; i < mix_samples; i++) {
					*dest_buff++ = (*src_buff++) / 2 + mix_buff[0][i] / 2;
					*dest_buff++ = (*src_buff++) / 2 + mix_buff[1][i] / 2;
				}
			} else {
				for (int i = 0; i < mix_samples; i++) {
					*dest_buff++ = (*src_buff++) / 2 + mix_buff[0][i] / 2;
				}
			}
		}

		handle->res_remain_samples -= mix_samples;
		samples -= mix_samples;
		mix_num += mix_samples;
		if (samples <= 0)
			break;

		/* 2) read mix stream and do resample as required */
		mix_pcm.samples = 0;
		ret = stream_read_pcm(&mix_pcm, handle->mix_stream, handle->res_in_samples, INT32_MAX);
		if (ret <= 0)
			break;

		if (handle->res_handle) {
			u8_t res_channels = min(handle->mix_channels, track_channels);

			if (ret < handle->res_in_samples) {
				memset((u16_t *)mix_pcm.pcm[0] + ret, 0, (handle->res_in_samples - ret) * 2);
				if (res_channels > 1)
					memset((u16_t *)mix_pcm.pcm[1] + ret, 0, (handle->res_in_samples - ret) * 2);
			}

			/* do resample */
			handle->res_out_samples = media_resample_process(handle->res_handle, res_channels,
					(void **)handle->res_out_buf, (void **)mix_pcm.pcm, handle->res_in_samples);
			handle->res_remain_samples = handle->res_out_samples;
		} else {
			handle->res_out_samples = ret;
			handle->res_remain_samples = handle->res_out_samples;
		}
	}

	return mix_num;
}

static int _audio_track_request_more_data(void *handle, u32_t reason)
{
	static u8_t printk_cnt;
	struct audio_track_t *audio_track = (struct audio_track_t *)handle;
	int read_len = audio_track->pcm_frame_size / 2;
	int stream_length = stream_get_length(audio_track->audio_stream);
	int ret = 0;
	bool reload_mode = ((audio_track->channel_mode & AUDIO_DMA_RELOAD_MODE) == AUDIO_DMA_RELOAD_MODE);
	u8_t *buf = NULL;

	if (reload_mode) {
		if (reason == AOUT_DMA_IRQ_HF) {
			buf = audio_track->pcm_frame_buff;
		} else if (reason == AOUT_DMA_IRQ_TC) {
			buf = audio_track->pcm_frame_buff + read_len;
		}
	} else {
		buf = audio_track->pcm_frame_buff;
		if (stream_length > audio_track->pcm_frame_size) {
			read_len = audio_track->pcm_frame_size;
		} else {
			read_len = audio_track->pcm_frame_size / 2;
		}
		if (audio_track->channel_id == AOUT_FIFO_DAC0) {
			if (reason == AOUT_DMA_IRQ_TC) {
				printk("pcm empty\n");
			}
		}
	}

	if (audio_track->compensate_samples > 0) {
		/* insert data */
		if (audio_track->compensate_samples < read_len) {
			read_len = audio_track->compensate_samples;
		}
		memset(buf, 0, read_len);
		audio_track->fill_cnt += read_len;
		audio_track->compensate_samples -= read_len;
		goto exit;
	} else if (audio_track->compensate_samples < 0) {
		/* drop data */
		if (read_len > -audio_track->compensate_samples) {
			read_len = -audio_track->compensate_samples;
		}
		if (stream_get_length(audio_track->audio_stream) >= read_len * 2) {
			stream_read(audio_track->audio_stream, buf, read_len);
			_aduio_track_update_output_samples(audio_track, read_len);
			audio_track->fill_cnt -= read_len;
			audio_track->compensate_samples += read_len;
		}
	}

	if (read_len > stream_get_length(audio_track->audio_stream)) {
		if (audio_track->stream_type == AUDIO_STREAM_TTS || audio_track->flushed) {
			read_len = stream_get_length(audio_track->audio_stream);
		} else if (!audio_track->flushed) {
			memset(buf, 0, read_len);
			audio_track->fill_cnt += read_len;
			if (!printk_cnt++) {
				printk("F\n");
			}
			goto exit;
		}
	}

	ret = stream_read(audio_track->audio_stream, buf, read_len);
	if (ret != read_len) {
		memset(buf, 0, read_len);
		audio_track->fill_cnt += read_len;
		if (!printk_cnt++) {
			printk("F\n");
		}
	} else {
		_aduio_track_update_output_samples(audio_track, read_len);
	}

	if (audio_track->muted) {
		memset(buf, 0, read_len);
		audio_track->fill_cnt += read_len;
		goto exit;
	}


exit:
	if (!reload_mode && read_len > 0) {
		if (audio_track->fade_out_start) {
			int fade_outsamples = audio_track->audio_mode == AUDIO_MODE_MONO ?
						read_len / 2 : read_len / 4;
			if (audio_track->fade_out_cnt - fade_outsamples <= 0 && audio_track->fade_out_cnt > 0) {
				_audio_track_fadeout_procress(audio_track, buf, fade_outsamples);
				if (audio_track->fade_out_cnt <= 0) {
					audio_track->fade_out_cnt = 0;
					audio_track->fade_out_samples = 0;
				}
			} else if (audio_track->fade_out_cnt > 0 && audio_track->fade_out_cnt > fade_outsamples){
				audio_track->fade_out_cnt -= fade_outsamples;
				audio_track->fade_out_samples -= fade_outsamples;
			} else {
				if (audio_track->fade_out_mute > 0) {
					memset(buf, 0, read_len);
					audio_track->fade_out_mute -= fade_outsamples;
				}
			}
		}
#if FADE_OUT_TIME_MS > 0
		if (audio_track->flushed && audio_track->fade_handle) {
			int samples = audio_track->audio_mode == AUDIO_MODE_MONO ?
					    read_len / 2 : read_len / 4;
			media_fade_process(audio_track->fade_handle, (void **)&buf, samples);
		}
#endif
		hal_aout_channel_write_data(audio_track->audio_handle, buf, read_len);
	}

	if (audio_track->channel_id == AOUT_FIFO_DAC0) {
		/**local music max send 3K pcm data */
		if (audio_track->stream_type == AUDIO_STREAM_LOCAL_MUSIC) {
			if (stream_get_length(audio_track->audio_stream) > audio_track->pcm_frame_size) {
				stream_read(audio_track->audio_stream, buf, audio_track->pcm_frame_size);
				_aduio_track_update_output_samples(audio_track, audio_track->pcm_frame_size);
#if FADE_OUT_TIME_MS > 0
				if (audio_track->flushed && audio_track->fade_handle) {
					int samples = audio_track->audio_mode == AUDIO_MODE_MONO ?
								audio_track->pcm_frame_size / 2 : audio_track->pcm_frame_size / 4;
					media_fade_process(audio_track->fade_handle, (void **)&buf, samples);
				}
#endif
				hal_aout_channel_write_data(audio_track->audio_handle, buf, audio_track->pcm_frame_size);
			}

			if (stream_get_length(audio_track->audio_stream) > audio_track->pcm_frame_size) {
				stream_read(audio_track->audio_stream, buf, audio_track->pcm_frame_size);
				_aduio_track_update_output_samples(audio_track, audio_track->pcm_frame_size);
#if FADE_OUT_TIME_MS > 0
				if (audio_track->flushed && audio_track->fade_handle) {
					int samples = audio_track->audio_mode == AUDIO_MODE_MONO ?
								audio_track->pcm_frame_size / 2 : audio_track->pcm_frame_size / 4;
					media_fade_process(audio_track->fade_handle, (void **)&buf, samples);
				}
#endif
				hal_aout_channel_write_data(audio_track->audio_handle, buf, audio_track->pcm_frame_size);
			}
		}

		/**last frame send more 2 samples */
		if (audio_track->flushed && stream_get_length(audio_track->audio_stream) == 0) {
			memset(buf, 0, 8);
			hal_aout_channel_write_data(audio_track->audio_handle, buf, 8);
		}
	}
	return 0;
}

static void *_audio_track_init(struct audio_track_t *handle)
{
	audio_out_init_param_t aout_param = {0};

#ifdef AOUT_CHANNEL_AA
	if (handle->channel_type == AOUT_CHANNEL_AA) {
		aout_param.aa_mode = 1;
	}
#endif

	aout_param.sample_rate =  handle->output_sample_rate;
	aout_param.channel_type = handle->channel_type;
	aout_param.channel_id =  handle->channel_id;
	aout_param.data_width = 16;
	aout_param.channel_mode = handle->audio_mode;
	aout_param.left_volume = audio_system_get_current_pa_volume(handle->stream_type);
	aout_param.right_volume = aout_param.left_volume;

	aout_param.sample_cnt_enable = 1;

	aout_param.callback = _audio_track_request_more_data;
	aout_param.callback_data = handle;

	if ((handle->channel_mode & AUDIO_DMA_RELOAD_MODE) == AUDIO_DMA_RELOAD_MODE) {
		aout_param.dma_reload = 1;
		aout_param.reload_len = handle->pcm_frame_size;
		aout_param.reload_addr = handle->pcm_frame_buff;
	}

	return hal_aout_channel_open(&aout_param);
}

static void _audio_track_stream_observer_notify(void *observer, int readoff, int writeoff, int total_size,
										unsigned char *buf, int num, stream_notify_type type)
{
	struct audio_track_t *handle = (struct audio_track_t *)observer;

#if FADE_IN_TIME_MS > 0
	if (handle->fade_handle && type == STREAM_NOTIFY_PRE_WRITE) {
		int samples = handle->audio_mode == AUDIO_MODE_MONO ? num / 2 : num / 4;

		media_fade_process(handle->fade_handle, (void **)&buf, samples);
	}
#endif

	audio_system_mutex_lock();

	if (handle->mix_stream && (type == STREAM_NOTIFY_PRE_WRITE)) {
		_audio_track_data_mix(handle, (s16_t *)buf, num / 2, (s16_t *)buf);
	}
	audio_system_mutex_unlock();
	if (!handle->stared
		&& (type == STREAM_NOTIFY_WRITE)
		&& !audio_track_is_waitto_start(handle)
		&& stream_get_length(handle->audio_stream) >= handle->pcm_frame_size) {

		os_sched_lock();

		if (handle->event_cb)
			handle->event_cb(1, handle->user_data);

		handle->stared = 1;

		stream_read(handle->audio_stream, handle->pcm_frame_buff, handle->pcm_frame_size);
		_aduio_track_update_output_samples(handle, handle->pcm_frame_size);

		if ((handle->channel_mode & AUDIO_DMA_RELOAD_MODE) == AUDIO_DMA_RELOAD_MODE) {
			hal_aout_channel_start(handle->audio_handle);
		} else {
			hal_aout_channel_write_data(handle->audio_handle, handle->pcm_frame_buff, handle->pcm_frame_size);
		}

		os_sched_unlock();
	}
}

struct audio_track_t *audio_track_create(u8_t stream_type, int sample_rate,
									u8_t format, u8_t audio_mode, void *outer_stream,
									void (*event_cb)(u8_t event, void *user_data), void *user_data)
{
	struct audio_track_t *audio_track = NULL;

	while (audio_system_get_track()) {
		os_sleep(2);
	}

	audio_system_mutex_lock();

	audio_track = mem_malloc(sizeof(struct audio_track_t));
	if (!audio_track)
		goto err_exit;

	memset(audio_track, 0, sizeof(struct audio_track_t));
	audio_track->stream_type = stream_type;
	audio_track->audio_format = format;
	audio_track->audio_mode = audio_mode;
	audio_track->sample_rate = sample_rate;
	audio_track->compensate_samples = 0;

	audio_track->channel_type = audio_policy_get_out_channel_type(stream_type);
	audio_track->channel_id = audio_policy_get_out_channel_id(stream_type);
	audio_track->channel_mode = audio_policy_get_out_channel_mode(stream_type);
	audio_track->volume = audio_system_get_stream_volume(stream_type);
	audio_track->output_sample_rate = audio_system_get_output_sample_rate();

	if (!audio_track->output_sample_rate)
		audio_track->output_sample_rate = audio_track->sample_rate;


	if (audio_track->audio_mode == AUDIO_MODE_DEFAULT)
		audio_track->audio_mode = audio_policy_get_out_audio_mode(stream_type);

	if (audio_track->audio_mode == AUDIO_MODE_MONO) {
		audio_track->frame_size = 2;
	} else if (audio_mode == AUDIO_MODE_STEREO) {
		audio_track->frame_size = 4;
	}

	/* dma reload buff */
	if (system_check_low_latencey_mode()) {
		if (audio_track->sample_rate <= 16) {
			audio_track->pcm_frame_size = 256;
		} else {
			audio_track->pcm_frame_size = 512;
		}
	} else {
		audio_track->pcm_frame_size = (sample_rate <= 16) ? 512 : 1024;
	}

	if (audio_track->pcm_frame_size / 2 < CONFIG_PCMBUF_HF_THRESHOLD) {
		audio_track->pcm_frame_size = 1024;
	}

	audio_track->pcm_frame_buff = reload_pcm_buff;
	if (!audio_track->pcm_frame_buff)
		goto err_exit;

	audio_track->audio_handle = _audio_track_init(audio_track);
	if (!audio_track->audio_handle) {
		goto err_exit;
	}

	if (audio_system_get_current_volume(audio_track->stream_type) == 0
		&& audio_track->stream_type != AUDIO_STREAM_OKMIC
		&& audio_track->stream_type != AUDIO_STREAM_OKMIC_FM
		&& audio_track->stream_type != AUDIO_STREAM_OKMIC_LINE_IN) {
		hal_aout_channel_mute_ctl(audio_track->audio_handle, 1);
	} else {
		hal_aout_channel_mute_ctl(audio_track->audio_handle, 0);
	}

	if (audio_policy_get_out_channel_type(audio_track->stream_type) == AUDIO_CHANNEL_I2STX ||
		audio_policy_get_out_channel_type(audio_track->stream_type) == AUDIO_CHANNEL_SPDIFTX) {
		if (audio_policy_get_out_channel_id(audio_track->stream_type) == AOUT_FIFO_DAC0) {
			audio_track_set_volume(audio_track, audio_system_get_current_volume(audio_track->stream_type));
		}
	}

	if (outer_stream) {
		audio_track->audio_stream = ringbuff_stream_create((struct acts_ringbuf *)outer_stream);
	} else {
		audio_track->audio_stream = ringbuff_stream_create_ext(
									media_mem_get_cache_pool(OUTPUT_PCM, stream_type),
									media_mem_get_cache_pool_size(OUTPUT_PCM, stream_type));
	}

	if (!audio_track->audio_stream) {
		goto err_exit;
	}

	if (stream_open(audio_track->audio_stream, MODE_IN_OUT | MODE_WRITE_BLOCK)) {
		stream_destroy(audio_track->audio_stream);
		audio_track->audio_stream = NULL;
		SYS_LOG_ERR(" stream open failed ");
		goto err_exit;
	}

	stream_set_observer(audio_track->audio_stream, audio_track,
		_audio_track_stream_observer_notify, STREAM_NOTIFY_WRITE | STREAM_NOTIFY_PRE_WRITE);

	if (audio_system_register_track(audio_track)) {
		SYS_LOG_ERR(" registy track failed ");
		goto err_exit;
	}

	if (audio_track->stream_type != AUDIO_STREAM_VOICE) {
		audio_track->fade_handle = media_fade_open(
				audio_track->sample_rate,
				audio_track->audio_mode == AUDIO_MODE_MONO ? 1 : 2, 1);
#if FADE_IN_TIME_MS > 0
		if (audio_track->fade_handle)
			media_fade_in_set(audio_track->fade_handle, FADE_IN_TIME_MS);
#endif
	}

	audio_track->event_cb = event_cb;
	audio_track->user_data = user_data;

	if (system_check_low_latencey_mode() && audio_track->sample_rate <= 16) {
		int he_thres = CONFIG_PCMBUF_HE_THRESHOLD > 0x60? 0x30 : 0x10;
		int hf_thres = he_thres + 0x10;
		hal_aout_set_pcm_threshold(audio_track->audio_handle, he_thres, hf_thres);
	}

	if (stream_get_space(audio_track->audio_stream)+ stream_get_length(audio_track->audio_stream) < audio_track->pcm_frame_size) {
		audio_track->pcm_frame_size = stream_get_space(audio_track->audio_stream);
		if (audio_track->pcm_frame_size / 2 < CONFIG_PCMBUF_HF_THRESHOLD) {
			hal_aout_set_pcm_threshold(audio_track->audio_handle, CONFIG_PCMBUF_HE_THRESHOLD / 2, CONFIG_PCMBUF_HF_THRESHOLD / 2);
		}
	}

	SYS_LOG_DBG("stream_type : %d", audio_track->stream_type);
	SYS_LOG_DBG("audio_format : %d", audio_track->audio_format);
	SYS_LOG_DBG("audio_mode : %d", audio_track->audio_mode);
	SYS_LOG_DBG("channel_type : %d", audio_track->channel_type);
	SYS_LOG_DBG("channel_id : %d", audio_track->channel_id);
	SYS_LOG_DBG("channel_mode : %d", audio_track->channel_mode);
	SYS_LOG_DBG("input_sr : %d ", audio_track->sample_rate);
	SYS_LOG_DBG("output_sr : %d", audio_track->output_sample_rate);
	SYS_LOG_DBG("volume : %d", audio_track->volume);
	SYS_LOG_DBG("audio_stream : %p", audio_track->audio_stream);
	audio_system_mutex_unlock();
	return audio_track;

err_exit:
	if (audio_track)
		mem_free(audio_track);
	audio_system_mutex_unlock();
	return NULL;
}

int audio_track_destory(struct audio_track_t *handle)
{
	assert(handle);

	SYS_LOG_INF("destory %p begin", handle);
	audio_system_mutex_lock();

	if (audio_system_unregister_track(handle)) {
		SYS_LOG_ERR(" failed\n");
		return -ESRCH;
	}

	if (handle->audio_handle) {
		hal_aout_channel_close(handle->audio_handle);
	}

	if (handle->audio_stream)
		stream_destroy(handle->audio_stream);

	if (handle->fade_handle)
		media_fade_close(handle->fade_handle);

	mem_free(handle);

	audio_system_mutex_unlock();
	SYS_LOG_INF("destory %p ok", handle);
	return 0;
}

int audio_track_start(struct audio_track_t *handle)
{
	assert(handle);

	audio_track_set_waitto_start(handle, false);

	if (!handle->stared && stream_get_length(handle->audio_stream) >= handle->pcm_frame_size) {

		os_sched_lock();

		if (handle->event_cb) {
			handle->event_cb(1, handle->user_data);
		}

		handle->stared = 1;

		stream_read(handle->audio_stream, handle->pcm_frame_buff, handle->pcm_frame_size);
		_aduio_track_update_output_samples(handle, handle->pcm_frame_size);

		if ((handle->channel_mode & AUDIO_DMA_RELOAD_MODE) == AUDIO_DMA_RELOAD_MODE) {
			hal_aout_channel_start(handle->audio_handle);
		} else {
			hal_aout_channel_write_data(handle->audio_handle, handle->pcm_frame_buff, handle->pcm_frame_size);
		}

		os_sched_unlock();
	}

	return 0;
}

int audio_track_stop(struct audio_track_t *handle)
{
	assert(handle);

	SYS_LOG_INF("stop %p begin ", handle);

	if (handle->audio_handle)
		hal_aout_channel_stop(handle->audio_handle);

	if (handle->audio_stream)
		stream_close(handle->audio_stream);

	SYS_LOG_INF("stop %p ok ", handle);
	return 0;
}

int audio_track_pause(struct audio_track_t *handle)
{
	assert(handle);

	handle->muted = 1;

	stream_flush(handle->audio_stream);

	return 0;
}

int audio_track_resume(struct audio_track_t *handle)
{
	assert(handle);

	handle->muted = 0;

	return 0;
}

int audio_track_mute(struct audio_track_t *handle, int mute)
{
	assert(handle);

    handle->muted = mute;

	return 0;
}

int audio_track_is_muted(struct audio_track_t *handle)
{
	assert(handle);

    return handle->muted;
}

int audio_track_write(struct audio_track_t *handle, unsigned char *buf, int num)
{
	int ret = 0;

	assert(handle && handle->audio_stream);

	ret = stream_write(handle->audio_stream, buf, num);
	if (ret != num) {
		SYS_LOG_WRN(" %d %d\n", ret, num);
	}

	return ret;
}

int audio_track_flush(struct audio_track_t *handle)
{
	int try_cnt = 0;
	SYS_IRQ_FLAGS flags;

	assert(handle);

	sys_irq_lock(&flags);
	if (handle->flushed) {
		sys_irq_unlock(&flags);
		return 0;
	}
	/**wait trace stream read empty*/
	handle->flushed = 1;
	sys_irq_unlock(&flags);

#if FADE_OUT_TIME_MS > 0
	if (handle->fade_handle) {
		int fade_time = stream_get_length(handle->audio_stream) / 2;

		if (handle->audio_mode == AUDIO_MODE_STEREO)
			fade_time /= 2;

		fade_time /= handle->sample_rate;

		audio_track_set_fade_out(handle, fade_time);
	}
#endif

	while (stream_get_length(handle->audio_stream) > 0
			&& try_cnt++ < 100 && handle->stared) {
		if (try_cnt % 10 == 0) {
			SYS_LOG_INF("try_cnt %d stream %d\n", try_cnt,
					stream_get_length(handle->audio_stream));
		}
		os_sleep(2);
	}

	SYS_LOG_INF("try_cnt %d, left_len %d\n", try_cnt,
			stream_get_length(handle->audio_stream));

	audio_track_set_mix_stream(handle, NULL, 0, 1, AUDIO_STREAM_TTS);

	if (handle->audio_stream) {
		stream_close(handle->audio_stream);
	}

	return 0;
}

int audio_track_set_fade_out(struct audio_track_t *handle, int fade_time)
{
	assert(handle);

#if FADE_OUT_TIME_MS > 0
	if (handle->fade_handle && !handle->fade_out) {

		media_fade_out_set(handle->fade_handle, min(FADE_OUT_TIME_MS, fade_time));
		handle->fade_out = 1;
	}
#endif
	return 0;
}

int audio_track_start_fade_out(struct audio_track_t *handle, int fade_cnd)
{
	assert(handle);

	if (!handle->fade_out_start) {
		handle->fade_out_mute = 2048;
		handle->fade_out_cnt = fade_cnd;
		handle->fade_out_samples = fade_cnd;
		handle->fade_out_start = 1;
	}
	return 0;
}

int audio_track_stop_fade_out(struct audio_track_t *handle)
{
	assert(handle);

	if (handle->fade_out_start) {
		handle->fade_out_start = 0;
	}
	return 0;
}


int audio_track_set_waitto_start(struct audio_track_t *handle, bool wait)
{
	assert(handle);

	handle->waitto_start = wait ? 1 : 0;
	return 0;
}

int audio_track_is_waitto_start(struct audio_track_t *handle)
{
	if (!handle)
		return 0;

	return handle->waitto_start;
}

int audio_track_set_volume(struct audio_track_t *handle, int volume)
{
	u32_t pa_volume = 0;

	assert(handle);

	SYS_LOG_INF(" volume %d\n", volume);
	if (volume) {
		hal_aout_channel_mute_ctl(handle->audio_handle, 0);
		pa_volume = audio_policy_get_pa_volume(handle->stream_type, volume);
		hal_aout_channel_set_pa_vol_level(handle->audio_handle, pa_volume);
	} else {
		hal_aout_channel_mute_ctl(handle->audio_handle, 1);
		if (audio_policy_get_out_channel_type(handle->stream_type) == AUDIO_CHANNEL_I2STX) {
			/* i2s not support mute, so we set lowest volume -71625*/
			pa_volume = -71625;
			hal_aout_channel_set_pa_vol_level(handle->audio_handle, pa_volume);
		}
	}

	handle->volume = volume;
	return 0;
}

int audio_track_set_pa_volume(struct audio_track_t *handle, int pa_volume)
{
	assert(handle);

	SYS_LOG_INF("pa_volume %d\n", pa_volume);

	hal_aout_channel_mute_ctl(handle->audio_handle, 0);
	hal_aout_channel_set_pa_vol_level(handle->audio_handle, pa_volume);

	/* sync back to volume level, though meaningless for AUDIO_STREAM_VOICE at present. */
	handle->volume = audio_policy_get_volume_level_by_db(handle->stream_type, pa_volume / 100);
	return 0;
}

int audio_track_set_mute(struct audio_track_t *handle, bool mute)
{
	assert(handle);

	SYS_LOG_INF(" mute %d\n", mute);

	hal_aout_channel_mute_ctl(handle->audio_handle, mute);

	return 0;
}

int audio_track_get_volume(struct audio_track_t *handle)
{
	assert(handle);

	return handle->volume;
}

int audio_track_set_samplerate(struct audio_track_t *handle, int sample_rate)
{
	assert(handle);

	handle->sample_rate = sample_rate;
	return 0;
}

int audio_track_get_samplerate(struct audio_track_t *handle)
{
	assert(handle);

	return handle->sample_rate;
}

io_stream_t audio_track_get_stream(struct audio_track_t *handle)
{
	assert(handle);

	return handle->audio_stream;
}

int audio_track_get_fill_samples(struct audio_track_t *handle)
{
	assert(handle);
	if (handle->fill_cnt) {
		SYS_LOG_INF("fill_cnt %d\n", handle->fill_cnt);
	}
	return handle->fill_cnt;
}

u32_t audio_track_get_output_samples(struct audio_track_t *handle)
{
	SYS_IRQ_FLAGS flags;
	u32_t ret = 0;

	assert(handle);

	sys_irq_lock(&flags);

	ret = handle->output_samples;

	handle->output_samples = 0;

	sys_irq_unlock(&flags);
	return ret;
}

int audio_track_get_remain_pcm_samples(struct audio_track_t *handle)
{
	assert(handle);

	return hal_aout_channel_get_remain_pcm_samples(handle->audio_handle);
}

int audio_track_compensate_samples(struct audio_track_t *handle, int samples_cnt)
{
	SYS_IRQ_FLAGS flags;

	assert(handle);

	sys_irq_lock(&flags);

	handle->compensate_samples = samples_cnt;

	sys_irq_unlock(&flags);
	return 0;
}

int audio_track_set_mix_stream(struct audio_track_t *handle, io_stream_t mix_stream,
		u8_t sample_rate, u8_t channels, u8_t stream_type)
{
	int res = 0;

	assert(handle);

	audio_system_mutex_lock();

	if (audio_system_get_track() != handle) {
		goto exit;
	}

	if (mix_stream) {
		//u16_t *frame_buf = media_mem_get_cache_pool(RESAMPLE_FRAME_KEYTONE_DATA, stream_type);
		//int frame_buf_size = media_mem_get_cache_pool_size(RESAMPLE_FRAME_KEYTONE_DATA, stream_type);
		u8_t track_channels = (handle->audio_mode != AUDIO_MODE_MONO) ? 2 : 1;
		u8_t res_channels = min(channels, track_channels);

		if (sample_rate != handle->sample_rate) {
			int frame_size;
			u16_t *frame_buf = NULL;

			handle->res_handle = media_resample_open(
					res_channels, sample_rate, handle->sample_rate,
					&handle->res_in_samples, &handle->res_out_samples, stream_type, 1);
			if (!handle->res_handle) {
				SYS_LOG_ERR("media_resample_open failed");
				res = -ENOMEM;
				goto exit;
			}

			frame_size = channels * (ROUND_UP(handle->res_in_samples, 2) + ROUND_UP(handle->res_out_samples, 2));

			frame_buf = mem_malloc(frame_size * 2);
			if (!frame_buf) {
				SYS_LOG_ERR("frame mem not enough");
				media_resample_close(handle->res_handle);
				handle->res_handle = NULL;
				res = -ENOMEM;
				goto exit;
			}

			handle->res_in_buf[0] = frame_buf;
			handle->res_in_buf[1] = (channels > 1) ?
					handle->res_in_buf[0] + ROUND_UP(handle->res_in_samples, 2) : handle->res_in_buf[0];

			handle->res_out_buf[0] = handle->res_in_buf[1] + ROUND_UP(handle->res_in_samples, 2);
			handle->res_out_buf[1] = (res_channels > 1) ?
					handle->res_out_buf[0] + ROUND_UP(handle->res_out_samples, 2) : handle->res_out_buf[0];
		} else {
			int frame_buf_size = 128;
			u16_t *frame_buf = mem_malloc(frame_buf_size * channels * 2);
			if (!frame_buf) {
				SYS_LOG_ERR("frame mem not enough");
				res = -ENOMEM;
				goto exit;
			}

			handle->res_in_samples = frame_buf_size / 2 / channels;
			handle->res_in_buf[0] = frame_buf;
			handle->res_in_buf[1] = (channels > 1) ?
					handle->res_in_buf[0] + handle->res_in_samples : handle->res_in_buf[0];

			handle->res_out_buf[0] = handle->res_in_buf[0];
			handle->res_out_buf[1] = handle->res_in_buf[1];
		}

		handle->res_out_samples = 0;
		handle->res_remain_samples = 0;

		/* open mix: only support 1 mix channels */
		if (channels == 1) {
			handle->mix_handle = media_mix_open(handle->sample_rate, track_channels, 1);
		}
	}

	handle->mix_stream = mix_stream;
	handle->mix_sample_rate = sample_rate;
	handle->mix_channels = channels;

	if (!handle->mix_stream) {
		if (handle->res_handle) {
			media_resample_close(handle->res_handle);
			handle->res_handle = NULL;

			if (handle->res_in_buf[0]) {
				mem_free(handle->res_in_buf[0]);
				handle->res_in_buf[0] = NULL;
			}
		}

		if (handle->mix_handle) {
			media_mix_close(handle->mix_handle);
			handle->mix_handle = NULL;
		}
	}

	SYS_LOG_INF("mix_stream %p, sample_rate %d->%d\n",
			mix_stream, sample_rate, handle->sample_rate);

exit:
	audio_system_mutex_unlock();
	return res;
}

io_stream_t audio_track_get_mix_stream(struct audio_track_t *handle)
{
	assert(handle);

	return handle->mix_stream;
}
