/*
 * Copyright (c) 2016 Actions Semi Co., Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief btsrv storage
 */

#define SYS_LOG_NO_NEWLINE
#define SYS_LOG_DOMAIN "btsrv_rdm"

#include <logging/sys_log.h>

#include <string.h>
#include <kernel.h>
#include <misc/slist.h>
#include <bluetooth/host_interface.h>
#include <property_manager.h>
#include "btsrv_inner.h"

static void stack_property_flush_req(char *key)
{
#ifdef CONFIG_PROPERTY
	btsrv_event_notify(MSG_BTSRV_BASE, MSG_BTSRV_REQ_FLUSH_NVRAM, (void *)key);
#endif
}

int btsrv_storage_init(void)
{
	return hostif_stack_storage_init(stack_property_flush_req);
}

int btsrv_storage_deinit(void)
{
	return hostif_stack_storage_deinit();
}

int btsrv_storage_get_linkkey(struct bt_linkkey_info *info, u8_t cnt)
{
	return hostif_stack_storage_get_linkkey((struct br_linkkey_info *)info, cnt);
}

int btsrv_storage_update_linkkey(struct bt_linkkey_info *info, u8_t cnt)
{
	return hostif_stack_storage_update_linkkey((struct br_linkkey_info *)info, cnt);
}

int btsrv_storage_write_ori_linkkey(bd_address_t *addr, u8_t *link_key)
{
	return hostif_stack_storage_write_ori_linkkey((bt_addr_t *)addr, link_key);
}

void btsrv_storage_clean_linkkey(void)
{
	hostif_stack_storage_clean_linkkey();
}
