/*
 * Copyright (c) 2016 Actions Semi Co., Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief bt srv hfp ag api interface
 */

#include <os_common_api.h>
#include <mem_manager.h>
#include <btservice_api.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "btsrv_inner.h"

int btif_hfp_ag_register_processer(void)
{
	int ret = 0;

	ret |= btsrv_register_msg_processer(MSG_BTSRV_HFP_AG, &btsrv_hfp_ag_process);
	ret |= btsrv_register_msg_processer(MSG_BTSRV_SCO, &btsrv_sco_process);
	return ret;
}

int btif_hfp_ag_start(btsrv_hfp_ag_callback cb)
{
	return btsrv_function_call(MSG_BTSRV_HFP_AG, MSG_BTSRV_HFP_AG_START, cb);
}

int btif_hfp_ag_stop(void)
{
	return btsrv_function_call(MSG_BTSRV_HFP_AG, MSG_BTSRV_HFP_AG_STOP, NULL);
}

int btif_hfp_ag_update_indicator(enum btsrv_hfp_hf_ag_indicators index, u8_t value)
{
	return btsrv_event_notify_ext(MSG_BTSRV_HFP_AG, MSG_BTSRV_HFP_AG_UPDATE_INDICATOR, (void*)index,value);
}

int btif_hfp_ag_send_event(u8_t *event, u16_t len)
{
	return btsrv_event_notify_malloc(MSG_BTSRV_HFP_AG, MSG_BTSRV_HFP_AG_SEND_EVENT, event,len,0);
}

