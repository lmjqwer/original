#include <device.h>
#include <stream.h>
#include <logging/sys_log.h>
#include <uart.h>
#include <acts_ringbuf.h>
//#include <malloc.h>
#include <soc.h>
#include <string.h>
#include <ringbuff_stream.h>
#include "uart_stream.h"

#define RAW_BUFFER_SIZE                 256
#define RINGBUF_STREAM_SIZE             1536

struct uart_stream_info {
    struct device *uart_dev;
    u32_t uart_baud_rate;
    io_stream_t rbuf_stream;
    struct acts_ringbuf *rbuf;
    u8_t *raw_buf;
    u16_t rb_read_i;
    u16_t rb_write_i;
    u8_t opened;
};

int raw_buf_has_data(struct uart_stream_info *info, u32_t *read_position, u32_t *readable_len)
{
    int valid_data_len = 0;

    if (info->rb_write_i > info->rb_read_i) {
        valid_data_len = info->rb_write_i - info->rb_read_i;
    } else if (info->rb_write_i < info->rb_read_i) {
        valid_data_len = RAW_BUFFER_SIZE - info->rb_read_i + info->rb_write_i;
    } else {
        valid_data_len = 0;
    }

    *read_position = info->rb_read_i;
    *readable_len = valid_data_len;

    return valid_data_len;
}

int get_data_from_raw_buf(io_stream_t wstream, u8_t *raw_buf, u32_t position, u32_t size)
{
    u32_t offset, len;

    offset = position;
    len = RAW_BUFFER_SIZE - offset;

    if (len >= size) {
        stream_write(wstream, raw_buf + offset, size);
    } else {
        stream_write(wstream, raw_buf + offset, len);
        stream_write(wstream, raw_buf, size - len);
    }

    return size;
}

/* @reson 0 transmission complete ,1 half transmission
*/
void uart_rx_isr(struct device *dev, u32_t priv_data, int reson)
{
    struct uart_stream_info* info = (struct uart_stream_info*)priv_data;
    u32_t read_position, readable_len, has_data;

    if (reson) {
        info->rb_write_i = RAW_BUFFER_SIZE / 2;
    } else {
        info->rb_write_i = RAW_BUFFER_SIZE;
    }

    has_data = raw_buf_has_data(info, &read_position, &readable_len);

    if (has_data) {
        get_data_from_raw_buf(info->rbuf_stream, info->raw_buf, read_position, readable_len);
    }

    if (reson) {
        info->rb_read_i = RAW_BUFFER_SIZE / 2;
    } else {
        info->rb_read_i = 0;
        info->rb_write_i = 0;
        // printk("uart_rx_isr %d\n", reson);
    }
}

int uart_stream_init(io_stream_t handle,void *param)
{
    struct uart_stream_info* info = NULL;
    struct uart_stream_param *uparam = param;
    int err = 0;

    info = mem_malloc(sizeof(struct uart_stream_info));
    if (!info) {
        SYS_LOG_ERR("OOM");
        err = -ENOSR;
        goto error;
    }
    memset(info, 0, sizeof(struct uart_stream_info));

    info->raw_buf = mem_malloc(RAW_BUFFER_SIZE);
    if (!info->raw_buf) {
        SYS_LOG_ERR("OOM");
        err = -ENOSR;
        goto error;
    }

    info->uart_dev = device_get_binding(uparam->uart_dev_name);
    info->uart_baud_rate = uparam->uart_baud_rate;
    if (!info->uart_dev) {
        SYS_LOG_ERR("device binding fail %s", uparam->uart_dev_name);
        err = -EIO;
        goto error;
    }

    info->rbuf = acts_ringbuf_alloc(RINGBUF_STREAM_SIZE);
    if (!info->rbuf) {
        SYS_LOG_ERR("rbuf NULL");
        err = -ENOSR;
        goto error;
    }

    //info->rbuf_stream = stream_create(TYPE_RINGBUFF_STREAM, info->rbuf);

	info->rbuf_stream = ringbuff_stream_create(info->rbuf);
    if (!info->rbuf_stream) {
        SYS_LOG_INF("rbuf_stream NULL");
        err = -ENOSR;
        goto error;
    }

    info->opened = 0;
    handle->data = info;

    SYS_LOG_INF();
    return 0;

error:
    if (info->rbuf) {
        acts_ringbuf_free(info->rbuf);
    }

    if (info->raw_buf) {
        mem_free(info->raw_buf);
    }

    if (info) {
        mem_free(info);
    }
    return err;
}

int uart_stream_open(io_stream_t handle, stream_mode mode)
{
    struct uart_stream_info* info = handle->data;

    if (!info || info->opened)
        return -EACCES;

    os_sched_lock();

    stream_open(info->rbuf_stream, MODE_IN_OUT);

    stream_flush(info->rbuf_stream);

    /* uart rx fifo access: dma */
    uart_fifo_switch(info->uart_dev, 0, UART_FIFO_DMA);
    uart_dma_recv_init(info->uart_dev, 0xff, uart_rx_isr, info);
    uart_dma_recv_config(info->uart_dev, info->raw_buf, RAW_BUFFER_SIZE);
    uart_dma_recv_start(info->uart_dev);

    info->opened = true;
    handle->mode = mode;
    handle->total_size = info->rbuf_stream->total_size;
    handle->cache_size = 0;
    handle->rofs = 0;
    handle->wofs = 0;

    os_sched_unlock();

    SYS_LOG_INF();

    return 0;
}

/* If there is not enough data, return directly.
 */
int uart_stream_read(io_stream_t handle, unsigned char *buf, int num)
{
    struct uart_stream_info *info = (struct uart_stream_info *)handle->data;

    if (!info)
        return 0;

    u32_t read_position, readable_len, has_data;

    SYS_IRQ_FLAGS flags;

    sys_irq_lock(&flags);

    info->rb_write_i =  RAW_BUFFER_SIZE - uart_dma_recv_remain_len(info->uart_dev);
    has_data = raw_buf_has_data(info, &read_position, &readable_len);

    if (has_data) {
        get_data_from_raw_buf(info->rbuf_stream, info->raw_buf, read_position, readable_len);
        info->rb_read_i += readable_len;
    }
    sys_irq_unlock(&flags);

    int read_len;

    read_len = stream_read(info->rbuf_stream, buf, num);

    handle->rofs = info->rbuf_stream->rofs;

    return read_len;
}

int uart_stream_tell(io_stream_t handle)
{
    struct uart_stream_info *info = (struct uart_stream_info *)handle->data;

    if (!info)
        return -EACCES;

    if (!info->opened) {
        return 0;
    }

    if(info->rbuf_stream)
        return stream_tell(info->rbuf_stream);

    return 0;
}

int uart_stream_get_length(io_stream_t handle)
{
    struct uart_stream_info *info = (struct uart_stream_info *)handle->data;

    if(!info)
        return -EACCES;

    if(info->rbuf_stream)
        return stream_get_length(info->rbuf_stream);

    return 0;
}

int uart_stream_get_space(io_stream_t handle)
{
    struct uart_stream_info *info = (struct uart_stream_info *)handle->data;

    if (!info)
        return -EACCES;

    if (info->rbuf_stream)
        return stream_get_space(info->rbuf_stream);

    return 0;
}

int uart_stream_write(io_stream_t handle, unsigned char *buf, int num)
{
    int tx_num = 0;
    struct uart_stream_info *info = (struct uart_stream_info *)handle->data;

    if (!info)
        return -EACCES;

    do {
        uart_poll_out(info->uart_dev, buf[tx_num++]);
    } while (tx_num < num);

    return tx_num;
}

int uart_stream_flush(io_stream_t handle)
{
    struct uart_stream_info *info = (struct uart_stream_info *)handle->data;
    u32_t last_remain_len;

    /* 1, wait uart fifo empty */
    k_busy_wait(2000);
    do {
        last_remain_len = uart_dma_recv_remain_len(info->uart_dev);
        /* bytes per second: info->uart_baud_rate/10
         * us per byte：1000000 / info->uart_baud_rate/10
         * for example: 3M baud rate, 4us per byte
         */
        k_busy_wait((10000000 / info->uart_baud_rate) + 50);
    } while (uart_dma_recv_remain_len(info->uart_dev) != last_remain_len);

    SYS_IRQ_FLAGS flags;
    sys_irq_lock(&flags);

    /* 2, reset info->raw_buf */
    info->rb_write_i =  RAW_BUFFER_SIZE - uart_dma_recv_remain_len(info->uart_dev);
    info->rb_read_i = info->rb_write_i;

    /* 3, flush rbuf_stream */
    stream_flush(info->rbuf_stream);

    sys_irq_unlock(&flags);

    SYS_LOG_INF("rbuf_stream: %d", stream_get_length(info->rbuf_stream));

    return 0;
}

int uart_stream_close(io_stream_t handle)
{
    struct uart_stream_info *info = (struct uart_stream_info *)handle->data;

    if (!info)
        return -EACCES;

    os_sched_lock();

    if (info->rbuf_stream) {
        stream_close(info->rbuf_stream);
    }

    uart_dma_recv_stop(info->uart_dev);

    info->opened = false;

    os_sched_unlock();

    SYS_LOG_INF();

    return 0;
}

int uart_stream_destroy(io_stream_t handle)
{
    int res = 0;
    struct uart_stream_info *info = (struct uart_stream_info *)handle->data;

    if (!info)
        return -EACCES;

    os_sched_lock();

    if (info->rbuf) {
        acts_ringbuf_free(info->rbuf);
        info->rbuf = NULL;
    }

    if (info->rbuf_stream) {
        stream_destroy(info->rbuf_stream);
        info->rbuf_stream = NULL;
    }

    mem_free(info);

    handle->data = NULL;

    os_sched_unlock();

    return res;
}

const stream_ops_t uart_stream_ops = {
    .init = uart_stream_init,
    .open = uart_stream_open,
    .read = uart_stream_read,
    .tell = uart_stream_tell,
    .get_length = uart_stream_get_length,
    .get_space = uart_stream_get_space,
    .write = uart_stream_write,
    .flush = uart_stream_flush,
    .close = uart_stream_close,
    .destroy = uart_stream_destroy,
};

