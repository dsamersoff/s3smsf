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
#include <sys/fcntl.h>
#include <sys/errno.h>
#include <sys/unistd.h>
#include <sys/select.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/uart_vfs.h"
#include "driver/uart.h"
#include "driver/gpio.h"

#include "esp_log.h"
#include "ssd1306.h"
#include "font8x8_basic.h"

#include "smsf-logging.h"
#include "smsf-hal.h"
#include "smsf-flow.h"

#define COM_DEVICE "/dev/uart/2"

extern struct smsf_options _opts;
int _uartno; // UART no

// Queue
QueueHandle_t _queue;

// static char _disp_buf[256];

// LCD  display configuration
#define SDA_PIN (GPIO_NUM_21)
#define SCL_PIN (GPIO_NUM_22)
#define RESET_PIN -1

SSD1306_t _disp; // Display device
volatile int _current_line = 0;

static void display_banner() {
    char buf[16];
    ssd1306_clear_screen(&_disp, false);
    ssd1306_contrast(&_disp, 0xff);

    snprintf(buf, sizeof(buf),"DM\\S %0X", _opts.version);
    ssd1306_display_text(&_disp, 0, buf, strlen(buf), false);
}

/* Screen layout
   0: Header
   1: Destination phone
   2: Connection status
   3:     empty line
   4: ... scroll number of messages in a loop
 */

static void display_task(void *arg) {
    char *recv_message;

    while(1) {
        if (xQueueReceive(_queue, &recv_message, 0) == pdTRUE) {
            switch ( _current_line) {
                case 1:
                  // Empty screen, clear and print the banner
                    display_banner();
                    break;
                case 3:
                    // First message in the scroll area, enable scroll before displaying message and leave empty line
                    ssd1306_software_scroll(&_disp, 4, 7);
                    _current_line += 1;
                    break;
                default:
                    break;
            }

            if (_current_line < 4) {
                // Header text
	            ssd1306_display_text(&_disp, _current_line, recv_message, strlen(recv_message), false);
            }
            else {
                // Message text
                ssd1306_scroll_text(&_disp, recv_message, strlen(recv_message), false);
            }

            _current_line += 1;
            free(recv_message);
        }
	    vTaskDelay(3000 / portTICK_PERIOD_MS);
    }
    vTaskDelete( NULL );
}


static void send_to_display(const char *format, ...) {
    va_list args;
    va_start(args, format);
    int size = vsnprintf(NULL, 0, format, args);
    char *buf = malloc(size+1);
    vsnprintf(buf, size+1, format, args);
    va_end(args);
    xQueueSend(_queue, &buf, 0);
}

static void uart_task(void *arg) {
    while(1) {
        _current_line = 1; // Not care about race

        if (flow_setup(_uartno, (notify_func_t *) send_to_display, NULL /* da ovevrirde*/) == 0) {
            while(1) {
                if (flow(_uartno, (notify_func_t *) send_to_display) != 0) {
                    // Repeat full cycle in case of errors
                    break;
                }
            }
        }
        vTaskDelay(5000 / portTICK_PERIOD_MS); // Let modem too bootstrap
    }

    vTaskDelete( NULL );
}

void app_main(void) {
    _queue = xQueueCreate(5, sizeof(char **));

    i2c_master_init(&_disp, SDA_PIN, SCL_PIN, RESET_PIN);
	ssd1306_init(&_disp, 128, 64);
    display_banner();

    log_write("display_task", "Display ready");

    if (com_open(COM_DEVICE, &_uartno) == -1) {
        log_errno("Can't open %s", COM_DEVICE);
        vTaskDelay(5000 / portTICK_PERIOD_MS);
        abort();
    }

    // Display task has lower priority then uart task
    xTaskCreate(display_task, "display_task", 2*1024, NULL, configMAX_PRIORITIES - 2, NULL);
    xTaskCreate(uart_task, "uart_task", 6 * 1024, NULL, configMAX_PRIORITIES - 1, NULL);
}
