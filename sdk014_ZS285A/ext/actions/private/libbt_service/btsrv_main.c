/*
 * Copyright (c) 2019 Actions Semi Co., Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief bt service interface
 */

#if defined(CONFIG_SYS_LOG)
#define SYS_LOG_NO_NEWLINE
#ifdef SYS_LOG_DOMAIN
#undef SYS_LOG_DOMAIN
#endif
#define SYS_LOG_DOMAIN "btsrv"
#include <logging/sys_log.h>
#endif

#include <os_common_api.h>
#include <btservice_api.h>
#include <mem_manager.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <thread_timer.h>
#include "btsrv_inner.h"

#define MAX_BTSRV_PROCESSER		(MSG_BTSRV_MAX - MSG_BTSRV_BASE)

static u8_t btstack_ready_flag;
static btsrv_msg_process msg_processer[MAX_BTSRV_PROCESSER];

#if (CONFIG_DEBUG_BT_STACK == 0)
static int _bt_service_init(struct app_msg *msg)
{
	btsrv_callback cb = _btsrv_get_msg_param_callback(msg);

	btstack_ready_flag = 0;
	if (!btsrv_adapter_init(cb)) {
		SYS_LOG_ERR("btstack_init failed\n");
		return -EAGAIN;
	}

	btsrv_adapter_run();

	if (cb) {
		/* wait for ready */
		while (!btstack_ready_flag)
			k_sleep(10);
	}

	return 0;
}

static int _bt_service_exit(void)
{
	btsrv_adapter_stop();
	srv_manager_thread_exit(BLUETOOTH_SERVICE_NAME);

	return 0;
}
#endif

void bt_service_set_bt_ready(void)
{
	btstack_ready_flag = 1;
}

int btsrv_register_msg_processer(u8_t msg_type, btsrv_msg_process processer)
{
	if ((msg_type < MSG_BTSRV_BASE) || (msg_type >= MSG_BTSRV_MAX) || !processer) {
		SYS_LOG_WRN("Unknow processer %p or msg_type %d\n", processer, msg_type);
		return -EINVAL;
	}

	msg_processer[msg_type - MSG_BTSRV_BASE] = processer;
	SYS_LOG_INF("Register %d processer\n", msg_type);
	return 0;
}

#if (CONFIG_DEBUG_BT_STACK == 0)
void bt_service_main_loop(void *parama1, void *parama2, void *parama3)
{
	struct app_msg msg = {0};
	bool terminaltion = false;
	int result = 0;

	while (!terminaltion) {
		if (receive_msg(&msg, thread_timer_next_timeout())) {
			switch (msg.type) {
			case MSG_EXIT_APP:
				_bt_service_exit();
				terminaltion = true;
				break;
			case MSG_INIT_APP:
				_bt_service_init(&msg);
				break;
			default:
				if (msg.type >= MSG_BTSRV_BASE && msg.type < MSG_BTSRV_MAX &&
					msg_processer[msg.type - MSG_BTSRV_BASE]) {
					msg_processer[msg.type - MSG_BTSRV_BASE](&msg);
				}
				break;
			}

			if (msg.callback) {
				msg.callback(&msg, result, NULL);
			}
		}
		thread_timer_handle_expired();
	}
}
#else
void bt_service_main_loop(void *parama1, void *parama2, void *parama3)
{
	struct app_msg msg = {0};
	bool terminaltion = false;

	while (!terminaltion) {
		if (receive_msg(&msg, OS_FOREVER)) {
			switch (msg.type) {
			case MSG_EXIT_APP:
				terminaltion = true;
				break;
			default:
				break;
			}
		}
	}
}
#endif
