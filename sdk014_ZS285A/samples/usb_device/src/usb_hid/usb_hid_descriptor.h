/*
 * Copyright (c) 2020 Actions Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief USB HID device descriptors.
 */

#ifndef __USB_HID_DESC_H__
#define __USB_HID_DESC_H__

#include <usb/usb_common.h>

/* Misc. macros */
#define LOW_BYTE(x)  ((x) & 0xFF)
#define HIGH_BYTE(x) ((x) >> 8)

static const u8_t usb_hid_fs_desc[] = {
	/* Device Descriptor */
	USB_DEVICE_DESC_SIZE,	/* bLength */
	USB_DEVICE_DESC,	/* bDescriptorType */
	LOW_BYTE(USB_2_0),	/* bcdUSB */
	HIGH_BYTE(USB_2_0),
	0x00,			/* bDeviceClass */
	0x00,			/* bDeviceSubClass */
	0x00,			/* bDeviceProtocol */
	MAX_PACKET_SIZE0,	/* bMaxPacketSize0 */
	/* Vendor Id */
	LOW_BYTE(CONFIG_USB_HID_DEVICE_VID),
	HIGH_BYTE(CONFIG_USB_HID_DEVICE_VID),
	/* Product Id */
	LOW_BYTE(CONFIG_USB_HID_DEVICE_PID),
	HIGH_BYTE(CONFIG_USB_HID_DEVICE_PID),
	/* Device Release Number */
	LOW_BYTE(BCDDEVICE_RELNUM),
	HIGH_BYTE(BCDDEVICE_RELNUM),
	0x01,	/* iManufacturer */
	0x02,	/* iProduct */
	0x03,	/* iSerialNumber */
	0x01,	/* bNumConfigurations */

	/* Configuration Descriptor */
	USB_CONFIGURATION_DESC_SIZE,	/* bLength */
	USB_CONFIGURATION_DESC,		/* bDescriptorType */
	LOW_BYTE(0x0029),		/* wTotalLength */
	HIGH_BYTE(0x0029),
	0x01,	/* bNumInterfaces */
	0x01,	/* bConfigurationValue */
	0x00,	/* iConfiguration */
	USB_CONFIGURATION_ATTRIBUTES,	/* bmAttributes */
	0x96,	/* MaxPower */

	/* Interface Descriptor */
	USB_INTERFACE_DESC_SIZE,	/* bLength */
	USB_INTERFACE_DESC,		/* bDescriptorType */
	CONFIG_USB_HID_DEVICE_IF_NUM,	/* bInterfaceNumber */
	0x00,	/* bAlternateSetting */
	0x02,	/* bNumEndpoints */
	0x03,	/* bInterfaceClass: HID Interface Class */
	0x00,	/* bInterfaceSubClass */
	0x00,	/* bInterfaceProtocol */
	0x00,	/* iInterface */

	/* HID Descriptor */
	sizeof(struct usb_hid_descriptor),	/* bLength */
	USB_HID_DESC,				/* bDescriptorType */
	LOW_BYTE(USB_1_1),			/* bcdHID */
	HIGH_BYTE(USB_1_1),
	0x00,	/* bCountryCode */
	0x01,	/* bNumDescriptors */
	USB_HID_REPORT_DESC,			/* bDescriptorType */
	LOW_BYTE(sizeof(hid_report_desc)),	/* wDescriptorLength */
	HIGH_BYTE(sizeof(hid_report_desc)),

	/* Endpoint Descriptor */
	USB_ENDPOINT_DESC_SIZE,	/* bLength */
	USB_ENDPOINT_DESC,	/* bDescriptorType */
	CONFIG_HID_INTERRUPT_IN_EP_ADDR,	/* bEndpointAddress: Direction: IN - EndpointID: n */
	0x03,	/* bmAttributes:Interrupt Transfer Type  */
	LOW_BYTE(CONFIG_HID_INTERRUPT_IN_EP_MPS),	/* wMaxPacketSize */
	HIGH_BYTE(CONFIG_HID_INTERRUPT_IN_EP_MPS),
	CONFIG_HID_INTERRUPT_EP_INTERVAL_FS,		/* bInterval */

	/* Endpoint Descriptor */
	USB_ENDPOINT_DESC_SIZE,	/* bLength */
	USB_ENDPOINT_DESC,	/* bDescriptorType */
	CONFIG_HID_INTERRUPT_OUT_EP_ADDR,	/* bEndpointAddress: Direction: OUT - EndpointID: n */
	0x03,	/* bmAttributes: Interrupt Transfer Type */
	LOW_BYTE(CONFIG_HID_INTERRUPT_OUT_EP_MPS),	/* wMaxPacketSize */
	HIGH_BYTE(CONFIG_HID_INTERRUPT_OUT_EP_MPS),
	CONFIG_HID_INTERRUPT_EP_INTERVAL_FS,		/* bInterval */
};

static const u8_t usb_hid_hs_desc[] = {
	/* Device Descriptor */
	USB_DEVICE_DESC_SIZE,	/* bLength */
	USB_DEVICE_DESC,	/* bDescriptorType */
	LOW_BYTE(USB_2_0),	/* bcdUSB */
	HIGH_BYTE(USB_2_0),
	0x00,	/* bDeviceClass */
	0x00,	/* bDeviceSubClass */
	0x00,	/* bDeviceProtocol */
	MAX_PACKET_SIZE0,	/* bMaxPacketSize0 */
	/* Vendor Id */
	LOW_BYTE(CONFIG_USB_HID_DEVICE_VID),
	HIGH_BYTE(CONFIG_USB_HID_DEVICE_VID),
	/* Product Id */
	LOW_BYTE(CONFIG_USB_HID_DEVICE_PID),
	HIGH_BYTE(CONFIG_USB_HID_DEVICE_PID),
	/* Device Release Number */
	LOW_BYTE(BCDDEVICE_RELNUM),
	HIGH_BYTE(BCDDEVICE_RELNUM),
	0x01,	/* iManufacturer */
	0x02,	/* iProduct */
	0x03,	/* iSerialNumber */
	0x01,	/* bNumConfigurations */

	/* Configuration Descriptor */
	USB_CONFIGURATION_DESC_SIZE,	/* bLength */
	USB_CONFIGURATION_DESC,		/* bDescriptorType */
	LOW_BYTE(0x0029),		/* wTotalLength */
	HIGH_BYTE(0x0029),
	0x01,	/* bNumInterfaces */
	0x01,	/* bConfigurationValue */
	0x00,	/* iConfiguration */
	USB_CONFIGURATION_ATTRIBUTES,	/* bmAttributes */
	0x96,	/* MaxPower */

	/* Interface Descriptor */
	USB_INTERFACE_DESC_SIZE,	/* bLength */
	USB_INTERFACE_DESC,		/* bDescriptorType */
	CONFIG_USB_HID_DEVICE_IF_NUM,	/* bInterfaceNumber */
	0x00,	/* bAlternateSetting */
	0x02,	/* bNumEndpoints */
	0x03,	/* bInterfaceClass: HID Interface Class */
	0x00,	/* bInterfaceSubClass */
	0x00,	/* bInterfaceProtocol */
	0x00,	/* iInterface */

	/* HID Descriptor */
	sizeof(struct usb_hid_descriptor),	/* bLength */
	USB_HID_DESC,				/* bDescriptorType */
	LOW_BYTE(USB_1_1),			/* bcdHID */
	HIGH_BYTE(USB_1_1),
	0x00,	/* bCountryCode */
	0x01,	/* bNumDescriptors */
	USB_HID_REPORT_DESC,			/* bDescriptorType */
	LOW_BYTE(sizeof(hid_report_desc)),	/* wDescriptorLength */
	HIGH_BYTE(sizeof(hid_report_desc)),

	/* Endpoint Descriptor */
	USB_ENDPOINT_DESC_SIZE, /* bLength */
	USB_ENDPOINT_DESC,	/* bDescriptorType */
	CONFIG_HID_INTERRUPT_IN_EP_ADDR,	/* bEndpointAddress: Direction: IN - EndpointID: n */
	0x03,	/* bmAttributes:Interrupt Transfer Type  */
	LOW_BYTE(USB_MAX_HS_INTR_MPS),		/* wMaxPacketSize */
	HIGH_BYTE(USB_MAX_HS_INTR_MPS),
	CONFIG_HID_INTERRUPT_EP_INTERVAL_HS,	/* bInterval */

	/* Endpoint Descriptor */
	USB_ENDPOINT_DESC_SIZE, /* bLength */
	USB_ENDPOINT_DESC,	/* bDescriptorType */
	CONFIG_HID_INTERRUPT_OUT_EP_ADDR,	/* bEndpointAddress: Direction: OUT - EndpointID: n */
	0x03,	/* bmAttributes: Interrupt Transfer Type */
	LOW_BYTE(USB_MAX_HS_INTR_MPS),		/* wMaxPacketSize */
	HIGH_BYTE(USB_MAX_HS_INTR_MPS),
	CONFIG_HID_INTERRUPT_EP_INTERVAL_HS,	/* bInterval */
};

#endif /*__USB_HID_DESC_H__*/
