#ifndef __UART_STREAM_H__
#define __UART_STREAM_H__


struct uart_stream_param {
	u8_t *uart_dev_name;
	int uart_baud_rate;
};


extern const stream_ops_t uart_stream_ops;


#endif
