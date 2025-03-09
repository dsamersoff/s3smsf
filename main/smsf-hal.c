/*
 * Copyright (C) 2025 Dmitry Samersoff (dms@samersoff.net)
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include "driver/uart_vfs.h"
#include "driver/uart.h"
#include "driver/gpio.h"

#include "smsf-logging.h"
#include "smsf-hal.h"

extern struct smsf_options _opts;

#define BAUDRATE 115200
#define READ_TIMEOUT 1000
#define READ_SIZE 1
#define RX_BUF_SIZE 1024

#define TXD_PIN (GPIO_NUM_25)
#define RXD_PIN (GPIO_NUM_27)

static int init_uart(int uart_no) {
    const uart_config_t uart_config = {
        .baud_rate = BAUDRATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    // We won't use a buffer for sending data.
    if (uart_driver_install(uart_no, RX_BUF_SIZE, 0, 0, NULL, 0) != ESP_OK) {
        log_err("Driver installation failed");
        return -1;
    }

    uart_param_config(uart_no, &uart_config);
    uart_set_pin(uart_no, TXD_PIN, RXD_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    return 0;
}

int com_open(const char *device, int* uart_no) {
    int luart_no = -1;

    // TODO: Ugly, do it better
    if (strcmp(device, "/dev/uart/1") == 0) {
        luart_no = UART_NUM_1;
    }
    else if (strcmp(device, "/dev/uart/2") == 0) {
        luart_no = UART_NUM_2;
    }

    if (luart_no == -1) {
        log_err("Invalid device %s", device);
        return -1;
    }

    if (init_uart(luart_no) == -1) {
        return -1;
    }

//    UART addressed by number, don't need file descriptor
//    int lfd = -1;
//    if ((lfd = open(device, O_RDWR)) == -1) {
//        return -1;
//    }

    // We have a driver now installed so set up the read/write functions to use driver also.
    uart_vfs_dev_use_driver(luart_no);

    *uart_no = luart_no;
    return 0;
 }

void com_close(int uart_no) {
    // Need not to close UART
    // close(fd);
}

int com_write(int uart_no, const char *data, int data_size, int* bytes_written) {
    int bw = uart_write_bytes(uart_no, data, data_size);
    if (bw == -1) {
        *bytes_written = 0;
        return -1;
    }
    *bytes_written = bw;
    return (bw == data_size) ? 0 : -1;
}

static int com_read_impl(int uart_no, char *data, int data_size, int timeout, int* bytes_read) {
    int br = 0;
    *bytes_read = 0;
    memset(data, 0, data_size);
    data_size -= 1; // make a room for \0, decreasing local var - changes will not be saved.

    while(1) {
        int ask_size = ((data_size - *bytes_read) < READ_SIZE) ? data_size - *bytes_read : READ_SIZE;
        br = uart_read_bytes(uart_no, data + (*bytes_read), ask_size, READ_TIMEOUT);
        if (br == -1 || br == 0) { // read error or EOF, bail out
            return br;
        }
        *bytes_read += br;
        if (*bytes_read == data_size) { // All done
            return 0;
        }
    }
}

int com_read(int uart_no, char *data, int data_size, int timeout, int* bytes_read) {
    if (data_size == 0) { // nothing to do
        *bytes_read = 0;
        return 0;
    }
    int res = com_read_impl(uart_no, data, data_size - 1, timeout, bytes_read);
    data[*bytes_read] = '\0'; // ensure null termination for convenience
    return res;
}

