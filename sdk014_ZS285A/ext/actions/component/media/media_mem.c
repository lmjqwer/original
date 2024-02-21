/*
 * Copyright (c) 2019 Actions Semiconductor Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file media mem interface
 */
#define SYS_LOG_DOMAIN "media"
#include <linker/section_tags.h>
#include <os_common_api.h>
#include <mem_manager.h>
#include <media_service.h>
#include <audio_system.h>
#include <bluetooth/l2cap.h>
#include "media_mem.h"

struct media_memory_cell {
	u8_t mem_type;
	u32_t mem_base;
	u32_t mem_size;
};

struct media_memory_block {
	u8_t stream_type;
	struct media_memory_cell mem_cell[27];
};


#ifdef CONFIG_MEDIA

#if	defined(CONFIG_AUDIO_INPUT_APP) || defined(CONFIG_USOUND_APP)
static char pcm_decoder_global_bss[0x300] _NODATA_SECTION(.pcm_decoder_global_bss) __aligned(4);
#endif

/*
 * sbc(msbc) dec: global - 0x0d10, share - 0x0000
 * sbc(msbc) enc: global - 0x0be4, share - 0x0000
 *
 * cvsd dec: global - 0x0308, share - 0x0000
 * cvsd enc: global - 0x0488, share - 0x0000
 */
static char sbc_dec_global_bss[0x0d20] _NODATA_SECTION(.SBC_DEC_BUF) __aligned(16);
static char sbc_enc_global_bss[0x0bec] _NODATA_SECTION(.SBC_ENC_BUF) __aligned(16);

#ifdef CONFIG_MUSIC_DAE_EXT
	static char dae_ext_buf[11264] _NODATA_SECTION(.DAE_EXT_BUF) __aligned(4);
#endif

#ifdef CONFIG_TWS

#define AVDTP_SBC_HEADER_LEN	13
#if defined(CONFIG_TWS_MONO_MODE) && !defined(CONFIG_DECODER_AAC)
static char tws_playload_buff[AVDTP_SBC_HEADER_LEN + 66 * 5] __aligned(4)  _NODATA_SECTION(.tws.playbackload_buf);
#else
static char tws_playload_buff[L2CAP_BR_MAX_MTU_A2DP] __aligned(4)  _NODATA_SECTION(.tws.playbackload_buf);
#endif
static char sbc_dec2_global_bss[0x0d20] _NODATA_SECTION(.btmusic_pcm_bss) __aligned(16);
/* btmusic and pcm codec based stream type, linein, usound, etc. */

#ifdef CONFIG_SF_APS
static char btmusic_pcm_bss[0x3800] _NODATA_SECTION(.btmusic_pcm_bss);
#else
static char btmusic_pcm_bss[0x3000] _NODATA_SECTION(.btmusic_pcm_bss);
#endif

#if defined CONFIG_TWS_MONO_MODE || defined(CONFIG_LOW_LATENCY_MODE)
// in mono mode, bit pool set 29, sbc one frame 66 bit
static char tws_cache_buff[66 * 5] __aligned(4)  _NODATA_SECTION(.tws.cache_buf);
#else
static char tws_cache_buff[119 * 6] __aligned(4)  _NODATA_SECTION(.tws.cache_buf);
#endif

#endif

/*
  * mp3 enc: inbuf - 0x1200, outbuf - 0x0600
  * opus enc: inbuf - 0x280, outbuf - 0x28
  * wav (lpcm) enc: inbuf - 0x200, outbuf - 0x200
  * wav (adpcm) enc: inbuf - 0x1FE4 (2041 sample pairs), outbuf - 0x800
  */
#ifdef CONFIG_RECORD_SERVICE
/* 2 recording sources * 1 channels, or 1 recording source * 2 channels, plus 1 pre/post process frame respectively */
static char wav_enc_adpcm_inbuf[0x1FE4 + 0x400] __in_section_unique(wav_enc.adpcm.inbuf);
static char wav_enc_adpcm_outbuf[0x800] __in_section_unique(wav_enc.adpcm.outbuf);
#endif

static char playback_input_buffer[13 * 1024] __in_section_unique(media.buff.noinit);

#ifndef CONFIG_DECODER_AAC
static char dae_music_global_bufs[128 * 4 * 2] _NODATA_SECTION(.music_dae_ovl_bss) __aligned(4);
/* temporary memory, only accessed in DAE_CMD_FRAME_PROCESS for 1 channel pcm data */
static char dae_music_share_buf[128 * 2] _NODATA_SECTION(.music_dae_ovl_bss) __aligned(2);
#endif

#ifdef CONFIG_TOOL_ASQT
/* asqt */
static char asqt_tool_stub_buf[1548] _NODATA_SECTION(tool.asqt.buf.noinit);
static char asqt_tool_data_buf[3072] _NODATA_SECTION(tool.asqt.buf.noinit);
#endif

#ifdef CONFIG_TOOL_ECTT
static char ectt_tool_buf[3072] _NODATA_SECTION(tool.ectt.buf.noinit);
#endif

#ifdef CONFIG_ACTIONS_DECODER
static char codec_stack[2048] __aligned(STACK_ALIGN) __in_section_unique(codec.noinit.stack);
#endif

#ifdef CONFIG_HFP_SPEECH
#ifdef CONFIG_AEC_TAIL_LENGTH
#define MSBC_AEC_TAIL_LENGTH (CONFIG_AEC_TAIL_LENGTH)
#else
#define MSBC_AEC_TAIL_LENGTH (32)
#endif
#define CVSD_AEC_TAIL_LENGTH (MSBC_AEC_TAIL_LENGTH * 2)
static char hfp_aec_global_bss[10536 * 2 + 1280 * 2 + MSBC_AEC_TAIL_LENGTH * 32 * 6] __hfp_aec_ovl_bss __aligned(4);
#ifdef CONFIG_OUTPUT_RESAMPLE
static char hfp_aec_share_bss[3082 * 2] __hfp_aec_ovl_bss __aligned(4);
#endif
#endif /* CONFIG_HFP_SPEECH */

#ifdef CONFIG_HFP_PLC
static char hfp_plc_global_bss[2030 * 2 + 120 * 2] __hfp_plc_ovl_bss __aligned(4);
#ifndef CONFIG_HFP_SPEECH
static char hfp_plc_share_bss[1950 * 2] __hfp_plc_ovl_bss __aligned(4);
#endif
#ifdef CONFIG_OUTPUT_RESAMPLE
static char resample_frame_plc_bss[560 * 2] __aligned(4)  _NODATA_SECTION(.resample.frame_buf_plc);
static char resample_share_plc_bss[1776] __aligned(4)  _NODATA_SECTION(.resample.frame_buf_plc);
static char resample_global_plc_bss[820 + 32] __aligned(4) _NODATA_SECTION(.resample.global_buf_plc);
#endif
#endif /* CONFIG_HFP_PLC */

#ifdef CONFIG_RESAMPLE
static char resample_keytone_global_bss[512] __aligned(4)  _NODATA_SECTION(.resample.global_buf);
static char resample_share_bss[1776] __aligned(4)  _NODATA_SECTION(.resample.share_buf);
//static char resample_frame_keytone_bss[280 * 4] __aligned(4)  _NODATA_SECTION(.resample.frame_buf);

#ifdef CONFIG_OUTPUT_RESAMPLE
static char resample_frame_bss[3184] __aligned(4)  _NODATA_SECTION(.resample.frame_buf);
static char resample_global_bss[820 + 32] __aligned(4) _NODATA_SECTION(.resample.global_buf);
static char resample_global_bss2[820 + 32] __aligned(4) _NODATA_SECTION(.resample.global_buf);

#endif
#endif

static const struct media_memory_block media_memory_config[] = {
#ifdef CONFIG_BT_MUSIC_APP
	{
		.stream_type = AUDIO_STREAM_MUSIC,
		.mem_cell = {
			{.mem_type = OUTPUT_DECODER,  .mem_base = (u32_t)&btmusic_pcm_bss[0x0000], .mem_size = 0x800,},
			{.mem_type = OUTPUT_PLAYBACK, .mem_base = (u32_t)&btmusic_pcm_bss[0x0800], .mem_size = 0x800,},
		#ifdef CONFIG_SF_APS
			{.mem_type = OUTPUT_PCM,      .mem_base = (u32_t)&btmusic_pcm_bss[0x1000], .mem_size = 0x1000,},
		#else
			{.mem_type = OUTPUT_PCM,      .mem_base = (u32_t)&btmusic_pcm_bss[0x1000], .mem_size = 0x800,},
		#endif
			{.mem_type = DECODER_GLOBAL_DATA, .mem_base = (u32_t)&sbc_dec_global_bss[0], .mem_size = sizeof(sbc_dec_global_bss),},

		#ifndef CONFIG_DECODER_AAC
			{.mem_type = DAE_MUSIC_SHARE_DATA, .mem_base = (u32_t)&dae_music_share_buf[0], .mem_size = 0x100,},
			{.mem_type = DAE_MUSIC_1_GLOBAL_DATA, .mem_base = (u32_t)&dae_music_global_bufs[0], .mem_size = 0x200,},
			{.mem_type = DAE_MUSIC_2_GLOBAL_DATA, .mem_base = (u32_t)&dae_music_global_bufs[0x200], .mem_size = 0x200,},
		#endif

		#ifdef CONFIG_TWS
		#ifdef CONFIG_SF_APS
			{.mem_type = TWS_LOCAL_INPUT, .mem_base = (u32_t)&btmusic_pcm_bss[0x2000], .mem_size = 0x1800,},
		#else
			{.mem_type = TWS_LOCAL_INPUT, .mem_base = (u32_t)&btmusic_pcm_bss[0x1800], .mem_size = 0x1800,},
		#endif
			{.mem_type = SBC_DECODER_GLOBAL_DATA, .mem_base = (u32_t)&sbc_dec2_global_bss[0], .mem_size = sizeof(sbc_dec2_global_bss),},
			{.mem_type = SBC_ENCODER_GLOBAL_DATA, .mem_base = (u32_t)&sbc_enc_global_bss[0], .mem_size = sizeof(sbc_enc_global_bss),},
			{.mem_type = INPUT_PLAYBACK,  .mem_base = (u32_t)&playback_input_buffer[0], .mem_size = sizeof(playback_input_buffer),},
		#endif

		#ifdef CONFIG_MUSIC_DAE_EXT
			{.mem_type = DAE_EXT_DATA,  .mem_base = (u32_t)&dae_ext_buf[0], .mem_size = sizeof(dae_ext_buf),},
		#endif

		#ifdef CONFIG_TOOL_ECTT
			{.mem_type = TOOL_ECTT_BUF, .mem_base = (u32_t)&ectt_tool_buf[0], .mem_size = sizeof(ectt_tool_buf),},
		#endif

		#ifdef CONFIG_ACTIONS_DECODER
			{.mem_type = CODEC_STACK, .mem_base = (u32_t)&codec_stack[0], .mem_size = sizeof(codec_stack),},
		#endif

		#ifdef CONFIG_RESAMPLE
		#ifdef CONFIG_OUTPUT_RESAMPLE
			{.mem_type = RESAMPLE_GLOBAL_DATA,  .mem_base = (u32_t)resample_global_bss, .mem_size = sizeof(resample_global_bss),},
			{.mem_type = RESAMPLE_FRAME_DATA, .mem_base = (u32_t)resample_frame_bss, .mem_size = sizeof(resample_frame_bss),},
		#endif
			{.mem_type = RESAMPLE_SHARE_DATA, .mem_base = (u32_t)resample_share_bss, .mem_size = sizeof(resample_share_bss),},
		#endif
		},
	},
#endif

#ifdef CONFIG_LCMUSIC_APP
	{
		.stream_type = AUDIO_STREAM_LOCAL_MUSIC,
		.mem_cell = {
			{.mem_type = TWS_LOCAL_INPUT, .mem_base = (u32_t)&playback_input_buffer[0], .mem_size = 0x1400,},
			{.mem_type = OUTPUT_PCM,      .mem_base = (u32_t)&playback_input_buffer[0x1400], .mem_size = 0x1800,},
			{.mem_type = OUTPUT_DECODER,  .mem_base = (u32_t)&playback_input_buffer[0x2c00], .mem_size = 0x0200,},
			{.mem_type = OUTPUT_PLAYBACK, .mem_base = (u32_t)&playback_input_buffer[0x2e00], .mem_size = 0x0600,},

		#ifndef CONFIG_DECODER_AAC
			{.mem_type = DAE_MUSIC_SHARE_DATA, .mem_base = (u32_t)&dae_music_share_buf[0], .mem_size = 0x100,},
			{.mem_type = DAE_MUSIC_1_GLOBAL_DATA, .mem_base = (u32_t)&dae_music_global_bufs[0], .mem_size = 0x200,},
			{.mem_type = DAE_MUSIC_2_GLOBAL_DATA, .mem_base = (u32_t)&dae_music_global_bufs[0x200], .mem_size = 0x200,},
		#endif

#if defined CONFIG_MUSIC_DAE_EXT && defined CONFIG_OUTPUT_RESAMPLE
			{.mem_type = DAE_EXT_DATA,	.mem_base = (u32_t)&dae_ext_buf[0], .mem_size = sizeof(dae_ext_buf),},
#endif
			{.mem_type = SBC_DECODER_GLOBAL_DATA, .mem_base = (u32_t)&sbc_dec_global_bss[0], .mem_size = sizeof(sbc_dec_global_bss),},
		#ifdef CONFIG_TWS
			{.mem_type = SBC_ENCODER_GLOBAL_DATA, .mem_base = (u32_t)&sbc_enc_global_bss[0], .mem_size = sizeof(sbc_enc_global_bss),},
		#endif

		#ifdef CONFIG_ACTIONS_DECODER
			{.mem_type = CODEC_STACK, .mem_base = (u32_t)&codec_stack[0], .mem_size = sizeof(codec_stack),},
		#endif

		#ifdef CONFIG_TOOL_ECTT
			{.mem_type = TOOL_ECTT_BUF, .mem_base = (u32_t)&ectt_tool_buf[0], .mem_size = sizeof(ectt_tool_buf),},
		#endif

		#ifdef CONFIG_RESAMPLE
		#ifdef CONFIG_OUTPUT_RESAMPLE
			{.mem_type = RESAMPLE_GLOBAL_DATA,  .mem_base = (u32_t)resample_global_bss, .mem_size = sizeof(resample_global_bss),},
			{.mem_type = RESAMPLE_FRAME_DATA, .mem_base = (u32_t)resample_frame_bss, .mem_size = sizeof(resample_frame_bss),},
		#endif
			{.mem_type = RESAMPLE_SHARE_DATA, .mem_base = (u32_t)resample_share_bss, .mem_size = sizeof(resample_share_bss),},
		#endif
		},
	},
#endif

#ifdef CONFIG_AUDIO_INPUT_APP
	{
		.stream_type = AUDIO_STREAM_LINEIN,
		.mem_cell = {
		#ifdef CONFIG_LOW_LATENCY_MODE
			{.mem_type = TWS_LOCAL_INPUT, .mem_base = (u32_t)&playback_input_buffer[0], .mem_size = 0x1000,},
			{.mem_type = OUTPUT_DECODER,  .mem_base = (u32_t)&btmusic_pcm_bss[0x0000], .mem_size = 0x400,},
			{.mem_type = OUTPUT_PLAYBACK, .mem_base = (u32_t)&btmusic_pcm_bss[0x0400], .mem_size = 0x400,},
			{.mem_type = OUTPUT_PCM,      .mem_base = (u32_t)&btmusic_pcm_bss[0x0800], .mem_size = 0x400,},

			{.mem_type = INPUT_PCM,       .mem_base = (u32_t)&btmusic_pcm_bss[0x1000], .mem_size = 0x400,},
			{.mem_type = INPUT_ENCBUF,    .mem_base = (u32_t)&btmusic_pcm_bss[0x1800], .mem_size = 0x200,},
			{.mem_type = OUTPUT_CAPTURE,  .mem_base = (u32_t)&btmusic_pcm_bss[0x1A00], .mem_size = 0x200,},
			{.mem_type = INPUT_PLAYBACK,  .mem_base = (u32_t)&btmusic_pcm_bss[0x1C00], .mem_size = 0x400,},
		#else
			{.mem_type = TWS_LOCAL_INPUT, .mem_base = (u32_t)&playback_input_buffer[0], .mem_size = 0x800,},
			{.mem_type = OUTPUT_DECODER,  .mem_base = (u32_t)&btmusic_pcm_bss[0x0000], .mem_size = 0x800,},
			{.mem_type = OUTPUT_PLAYBACK, .mem_base = (u32_t)&btmusic_pcm_bss[0x0800], .mem_size = 0x800,},
			{.mem_type = OUTPUT_PCM,      .mem_base = (u32_t)&playback_input_buffer[0x1000], .mem_size = 0x1800,},

			{.mem_type = INPUT_PCM,       .mem_base = (u32_t)&btmusic_pcm_bss[0x1000], .mem_size = 0x800,},
			{.mem_type = INPUT_ENCBUF,    .mem_base = (u32_t)&btmusic_pcm_bss[0x1800], .mem_size = 0x400,},
			{.mem_type = OUTPUT_CAPTURE,  .mem_base = (u32_t)&btmusic_pcm_bss[0x1c00], .mem_size = 0x400,},
			{.mem_type = INPUT_PLAYBACK,  .mem_base = (u32_t)&btmusic_pcm_bss[0x2000], .mem_size = 0x800,},
		#endif

#ifdef CONFIG_MUSIC_DAE_EXT
			{.mem_type = DAE_EXT_DATA,	.mem_base = (u32_t)&dae_ext_buf[0], .mem_size = sizeof(dae_ext_buf),},
#endif

		#ifndef CONFIG_DECODER_AAC
			{.mem_type = DAE_MUSIC_SHARE_DATA, .mem_base = (u32_t)&dae_music_share_buf[0], .mem_size = 0x100,},
			{.mem_type = DAE_MUSIC_1_GLOBAL_DATA, .mem_base = (u32_t)&dae_music_global_bufs[0], .mem_size = 0x200,},
			{.mem_type = DAE_MUSIC_2_GLOBAL_DATA, .mem_base = (u32_t)&dae_music_global_bufs[0x200], .mem_size = 0x200,},
		#endif

			{.mem_type = DECODER_GLOBAL_DATA, .mem_base = (u32_t)&pcm_decoder_global_bss[0], .mem_size = sizeof(pcm_decoder_global_bss),},

			{.mem_type = SBC_DECODER_GLOBAL_DATA, .mem_base = (u32_t)&sbc_dec_global_bss[0], .mem_size = sizeof(sbc_dec_global_bss),},
		#ifdef CONFIG_TWS
			{.mem_type = SBC_ENCODER_GLOBAL_DATA, .mem_base = (u32_t)&sbc_enc_global_bss[0], .mem_size = sizeof(sbc_enc_global_bss),},
		#endif

		#ifdef CONFIG_ACTIONS_DECODER
			{.mem_type = CODEC_STACK, .mem_base = (u32_t)&codec_stack[0], .mem_size = sizeof(codec_stack),},
		#endif

		#ifdef CONFIG_RESAMPLE
		#ifdef CONFIG_OUTPUT_RESAMPLE
			{.mem_type = RESAMPLE_GLOBAL_DATA,  .mem_base = (u32_t)resample_global_bss, .mem_size = sizeof(resample_global_bss),},
			{.mem_type = RESAMPLE_FRAME_DATA, .mem_base = (u32_t)resample_frame_bss, .mem_size = sizeof(resample_frame_bss),},
			{.mem_type = RESAMPLE_GLOBAL_DATA2,  .mem_base = (u32_t)resample_global_bss2, .mem_size = sizeof(resample_global_bss2),},
			{.mem_type = RESAMPLE_FRAME_DATA2,  .mem_base = (u32_t)&playback_input_buffer[0x2800], .mem_size = 0x800,},
		#endif
			{.mem_type = RESAMPLE_SHARE_DATA, .mem_base = (u32_t)resample_share_bss, .mem_size = sizeof(resample_share_bss),},
		#endif
		},
	},
#endif
#ifdef CONFIG_USOUND_APP
	{
		.stream_type = AUDIO_STREAM_USOUND,
		.mem_cell = {
			{.mem_type = TWS_LOCAL_INPUT, .mem_base = (u32_t)&playback_input_buffer[0], .mem_size = 0x1000,},
			{.mem_type = OUTPUT_DECODER,  .mem_base = (u32_t)&btmusic_pcm_bss[0x0000], .mem_size = 0x400,},
			{.mem_type = OUTPUT_PLAYBACK, .mem_base = (u32_t)&btmusic_pcm_bss[0x0400], .mem_size = 0x400,},
			{.mem_type = OUTPUT_PCM,      .mem_base = (u32_t)&playback_input_buffer[0x1000], .mem_size = 0x2000,},

			{.mem_type = INPUT_PCM,       .mem_base = (u32_t)&btmusic_pcm_bss[0x800], .mem_size = 0x800,},
			{.mem_type = INPUT_ENCBUF,    .mem_base = (u32_t)&btmusic_pcm_bss[0x1000], .mem_size = 0x200,},
			{.mem_type = OUTPUT_CAPTURE,  .mem_base = (u32_t)&btmusic_pcm_bss[0x1200], .mem_size = 0x200,},
			{.mem_type = INPUT_PLAYBACK,  .mem_base = (u32_t)&btmusic_pcm_bss[0x1400], .mem_size = 0x1000,},

			{.mem_type = USB_UPLOAD_CACHE, .mem_base = (u32_t)&btmusic_pcm_bss[0x27A0], .mem_size = 0x800,},
			{.mem_type = USB_UPLOAD_PAYLOAD, .mem_base = (u32_t)&btmusic_pcm_bss[0x2FA0], .mem_size = 0x60,},

			{.mem_type = DECODER_GLOBAL_DATA, .mem_base = (u32_t)&pcm_decoder_global_bss[0], .mem_size = sizeof(pcm_decoder_global_bss),},

#ifdef CONFIG_MUSIC_DAE_EXT
			{.mem_type = DAE_EXT_DATA,	.mem_base = (u32_t)&dae_ext_buf[0], .mem_size = sizeof(dae_ext_buf),},
#endif

		#ifndef CONFIG_DECODER_AAC
			{.mem_type = DAE_MUSIC_SHARE_DATA, .mem_base = (u32_t)&dae_music_share_buf[0], .mem_size = 0x100,},
			{.mem_type = DAE_MUSIC_1_GLOBAL_DATA, .mem_base = (u32_t)&dae_music_global_bufs[0], .mem_size = 0x200,},
			{.mem_type = DAE_MUSIC_2_GLOBAL_DATA, .mem_base = (u32_t)&dae_music_global_bufs[0x200], .mem_size = 0x200,},
		#endif

			{.mem_type = SBC_DECODER_GLOBAL_DATA, .mem_base = (u32_t)&sbc_dec_global_bss[0], .mem_size = sizeof(sbc_dec_global_bss),},
		#ifdef CONFIG_TWS
			{.mem_type = SBC_ENCODER_GLOBAL_DATA, .mem_base = (u32_t)&sbc_enc_global_bss[0], .mem_size = sizeof(sbc_enc_global_bss),},
		#endif

		#ifdef CONFIG_ACTIONS_DECODER
			{.mem_type = CODEC_STACK, .mem_base = (u32_t)&codec_stack[0], .mem_size = sizeof(codec_stack),},
		#endif

		#ifdef CONFIG_RESAMPLE
		#ifdef CONFIG_OUTPUT_RESAMPLE
			{.mem_type = RESAMPLE_GLOBAL_DATA,  .mem_base = (u32_t)resample_global_bss, .mem_size = sizeof(resample_global_bss),},
			{.mem_type = RESAMPLE_FRAME_DATA, .mem_base = (u32_t)resample_frame_bss, .mem_size = sizeof(resample_frame_bss),},
		#endif
			{.mem_type = RESAMPLE_SHARE_DATA, .mem_base = (u32_t)resample_share_bss, .mem_size = sizeof(resample_share_bss),},
		#endif
		},
	},
#endif
#ifdef CONFIG_BT_CALL_APP
	{
		.stream_type = AUDIO_STREAM_VOICE,
		.mem_cell = {
		#ifdef CONFIG_OUTPUT_RESAMPLE
			{.mem_type = INPUT_PLAYBACK,  .mem_base = (u32_t)&playback_input_buffer[0x000], .mem_size = 0x440,},
			{.mem_type = OUTPUT_DECODER,  .mem_base = (u32_t)&playback_input_buffer[0x440], .mem_size = 0x200,},
			{.mem_type = OUTPUT_PLAYBACK, .mem_base = (u32_t)&playback_input_buffer[0x640], .mem_size = 0x400,},
			{.mem_type = INPUT_PCM,      .mem_base = (u32_t)&playback_input_buffer[0xA40], .mem_size = 0x200,},
			{.mem_type = INPUT_CAPTURE,  .mem_base = (u32_t)&playback_input_buffer[0xC40], .mem_size = 0x200,},
			{.mem_type = INPUT_ENCBUF,   .mem_base = (u32_t)&playback_input_buffer[0xE40], .mem_size = 0x400,},
			{.mem_type = OUTPUT_CAPTURE, .mem_base = (u32_t)&playback_input_buffer[0x1240], .mem_size = 0x078,},
			{.mem_type = AEC_REFBUF0,    .mem_base = (u32_t)&playback_input_buffer[0x1300], .mem_size = 0xC00,},
			{.mem_type = OUTPUT_SCO, .mem_base = (u32_t)&playback_input_buffer[0x1F00], .mem_size = 0xE8,},
			{.mem_type = TX_SCO,     .mem_base = (u32_t)&playback_input_buffer[0x2000], .mem_size = 0x7C,},
			{.mem_type = RX_SCO,     .mem_base = (u32_t)&playback_input_buffer[0x2100], .mem_size = 0xF0,},
			{.mem_type = OUTPUT_PCM,      .mem_base = (u32_t)&playback_input_buffer[0x2200], .mem_size = 0xA00,},
			{.mem_type = AEC_SHARE_DATA,  .mem_base = (u32_t)&hfp_aec_share_bss[0], .mem_size = sizeof(hfp_aec_share_bss),},
			{.mem_type = PLC_SHARE_DATA,  .mem_base = (u32_t)&hfp_aec_share_bss[0], .mem_size = sizeof(hfp_aec_share_bss),},
		#else
			{.mem_type = INPUT_PLAYBACK,  .mem_base = (u32_t)&playback_input_buffer[0x000], .mem_size = 0x2A8,},
			{.mem_type = OUTPUT_DECODER,  .mem_base = (u32_t)&playback_input_buffer[0x2A8], .mem_size = 0x200,},
			{.mem_type = OUTPUT_PLAYBACK, .mem_base = (u32_t)&playback_input_buffer[0x4A8], .mem_size = 0x400,},
			{.mem_type = OUTPUT_PCM,      .mem_base = (u32_t)&playback_input_buffer[0x8A8], .mem_size = 0x200,},
			{.mem_type = INPUT_PCM,      .mem_base = (u32_t)&playback_input_buffer[0xAA8], .mem_size = 0x200,},
			{.mem_type = INPUT_CAPTURE,  .mem_base = (u32_t)&playback_input_buffer[0xCA8], .mem_size = 0x200,},
			{.mem_type = INPUT_ENCBUF,   .mem_base = (u32_t)&playback_input_buffer[0xEA8], .mem_size = 0x400,},
			{.mem_type = OUTPUT_CAPTURE, .mem_base = (u32_t)&playback_input_buffer[0x12A8], .mem_size = 0x078,},
			{.mem_type = AEC_REFBUF0,    .mem_base = (u32_t)&playback_input_buffer[0x1320], .mem_size = 0x600,},
			{.mem_type = OUTPUT_SCO, .mem_base = (u32_t)&playback_input_buffer[0x1920], .mem_size = 0xE8,},
			{.mem_type = TX_SCO,     .mem_base = (u32_t)&playback_input_buffer[0x1A08], .mem_size = 0x7C,},
			{.mem_type = RX_SCO,     .mem_base = (u32_t)&playback_input_buffer[0x1A84], .mem_size = 0xF0,},
			{.mem_type = PLC_SHARE_DATA,  .mem_base = (u32_t)&playback_input_buffer[0x1B80], .mem_size = 3082 * 2,},
			{.mem_type = AEC_SHARE_DATA,  .mem_base = (u32_t)&playback_input_buffer[0x1B80], .mem_size = 3082 * 2,},
		#endif

		#ifndef CONFIG_DECODER_AAC
			{.mem_type = DAE_MUSIC_SHARE_DATA, .mem_base = (u32_t)&dae_music_share_buf[0], .mem_size = 0x100,},
			{.mem_type = DAE_MUSIC_1_GLOBAL_DATA, .mem_base = (u32_t)&dae_music_global_bufs[0], .mem_size = 0x200,},
			{.mem_type = DAE_MUSIC_2_GLOBAL_DATA, .mem_base = (u32_t)&dae_music_global_bufs[0x200], .mem_size = 0x200,},
		#endif

			{.mem_type = DECODER_GLOBAL_DATA, .mem_base = (u32_t)&sbc_dec_global_bss[0], .mem_size = sizeof(sbc_dec_global_bss),},
			{.mem_type = ENCODER_GLOBAL_DATA, .mem_base = (u32_t)&sbc_enc_global_bss[0], .mem_size = sizeof(sbc_enc_global_bss),},
		#ifdef CONFIG_HFP_SPEECH
			{.mem_type = AEC_GLOBAL_DATA, .mem_base = (u32_t)&hfp_aec_global_bss[0], .mem_size = sizeof(hfp_aec_global_bss),},
		#endif
		#ifdef CONFIG_HFP_PLC
			{.mem_type = PLC_GLOBAL_DATA, .mem_base = (u32_t)&hfp_plc_global_bss[0], .mem_size = sizeof(hfp_plc_global_bss),},
		#ifndef CONFIG_HFP_SPEECH
			{.mem_type = PLC_SHARE_DATA,  .mem_base = (u32_t)&hfp_plc_share_bss[0], .mem_size = sizeof(hfp_plc_share_bss),},
		#else
		#endif
		#endif

		#ifdef CONFIG_TOOL_ASQT
			{.mem_type = TOOL_ASQT_STUB_BUF, .mem_base = (u32_t)&asqt_tool_stub_buf[0], .mem_size = sizeof(asqt_tool_stub_buf),},
			{.mem_type = TOOL_ASQT_DUMP_BUF, .mem_base = (u32_t)&asqt_tool_data_buf[0], .mem_size = sizeof(asqt_tool_data_buf),},
		#endif

		#ifdef CONFIG_ACTIONS_DECODER
			{.mem_type = CODEC_STACK, .mem_base = (u32_t)&codec_stack[0], .mem_size = sizeof(codec_stack),},
		#endif
		#if defined(CONFIG_HFP_PLC) && defined(CONFIG_RESAMPLE) && defined(CONFIG_OUTPUT_RESAMPLE)
			{.mem_type = RESAMPLE_GLOBAL_DATA,  .mem_base = (u32_t)resample_global_plc_bss, .mem_size = sizeof(resample_global_plc_bss),},
			{.mem_type = RESAMPLE_SHARE_DATA, .mem_base = (u32_t)resample_share_plc_bss, .mem_size = sizeof(resample_share_plc_bss),},
			{.mem_type = RESAMPLE_FRAME_DATA, .mem_base = (u32_t)resample_frame_plc_bss, .mem_size = sizeof(resample_frame_plc_bss),},
		#endif
		},
	},
#endif

	{
		.stream_type = AUDIO_STREAM_TTS,
		.mem_cell = {
			{.mem_type = TWS_LOCAL_INPUT, .mem_base = (u32_t)&playback_input_buffer[0], .mem_size = 0x1400,},
			{.mem_type = OUTPUT_PCM,      .mem_base = (u32_t)&playback_input_buffer[0x1400], .mem_size = 2048,},
			{.mem_type = OUTPUT_DECODER,  .mem_base = (u32_t)&playback_input_buffer[0x2c00], .mem_size = 0x0100,},
			{.mem_type = OUTPUT_PLAYBACK, .mem_base = (u32_t)&playback_input_buffer[0x2d00], .mem_size = 0x0600,},

#if defined CONFIG_MUSIC_DAE_EXT && defined CONFIG_OUTPUT_RESAMPLE
			{.mem_type = DAE_EXT_DATA,	.mem_base = (u32_t)&dae_ext_buf[0], .mem_size = sizeof(dae_ext_buf),},
#endif

		#ifndef CONFIG_DECODER_AAC
			{.mem_type = DAE_MUSIC_SHARE_DATA, .mem_base = (u32_t)&dae_music_share_buf[0], .mem_size = 0x100,},
			{.mem_type = DAE_MUSIC_1_GLOBAL_DATA, .mem_base = (u32_t)&dae_music_global_bufs[0], .mem_size = 0x200,},
			{.mem_type = DAE_MUSIC_2_GLOBAL_DATA, .mem_base = (u32_t)&dae_music_global_bufs[0x200], .mem_size = 0x200,},
		#endif

			{.mem_type = SBC_DECODER_GLOBAL_DATA, .mem_base = (u32_t)&sbc_dec_global_bss[0], .mem_size = sizeof(sbc_dec_global_bss),},
		#ifdef CONFIG_TWS
			{.mem_type = SBC_ENCODER_GLOBAL_DATA, .mem_base = (u32_t)&sbc_enc_global_bss[0], .mem_size = sizeof(sbc_enc_global_bss),},
		#endif

		#ifdef CONFIG_ACTIONS_DECODER
			{.mem_type = CODEC_STACK, .mem_base = (u32_t)&codec_stack[0], .mem_size = sizeof(codec_stack),},
		#endif

		#ifdef CONFIG_TOOL_ECTT
			{.mem_type = TOOL_ECTT_BUF, .mem_base = (u32_t)&ectt_tool_buf[0], .mem_size = sizeof(ectt_tool_buf),},
		#endif

		#ifdef CONFIG_RESAMPLE
			{.mem_type = RESAMPLE_GLOBAL_DATA,  .mem_base = (u32_t)resample_keytone_global_bss, .mem_size = sizeof(resample_keytone_global_bss),},
			{.mem_type = RESAMPLE_SHARE_DATA, .mem_base = (u32_t)resample_share_bss, .mem_size = sizeof(resample_share_bss),},
			//{.mem_type = RESAMPLE_FRAME_KEYTONE_DATA, .mem_base = (u32_t)resample_frame_keytone_bss, .mem_size = sizeof(resample_frame_keytone_bss),},
		#ifdef CONFIG_OUTPUT_RESAMPLE
			{.mem_type = RESAMPLE_FRAME_DATA, .mem_base = (u32_t)resample_frame_bss, .mem_size = sizeof(resample_frame_bss),},
		#endif
		#endif
		},
	},
#ifdef CONFIG_RECORD_APP
	{
		.stream_type = AUDIO_STREAM_LOCAL_RECORD,
		.mem_cell = {
			{.mem_type = INPUT_PCM,      .mem_base = (u32_t)&playback_input_buffer[0x0000], .mem_size = 0x800,},
			{.mem_type = INPUT_ENCBUF,   .mem_base = (u32_t)&playback_input_buffer[0x0800], .mem_size = 0x2000,},
			{.mem_type = OUTPUT_CAPTURE, .mem_base = (u32_t)&playback_input_buffer[0x2800], .mem_size = 0x800,},

		#ifdef CONFIG_ACTIONS_DECODER
			{.mem_type = CODEC_STACK, .mem_base = (u32_t)&codec_stack[0], .mem_size = sizeof(codec_stack),},
		#endif
		},
	},
#endif
#ifdef CONFIG_GMA_APP
	{
		.stream_type = AUDIO_STREAM_GMA_RECORD,
		.mem_cell = {
			{.mem_type = INPUT_PCM,      .mem_base = (u32_t)&playback_input_buffer[0x0000], .mem_size = 0x800,},
			{.mem_type = INPUT_ENCBUF,   .mem_base = (u32_t)&playback_input_buffer[0x0800], .mem_size = 0x1200,},
			{.mem_type = OUTPUT_CAPTURE, .mem_base = (u32_t)&playback_input_buffer[0x1A00], .mem_size = 0x600,},

		#ifndef CONFIG_DECODER_AAC
			{.mem_type = DAE_MUSIC_SHARE_DATA, .mem_base = (u32_t)&dae_music_share_buf[0], .mem_size = 0x100,},
			{.mem_type = DAE_MUSIC_1_GLOBAL_DATA, .mem_base = (u32_t)&dae_music_global_bufs[0], .mem_size = 0x200,},
			{.mem_type = DAE_MUSIC_2_GLOBAL_DATA, .mem_base = (u32_t)&dae_music_global_bufs[0x200], .mem_size = 0x200,},
		#endif

		#ifdef CONFIG_ACTIONS_DECODER
			{.mem_type = CODEC_STACK, .mem_base = (u32_t)&codec_stack[0], .mem_size = sizeof(codec_stack),},
		#endif
		},
	},
#endif
#ifdef CONFIG_RECORD_SERVICE
	{
		.stream_type = AUDIO_STREAM_BACKGROUND_RECORD,
		.mem_cell = {
			{.mem_type = INPUT_ENCBUF,   .mem_base = (u32_t)&wav_enc_adpcm_inbuf[0],  .mem_size = sizeof(wav_enc_adpcm_inbuf),},
			{.mem_type = OUTPUT_CAPTURE, .mem_base = (u32_t)&wav_enc_adpcm_outbuf[0], .mem_size = sizeof(wav_enc_adpcm_outbuf),},
		},
	},
#endif
#ifdef CONFIG_TWS
	{
		.stream_type = AUDIO_STREAM_TWS,
		.mem_cell = {
			{.mem_type = INPUT_PLAYBACK,  .mem_base = (u32_t)&playback_input_buffer[0], .mem_size = sizeof(playback_input_buffer),},
			{.mem_type = TWS_PLAYLOAD,  .mem_base = (u32_t)&tws_playload_buff[0], .mem_size = sizeof(tws_playload_buff),},
			{.mem_type = TWS_CACHE,  .mem_base = (u32_t)&tws_cache_buff[0], .mem_size = sizeof(tws_cache_buff),},
		},
	},
#endif
};

static const struct media_memory_block *_memdia_mem_find_memory_block(int stream_type)
{
	const struct media_memory_block *mem_block = NULL;

	if (stream_type == AUDIO_STREAM_FM
		|| stream_type == AUDIO_STREAM_I2SRX_IN
		|| stream_type == AUDIO_STREAM_SPDIF_IN
		|| stream_type == AUDIO_STREAM_MIC_IN) {
		stream_type = AUDIO_STREAM_LINEIN;
	}

	for (int i = 0; i < ARRAY_SIZE(media_memory_config) ; i++) {
		mem_block = &media_memory_config[i];
		if (mem_block->stream_type == stream_type) {
			return mem_block;
		}
	}

	return NULL;
}

static const struct media_memory_cell *_memdia_mem_find_memory_cell(const struct media_memory_block *mem_block, int mem_type)
{
	const struct media_memory_cell *mem_cell = NULL;

	for (int i = 0; i < ARRAY_SIZE(mem_block->mem_cell) ; i++) {
		mem_cell = &mem_block->mem_cell[i];
		if (mem_cell->mem_type == mem_type) {
			return mem_cell;
		}
	}

	return NULL;
}

void *media_mem_get_cache_pool(int mem_type, int stream_type)
{
	const struct media_memory_block *mem_block = NULL;
	const struct media_memory_cell *mem_cell = NULL;
	void *addr = NULL;

	mem_block = _memdia_mem_find_memory_block(stream_type);

	if (!mem_block) {
		goto exit;
	}

	mem_cell = _memdia_mem_find_memory_cell(mem_block, mem_type);

	if (!mem_cell) {
		goto exit;
	}

	return (void *)mem_cell->mem_base;

exit:
	return addr;
}

int media_mem_get_cache_pool_size(int mem_type, int stream_type)
{
	const struct media_memory_block *mem_block = NULL;
	const struct media_memory_cell *mem_cell = NULL;
	int mem_size = 0;

	mem_block = _memdia_mem_find_memory_block(stream_type);

	if (!mem_block) {
		goto exit;
	}

	mem_cell = _memdia_mem_find_memory_cell(mem_block, mem_type);

	if (!mem_cell) {
		goto exit;
	}

	return mem_cell->mem_size;

exit:
	return mem_size;
}
#else
void *media_mem_get_cache_pool(int mem_type, int stream_type)
{
	return NULL;
}
int media_mem_get_cache_pool_size(int mem_type, int stream_type)
{
	return 0;
}
#endif
