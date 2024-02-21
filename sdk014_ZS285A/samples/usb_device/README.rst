(1)/Kconfig.zephyr
source "samples/usb_device/src/Kconfig"

(2)ext/actions/porting/hal/Makefile
#obj-$(CONFIG_USB_AUDIO_SOURCESINK) += usb_audio/

attention! some common configurations are set in the file: \ext\actions\Kconfig.defconfig
such as CONFIG_HID_REPORT_DESC_SIZE.

