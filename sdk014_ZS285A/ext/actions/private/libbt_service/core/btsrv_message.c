/*
 * Copyright (c) 2016 Actions Semi Co., Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief bt service notify interface
 */
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <zephyr.h>
#include <arch/cpu.h>
#include <misc/byteorder.h>
#include <logging/sys_log.h>
#include <misc/util.h>
#include <mem_manager.h>

#include "btsrv_inner.h"

static void msg_free_memory(struct app_msg *msg, int result, void *param)
{
	if (msg->ptr) {
		mem_free(msg->ptr);
	}
}

int btsrv_function_call(int func_type, int cmd, void *param)
{
	struct app_msg msg = {0};

	msg.type = func_type;
	msg.cmd = cmd;
	msg.ptr = param;
	SYS_LOG_DBG("func_type %d, cmd %d\n", func_type, cmd);
	return !send_async_msg(BLUETOOTH_SERVICE_NAME, &msg);
}

int btsrv_event_notify(int event_type, int cmd, void *param)
{
	struct app_msg msg = {0};

	msg.type = event_type;
	msg.cmd = cmd;
	msg.ptr = param;
	SYS_LOG_DBG("event_type %d, cmd %d\n", event_type, cmd);
	return !send_async_msg(BLUETOOTH_SERVICE_NAME, &msg);
}

int btsrv_event_notify_ext(int event_type, int cmd, void *param, u8_t code)
{
	struct app_msg msg = {0};

	msg.type = event_type;
	msg.cmd = cmd;
	msg.reserve = code;
	msg.ptr = param;
	SYS_LOG_DBG("event_type %d, cmd %d\n", event_type, cmd);
	return !send_async_msg(BLUETOOTH_SERVICE_NAME, &msg);
}

int btsrv_event_notify_malloc(int event_type, int cmd, u8_t *data, u16_t len, u8_t code)
{
	int ret;
	u8_t *addr;
	struct app_msg msg = {0};

	/*malloc add 1 bytes reserve for string \0*/
	addr = mem_malloc(len + 1);
	if (addr == NULL) {
		return -ENOMEM;
	}
	memset(addr, 0, len + 1);
	if (data) {
		memcpy(addr, data, len);
	}

	msg.type = event_type;
	msg.cmd = cmd;
	msg.reserve = code;
	msg.ptr = addr;
	msg.callback = &msg_free_memory;
	SYS_LOG_DBG("event_type %d, cmd %d\n", event_type, cmd);

	if (send_async_msg(BLUETOOTH_SERVICE_NAME, &msg)) {
		ret = 0;
	} else {
		mem_free(addr);
		ret = -EIO;
	}

	return ret;
}
