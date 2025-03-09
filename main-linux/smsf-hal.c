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

#include "smsf-hal.h"

#define BAUDRATE B115200
#define READ_USLEEP 1000
#define READ_SIZE 1

static int set_com_parameters(int fd) {
    struct termios options;

    tcgetattr(fd, &options);

    cfsetispeed(&options, BAUDRATE);
    cfsetospeed(&options, BAUDRATE);

    options.c_cflag |= (CLOCAL | CREAD);
    options.c_cflag &= ~CSIZE;
    options.c_cflag |= CS8;
    options.c_cflag &= ~PARENB;
    options.c_cflag &= ~CSTOPB;
    options.c_cflag &= ~CRTSCTS;

    options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    options.c_iflag &= ~(IXON | IXOFF | IXANY);
    options.c_iflag &= ~(ICRNL | INLCR);
    options.c_oflag &= ~OPOST;

    tcsetattr(fd, TCSANOW, &options);
    return 0;
}

int com_open(const char *device, int* fd) {
    int lfd = open(device, O_RDWR | O_NOCTTY | O_SYNC);
    if (lfd < 0) {
        return -1;
    }

    if (set_com_parameters(lfd) < 0) {
        close(lfd);
        return -1;
    }

    *fd = lfd;
    return 0;
 }

void com_close(int fd) {
    close(fd);
}

int com_write(int fd, const char *data, int data_size, int* bytes_written) {
    int bw = write(fd, data, data_size);
    if (bw == -1) {
        *bytes_written = 0;
        return -1;
    }
    *bytes_written = bw;
    return (bw == data_size) ? 0 : -1;
}

static int com_read_impl(int fd, char *data, int data_size, int timeout, int* bytes_read) {
    int br = 0;
    *bytes_read = 0;
    memset(data, 0, data_size);
    data_size -= 1; // make a room for \0, decreasing local var - changes will not be saved.

    while(1) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(fd, &read_fds);

        // Set timeout
        struct timeval timeout_tv;
        timeout_tv.tv_sec = timeout;
        timeout_tv.tv_usec = 0;

        int ready = select(fd + 1, &read_fds, NULL, NULL, &timeout_tv);
        if (ready == -1 || ready == 0) { // select error or timeout
            return ready;
        }

        if (FD_ISSET(fd, &read_fds)) {
            // br = read(fd, data + (*bytes_read), data_size - (*bytes_read));
            int ask_size = ((data_size - *bytes_read) < READ_SIZE) ? data_size - *bytes_read : READ_SIZE;
            br = read(fd, data + (*bytes_read), ask_size);
            if (br == -1 || br == 0) { // read error or EOF, bail out
                return br;
            }
            *bytes_read += br;
        }

        if (*bytes_read == data_size) { // All done
            return 0;
        }
        usleep(READ_USLEEP);
    }
}

int com_read(int fd, char *data, int data_size, int timeout, int* bytes_read) {
    if (data_size == 0) { // nothing to do
        *bytes_read = 0;
        return 0;
    }
    int res = com_read_impl(fd, data, data_size - 1, timeout, bytes_read);
    data[*bytes_read] = '\0'; // ensure null termination for convenience
    return res;
}

