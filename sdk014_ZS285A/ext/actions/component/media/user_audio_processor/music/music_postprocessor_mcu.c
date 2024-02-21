/*
 * Copyright (c) 2020, Actions Semi Co., Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <section_overlay.h>
#include <linker/section_tags.h>
#include <audio_processor.h>
#include <audio_system.h>
#include <audio_policy.h>
#include <string.h>
#include <media_service.h>
#include <media_service.h>

/* dae block size */
#define DAE_BLOCK_SIZE	(128)
/* for dae library */

static int dae_global_bufs[DAE_BLOCK_SIZE * 2] __aligned(4);

extern void *media_mix_open(u8_t sample_rate, u8_t channels, u8_t is_interweaved);
extern void media_mix_close(void *handle);
extern int media_mix_mono_process(void *handle, void *src1, void *src2, void *mix_out, int samples, int volume);


typedef struct music_postprocessor_data {
	uint8_t stream_type;
	uint8_t channels;
	uint8_t	bit_width;
	uint16_t samples;
	uint32_t sample_rate;
	int volume;
	int soft_volume_coef;
	struct acts_ringbuf *inbufs[2];
	struct acts_ringbuf *outbuf;
	void *pcm[2];
	void *mix_handle;
} music_postprocessor_data_t;

extern int media_samplerate_khz_to_hz(u8_t samplerate_khz);
extern int media_db_to_coef15(int db);

static int _music_postprocessor_initialize(audio_processor_t *handle, void *init_param)
{
	audio_postprocessor_init_param_t *post_initparam = init_param;
	music_postprocessor_data_t *data = NULL;

	data = mem_malloc(sizeof(*data));
	if (!data)
		return -ENOMEM;

	memset(data, 0, sizeof(*data));

	data->inbufs[0] = post_initparam->inbufs[0];
	data->inbufs[1] = post_initparam->inbufs[1];
	data->outbuf = post_initparam->outbuf;
	data->samples = DAE_BLOCK_SIZE;
	data->channels = post_initparam->channels;
	data->sample_rate = media_samplerate_khz_to_hz(post_initparam->sample_rate);
	data->bit_width = post_initparam->sample_bits;
	data->stream_type = post_initparam->stream_type;

	data->volume = audio_policy_get_da_volume(post_initparam->stream_effect,
					 audio_system_get_stream_volume(post_initparam->stream_effect));

	data->soft_volume_coef = media_db_to_coef15(data->volume);

	if ((acts_ringbuf_size(data->inbufs[0]) & (DAE_BLOCK_SIZE * 2 -1)) ||
		(acts_ringbuf_size(data->outbuf) & (DAE_BLOCK_SIZE * 2 -1)))
		goto err_out;

	if (data->inbufs[1] &&
		(acts_ringbuf_size(data->inbufs[1]) & (DAE_BLOCK_SIZE * 2 -1)))
		goto err_out;

	data->mix_handle = media_mix_open(data->sample_rate, data->channels, 0);

	handle->priv = data;
	printk("_music_postprocessor_initialize  \n");
	printk("sample_rate %d \n",data->sample_rate);
	printk("channels %d \n",data->channels);
	printk("channel l  %p , channel r %p \n",data->inbufs[0],data->inbufs[1]);
	printk("volume %d  \n",data->volume);
	return 0;

err_out:
	mem_free(data);
	return -ENOMEM;
}

static int _music_postprocessor_destroy(audio_processor_t *handle)
{
	music_postprocessor_data_t *data = handle->priv;
	if (data->mix_handle) {
		media_mix_close(data->mix_handle);
		data->mix_handle = NULL;
	}
	return 0;
}

static int _music_postprocessor_process(audio_processor_t *handle)
{
	music_postprocessor_data_t *data = handle->priv;
	const uint16_t frame_len = data->samples * 2;
	uint16_t *outbuf_ptr;
	int *effect_input = dae_global_bufs;
	uint32_t buflen = 0;
	int i;

	outbuf_ptr = acts_ringbuf_tail_ptr(data->outbuf, &buflen);

	if (buflen < data->samples * data->channels * 2)
		return 0;

	for (i = 0; i < data->channels; i++) {
		data->pcm[i] = acts_ringbuf_head_ptr(data->inbufs[i], &buflen);
		if (buflen < frame_len)
			return 0;
	}

	/**mono data */
	if (data->channels == 1) {
		for (i = 0; i < data->samples; i++) {
			effect_input[i] = (*(short *)data->pcm[0] << 16);
			data->pcm[0] = (uint16_t *)data->pcm[0] + 1;
		}
		acts_ringbuf_drop(data->inbufs[0], frame_len);
	} else {
		/**mix and adjust volume*/
		//media_mix_mono_process(data->mix_handle, data->pcm[0], data->pcm[1], effect_input, data->samples, data->volume);
		for (i = 0; i < data->samples; i++) {
			effect_input[i * 2] = (*(short *)data->pcm[0] << 16);
			effect_input[i * 2 + 1] = (*(short *)data->pcm[1] << 16);
			data->pcm[0] = (uint16_t *)data->pcm[0] + 1;
			data->pcm[1] = (uint16_t *)data->pcm[1] + 1;
		}

		acts_ringbuf_drop(data->inbufs[1], frame_len);
		acts_ringbuf_drop(data->inbufs[0], frame_len);
	}

	//TODO: add post process
	//user_effect_process(effect_input, effect_input, data->sample_rate, frame_len);

	if (data->channels == 2) {
		for (i = 0; i < data->samples; i++) {
			outbuf_ptr[i * 2] =  effect_input[i * 2] >> 16;
			outbuf_ptr[i * 2 + 1] =  effect_input[i * 2 + 1] >> 16;
		}
		acts_ringbuf_fill_none(data->outbuf, data->samples * 4);
	} else {
		for (i = 0; i < data->samples; i++) {
			outbuf_ptr[i] =  effect_input[i] >> 16;
		}
		acts_ringbuf_fill_none(data->outbuf, data->samples * 2);
	}
	return data->samples;
}

static int _music_postprocessor_flush(audio_processor_t *handle)
{
	music_postprocessor_data_t *data = handle->priv;
	const u16_t frame_len = data->samples * 2;
	int buf_len = acts_ringbuf_length(data->inbufs[0]);
	int fill_len = ROUND_UP(buf_len, frame_len) - buf_len;
	int samples = 0;

	if (fill_len > 0) {
		acts_ringbuf_fill(data->inbufs[0], 0, fill_len);
		if (data->inbufs[1])
			acts_ringbuf_fill(data->inbufs[1], 0, fill_len);
	}

	while (_music_postprocessor_process(handle) > 0)
		samples += data->samples;

	return samples;
}

static int _music_postprocessor_set_parameter(audio_processor_t *handle,
			int pname, void *param, unsigned int psize)
{
	music_postprocessor_data_t *data = handle->priv;
	switch (pname) {
	case MEDIA_EFFECT_EXT_SET_VOLUME:
	{
		data->volume = audio_policy_get_da_volume(
				data->stream_type, (unsigned int)param & 0xff);
		data->soft_volume_coef = media_db_to_coef15(data->volume);
		SYS_LOG_INF("soft volume %d", data->volume);
		break;
	}
	default:
		break;
	}
	return 0;
}

static int _music_postprocessor_get_parameter(audio_processor_t *handle,
		int pname, void *param, unsigned int psize)
{

	return 0;
}

static int _music_postprocessor_dump(audio_processor_t *handle, int data_tag, struct acts_ringbuf *data_buf)
{
	return 0;
}

static int _music_postprocessor_dump_session(audio_processor_t *handle)
{

	return 0;
}

const audio_processor_module_t user_music_postprocessor = {
	.id = MUSIC_POSTPROCESSOR_MODULE_ID,
	.version = MAKE_AUDIO_PROCESSOR_VERSION(0, 0, 1),
	.name = "user music postprocessor",
	.initialize = _music_postprocessor_initialize,
	.destroy = _music_postprocessor_destroy,
	.process = _music_postprocessor_process,
	.flush = _music_postprocessor_flush,
	.set_parameter = _music_postprocessor_set_parameter,
	.get_parameter = _music_postprocessor_get_parameter,
	.dump = _music_postprocessor_dump,
	.dump_session = _music_postprocessor_dump_session,
};


void user_post_processor_register(void)
{
	audio_postprocessor_register((audio_processor_module_t *) &user_music_postprocessor, AUDIO_STREAM_DEFAULT);
	audio_postprocessor_register((audio_processor_module_t *) &user_music_postprocessor, AUDIO_STREAM_USOUND);
}


