/*
 * Copyright (c) 2018 Actions Corporation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <string.h>
#include <drivers/usb/usb_phy.h>
#include <usb/class/usb_hid.h>
#include "usb_hid_report_desc.h"
#include "usb_hid_descriptor.h"
#include "usb_hid_handler.h"

#define SYS_LOG_LEVEL CONFIG_USB_HID_DEVICE_LOG_LEVEL
#define SYS_LOG_DOMAIN "usb_hid_handler"
#include <logging/sys_log.h>

static System_call_status_flag call_status_cb;
static u32_t recv_report;
static u8_t usb_hid_out_buf[CONFIG_USB_HID_MAX_PAYLOAD_SIZE];

void usb_hid_call_status_cb(System_call_status_flag cb)
{
	call_status_cb = cb;
}

static int debug_cb(struct usb_setup_packet *setup, s32_t *len, u8_t **data)
{
	SYS_LOG_DBG("Debug callback");

	return -ENOTSUP;
}

/* system call status code */
static int usb_hid_set_report(struct usb_setup_packet *setup, s32_t *len,
				u8_t **data)
{
	u8_t *temp_data = *data;

	SYS_LOG_DBG("temp_data[0]: 0x%04x", temp_data[0]);
	SYS_LOG_DBG("temp_data[1]: 0x%04x", temp_data[1]);
	SYS_LOG_DBG("temp_data[2]: 0x%04x", temp_data[2]);

	recv_report = (temp_data[2] << 16) | (temp_data[1] << 8) | temp_data[0];

	SYS_LOG_DBG("recv_report:0x%04x", recv_report);

	if (call_status_cb) {
			call_status_cb(recv_report);
	}

	return 0;
}

static void hid_inter_in_ready(void)
{
	SYS_LOG_DBG("hid_inter_in_ready\n");

	return;
}

/*
 * API: read the output report.
 */
static void hid_inter_out_ready(void)
{
	u32_t bytes_to_read;
	int ret = 0;
	memset(usb_hid_out_buf, 0, sizeof(usb_hid_out_buf));

	/* get all bytes were received. */
	ret = hid_int_ep_read(usb_hid_out_buf, sizeof(usb_hid_out_buf),
			&bytes_to_read);
	if (!ret) {
		SYS_LOG_DBG("bytes_to_read:%d\n", bytes_to_read);
		switch (usb_hid_out_buf[0]) {
		case HID_SELF_DEFINED_KEY_ID:
			SYS_LOG_DBG("self-defind-key:\n");
			for (u8_t i = 0; i < bytes_to_read; i++) {
				SYS_LOG_DBG("usb_hid_out_buf[%d] = 0x%02x ", i, usb_hid_out_buf[i]);
			}
			break;

		case HID_SELF_DEFINED_DAT_ID:
			SYS_LOG_DBG("sel-defined-data:\n");
			for (u8_t i = 0; i < bytes_to_read; i++) {
				SYS_LOG_DBG("usb_hid_out_buf[%d] = 0x%02x ", i, usb_hid_out_buf[i]);
			}
			break;

		case REPORT_ID_2:
			SYS_LOG_DBG("kbd-out-report:\n");
			for (u8_t i = 0; i < bytes_to_read; i++) {
				SYS_LOG_DBG("usb_hid_out_buf[%d] = 0x%02x ", i, usb_hid_out_buf[i]);
			}
			break;

		}

		return;
	} else {
		SYS_LOG_ERR("read output report error");
		return;
	}
}

static const struct hid_ops ops = {
	.get_report = debug_cb,
	.get_idle = debug_cb,
	.get_protocol = debug_cb,
	.set_report = usb_hid_set_report,
	.set_idle = debug_cb,
	.set_protocol = debug_cb,
	.int_in_ready = hid_inter_in_ready,
	.int_out_ready = hid_inter_out_ready,
};

static int usb_hid_tx(const u8_t *buf, u16_t len)
{
	u32_t wrote, interval = 0;
	int ret, count = 0;
	u8_t speed_mode = usb_device_speed();

	/* wait one interval at most, unit: 125us */
	if (speed_mode == USB_SPEED_HIGH) {
		count = 1 << (CONFIG_HID_INTERRUPT_EP_INTERVAL_HS - 1);
	} else if (speed_mode == USB_SPEED_FULL || speed_mode == USB_SPEED_LOW) {
		interval = CONFIG_HID_INTERRUPT_EP_INTERVAL_FS;
		count =  interval * 20;
	}

	do {
		ret = hid_int_ep_write(buf, len, &wrote);
		if (ret == -EAGAIN) {
			k_busy_wait(125);
		}
	} while ((ret == -EAGAIN) && (--count > 0));

	if (ret) {
		SYS_LOG_ERR("ret: %d", ret);
	} else if (!ret && wrote != len) {
		SYS_LOG_ERR("wrote: %d, len: %d", wrote, len);
	}

	return ret;
}

/*
 * byte-0: Report ID
 * byte-1: Button
 * byte-2: X
 * byte-3: Y
 * byte-4: Wheel
 */
int hid_tx_mouse(const u8_t *buf, u16_t len)
{
	u8_t data[HID_SIZE_MOUSE];

	data[0] = REPORT_ID_1;
	data[1] = buf[2] & 0x1f;
	data[2] = buf[0];
	data[3] = buf[1];
	data[4] = 0;

	return usb_hid_tx(data, sizeof(data));
}

int usb_hid_tx_keybord(const u8_t key_code)
{
	int ret = 0;
	u8_t data[HID_SIZE_KEYBOARD];

	memset(data, 0, sizeof(data));
	data[0] = REPORT_ID_2;
	data[3] = key_code;

	ret = usb_hid_tx(data, sizeof(data));
	if (ret == 0) {
		memset(data, 0, sizeof(data));
		data[0] = REPORT_ID_2;
		ret = usb_hid_tx(data, sizeof(data));
	}

	return ret;
}

int usb_hid_tx_consumer(const u8_t key_code)
{
	int ret = 0;
	u8_t data[HID_SIZE_MEDIA_CTRL];

	memset(data, 0, sizeof(data));
	data[0] = MEDIA_CTRL_REPORT_ID;
	data[1] = key_code;
	ret = usb_hid_tx(data, sizeof(data));
	if (ret == 0) {
		memset(data, 0, sizeof(data));
		data[0] = MEDIA_CTRL_REPORT_ID;
		ret = usb_hid_tx(data, sizeof(data));
	}

	return ret;
}

#ifdef CONFIG_USB_SELF_DEFINED_REPORT
int usb_hid_tx_self_defined_key(const u8_t *buf, u8_t len)
{
	int ret = 0;
	u8_t data[HID_SIZE_SELF_DEFINED_KEY];
	if (len != HID_SIZE_SELF_DEFINED_KEY - 1) {
		SYS_LOG_ERR("illegal data length!");
		return -1;
	}
	memset(data, 0, HID_SIZE_SELF_DEFINED_KEY);
	data[0] = HID_SELF_DEFINED_KEY_ID;
	memcpy(&data[1], buf, len);
	ret = usb_hid_tx(data, sizeof(data));
	if (ret == 0) {
		memset(data, 0, sizeof(data));
		data[0] = HID_SELF_DEFINED_KEY_ID;
		ret = usb_hid_tx(data, sizeof(data));
	} else {
		SYS_LOG_ERR("hid tx self-defined key error");
	}
	return ret;
}

int usb_hid_tx_self_defined_dat(const u8_t *buf, u8_t len)
{
	int ret = 0;
	u8_t data[HID_SIZE_SELF_DEFINED_DAT];
	if (len != HID_SIZE_SELF_DEFINED_DAT - 1) {
		SYS_LOG_ERR("illegal data length!");
		return -1;
	}
	memset(data, 0, HID_SIZE_SELF_DEFINED_DAT);
	data[0] = HID_SELF_DEFINED_DAT_ID;
	memcpy(&data[1], buf, len);
	ret = usb_hid_tx(data, sizeof(data));
	if (ret) {
		SYS_LOG_ERR("hid tx self-defined key error");
	}

	return ret;
}
#endif

int usb_hid_test_init(struct device *dev)
{
	usb_phy_init();
	usb_phy_enter_b_idle();

	/* register hid report descriptor */
	usb_hid_register_device(hid_report_desc, sizeof(hid_report_desc), &ops);

	/* register device descriptors */
	usb_device_register_descriptors(usb_hid_fs_desc, usb_hid_hs_desc);

	/* USB HID initialize */
	usb_hid_init();

	return 0;
}

