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

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>

#ifdef HAVE_SYSLOG
  #include <syslog.h>
#endif

#include "smsf-logging.h"
#include "smsf-util.h"

struct smsf_options _opts = { SMSF_VERSION, LOG_NOTICE, 0 /* SYSLOG */, 0 /*SLOW_READ*/ };

int log_impl(int verbosity, int err_code, const char *err_str, const char *format, ...) {
    static char buf[1024] = {0};
    static char *VB_NAMES[8] = {"EMEGR", "ALERT", "CRIT", "ERR", "WARNING", "NOTICE", "INFO", "DEBUG"};

    int offs = 0;

    if (_opts.verbosity >= verbosity) {
        va_list args;
        va_start(args, format);
        offs += vsnprintf(buf + offs, sizeof(buf) - offs, format, args);
        va_end(args);

        if (err_str != NULL) {
            snprintf(buf + offs, sizeof(buf) - offs, " - %s (%d)", err_str, err_code);
        }

#ifdef HAVE_SYSLOG
        if (_opts.syslog) {
            // No need to set prefix manually for syslog
            syslog(verbosity, "%s", buf);
        }
#endif
        // Used plain stderr to reduce the number of places where we have
        // os specific staff, but
        //   esp_log_write((verbosity == 1) ? ESP_LOG_ERROR : ESP_LOG_INFO), LOG_PREFIX, "%s\r\n", buf);
        // might be better choice.
        fprintf(stderr, "%s[%s]:%s\n", LOG_PREFIX, VB_NAMES[verbosity], buf);
        fflush(stderr);
    }

    // Shorthand that allows to use log_* macro in return statement
    return -1;
}

void dump(const char *ptr, int len) {
    if (_opts.verbosity >= LOG_DEBUG) {
        fwrite(ptr, len, 1, stderr);
    }
}

void dump_as_hex(const char *msg, const uint8_t *ptr, int len) {
    if (_opts.verbosity >= LOG_DEBUG) {
        FILE *fp = stdout;

        fprintf(fp, "======= %s (%d) : =========\n", msg, len);
        for (int i = 0; i < len; i++) {
            fprintf(fp, "%02x ", ptr[i]);
            if (((i+1) % 16) == 0)
              fprintf(fp, "\n");
        }
        fprintf(fp, "\n");
        fflush(fp);
    }
}

void dump_by_line(const char *buf) {
    if (_opts.verbosity >= LOG_DEBUG) {
        int pos = 0;
        const char *line;
        int line_len;

        while(pos != -1) {
            read_line(buf, &pos, &line, &line_len);
            if (line_len > 0) {
                fwrite(line, line_len, 1, stderr);
            }
        }
        fflush(stderr);
    }
}
