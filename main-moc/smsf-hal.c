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
#include <termios.h>

#include "smsf-logging.h"
#include "smsf-hal.h"


#define BAUDRATE B115200
#define READ_USLEEP 1000
#define READ_SIZE 1

char _last_command[4096];
int _last_index = 0;

int com_open(const char *device, int* fd) {
    *fd = 42;
    return 0;
 }

void com_close(int fd) {
   // pass;
}

int com_write(int fd, const char *data, int data_size, int* bytes_written) {
    (void) fd; // not used

    if (_last_index == 0) {
        memset(_last_command, 0, sizeof(_last_command));
    }

    if (data_size + _last_index > sizeof(_last_command) - 1) {
        log_err("Data is too large %d for %d at ", data_size, sizeof(_last_command) - 1, _last_index);
        return -1;
    }
    memcpy(_last_command + _last_index, data, data_size);
    *bytes_written = data_size;
    return  0;
}

int com_read_impl(int fd, char *data, int data_size, int timeout, int* bytes_read) {
    //  MOC'ed responses
    *bytes_read = 0;
    if (strncmp(_last_command, "AT\r\n", 4) == 0) {
        char resp[] = "ERROR\r\n";
        if (data_size < sizeof(resp) - 1) {
            log_err("Data buffer is too small {%s} {%s}", _last_command, resp);
            return -1;
        }
        strcpy(data, resp);
        *bytes_read = strlen(resp);
        return 0;
    }

    if (strncmp(_last_command, "AT+CPBR=1\r\n", 11) == 0) {
        char resp[] = "+CPBR: 1,\"79219800469\",129,\"005000520049004D0041005200590020004E0055004D004200450052\"\r\nOK\r\n";
        if (data_size < sizeof(resp) - 1) {
            log_err("Data buffer is too small {%s} {%s}", _last_command, resp);
            return -1;
        }
        strcpy(data, resp);
        *bytes_read = strlen(resp);
        return 0;
    }

    if (strncmp(_last_command, "AT+CPMS?\r\n", 11) == 0) {
        char resp[] = "+CPMS: \"SM\",2,10,\"SM\",2,10,\"SM\",2,10\r\nOK\r\n";
        if (data_size < sizeof(resp) - 1) {
            log_err("Data buffer is too small {%s} {%s}", _last_command, resp);
            return -1;
        }
        strcpy(data, resp);
        *bytes_read = strlen(resp);
        return 0;
    }

    return -1;
}

int com_read(int fd, char *data, int data_size, int timeout, int* bytes_read) {
    (void) fd; // not used
    if (data_size == 0) { // nothing to do
        *bytes_read = 0;
        return 0;
    }

    int res = com_read_impl(fd, data, data_size, timeout, bytes_read);

    data[*bytes_read] = '\0'; // ensure null termination for convenience
    _last_index = 0;
    return res;
}

