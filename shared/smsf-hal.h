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

#ifndef _SMSF_HAL_H
#define _SMSF_HAL_H

/**
 * @brief open comm port and set required communication parameters
 *
 * @param device - name of device to open
 * @param fd  - output file descriptor of open port
 * @return int - 0 - success, -1 - errors
 */
int com_open(const char *device, int* fd);

/**
 * @brief close com port, no error check is performed
 *
 * @param fd - file descriptor
 */
void com_close(int fd);

/**
 * @brief write data to com port
 *
 * @param fd  - descriptor to write
 * @param data - data to write
 * @param data_size - size of data to write
 * @param timeout - time to block writing
 * @param bytes_written - bytes actually written
 * @return int - 0 - success, -1 - errors
 */
int com_write(int fd, const char *data, int data_size, int *bytes_written);

/**
 * @brief read data from com port
 *
 * @param fd  - descriptor to read
 * @param data - data buffer to read to
 * @param data_size - size of data buffer
 * @param timeout - time to block readin
 * @param bytes_written - bytes actually written
 * @return int - 0 - success, -1 - errors
 */

int com_read(int fd, char *data, int data_size, int timeout, int *bytes_read);

#endif