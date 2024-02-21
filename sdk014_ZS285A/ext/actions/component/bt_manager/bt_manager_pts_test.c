/*
 * Copyright (c) 2019 Actions Semi Co., Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief bt manager.
 */
#define SYS_LOG_NO_NEWLINE
#define SYS_LOG_DOMAIN "bt manager"

#include <logging/sys_log.h>

#include <zephyr.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <msg_manager.h>
#include <mem_manager.h>
#include <bt_manager.h>
#include <bt_manager_inner.h>
#include <sys_event.h>
#include <btservice_api.h>
#include <dvfs.h>
#include <shell/shell.h>
#include <bluetooth/host_interface.h>
#include <property_manager.h>

#define PTS_TEST_SHELL_MODULE		"pts"

static int pts_connect_acl(int argc, char *argv[])
{
	int cnt;
	struct autoconn_info info[3];

	memset(info, 0, sizeof(info));
	cnt = btif_br_get_auto_reconnect_info(info, 1);
	if (cnt == 0) {
		SYS_LOG_WRN("Never connect to pts dongle\n");
		return -1;
	}

	info[0].addr_valid = 1;
	info[0].tws_role = 0;
	info[0].a2dp = 0;
	info[0].hfp = 0;
	info[0].avrcp = 0;
	info[0].hfp_first = 0;
	btif_br_set_auto_reconnect_info(info, 3);

	btif_br_connect(&info[0].addr);
	return 0;
}


static int pts_connect_acl_a2dp_avrcp(int argc, char *argv[])
{
	int cnt;
	struct autoconn_info info[3];

	memset(info, 0, sizeof(info));
	cnt = btif_br_get_auto_reconnect_info(info, 1);
	if (cnt == 0) {
		SYS_LOG_WRN("Never connect to pts dongle\n");
		return -1;
	}

	info[0].addr_valid = 1;
	info[0].tws_role = 0;
	info[0].a2dp = 1;
	info[0].hfp = 0;
	info[0].avrcp = 1;
	info[0].hfp_first = 0;
	btif_br_set_auto_reconnect_info(info, 3);
	property_flush(NULL);

	bt_manager_startup_reconnect();
	return 0;
}

static int pts_connect_acl_hfp(int argc, char *argv[])
{
	int cnt;
	struct autoconn_info info[3];

	memset(info, 0, sizeof(info));
	cnt = btif_br_get_auto_reconnect_info(info, 1);
	if (cnt == 0) {
		SYS_LOG_WRN("Never connect to pts dongle\n");
		return -1;
	}

	info[0].addr_valid = 1;
	info[0].tws_role = 0;
	info[0].a2dp = 0;
	info[0].hfp = 1;
	info[0].avrcp = 0;
	info[0].hfp_first = 1;
	btif_br_set_auto_reconnect_info(info, 3);
	property_flush(NULL);

	bt_manager_startup_reconnect();
	return 0;
}

static int pts_hfp_cmd(int argc, char *argv[])
{
	char *cmd;

	if (argc < 2) {
		SYS_LOG_INF("Used: pts hfp_cmd xx");
		return -EINVAL;
	}

	cmd = argv[1];
	SYS_LOG_INF("hfp cmd:%s", cmd);

	/* AT cmd
	 * "ATA"			Answer call
	 * "AT+CHUP"		rejuet call
	 * "ATD1234567;"	Place a Call with the Phone Number Supplied by the HF.
	 * "ATD>1;"			Memory Dialing from the HF.
	 * "AT+BLDN"		Last Number Re-Dial from the HF.
	 * "AT+CHLD=0"	Releases all held calls or sets User Determined User Busy (UDUB) for a waiting call.
	 * "AT+CHLD=x"	refer HFP_v1.7.2.pdf.
	 * "AT+NREC=x"	Noise Reduction and Echo Canceling.
	 * "AT+BVRA=x"	Bluetooth Voice Recognition Activation.
	 * "AT+VTS=x"		Transmit DTMF Codes.
	 * "AT+VGS=x"		Volume Level Synchronization.
	 * "AT+VGM=x"		Volume Level Synchronization.
	 * "AT+CLCC"		List of Current Calls in AG.
	 * "AT+BTRH"		Query Response and Hold Status/Put an Incoming Call on Hold from HF.
	 * "AT+CNUM"		HF query the AG subscriber number.
	 * "AT+BIA="		Bluetooth Indicators Activation.
	 * "AT+COPS?"		Query currently selected Network operator.
	 */

	if (btif_pts_send_hfp_cmd(cmd)) {
		SYS_LOG_WRN("Not ready\n");
	}
	return 0;
}

static int pts_hfp_connect_sco(int argc, char *argv[])
{
	if (btif_pts_hfp_active_connect_sco()) {
		SYS_LOG_WRN("Not ready\n");
	}

	return 0;
}

static int pts_disconnect(int argc, char *argv[])
{
	btif_br_disconnect_device(BTSRV_DISCONNECT_ALL_MODE);
	return 0;
}

static int pts_a2dp_test(int argc, char *argv[])
{
	char *cmd;
	uint8_t value;

	if (argc < 2) {
		SYS_LOG_INF("Used: pts a2dp xx\n");
		return -EINVAL;
	}

	cmd = argv[1];
	SYS_LOG_INF("a2dp cmd:%s\n", cmd);

	if (!strcmp(cmd, "delayreport")) {
		bt_manager_a2dp_send_delay_report(1000);	/* Delay report 100ms */
	} else if (!strcmp(cmd, "cfgerrcode")) {
		if (argc < 3) {
			SYS_LOG_INF("Used: pts a2dp cfgerrcode 0xXX\n");
		}

		value = strtoul(argv[2], NULL, 16);
		btif_pts_a2dp_set_err_code(value);
		SYS_LOG_INF("Set a2dp err code 0x%x\n", value);
	}

	return 0;
}

static int pts_avrcp_test(int argc, char *argv[])
{
	char *cmd;
	u8_t value;

	if (argc < 2) {
		SYS_LOG_INF("Used: pts avrcp xx\n");
		return -EINVAL;
	}

	cmd = argv[1];
	SYS_LOG_INF("avrcp cmd:%s", cmd);

	if (!strcmp(cmd, "playstatus")) {
		btif_pts_avrcp_get_play_status();
	} else if (!strcmp(cmd, "pass")) {
		if (argc < 3) {
			SYS_LOG_INF("Used: pts avrcp pass 0xXX\n");
			return -EINVAL;
		}
		value = strtoul(argv[2], NULL, 16);
		btif_pts_avrcp_pass_through_cmd(value);
	} else if (!strcmp(cmd, "volume")) {
		if (argc < 3) {
			SYS_LOG_INF("Used: pts avrcp volume 0xXX\n");
			return -EINVAL;
		}
		value = strtoul(argv[2], NULL, 16);
		btif_pts_avrcp_notify_volume_change(value);
	} else if (!strcmp(cmd, "regnotify")) {
		btif_pts_avrcp_reg_notify_volume_change();
	}

	return 0;
}

static u8_t spp_chnnel;

static void pts_spp_connect_failed_cb(u8_t channel)
{
	SYS_LOG_INF("channel:%d\n", channel);
	spp_chnnel = 0;
}

static void pts_spp_connected_cb(u8_t channel, u8_t *uuid)
{
	SYS_LOG_INF("channel:%d\n", channel);
	spp_chnnel = channel;
}

static void pts_spp_receive_data_cb(u8_t channel, u8_t *data, u32_t len)
{
	SYS_LOG_INF("channel:%d data len %d\n", channel, len);
}

static void pts_spp_disconnected_cb(u8_t channel)
{
	SYS_LOG_INF("channel:%d\n", channel);
	spp_chnnel = 0;
}

static const struct btmgr_spp_cb pts_spp_cb = {
	.connect_failed = pts_spp_connect_failed_cb,
	.connected = pts_spp_connected_cb,
	.disconnected = pts_spp_disconnected_cb,
	.receive_data = pts_spp_receive_data_cb,
};

static const u8_t pts_spp_uuid[16] = {0xFB, 0x34, 0x9B, 0x5F, 0x80, \
	0x00, 0x00, 0x80, 0x00, 0x10, 0x00, 0x00, 0x01, 0x11, 0x00, 0x00};

static void bt_pts_spp_connect(void)
{
	int cnt;
	struct autoconn_info info[3];

	memset(info, 0, sizeof(info));
	cnt = btif_br_get_auto_reconnect_info(info, 1);
	if (cnt == 0) {
		SYS_LOG_WRN("Never connect to pts dongle\n");
		return;
	}

	spp_chnnel = bt_manager_spp_connect(&info[0].addr, (u8_t *)pts_spp_uuid, (struct btmgr_spp_cb *)&pts_spp_cb);
	SYS_LOG_INF("channel:%d\n", spp_chnnel);
}

static int pts_spp_test(int argc, char *argv[])
{
	char *cmd;

	if (argc < 2) {
		SYS_LOG_INF("Used: pts spp xx\n");
		return -EINVAL;
	}

	cmd = argv[1];
	SYS_LOG_INF("spp cmd:%s\n", cmd);

	if (!strcmp(cmd, "register")) {
		bt_manager_spp_reg_uuid((u8_t *)pts_spp_uuid, (struct btmgr_spp_cb *)&pts_spp_cb);
	} else if (!strcmp(cmd, "connect")) {
		bt_pts_spp_connect();
	} else if (!strcmp(cmd, "disconnect")) {
		if (spp_chnnel) {
			bt_manager_spp_disconnect(spp_chnnel);
		}
	}

	return 0;
}

static int pts_scan_test(int argc, char *argv[])
{
	char *cmd;

	if (argc < 2) {
		SYS_LOG_INF("Used: pts scan on/off\n");
		return -EINVAL;
	}

	cmd = argv[1];
	SYS_LOG_INF("scan cmd:%s\n", cmd);

	if (!strcmp(cmd, "on")) {
		btif_br_set_connnectable(true);
		btif_br_set_discoverable(true);
	} else if (!strcmp(cmd, "off")) {
		btif_br_set_connnectable(false);
		btif_br_set_discoverable(false);
	}

	return 0;
}

static int pts_clean_test(int argc, char *argv[])
{
	char *cmd;

	if (argc < 2) {
		SYS_LOG_INF("Used: pts clean xxx\n");
		return -EINVAL;
	}

	cmd = argv[1];
	SYS_LOG_INF("clean cmd:%s\n", cmd);

	if (!strcmp(cmd, "linkkey")) {
		btif_br_clean_linkkey();
	}

	return 0;
}

static int pts_auth_test(int argc, char *argv[])
{
	char *cmd;

	if (argc < 2) {
		SYS_LOG_INF("Used: pts auth xxx\n");
		return -EINVAL;
	}

	cmd = argv[1];
	SYS_LOG_INF("auth cmd:%s\n", cmd);

	if (!strcmp(cmd, "register")) {
		btif_pts_register_auth_cb(true);
	} else if (!strcmp(cmd, "unregister")) {
		btif_pts_register_auth_cb(false);
	}

	return 0;
}

static const struct shell_cmd pts_test_commands[] = {
	{ "connect_acl", pts_connect_acl, "pts active connect acl"},
	{ "connect_acl_a2dp_avrcp", pts_connect_acl_a2dp_avrcp, "pts active connect acl/a2dp/avrcp"},
	{ "connect_acl_hfp", pts_connect_acl_hfp, "pts active connect acl/hfp"},
	{ "hfp_cmd", pts_hfp_cmd, "pts hfp command"},
	{ "hfp_connect_sco", pts_hfp_connect_sco, "pts hfp active connect sco"},
	{ "disconnect", pts_disconnect, "pts do disconnect"},
	{ "a2dp", pts_a2dp_test, "pts a2dp test"},
	{ "avrcp", pts_avrcp_test, "pts avrcp test"},
	{ "spp", pts_spp_test, "pts spp test"},
	{ "scan", pts_scan_test, "pts scan test"},
	{ "clean", pts_clean_test, "pts scan test"},
	{ "auth", pts_auth_test, "pts auth test"},
	{ NULL, NULL, NULL}
};

SHELL_REGISTER(PTS_TEST_SHELL_MODULE, pts_test_commands);
