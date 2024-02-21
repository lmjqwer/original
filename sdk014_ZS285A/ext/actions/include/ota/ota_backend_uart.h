
#ifndef __OTA_BACKEND_UART_H_
#define __OTA_BACKEND_UART_H_

#include <ota/ota_backend.h>

extern struct ota_backend *ota_backend_uart_init(ota_backend_notify_cb_t cb, void *param);

#endif

