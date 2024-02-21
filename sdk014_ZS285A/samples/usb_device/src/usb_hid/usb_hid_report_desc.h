/*
 * Copyright (c) 2020 Actions Corporation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef _HID_REPORT_DESC_H_
#define _HID_REPORT_DESC_H_
#include <usb/class/usb_hid.h>

/* usb hid mouse */
#define REPORT_ID_1			0x01
/* usb hid keyboard */
#define REPORT_ID_2			0x02
/* self define key */
#define HID_SELF_DEFINED_KEY_ID		0x03
/* self define data */
#define HID_SELF_DEFINED_DAT_ID		0x04
/* media volume ctrl */
#define MEDIA_CTRL_REPORT_ID		0x05
/* telephone status */
#define TELEPHONE_STATUS_REPORT_ID	0x06

/* definition of USB HID report descriptor */
static const u8_t hid_report_desc[] = {
	/*
	 * report id: 1
	 * USB Mouse, has input report only.
	 */
	/* 0x05, 0x01,	USAGE_PAGE (Generic Desktop) */
	HID_GI_USAGE_PAGE, USAGE_GEN_DESKTOP,
	/* 0x09, 0x02,	USAGE (Mouse) */
	HID_LI_USAGE, 0x02,
	/* 0xa1, 0x01,	COLLECTION (Application) */
	HID_MI_COLLECTION, COLLECTION_APPLICATION,
	/* 0x85, 0x03,	REPORT_ID (1) */
	HID_GI_REPORT_ID, REPORT_ID_1,
	/* 0x09, 0x01,	USAGE (Pointer) */
	HID_LI_USAGE, 0x01,
	/* 0xa1, 0x00,	COLLECTION (Physical) */
	HID_MI_COLLECTION, 0x00,
	/* 0x05, 0x09,	USAGE_PAGE (Button) */
	HID_GI_USAGE_PAGE, 0x09,
	/* 0x19, 0x01,	USAGE_MINIMUM (0x1) */
	0x19, 0x01,
	/* 0x29, 0x08,	USAGE_MAXIMUM (0x8) */
	0x29, 0x08,
	/* 0x15, 0x00,	LOGICAL_MINIMUM (0x0) */
	HID_GI_LOGICAL_MIN(1), 0x00,
	/* 0x25, 0x01,	LOGICAL_MAXIMUM (0x1) */
	HID_GI_LOGICAL_MAX(1), 0x01,
	/* 0x95, 0x08,	REPORT_COUNT (8) */
	HID_GI_REPORT_COUNT, 0x08,
	/* 0x75, 0x01,	REPORT_SIZE (1) */
	HID_GI_REPORT_SIZE, 0x01,
	/* 0x81, 0x02,	INPUT (Data, Var, Abs) */
	HID_MI_INPUT, 0x02,
	/* 0x05, 0x01,	USAGE_PAGE (Generic Desktop) */
	HID_GI_USAGE_PAGE, USAGE_GEN_DESKTOP,
	/* 0x09, 0x30,	USAGE (X) */
	HID_LI_USAGE, 0x30,
	/* 0x09, 0x31,	USAGE (Y) */
	HID_LI_USAGE, 0x31,
	/* 0x15, 0x81,	LOGICAL_MINIMUM (0x81) */
	HID_GI_LOGICAL_MIN(1), 0x81,
	/* 0x25, 0x7f,	LOGICAL_MAXIMUM (0x7f) */
	HID_GI_LOGICAL_MAX(1), 0x7f,
	/* 0x75, 0x08,	REPORT_SIZE (8) */
	HID_GI_REPORT_SIZE, 0x08,
	/* 0x95, 0x02,	REPORT_COUNT (2) */
	HID_GI_REPORT_COUNT, 0x02,
	/* 0x81, 0x06,	INPUT (Data, Var, Rel) */
	HID_MI_INPUT, 0x06,
	/* 0x09, 0x38,	USAGE (Wheel) */
	HID_LI_USAGE, 0x38,
	/* 0x15, 0x81,	LOGICAL_MINIMUM (0x81) */
	HID_GI_LOGICAL_MIN(1), 0x81,
	/* 0x25, 0x7f,	LOGICAL_MAXIMUM (0x7f) */
	HID_GI_LOGICAL_MAX(1), 0x7f,
	/* 0x75, 0x08,	REPORT_SIZE (8) */
	HID_GI_REPORT_SIZE, 0x08,
	/* 0x95, 0x01,	REPORT_COUNT (1) */
	HID_GI_REPORT_COUNT, 0x01,
	/* 0x81, 0x06, INPUT (Data, Var, Rel) */
	HID_MI_INPUT, 0x06,
	/* 0xc0 ,	END_COLLECTION */
	HID_MI_COLLECTION_END,
	/* 0xc0 ,	END_COLLECTION */
	HID_MI_COLLECTION_END,

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
	 * HID self-define key report id: 3
	 * HID Class: support self-defined key(8 bytes), has input and output reports.
	 */
	0x05, 0x01, /* USAGE_PAGE (Generic Desktop) */
	0x09, 0x00, /* USAGE (0) */
	0xa1, 0x01, /* COLLECTION (Application) */
	0x85, HID_SELF_DEFINED_KEY_ID, /* Report ID (3) */
	0x15, 0x00, /* LOGICAL_MINIMUM (0) */
	0x25, 0xff, /* LOGICAL_MAXIMUM (255) */
	0x19, 0x01, /* USAGE_MINIMUM (1) */
	0x29, 0x08, /* USAGE_MAXIMUM (8) */
	0x95, 0x08, /* REPORT_COUNT (8) */
	0x75, 0x08, /* REPORT_SIZE (8) */
	0x81, 0x02, /* INPUT (Data,Var,Abs) */
	0x19, 0x01, /* USAGE_MINIMUM (1) */
	0x29, 0x08, /* USAGE_MAXIMUM (8) */
	0x91, 0x02, /* OUTPUT (Data,Var,Abs) */
	0xc0,       /* END_COLLECTION */

	/*
	 * HID self-define data report ID: 4
	 * HID Class: support self-defined data(64 bytes), has input and output reports.
	 */
	0x05, 0x01, /* USAGE_PAGE (Generic Desktop) */
	0x09, 0x00, /* USAGE (0) */
	0xa1, 0x01, /* COLLECTION (Application) */
	0x85, HID_SELF_DEFINED_DAT_ID, /* Report ID (3) */
	0x15, 0x00, /* LOGICAL_MINIMUM (0) */
	0x25, 0xff, /* LOGICAL_MAXIMUM (255) */
	0x19, 0x01, /* USAGE_MINIMUM (1) */
	0x29, 0x08, /* USAGE_MAXIMUM (8) */
	0x95, 0x3f, /* REPORT_COUNT (63) */
	0x75, 0x08, /* REPORT_SIZE (8) */
	0x81, 0x02, /* INPUT (Data,Var,Abs) */
	0x19, 0x01, /* USAGE_MINIMUM (1) */
	0x29, 0x08, /* USAGE_MAXIMUM (8) */
	0x91, 0x02, /* OUTPUT (Data,Var,Abs) */
	0xc0,       /* END_COLLECTION */

	/*
	 * media volume ctrl report id: 5
	 * USB Audio volume control, has input report only.
	 */
	0x05, 0x0c, /* USAGE_PAGE (Consumer) */
	0x09, 0x01, /* USAGE (Consumer Control) */
	0xa1, 0x01, /* COLLECTION (Application) */
	0x85, MEDIA_CTRL_REPORT_ID, /* Report ID (5)*/
	0x15, 0x00, /* Logical Minimum (0x00) */
	0x25, 0x01, /* Logical Maximum (0x01) */
	0x09, 0xe9, /* USAGE (Volume Up) */
	0x09, 0xea, /* USAGE (Volume Down) */
	0x09, 0xe2, /* USAGE (Mute) */
	0x09, 0xcd, /* USAGE (Play/Pause) */
	0x09, 0xb5, /* USAGE (Scan Next Track) */
	0x09, 0xb6, /* USAGE (Scan Previous Track) */
	0x09, 0xb3, /* USAGE (Fast Forward) */
	0x09, 0xb7, /* USAGE (Stop) */
	0x75, 0x01, /* Report Size (0x01) */
	0x95, 0x08, /* Report Count (0x08) */
	0x81, 0x42, /* Input() */
	0xc0,	    /* END_COLLECTION */

	/*
	 * telephone status report id: 6
	 * telephone status report.
	 */
	0x05, 0x0b, /* Usage Page(Telephony ) */
	0x09, 0x01, /* Usage(Phone) */
	0xa1, 0x01, /* Collection(Application ) */
	0x85, TELEPHONE_STATUS_REPORT_ID,	/* Report ID(6) */
	0x15, 0x00, /* Logical Minimum (0x00) */
	0x25, 0x01, /* Logical Maximum (0x01) */
	0x09, 0x20, /* USAGE (Hook Switch) */
	0x09, 0x21, /* Usage(Flash) */
	0x09, 0x2F, /* Usage(Phone Mute) */
	0x75, 0x01, /* Report Size(0x1 ) */
	0x95, 0x03, /* Report Count(0x3 ) */
	0x81, 0x02, /* Input (Data,Variable,Absolute) */
	0x75, 0x05, /* Report Size(0x5) */
	0x95, 0x01, /* Report Count(0x1) */
	0x81, 0x03, /* Input (Cnst,Variable,Absolute) */
	0x05, 0x08, /* Usage Page(LEDs) */
	0x15, 0x00, /* Logical Minimum(0x0) */
	0x25, 0x01, /* Logical Maximum(0x1) */
	0x09, 0x08, /* USAGE (Do Not Disturb):0x01 */
	0x09, 0x17, /* USAGE (Off-Hook):0x02 */
	0x09, 0x18, /* USAGE (Ring):0x04 */
	0x09, 0x19, /* USAGE (Message Waiting):0x08 */
	0x09, 0x1a, /* USAGE (Data Mode):0x10 */
	0x09, 0x1e, /* USAGE (Speaker):0x20 */
	0x09, 0x1f, /* USAGE (Head Set):0x40 */
	0x09, 0x20, /* USAGE (Hold):0x80 */
	0x09, 0x21, /* USAGE (Microphone):0x0100 */
	0x09, 0x22, /* USAGE (Coverage):0x0200 */
	0x09, 0x23, /* USAGE (Night Mode):0x0400 */
	0x09, 0x24, /* USAGE (Send Calls):0x0800 */
	0x09, 0x25, /* USAGE (Call Pickup):0x1000 */
	0x09, 0x26, /* USAGE (Conference):0x2000 */
	0x09, 0x09, /* Usage(Mute):0x4000 */
	0x75, 0x01, /* Report Size (0x01) */
	0x95, 0x0f, /* Report Count (0x0f) */
	0x91, 0x02, /* Output()*/
	0x75, 0x01, /* Report Size (0x01) */
	0x95, 0x01, /* Report Count (0x01) */
	0x91, 0x03, /* Output (Const,Variable,Absolute) */
	0xc0,       /* END_COLLECTION */
};

/*
 * according to USB HID report descriptor.
 */
#define HID_SIZE_MOUSE		5

#define HID_SIZE_KEYBOARD	9
#define NUM_LOCK_CODE		0x53
#define CAPS_LOCK_CODE		0x39

#define	HID_SIZE_SELF_DEFINED_KEY 8
#define	HID_SIZE_SELF_DEFINED_DAT 64

#define HID_SIZE_MEDIA_CTRL	2
#define PLAYING_INC  	0x01
#define PLAYING_DEC  	0x02
#define PLAYING_MUTE	0x04
#define PLAYING_PAUSE   0x08
#define PLAYING_NEXT   	0x10
#define PLAYING_PREV    0x20

#endif /* _HID_REPORT_DESC_H_ */

