/*
 * Copyright (c) 2019 Actions Semiconductor Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief usound event
 */

#include "usound.h"
#include "tts_manager.h"
#include <usb/class/usb_audio.h>

#ifdef CONFIG_ESD_MANAGER
#include "esd_manager.h"
#endif

static void _usound_tts_delay_resume(struct thread_timer *ttimer, void *expiry_fn_arg)
{
	struct usound_app_t *usound = usound_get_app();
	usound->need_resume_play = 0;
	usound_start_play();
}
void usound_tts_event_proc(struct app_msg *msg)
{
	struct usound_app_t *usound = usound_get_app();

	if (!usound)
		return;

	switch (msg->value) {
	case TTS_EVENT_START_PLAY:
		if (usound->player) {
			usound->need_resume_play = 1;
			usound_stop_play();
		}
		break;
	case TTS_EVENT_STOP_PLAY:
		if (usound->need_resume_play) {
			if (thread_timer_is_running(&usound->monitor_timer)) {
				thread_timer_stop(&usound->monitor_timer);
			}
			thread_timer_init(&usound->monitor_timer, _usound_tts_delay_resume, NULL);
			thread_timer_start(&usound->monitor_timer, 2000, 0);
		}
		break;
	default:
		break;
	}
}

void usound_input_event_proc(struct app_msg *msg)
{
	struct usound_app_t *usound = usound_get_app();

	if (!usound)
		return;

	switch (msg->cmd) {
	case MSG_USOUND_PLAY_PAUSE_RESUME:
		if (usound->playing) {
			usb_hid_control_pause_play();
			sys_event_notify(SYS_EVENT_PLAY_PAUSE);
		} else {
			sys_event_notify(SYS_EVENT_PLAY_START);
			usb_hid_control_pause_play();
		}
		break;
	case MSG_USOUND_PLAY_VOLUP:
		usound->volume_req_type = USOUND_VOLUME_INC;
		usound->volume_req_level = usound->current_volume_level + 1;
		if (usound->volume_req_level > 16) {
			usound->volume_req_level = 16;
		}
		usound_view_volume_show(usound->volume_req_level);
		usb_hid_control_volume_inc();
		usound->current_volume_level = usound->volume_req_level;
		break;
	case MSG_USOUND_PLAY_VOLDOWN:
		usound->volume_req_type = USOUND_VOLUME_DEC;
		usound->volume_req_level = usound->current_volume_level - 1;
		if (usound->volume_req_level < 0) {
			usound->volume_req_level = 0;
		}

		usound_view_volume_show(usound->volume_req_level);
		usb_hid_control_volume_dec();
		usound->current_volume_level = usound->volume_req_level;
		break;
	case MSG_USOUND_PLAY_NEXT:
		sys_event_notify(SYS_EVENT_PLAY_NEXT);
		usb_hid_control_play_next();
		break;
	case MSG_USOUND_PLAY_PREV:
		sys_event_notify(SYS_EVENT_PLAY_PREVIOUS);
		usb_hid_control_play_prev();
		break;
	default:
		break;
	}
}

void usound_event_proc(struct app_msg *msg)
{
	struct usound_app_t *usound = usound_get_app();

	if (!usound)
		return;
	SYS_LOG_INF("cmd %d\n", msg->cmd);
	switch (msg->cmd) {
	case MSG_USOUND_STREAM_START:
	#ifndef CONFIG_USOUND_MIC
		if (!usound->playing) {
			usound_start_play();
		} else {
			usb_audio_set_stream(usound->usound_stream);
		}
	#endif
		usound->playing = 1;
		usound_show_play_status(true);
		break;
	case MSG_USOUND_STREAM_STOP:
	#ifndef CONFIG_USOUND_MIC
		if (usound->playing) {
			usound_stop_play();
		} else {
			usb_audio_set_stream(NULL);
		}
	#endif
		usound->playing = 0;
		usound_show_play_status(false);
		break;
	case MSG_USOUND_MIC_OPEN:
	{

		SYS_IRQ_FLAGS flags;
		sys_irq_lock(&flags);
		if(usound->usound_upload_stream) {
			stream_flush(usound->usound_upload_stream);
		}

		sys_irq_unlock(&flags);

		break;
	}
	case MSG_USOUND_MIC_CLOSE:
		break;
	default:
		break;
	}

#ifdef CONFIG_ESD_MANAGER
	{
		u8_t playing = usound->playing;

		esd_manager_save_scene(TAG_PLAY_STATE, &playing, 1);
	}
#endif

}


void usound_bt_event_proc(struct app_msg *msg)
{

	if (msg->cmd == BT_REQ_RESTART_PLAY) {
		usound_stop_play();
		os_sleep(200);
		usound_start_play();
	}
	if (msg->cmd == BT_TWS_DISCONNECTION_EVENT) {
		struct usound_app_t *usound = usound_get_app();
		if (usound->player) {
			media_player_set_output_mode(usound->player, MEDIA_OUTPUT_MODE_DEFAULT);
		}
	}
#ifndef CONFIG_TWS_BACKGROUND_BT
	if (msg->cmd == BT_TWS_CONNECTION_EVENT) {
		bt_manager_halt_phone();
	} else if (msg->cmd == BT_TWS_DISCONNECTION_EVENT) {
		bt_manager_resume_phone();
	}
#endif
}
