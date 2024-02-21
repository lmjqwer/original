/*
 * Copyright (c) 2018 Actions Corporation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __USB_HANDLER_H_
#define __USB_HANDLER_H_
#include <drivers/usb/usb_dc.h>
#include <usb/usbstruct.h>

typedef void (*System_call_status_flag)(u32_t status_flag);
extern void usb_audio_tx_flush(void);
extern int usb_audio_tx(const u8_t *buf, u16_t len);
extern bool usb_audio_tx_enabled(void);
extern void usb_audio_set_tx_start(bool start);
extern int usb_hid_tx(const u8_t *buf, u16_t len);
extern int usb_dev_composite_pre_init(struct device *dev);
extern int usb_dev_composite_del_init(void);

#endif /* __USB_HANDLER_H_ */
