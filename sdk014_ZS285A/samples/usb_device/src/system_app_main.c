/*
 * Copyright (c) 2012-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdlib.h>
#include <string.h>
#include <misc/printk.h>
#include <usb/usb_device.h>
#include <drivers/usb/usb_phy.h>
#include <logging/sys_log.h>
#include <class/usb_audio.h>
#include <class/usb_stub.h>
#include <class/usb_msc.h>

#include "usb_uac_handler.h"
#include "usb_hid_handler.h"

#define UNINSTALL_TEST 0

#ifdef CONFIG_SUPPORT_USB_AUDIO_SOURCE
static u8_t MIC_DAT[] = "66cdefghijklmnopqrstuvwxyz123456";
#endif

#if  UNINSTALL_TEST
void usb_dev_uninstall_test(void);
#endif

extern int usb_hid_test_init(struct device *dev);

void main(void)
{
	usb_phy_init();
	usb_phy_enter_b_idle();
	usb_hid_test_init(NULL);
#if 0
	usb_dev_composite_pre_init(NULL);
	usb_hid_test_init(NULL)
	usb_stub_init(NULL);
	usb_mass_storage_init(NULL);
#endif
	while (1) {
		k_sleep(MSEC(500));

		/* test for usb audio source */
#ifdef CONFIG_SUPPORT_USB_AUDIO_SOURCE
		if (usb_audio_tx_enabled()) {
			SYS_LOG_DBG("tx audio source dat");
			usb_audio_tx(MIC_DAT, strlen(MIC_DAT));
		}
#endif

#if UNINSTALL_TEST
		usb_dev_install_uninstall_test();
#endif

	}
}


#if UNINSTALL_TEST
void usb_dev_uninstall_test(void)
{

	u8_t cnt = 0;

	u32_t count = 0;

	while (1) {
		k_sleep(1000);
		k_sleep(1000);
		k_sleep(1000);
		cnt++;
		if (cnt == 8) {
			SYS_LOG_ERR("cnt=%d\r\n", cnt);
			SYS_LOG_WRN("usb_device_init()");
#if 0
			usb_dev_composite_pre_init(NULL);
			usb_stub_init(NULL);
			usb_mass_storage_init(NULL);
#endif

		} else if (cnt == 16) {
			SYS_LOG_ERR("cnt=%d\r\n", cnt);
			cnt = 0;
			SYS_LOG_WRN("usb_device_exit()");
#if 0
			usb_dev_composite_del_init();
			usb_stub_exit();
			usb_mass_storage_exit();
#endif
			count++;
			SYS_LOG_ERR("count=%d\r\n", count);
		} else {
			SYS_LOG_WRN("cnt=%d\r\n", cnt);
		}
	}
}
#endif
