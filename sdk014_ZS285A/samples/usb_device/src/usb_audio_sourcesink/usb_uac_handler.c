/*
 * Copyright (c) 2018 Actions Corporation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <zephyr.h>
#include <init.h>
#include <string.h>
#include <stdlib.h>
#include <os_common_api.h>
#include <usb/usb_device.h>
#include <usb/class/usb_hid.h>
#include <usb/class/usb_audio.h>
#include <misc/byteorder.h>
#include <shell/shell.h>

#define SYS_LOG_LEVEL CONFIG_SYS_LOG_USB_DEVICE_LEVEL
#define SYS_LOG_DOMAIN "usb/usb_handler"
#include <logging/sys_log.h>

#include "usb_uac_handler.h"
#include "usb_hid_handler.h"
#include "usb_hid_report_desc.h"
#include "usb_audio_device_desc.h"

/* USB max packet size */
#define UAC_TX_UNIT_SIZE	MAX_UPLOAD_PACKET
#define UAC_TX_DUMMY_SIZE	MAX_UPLOAD_PACKET

/*
 * NOTE: should be multiple of 20 and as big as possible.
 *
 * 16K*16@16:1 will enqueue 640-byte each time, 8K*16@4:1
 * will enqueue 160-byte each time.
 */
#define UAC_TX_UNIT_NUM	2

/* total length */
#define UAC_TX_BUF_SIZE	(UAC_TX_UNIT_SIZE * UAC_TX_UNIT_NUM)

/*
 * Start transfer until tx_buf has at least UAC_TX_SIZE_OPT bytes.
 */
#ifdef UAC_TX_OPT
#define UAC_TX_COUNT_OPT	20
#define UAC_TX_SIZE_OPT	(UAC_TX_UNIT_SIZE * UAC_TX_COUNT_OPT)
#else
#define UAC_TX_SIZE_OPT	0
#endif

static u8_t tx_buf[UAC_TX_BUF_SIZE];
static u8_t *tx_buf_enq = tx_buf;
static u8_t *tx_buf_deq = tx_buf;

/* available length, unit: byte */
static u32_t tx_buf_avail = sizeof(tx_buf);

static bool tx_started;

static inline bool has_data(void)
{
	/*
	 * In case of enqueue length is less than UAC_TX_UNIT_SIZE
	 *
	 * return tx_buf_avail < sizeof(tx_buf) - UAC_TX_SIZE_OPT;
	 */
	return (sizeof(tx_buf) - UAC_TX_SIZE_OPT - tx_buf_avail) >= UAC_TX_UNIT_SIZE;
}

static inline bool has_room(u16_t len)
{
	return len <= tx_buf_avail;
}

static inline void turn_around(u8_t **buf)
{
	if (*buf == (tx_buf + sizeof(tx_buf))) {
		*buf = tx_buf;
	}
}

static int uac_enqueue(const u8_t *buf, u16_t len)
{
	if (!has_room(len)) {
		return -ENOMEM;
	}

	memcpy(tx_buf_enq, buf, len);

	/*
	 * NOTICE: buffer should be continuous!
	 */
	tx_buf_enq += len;
	tx_buf_avail -= len;
	turn_around(&tx_buf_enq);

	return 0;
}

static u8_t *uac_dequeue(void)
{
	u8_t *start;

	if (!has_data()) {
		return NULL;
	}

	start = tx_buf_deq;

	tx_buf_deq += UAC_TX_UNIT_SIZE;
	tx_buf_avail += UAC_TX_UNIT_SIZE;
	turn_around(&tx_buf_deq);

	return start;
}

/*
 * FIXME: There may be some data not transported to USB yet.
 */
static inline void uac_cleanup(void)
{
	u32_t key = irq_lock();

	tx_buf_enq = tx_buf_deq = tx_buf;
	tx_buf_avail = sizeof(tx_buf);

	irq_unlock(key);
}

static int usb_audio_tx_unit(const u8_t *buf)
{
	u32_t wrote;

	return usb_write(CONFIG_USB_AUDIO_SOURCE_IN_EP_ADDR, buf,
					UAC_TX_UNIT_SIZE, &wrote);
}

static int usb_audio_tx_dummy(void)
{
	/* NOTE: Could use stack to save memory */
	static const u8_t dummy_buf[UAC_TX_DUMMY_SIZE];
	u32_t wrote;

	return usb_write(CONFIG_USB_AUDIO_SOURCE_IN_EP_ADDR, dummy_buf,
					sizeof(dummy_buf), &wrote);
}

static int usb_audio_ep_start(void)
{
	const u8_t *buf;

	buf = uac_dequeue();
	if (!buf) {
		return usb_audio_tx_dummy();
	} else {
		return usb_audio_tx_unit(buf);
	}
}

/*
 * Interrupt Context
 */
static void usb_audio_in_ep_complete(u8_t ep,
	enum usb_dc_ep_cb_status_code cb_status)
{
	SYS_LOG_DBG("complete: ep = 0x%02x  ep_status_code = %d", ep, cb_status);
	/* In transaction request on this EP, Send recording data to PC */
	if (USB_EP_DIR_IS_IN(ep)) {
		usb_audio_ep_start();
	}

}

/*
 * Interrupt Context
 */
static void usb_audio_out_ep_complete(u8_t ep,
	enum usb_dc_ep_cb_status_code cb_status)
{
	u32_t read_byte, res;
	u8_t ISOC_out_Buf[640];

	SYS_LOG_DBG("complete: ep = 0x%02x  ep_status_code = %d", ep, cb_status);
	/* Out transaction on this EP, data is available for read */
	if (USB_EP_DIR_IS_OUT(ep)) {
		if (cb_status == USB_DC_EP_DATA_OUT) {
			SYS_LOG_DBG("data is available for out ep to read");
			res = usb_audio_device_ep_read(ISOC_out_Buf,
				sizeof(ISOC_out_Buf), &read_byte);
			if (!res && read_byte != 0) {
				SYS_LOG_DBG("Read out ep succe:%d\r\n", read_byte);
#if 0
				for (u8_t i = 0; i < read_byte; i++) {
					SYS_LOG_DBG("ISOC_out_Buf[%d]:%x\r\n", i,
						ISOC_out_Buf[i]);
				}
#endif
			} else {
				SYS_LOG_DBG("Read out ep fail!");
				memset(ISOC_out_Buf, 0, sizeof(ISOC_out_Buf));
			}
			usb_audio_sink_outep_flush();
		}
	}
}

static void usb_audio_start_stop(bool start)
{
	if (!start) {
		usb_audio_source_inep_flush();
		/*
		 * Don't cleanup in "Set Alt Setting 0" case, Linux may send
		 * this before going "Set Alt Setting 1".
		 */
		/* uac_cleanup(); */
	} else {
		/*
		 * First packet is all-zero in case payload be flushed (Linux
		 * may send "Set Alt Setting" several times).
		 */
		usb_audio_tx_dummy();
	}
}

int usb_host_sync_volume_to_device(int host_volume)
{
	int vol_db;

	if (host_volume == 0x0000) {
		vol_db = 0;
	} else {
		vol_db = (int)((host_volume - 65536)*10 / 256.0);
	}
	return vol_db;
}

static void usb_audio_source_vol_sync(int *pstore_info, u8_t info_type)
{

	int volume_db = 0;
	SYS_LOG_DBG("*pstore_info=0x%04x info_type=%d", *pstore_info,
		info_type);

	switch (info_type) {
	case USOUND_SYNC_HOST_MUTE:
		SYS_LOG_DBG("Host Set Mute");
		break;

	case USOUND_SYNC_HOST_UNMUTE:
		SYS_LOG_DBG("Host Set UnMute");
		break;

	case USOUND_SYNC_HOST_VOL_TYPE:
		volume_db = usb_host_sync_volume_to_device(*pstore_info);
		SYS_LOG_DBG("vol:0x%04x   Volume(db): %d\n", *pstore_info, volume_db);
		break;

	default:
		break;
	}
}

static void usb_audio_sink_vol_sync(int *pstore_info, u8_t info_type)
{

	int volume_db = 0;
	SYS_LOG_DBG("*pstore_info=0x%04x info_type=%d", *pstore_info,
		info_type);

	switch (info_type) {
	case USOUND_SYNC_HOST_MUTE:
		SYS_LOG_DBG("Host Set Mute");
		break;

	case USOUND_SYNC_HOST_UNMUTE:
		SYS_LOG_DBG("Host Set UnMute");
		break;

	case USOUND_SYNC_HOST_VOL_TYPE:
		volume_db = usb_host_sync_volume_to_device(*pstore_info);
		SYS_LOG_WRN("vol:0x%04x   Volume(db): %d\n", *pstore_info, volume_db);
		break;

	default:
		break;
	}
}

void usb_audio_set_tx_start(bool start)
{
	tx_started = start;

	/* cleanup at start time */
	if (start) {
		uac_cleanup();
	}
}

void usb_audio_tx_flush(void)
{
	usb_audio_source_inep_flush();
}

int usb_audio_tx(const u8_t *buf, u16_t len)
{
	u32_t key;
	int ret;

	key = irq_lock();

	ret = uac_enqueue(buf, len);

	/*
	 * FIXME: Should flush endpoint FIFO for the first transport?
	 *
	 * NOTICE: There may be race condition, think of that all-zero
	 * packet is filled in FIFO and USB is tranporting, we flush FIFO
	 * will make the transport failed.
	 */
#if 0
	if (!tx_started) {
		tx_started = true;
		usb_audio_source_inep_flush();
		usb_audio_ep_start();
	}
#endif

	irq_unlock(key);

	return ret;
}

bool usb_audio_tx_enabled(void)
{
	return usb_audio_device_enabled();
}

int usb_hid_tx(const u8_t *buf, u16_t len)
{
	u32_t wrote, interval;
	int ret, count = 0;
	u8_t speed_mode = usb_device_speed();

	/* wait one interval at most, unit: 125us */
	if (speed_mode == USB_SPEED_HIGH) {
		count = 1 << (CONFIG_HID_INTERRUPT_EP_INTERVAL_HS - 1);
	} else if (speed_mode == USB_SPEED_FULL || speed_mode == USB_SPEED_LOW) {
		interval = CONFIG_HID_INTERRUPT_EP_INTERVAL_FS;
		count =  interval * 20;
	} else {
		;
	}

	do {
		ret = hid_int_ep_write(buf, len, &wrote);
		if (ret == -EAGAIN) {
			k_busy_wait(125);
		}
	} while ((ret == -EAGAIN) && (--count > 0));

	if (ret) {
		SYS_LOG_ERR("ret: %d", ret);
	} else if (!ret && wrote != len) {
		SYS_LOG_ERR("wrote: %d, len: %d", wrote, len);
	}

	return ret;
}

static void _usb_audio_call_status_cb(u32_t status_rec)
{
	SYS_LOG_DBG("status_rec: 0x%04x", status_rec);
}

#ifdef CONFIG_NVRAM_CONFIG
#include <string.h>
#include <nvram_config.h>
#include <property_manager.h>
#endif

int usb_device_composite_fix_dev_sn(void)
{
	static u8_t mac_str[CONFIG_USB_DEVICE_STRING_DESC_MAX_LEN];
	int ret;
	int read_len;
#ifdef CONFIG_NVRAM_CONFIG
	read_len = nvram_config_get(CFG_BT_MAC, mac_str, CONFIG_USB_DEVICE_STRING_DESC_MAX_LEN);
	if (read_len < 0) {
		SYS_LOG_DBG("no sn data in nvram: %d", read_len);
		ret = usb_device_register_string_descriptor(DEV_SN_DESC, CONFIG_USB_APP_AUDIO_DEVICE_SN, strlen(CONFIG_USB_APP_AUDIO_DEVICE_SN));
		if (ret)
			return ret;
	} else {
		ret = usb_device_register_string_descriptor(DEV_SN_DESC, mac_str, read_len);
		if (ret)
			return ret;
	}
#else
	ret = usb_device_register_string_descriptor(DEV_SN_DESC, CONFIG_USB_APP_AUDIO_DEVICE_SN, strlen(CONFIG_USB_APP_AUDIO_DEVICE_SN));
		if (ret)
			return ret;
#endif
	return 0;
}

static int composite_pre_init(struct device *dev)
{
	u8_t ret = 0;

	/* Register composite device descriptor */
	usb_device_register_descriptors(usb_audio_dev_fs_desc, usb_audio_dev_hs_desc);

	/* Register composite device string descriptor */
	ret = usb_device_register_string_descriptor(MANUFACTURE_STR_DESC, CONFIG_USB_APP_AUDIO_DEVICE_MANUFACTURER, strlen(CONFIG_USB_APP_AUDIO_DEVICE_MANUFACTURER));
	if (ret) {
		goto exit;
	}
	ret = usb_device_register_string_descriptor(PRODUCT_STR_DESC, CONFIG_USB_APP_AUDIO_DEVICE_PRODUCT, strlen(CONFIG_USB_APP_AUDIO_DEVICE_PRODUCT));
	if (ret) {
		goto exit;
	}
	ret = usb_device_composite_fix_dev_sn();
	if (ret) {
		goto exit;
	}

	/* Register HID device */
	usb_hid_device_init();

	/* Register callbacks functiongs*/
	usb_audio_source_register_start_cb(usb_audio_start_stop);
	usb_audio_device_register_inter_in_ep_cb(usb_audio_in_ep_complete);
	usb_audio_device_register_inter_out_ep_cb(usb_audio_out_ep_complete);
	usb_audio_source_register_volume_sync_cb(usb_audio_source_vol_sync);
	usb_audio_sink_register_volume_sync_cb(usb_audio_sink_vol_sync);
	usb_audio_register_call_status_cb(_usb_audio_call_status_cb);
	/* level:8 PC:34%*/
	usb_audio_sink_set_cur_vol(0xEF00);

	/* USB Audio Source & Sink Initialize */
	ret = usb_audio_composite_dev_init();
	if (ret) {
		SYS_LOG_ERR("usb audio init failed");
		goto exit;
	}

	usb_device_composite_init(NULL);
exit:
	return ret;

	return 0;
}

int usb_dev_composite_pre_init(struct device *dev)
{
	return composite_pre_init(dev);
}

int usb_dev_composite_del_init(void)
{
	int ret;

	ret = usb_disable();
	if (ret) {
		SYS_LOG_ERR("Failed to disable USB: %d", ret);
		return ret;
	}
	usb_deconfig();
	return 0;
}
