/*
 * Copyright (c) 2018 Actions Corporation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __USB_HID_HANDLER_H__
#define __USB_HID_HANDLER_H__
#include <usb/class/usb_audio.h>

typedef void (*System_call_status_flag)(u32_t status_flag);

int usb_hid_test_init(struct device *dev);
int hid_tx_mouse(const u8_t *buf, u16_t len);
int usb_hid_tx_keybord(const u8_t key_code);
int usb_hid_tx_consumer(const u8_t key_code);
int usb_hid_tx_self_defined_key(const u8_t *buf, u8_t len);
int usb_hid_tx_self_defined_dat(const u8_t *buf, u8_t len);
void usb_hid_call_status_cb(System_call_status_flag cb);

#endif /* __USB_HID_HANDLER_H__ */

