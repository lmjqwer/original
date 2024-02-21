# SPDX-License-Identifier: Apache-2.0
ccflags-y += -I${ZEPHYR_BASE}/include/usb -I${ZEPHYR_BASE}/subsys/usb

subdir-ccflags-y += -I${ZEPHYR_BASE}/samples/usb_device/src/usb_audio_sourcesink/
subdir-ccflags-y += -I${ZEPHYR_BASE}/samples/usb_device/src/usb_cdc_acm/
subdir-ccflags-y += -I${ZEPHYR_BASE}/samples/usb_device/src/usb_hid/

obj-y += system_app_main.o

# USB Device
ifdef CONFIG_USB
	ifdef CONFIG_USB_AUDIO_SOURCESINK
	ifdef CONFIG_USB_DEVICE_HID
		obj-y += usb_audio_sourcesink/
	endif
	else
	ifdef CONFIG_USB_DEVICE_HID
		obj-y += usb_hid/
	endif
	endif

	obj-$(CONFIG_USB_CDC_ACM) += usb_cdc_acm/

endif
