/*
 * Copyright (c) 2020 Actions Semi Co., Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <arithmetic.h>
#include <os_common_api.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#if defined(CONFIG_SYS_LOG)
#ifdef SYS_LOG_DOMAIN
#undef SYS_LOG_DOMAIN
#endif
#define SYS_LOG_DOMAIN "aps"
#include <logging/sys_log.h>
#endif

static as_res_finetune_params global_aps_param;

void *audio_aps_open(int level)
{
	void *handle = NULL;
	int res;

	memset(&global_aps_param, 0, sizeof(as_res_finetune_params));

	global_aps_param.level = level;
	global_aps_param.high_quality = 1;
	global_aps_param.up_flag = 0;
	global_aps_param.flag = 0;

	res = as_res_finetune_ops(&handle, RES_FINETUNE_CMD_OPEN, (int)&global_aps_param);

	if (res || !handle) {
		SYS_LOG_ERR("APS_CMD_OPEN failed (res=%d)", res);
		return NULL;
	}
	SYS_LOG_INF("%p", handle);
	return handle;
}

void audio_aps_close(void *handle)
{
	if (handle) {
		as_res_finetune_ops(handle, RES_FINETUNE_CMD_CLOSE, 0);
		memset(&global_aps_param, 0, sizeof(as_res_finetune_params));
	}
	SYS_LOG_INF("%p", handle);
}

int audio_aps_set_level(void *handle, int level)
{
	if (!handle)
		return -EFAULT;

	if (level > 0) {
		global_aps_param.up_flag = 1;
	} else if(level < 0) {
		global_aps_param.up_flag = 0;
	} else {
		global_aps_param.flag = 0;
	}
	SYS_LOG_DBG("handle %p %d", handle, level);
	return 0;
}

int audio_aps_process(void *handle, void *output_buf, void *input_buf, int input_samples)
{
	if (!handle)
		return -EFAULT;

	global_aps_param.in_samp = input_samples;
	global_aps_param.input_buf = input_buf;
	global_aps_param.output_buf = output_buf;

	as_res_finetune_ops(handle, RES_FINETUNE_CMD_PROCESS, (int)&global_aps_param);

	return global_aps_param.out_samp;
}
