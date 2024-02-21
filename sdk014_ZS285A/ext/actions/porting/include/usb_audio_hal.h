/*
 * Copyright (c) 2018 Actions Corporation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __USB_SOUND_H__
#define __USB_SOUND_H__
#include <usb/class/usb_audio.h>
#include <stream.h>

typedef void (*usb_audio_event_callback)(u8_t event_type, u16_t event_param);

int usb_hid_control_pause_play(void);
int usb_hid_control_volume_dec(void);
int usb_hid_control_volume_inc(void);
int usb_hid_control_play_next(void);
int usb_hid_control_play_prev(void);
int usb_hid_control_play_fast(void);
int usb_hid_control_play_slow(void);
int usb_hid_control_mic_mute(void);
int usb_hid_telephone_drop(void);
int usb_hid_telephone_answer(void);
int usb_host_sync_volume_to_device(int host_volume);


int usb_audio_init(usb_audio_event_callback cb);
int usb_audio_deinit(void);
int usb_audio_set_stream(io_stream_t stream);

io_stream_t usb_audio_upload_stream_create(void);

#endif
