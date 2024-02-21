/*
 * Copyright (c) 2016 Actions Semi Co., Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief bt spp interface
 */

#include <os_common_api.h>
#include <mem_manager.h>
#include <btservice_api.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "btsrv_inner.h"

int btif_spp_register_processer(void)
{
	return btsrv_register_msg_processer(MSG_BTSRV_SPP, &btsrv_spp_process);
}

int btif_spp_reg(struct bt_spp_reg_param *param)
{
	if (!param || !param->app_id || !param->uuid) {
		return -EINVAL;
	}

	return btsrv_function_call_malloc(MSG_BTSRV_SPP, MSG_BTSRV_SPP_REGISTER, (void *)param, sizeof(struct bt_spp_reg_param), 0);
}

int btif_spp_send_data(u8_t app_id, u8_t *data, u32_t len)
{
	return btsrv_spp_send_data(app_id, data, len);
}

int btif_spp_connect(struct bt_spp_connect_param *param)
{
	if (!param->app_id || !param->uuid) {
		return -EINVAL;
	}

	return btsrv_function_call_malloc(MSG_BTSRV_SPP, MSG_BTSRV_SPP_CONNECT, (void *)param, sizeof(struct bt_spp_connect_param), 0);
}

int btif_spp_disconnect(u8_t app_id)
{
	int id = app_id;

	return btsrv_function_call(MSG_BTSRV_SPP, MSG_BTSRV_SPP_DISCONNECT, (void *)id);
}

u8_t btif_spp_get_rfcomm_channel(u8_t app_id)
{
	return btsrv_spp_get_rfcomm_channel(app_id);
}

int btif_spp_start(btsrv_spp_callback cb)
{
	return btsrv_function_call(MSG_BTSRV_SPP, MSG_BTSRV_SPP_START, cb);
}

int btif_spp_stop(void)
{
	return btsrv_function_call(MSG_BTSRV_SPP, MSG_BTSRV_SPP_STOP, NULL);
}
