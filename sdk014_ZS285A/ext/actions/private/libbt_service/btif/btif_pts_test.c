/*
 * Copyright (c) 2016 Actions Semi Co., Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief bt srv api interface
 */

#include <os_common_api.h>
#include <mem_manager.h>
#include <btservice_api.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "btsrv_inner.h"

int btif_pts_send_hfp_cmd(char *cmd)
{
	return btsrv_pts_send_hfp_cmd(cmd);
}

int btif_pts_hfp_active_connect_sco(void)
{
	return btsrv_pts_hfp_active_connect_sco();
}

int btif_pts_a2dp_set_err_code(uint8_t err_code)
{
	bt_pts_a2dp_set_err_code(err_code);
	return 0;
}

int btif_pts_avrcp_get_play_status(void)
{
	if (btsrv_is_pts_test()) {
		btsrv_pts_avrcp_get_play_status();
	}
	return 0;
}

int btif_pts_avrcp_pass_through_cmd(u8_t opid)
{
	if (btsrv_is_pts_test()) {
		btsrv_pts_avrcp_pass_through_cmd(opid);
	}
	return 0;
}

int btif_pts_avrcp_notify_volume_change(u8_t volume)
{
	if (btsrv_is_pts_test()) {
		btsrv_pts_avrcp_notify_volume_change(volume);
	}
	return 0;
}

int btif_pts_avrcp_reg_notify_volume_change(void)
{
	if (btsrv_is_pts_test()) {
		btsrv_pts_avrcp_reg_notify_volume_change();
	}
	return 0;
}

int btif_pts_register_auth_cb(bool reg_auth)
{
	if (btsrv_is_pts_test()) {
		btsrv_pts_register_auth_cb(reg_auth);
	}

	return 0;
}
