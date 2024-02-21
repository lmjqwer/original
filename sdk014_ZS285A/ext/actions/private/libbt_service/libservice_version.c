/*
 * Copyright (c) 2019 Actions Semiconductor Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief lib OTA version interface
 */
#include <misc/printk.h>
#include <kernel.h>

#define LIBBTSERVICE_VERSION_NUMBER     0x02000000
#define LIBBTSERVICE_VERSION_STRING     "2.0.0"

u32_t libbtservice_version_dump(void)
{
	printk("libbtservice:version %s ,release time: %s:%s\n", LIBBTSERVICE_VERSION_STRING, __DATE__, __TIME__);
	return LIBBTSERVICE_VERSION_NUMBER;
}