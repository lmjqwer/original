#include <string.h>
#include <stdio.h>
#include <shell/shell.h>
#include "usb_hid_handler.h"

static int shell_hid_consumer(int argc, char *argv[])
{
	if (!strcmp(argv[1], "up")) {
		usb_hid_control_volume_inc();
	} else if (!strcmp(argv[1], "down")) {
		usb_hid_control_volume_dec();
	} else if (!strcmp(argv[1], "mute")) {
		usb_hid_control_volume_mute();
	} else if (!strcmp(argv[1], "next")) {
		usb_hid_control_play_next();
	} else if (!strcmp(argv[1], "prev")) {
		usb_hid_control_play_prev();
	} else if (!strcmp(argv[1], "fast")) {
		usb_hid_control_play_fast();
	} else if (!strcmp(argv[1], "slow")) {
		usb_hid_control_play_slow();
	}

	return 0;
}

static const struct shell_cmd commands[] = {
	{ "ctl", shell_hid_consumer, "NULL" },
	{ NULL, NULL, NULL}
};

SHELL_REGISTER("hid", commands);

