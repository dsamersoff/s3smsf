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

#ifndef _SMSF_LOGGING_H_
#define _SMSF_LOGGING_H_

#include <errno.h>
#include <stdint.h>
#include <string.h>

#ifdef HAVE_SYSLOG
  #include <syslog.h>
#endif

#ifdef ESP_PLATFORM
#include "esp_log.h"
#endif

#define SMSF_VERSION 0x2012

/**
 * @brief Library options.
 */
struct smsf_options {
    int version;      //! binary version
    int verbosity;    //! report errors 0 - silent, 1 - error, 2 - noise, 3 -debug
    int syslog;       //! print messages to syslog if syslog is available
    int slow_read;    //! Use AT+CMGR=<id> instead of AT+CMGL=4 to read messages
};

#ifndef HAVE_SYSLOG
    // Align LOG_LEVELS with SYSLOG,
    // but not all levels used
    #define LOG_EMERG   0   /* system is unusable */
    #define LOG_ALERT   1   /* action must be taken immediately */
    #define LOG_CRIT    2   /* critical conditions */
    #define LOG_ERR     3   /* error conditions */
    #define LOG_WARNING 4   /* warning conditions */
    #define LOG_NOTICE  5   /* normal but significant condition */
    #define LOG_INFO    6   /* informational */
    #define LOG_DEBUG   7   /* debug-level messages */
#endif

#define LOG_PREFIX "smsf: "

/* Convience error checking */
#define CHECK(a) if ((a) == -1) { return -1; }
#define CHECK0(var, a) { var = (a); if (var != 0) { return var; } }

/* Logging */
#define log_write(fmt, args...)  log_impl(LOG_EMERG, 0, NULL, fmt, ##args)
#define log_err(fmt, args...)    log_impl(LOG_ERR, 0, NULL, fmt, ##args)
#define log_errno(fmt, args...)  log_impl(LOG_ERR, errno, strerror(errno), fmt, ##args)
#define log_info(fmt, args...)  log_impl(LOG_INFO, 0, NULL, fmt, ##args)
#define log_noise(fmt, args...)  log_impl(LOG_NOTICE, 0, NULL, fmt, ##args)
#define log_debug(fmt, args...)  log_impl(LOG_DEBUG, 0, NULL, fmt, ##args)

#define assert_ret(cond, fmt, args...) { if (!(cond)){ log_impl(LOG_ERR, 0, NULL, "%s:%d " fmt, __FILE__, __LINE__, ##args); return -1; }}

int log_impl(int should_log, int err_code, const char *err_str, const char *format, ...);

void dump(const char *ptr, int len);
void dump_as_hex(const char *msg, const uint8_t *ptr, int len);
void dump_by_line(const char *ptr);

#endif
