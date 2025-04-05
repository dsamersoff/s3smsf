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

#ifndef _SMSF_UTIL_H
#define _SMSF_UTIL_H

#include <stdint.h>
#include <time.h>

#define MIN(x, y) (((x) < (y)) ? (x) : (y))

void ui_to_str(unsigned int num, char *str);
void ui_to_hex(unsigned int num, char *str);

int bin2hex(const unsigned char *bin, int bi_len, char *hex);
int hex2bin(const char *hex, int hex_len, unsigned char *bin);


/**
 * @brief Read buffer by line, starting with pos.
 *
 * @param buf - null-terminated buffer to read
 * @param pos - current read pos
 * @param line - pointer to start of line, null termination is not guaranteed
 * @param line_len - len of line, without \r and \n
 */
void read_line(const char *buf, int *pos, const char **line, int *line_len);

/**
 * @brief Skip part of line before first ", then copy bytes under next quote and stop.
 *
 * @param dest - buffer to copy
 * @param dest_size - capacity of the buffer
 * @param src  - source, might be not null-terminated
 * @param src_len - length of source
 * @return int - number of bytes consumed
 */
int copy_quoted(char *dest, int dest_size, const char *src, int src_len);

/**
 * @brief src16 for SMS hashing
 *
 * @param data
 * @param len
 * @return int16_t
 */

int16_t crc16(const char* data, int len);

/**
 * @brief Convert ISO 8601 ts to unix ts
 *        TimeZone is not respected
 *
 * @param iso_time - iso timestamp e.g. "2024-03-04T12:34:56Z+3"
 * @return long - Unix timestamp (integer seconds since the epoch).
 */

time_t iso2time(const char *iso_time);

/**
 * @brief Convert GSM ts to unix ts
 *        TimeZone is not respected
 *
 * @param gsm_time - iso timestamp e.g. "25/04/01,20:42:13+12"
 * @return long - Unix timestamp (integer seconds since the epoch).
 */

time_t gsm2time(const char *gsm_time);

#endif