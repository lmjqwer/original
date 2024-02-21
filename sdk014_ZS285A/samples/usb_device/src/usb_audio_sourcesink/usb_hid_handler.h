#ifndef __USB_HID_HANDLER_H_
#define __USB_HID_HANDLER_H_
#include <zephyr/types.h>

int usb_hid_control_pause_play(void);
int usb_hid_control_volume_dec(void);
int usb_hid_control_volume_inc(void);
int usb_hid_control_volume_mute(void);
int usb_hid_control_play_next(void);
int usb_hid_control_play_prev(void);
int usb_hid_control_play_fast(void);
int usb_hid_control_play_slow(void);
int usb_hid_control_mic_mute(void);
int usb_hid_telephone_drop(void);
int usb_hid_telephone_answer(void);
int usb_hid_device_init(void);

typedef void (*System_call_status_flag)(u32_t status_flag);

void usb_audio_register_call_status_cb(System_call_status_flag cb);

#endif /* __USB_HID_HANDLER_H_ */

