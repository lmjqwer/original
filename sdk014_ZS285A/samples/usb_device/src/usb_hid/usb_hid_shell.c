/*
 * Copyright (c) 2018 Actions Corporation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr.h>
#include <string.h>
#include <stdlib.h>
#include <init.h>
#include <shell/shell.h>

#define SYS_LOG_LEVEL CONFIG_USB_HID_DEVICE_LOG_LEVEL
#define SYS_LOG_DOMAIN "usb_hid_shell"
#include <logging/sys_log.h>

#include "usb_hid_report_desc.h"
#include "usb_hid_handler.h"

/*
 * byte-0: X
 * byte-1: Y
 * byte-2: Button
 *
 * Examples:
 *     hid mouse 1 20 e1 (X: 1, Y: 20, Button: 1)
 *     hid mouse 1 20 e0 (X: 1, Y: 20, Button: 0)
 */
static int shell_hid_mouse(int argc, char *argv[])
{
	unsigned int value;
	u8_t data[3];

	value = strtoul(argv[3], NULL, 16);
	if (value > 0xff || value < 0xe0) {
		printk("value: 0x%x\n", value);
		return -EINVAL;
	}
	data[2] = value;

	value = strtoul(argv[1], NULL, 16);
	if (value > 0xff) {
		printk("value: 0x%x\n", value);
		return -EINVAL;
	}
	data[0] = value;

	value = strtoul(argv[2], NULL, 16);
	if (value > 0xff) {
		printk("value: 0x%x\n", value);
		return -EINVAL;
	}
	data[1] = value;

	return hid_tx_mouse(data, sizeof(data));
}

/*
 *
 * Examples:
 *     hid kbd num
 *     hid kbd cap
 */
static int shell_hid_keyboard(int argc, char *argv[])
{
	if (!strcmp(argv[1], "num")) {
		usb_hid_tx_keybord(NUM_LOCK_CODE);
	} else if (!strcmp(argv[1], "cap")) {
		usb_hid_tx_keybord(CAPS_LOCK_CODE);
	}

	return 0;
}

/*
 * Examples:
 *     hid vol up
 *     hid vol down
 *     hid vol mute
 *     hid vol next
 *     hid vol pre
 */
static int shell_hid_consumer(int argc, char *argv[])
{
	if (!strcmp(argv[1], "up")) {
		usb_hid_tx_consumer(PLAYING_INC);
	} else if (!strcmp(argv[1], "down")) {
		usb_hid_tx_consumer(PLAYING_DEC);
	} else if (!strcmp(argv[1], "mute")) {
		usb_hid_tx_consumer(PLAYING_MUTE);
	} else if (!strcmp(argv[1], "next")) {
		usb_hid_tx_consumer(PLAYING_NEXT);
	} else if (!strcmp(argv[1], "prev")) {
		usb_hid_tx_consumer(PLAYING_PREV);
	} else if (!strcmp(argv[1], "play")) {
		usb_hid_tx_consumer(PLAYING_PAUSE);
	}

	return 0;
}

#ifdef CONFIG_USB_SELF_DEFINED_REPORT
static int shell_hid_self_defined_key(int argc, char *argv[])
{
	/* self-defined key value */
	u8_t dat_buff[7]={0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0X07};
	if (!strcmp(argv[1], "tx")) {
		usb_hid_tx_self_defined_key(dat_buff, sizeof(dat_buff)/sizeof(u8_t));
	}
	return 0;
}

static int shell_hid_self_defined_dat(int argc, char *argv[])
{
	/* self-defined data */
	u8_t dat_buff[]={"ABCDEFGHIJKLMNOPQRSTUVWXYZ123456ABCDEFGHIJKLMNOPQRSTUVWXYZ12345"};
	if (!strcmp(argv[1], "tx")) {
		usb_hid_tx_self_defined_dat(dat_buff, strlen(dat_buff));
	}
	return 0;
}
#endif

static const struct shell_cmd commands[] = {
	{ "mouse", shell_hid_mouse, "NULL" },
	{ "kbd", shell_hid_keyboard, "NULL" },
	{ "vol", shell_hid_consumer, "NULL" },
#ifdef CONFIG_USB_SELF_DEFINED_REPORT
	{"sel-key", shell_hid_self_defined_key, "NULL"},
	{"sel-dat", shell_hid_self_defined_dat, "NULL"},
#endif
	{ NULL, NULL, NULL}
};

SHELL_REGISTER("hid", commands);
