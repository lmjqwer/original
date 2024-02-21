/*
 * Copyright (c) 2018 Actions Corporation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef _USB_HID_INNER_H_
#define _USB_HID_INNER_H_

#include <drivers/usb/usb_dc.h>
#include <usb/usbstruct.h>
#include <usb/class/usb_hid.h>

#define CONFIG_USB_SELF_DEFINED_REPORT 1

/* Mouse */
#define REPORT_ID_1		0x01
/* Keyboard */
#define REPORT_ID_2		0x02
/* Self-defined */
#define REPORT_ID_3		0x03
/* Consumer */
#define REPORT_ID_4		0x04
/* Telephony*/
#define REPORT_ID_5		0x05

/*
 * Report descriptor for keys, button and so on
 */
static const u8_t hid_report_desc[] = {
	/*
	 * report id: 2
	 * USB keyboard, has input and output report, include Caps Lock and Num Lock.
	 */
	0x05, 0x01, /* USAGE_PAGE (Generic Desktop) */
	0x09, 0x06, /* USAGE (Keyboard, bType: 0x06) */
	0xa1, 0x01, /* COLLECTION (Application) */
	0x85, REPORT_ID_2, /* Report ID (2) */
	0x05, 0x07, /* USAGE_PAGE (Keyboard/Keypad) */
	0x19, 0xe0, /* USAGE_MINIMUM (Keyboard LeftControl) */
	0x29, 0xe7, /* USAGE_MAXIMUM (Keyboard Right GUI) */
	0x15, 0x00, /* LOGICAL_MINIMUM (0) */
	0x25, 0x01, /* LOGICAL_MAXIMUM (1) */
	0x95, 0x08, /* REPORT_COUNT (8) */
	0x75, 0x01, /* REPORT_SIZE (1) */
	0x81, 0x02, /* INPUT (Data,Var,Abs) */
	0x95, 0x01, /* REPORT_COUNT (1) */
	0x75, 0x08, /* REPORT_SIZE (8) */
	0x81, 0x03, /* INPUT (Cnst,Var,Abs) */
	0x95, 0x06, /* REPORT_COUNT (6) */
	0x75, 0x08, /* REPORT_SIZE (8) */
	0x15, 0x00, /* LOGICAL_MINIMUM (0) */
	0x25, 0xFF, /* LOGICAL_MAXIMUM (255) */
	0x05, 0x07, /* USAGE_PAGE (Keyboard/Keypad) */
	0x19, 0x00, /* USAGE_MINIMUM (Reserved (no event indicated)) */
	0x29, 0x65, /* USAGE_MAXIMUM (Keyboard Application) */
	0x81, 0x00, /* INPUT (Data,Ary,Abs) */
	0x25, 0x01, /* LOGICAL_MAXIMUM (1) */
	0x95, 0x05, /* REPORT_COUNT (5) */
	0x75, 0x01, /* REPORT_SIZE (1) */
	0x05, 0x08, /* USAGE_PAGE (LEDs) */
	0x19, 0x01, /* USAGE_MINIMUM (Num Lock) */
	0x29, 0x05, /* USAGE_MAXIMUM (Kana) */
	0x91, 0x02, /* OUTPUT (Data,Var,Abs) */
	0x95, 0x01, /* REPORT_COUNT (1) */
	0x75, 0x03, /* REPORT_SIZE (3) */
	0x91, 0x03, /* OUTPUT (Cnst,Var,Abs) */
	0xc0,	     /* END_COLLECTION */

	/*
	 * report id: 4
	 * Consumer Usage Page.
	 */
	0x05, 0x0c, /* USAGE_PAGE (Consumer) */
	0x09, 0x01, /* USAGE (Consumer Control) */
	0xa1, 0x01, /* COLLECTION (Application) */
	0x85, REPORT_ID_4, /* Report ID (5)*/
	0x15, 0x00, /* Logical Minimum (0x00) */
	0x25, 0x01, /* Logical Maximum (0x01) */
	0x09, 0xe9, /* USAGE (Volume Up) */
	0x09, 0xea, /* USAGE (Volume Down) */
	0x09, 0xe2, /* USAGE (Mute) */
	0x09, 0xcd, /* USAGE (Play/Pause) */
	0x09, 0xb5, /* USAGE (Scan Next Track) */
	0x09, 0xb6, /* USAGE (Scan Previous Track) */
	0x09, 0xb3, /* USAGE (Fast Forward) */
	0x09, 0xb4, /* USAGE (Rewind) */
	0x75, 0x01, /* Report Size (0x01) */
	0x95, 0x08, /* Report Count (0x08) */
	0x81, 0x42, /* Input (Data,Var,Abs) */
	0xc0,	    /* END_COLLECTION */
};

/*
 * according to USB HID report descriptor.
 */
#define HID_SIZE_MOUSE		5

/* Keyboard */
#define HID_SIZE_KEYBOARD	9
#define NUM_LOCK_ID		0x53
#define CAPS_LOCK_ID		0x39

enum HID_KEYBOARD_INPUT_BIT_IDX
{
	HID_CTRL_LEFT = 1,
	HID_SHIFT_LEFT,
	HID_ALT_LEFT,
	HID_WIN_LEFT,
	HID_CTRL_RIGHT,
	HID_SHIFT_RIGHT,
	HID_ALT_RIGHT,
	HID_WIN_RIGHT,
};

enum HID_KEYBOARD_OUTPUT_BIT_IDX
{
	HID_NUM_LOCK = 0,
	HID_CAPS_LOCK,
	HID_SCROLL_LOCK,
	HID_COMPOSE,
	HID_KANA,
	HID_POWER,
	HID_SHIFT,
};

#define KBD_P_ID	0x13
#define KBD_RIGHT_ARROW	0x4F
#define KBD_LEFT_ARROW	0x50
#define KBD_DOWN_ARROW	0x51
#define KBD_UP_ARROW	0x52

/* Self_defined */
#define	HID_SIZE_SELF_DEFINED_DAT	64

/* Self_consumer */
#define HID_SIZE_CONSUMER	2
#define PLAYING_VOLUME_INC  	0x01
#define PLAYING_VOLUME_DEC  	0x02
#define PLAYING_VOLUME_MUTE  	0x04
#define PLAYING_AND_PAUSE   	0x08
#define PLAYING_NEXTSONG    	0x10
#define PLAYING_PREVSONG    	0x20

/* Telephony */
#define HID_SIZE_TELEPHONY	3
enum HID_TELEPHONY_INPUT_BIT_IDX
{
	HID_HOOK_SWITCH = 1,
	HID_LINE_BUSY_TONE,
	HID_SPEAKER_PHONE,
	HID_LINE,
	HID_PHONE_MUTE,
	HID_FLASH,
	HID_REDIAL,
	HID_DROP,
};

enum HID_TELEPHONY_OUTPUT_BIT_IDX
{
	HID_OFF_HOOK = 0,
	HID_SPEAKER,
	HID_MUTE,
	HID_RING,
	HID_HOLD,
	HID_MICROPHONE,
	HID_ON_LINE,
};

#define STATUS_HANG_UP 0x0005
#define STATUS_RING    0x0805
#define STATUS_ON_CALL 0x0105
#define STATUS_MIC_MUTE 0x0505

#endif /* __HID_HANDLER_H_ */

