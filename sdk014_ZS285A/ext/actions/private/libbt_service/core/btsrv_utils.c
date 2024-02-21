/*
 * Copyright (c) 2016 Actions Semi Co., Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief bt service core interface
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

#include <device.h>
#include <init.h>
#include <uart.h>
#include <property_manager.h>

#include <bluetooth/host_interface.h>
#include "btsrv_inner.h"

bd_address_t *GET_CONN_BT_ADDR(struct bt_conn *conn)
{
	struct bt_conn_info info;

	if (conn != NULL) {
		if (hostif_bt_conn_get_info(conn, &info) >= 0) {
			return (bd_address_t *)info.br.dst;
		}
	}

	return NULL;
}

u32_t bt_rand32_get(void)
{
	u32_t rand32, i;

	rand32 = os_uptime_get_32();
	if (btsrv_info) {
		for (i = 0; i < 6; i++)
			rand32 ^= btsrv_info->device_addr[i];
	}

	return rand32;
}

int btsrv_set_negative_prio(void)
{
	int prio;

	prio = os_thread_priority_get(os_current_get());
	if (prio >= 0) {
		os_thread_priority_set(os_current_get(), -1);
	}

	return prio;
}

void btsrv_revert_prio(int prio)
{
	if (prio >= 0) {
		os_thread_priority_set(os_current_get(), prio);
	}
}

int btsrv_property_set(const char *key, char *value, int value_len)
{
	int ret = -EIO;

#ifdef CONFIG_PROPERTY
	ret = property_set(key, value, value_len);
	btsrv_event_notify(MSG_BTSRV_BASE, MSG_BTSRV_REQ_FLUSH_NVRAM, (void *)key);
#endif
	return ret;
}

int btsrv_property_get(const char *key, char *value, int value_len)
{
	int ret = -EIO;

#ifdef CONFIG_PROPERTY
	ret = property_get(key, value, value_len);
#endif
	return ret;
}
