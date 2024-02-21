/*
 * Copyright (c) 2016 Actions Semi Co., Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief bt avrcp interface
 */

#include <os_common_api.h>
#include <mem_manager.h>
#include <btservice_api.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "btsrv_inner.h"

int btif_avrcp_register_processer(void)
{
	return btsrv_register_msg_processer(MSG_BTSRV_AVRCP, &btsrv_avrcp_process);
}

int btif_avrcp_start(btsrv_avrcp_callback_t *cb)
{
	return btsrv_function_call(MSG_BTSRV_AVRCP, MSG_BTSRV_AVRCP_START, (void *)cb);
}

int btif_avrcp_stop(void)
{
	return btsrv_function_call(MSG_BTSRV_AVRCP, MSG_BTSRV_AVRCP_STOP, (void *)NULL);
}

int btif_avrcp_connect(bd_address_t *addr)
{
	return btsrv_function_call_malloc(MSG_BTSRV_AVRCP, MSG_BTSRV_AVRCP_CONNECT_TO, (void *)addr, sizeof(bd_address_t), 0);
}

int btif_avrcp_disconnect(bd_address_t *addr)
{
	return btsrv_function_call_malloc(MSG_BTSRV_AVRCP, MSG_BTSRV_AVRCP_DISCONNECT, (void *)addr, sizeof(bd_address_t), 0);
}

int btif_avrcp_send_command(int command)
{
	return btsrv_function_call(MSG_BTSRV_AVRCP, MSG_BTSRV_AVRCP_SEND_CMD, (void *)command);
}

int btif_avrcp_sync_vol(u32_t volume)
{
	return btsrv_function_call(MSG_BTSRV_AVRCP, MSG_BTSRV_AVRCP_SYNC_VOLUME, (void *)volume);
}

int btif_avrcp_get_id3_info()
{
	return btsrv_function_call(MSG_BTSRV_AVRCP, MSG_BTSRV_AVRCP_GET_ID3_INFO, (void *)NULL);
}

int btif_avrcp_set_absolute_volume(u8_t *data, u8_t len)
{
	u32_t param, i;

	if (len < 1 || len > 3) {
		return -EINVAL;
	}

	param = (len << 24);
	for (i = 0; i < len; i++) {
		param |= (data[i] << (8*i));
	}

	return btsrv_function_call(MSG_BTSRV_AVRCP, MSG_BTSRV_AVRCP_SET_ABSOLUTE_VOLUME, (void *)param);
}
