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
#include <string.h>
#include <time.h>


#include "smsf-util.h"

static void reverse_str(char *str, int len) {
    int start = 0, end = len - 1;
    while (start < end) {
        char temp = str[start];
        str[start] = str[end];
        str[end] = temp;
        start++;
        end--;
    }
}

// Function to convert a non-negative integer to a string
void ui_to_str(unsigned int num, char *str) {
    int i = 0;

    // Handle zero case
    if (num == 0) {
        str[i++] = '0';
        str[i] = 0;
        return;
    }

    // Convert digits to characters (in reverse order)
    while (num > 0) {
        str[i++] = (num % 10) + '0';
        num /= 10;
    }
    str[i] = 0;
    reverse_str(str, i);
}

// Function to convert a non-negative integer to a hex string %02X
void ui_to_hex(unsigned int num, char *str) {
    int i = 0;

    // Handle zero case
    if (num == 0) {
        str[i++] = '0';
        str[i++] = '0';
        str[i] = 0;
        return;
    }

    // Convert digits to hex characters (in reverse order)
    while (num > 0) {
        int digit = num % 16;
        str[i++] = (digit < 10) ? (digit + '0') : (digit - 10 + 'A');
        num /= 16;
    }
    if (i < 2) {
        str[i++] = '0';
    }
    str[i] = 0;
    reverse_str(str, i);
}


/* Convert hex string to bin array, return the number of converted bytes */
int bin2hex(const unsigned char *bin, int bi_len, char *hex) {
    const char * hex_digs = "0123456789ABCDEF";
    int hi = 0, bi = 0;
    while(bi < bi_len) {
        hex[hi] = hex_digs[(bin[bi] >> 4) & 0xf];
        hex[hi+1] = hex_digs[bin[bi]& 0xf];
        bi ++;
        hi += 2;
    }
    return hi;
}

/* Convert hex string to bin array, return the number of converted bytes */
int hex2bin(const char *hex, int hex_len, unsigned char *bin) {
    int hi = 0, bi = 0;
    while(hi < hex_len) {
        unsigned char c1 = (hex[hi] | 0x20);
        unsigned char c2 = (hex[hi+1] | 0x20);
        if (c1 == 0 || c2 == 0) {
           break;
        }
        c1 = (c1 >= '0' && c1 <= '9') ? c1 - '0' : \
               (c1 >= 'a' && c1 <= 'f') ? c1 - 'a' + 10 : c1;
        c2 = (c2 >= '0' && c2 <= '9') ? c2 - '0' : \
               (c2 >= 'a' && c2 <= 'f') ? c2 - 'a' + 10 : c2;
        if (c1 > 16 || c2 > 16) {
           break;
        }
        bin[bi] = (c1 << 4) | c2;
        bi ++;
        hi += 2;
    }
    return hi;
}

void read_line(const char *buf, int *pos, const char **line, int *line_len) {
    const char *p = buf +(*pos);
    const char *s = strchr(p, '\n');

    // End of buffer reached
    if (s == NULL) {
        *line = p;
        *line_len = s - p;
        *pos = -1;
        return;
    }

    *line = p;
    *line_len = s - p;
    *pos = (s - buf) + 1;
}

int copy_quoted(char *dest, int dest_size, const char *src, int src_len) {
    const char *s = src;
    char *d = dest;
    int skip = 1;
    const char *src_e = src + src_len;
    char *dest_e = dest + dest_size - 1;

    while(1) {
        if (*s == '\"') {
            if (!skip) { // closing quote, bail out
                ++s;
                *d = 0;
                break;
            }
            skip = !skip; // open quote don't skip
            ++s;
            continue;
        }

        if (*s != '\"') {
            if (!skip) {
                *d = *s;
                ++d;
            }
            ++s;
        }

        if (s == src_e || d == dest_e) {
            *d = 0;
            break;
        }
    }
    return s - src;
}

int16_t crc16(const char* data, int len) {
    uint8_t i;
    uint16_t crc = 0xffff;
    while (len--) {
        crc ^= *(unsigned char *)data++ << 8;
        for (i = 0; i < 8; i++) {
            crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1;
        }
    }

    return crc & 0xffff;
}

#if 0
time_t iso2ctime(const char *iso_time) {
    struct tm tm = {0};
    char *ret = strptime(iso_time, "%Y-%m-%dT%H:%M:%SZ%z", &tm); // GNU extension
    if (!ret) {
        return -1;
    }
    return mktime(&tm);
}
#endif

// Function to convert ISO 8601 string to timestamp
// strptime is not portable
time_t iso2time(const char *iso_time) {
    int year, month, day, hour, minute, second;

    // Parse the ISO 8601 formatted string
    if (sscanf(iso_time, "%4d-%2d-%2dT%2d:%2d:%2d", &year, &month, &day, &hour, &minute, &second) != 6) {
        return -1;
    }

    struct tm tm = {0};
    tm.tm_year = year - 1900;
    tm.tm_mon = month - 1;
    tm.tm_mday = day;
    tm.tm_hour = hour;
    tm.tm_min = minute;
    tm.tm_sec = second;

    return mktime(&tm);
}
