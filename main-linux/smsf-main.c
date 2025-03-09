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
#include <stdarg.h>
#include <unistd.h>
#include <syslog.h>

#include "smsf-logging.h"
#include "smsf-daemon.h"
#include "smsf-hal.h"
#include "smsf-ata.h"
#include "smsf-pdu.h"
#include "smsf-flow.h"

#define PROG_NAME "s3smsf"
#define COM_DEVICE "/dev/ttyUSB0"

extern struct smsf_options _opts;
int _fd = 0;

// TODO
static void send_to_display(const char *format, ...) {
    // PASS
}

static void usage(const char *msg) {
    if (msg != NULL) {
        fprintf(stderr, "Bad command line: %s\n", msg);
    }
    const char help[] = "\n" \
        "s3smsf -a <destination address> - override destination address, default read contact \"PRIMARY NUMBER\"\n" \
        "s3smsf -c <command> - execute one of management commands and exit, e.g. \"++CLEAR\" see documentation\n" \
        "s3smsf -p <port> - modem port device, default /dev/ttyUSB0\n" \
        "s3smsf -v - set verbosity level 3 (ERROR), 7 (DEBUG), default - NOISE\n" \
        "s3smsf -D - daemonize\n" \
        "s3smsf -K - kill running daemon\n" \
        "s3smsf -L - duplicate all messages to syslog\n" \
        "";

    fprintf(stderr, "Usage: %s\n", help);
    exit(7);
}


int main(int argc, char* argv[]) {

    printf("S3SMS forwarder v.%x\n", _opts.version);

    char * o_destaddr = NULL;
    char * o_command = NULL;
    char * o_port = NULL;
    int o_daemonize = 0;
    int o_killrunning = 0;

    int c;
    while ((c = getopt(argc, argv, "a:c:p:v:KLD")) != -1) {
        switch (c) {
            case 'a':
                o_destaddr = strdup(optarg); // Expected memory leaks.
                break;
            case 'c':
                o_command = strdup(optarg);
                break;
            case 'p':
                o_port = strdup(optarg);
                break;
            case 'v':
                _opts.verbosity = atoi(optarg);
                if (_opts.verbosity < LOG_ERR) {
                    usage("Bad verbosity option");
                }
                break;
            case 'K':
                o_killrunning = 1;
                break;
            case 'L':
                _opts.syslog = 1; // Silence some output
                break;
            case 'D':
                o_daemonize = 1;
                break;
            default:
                usage("Invalid arguments");
        }
    }

    // Validate
    if (o_daemonize && o_command != NULL) {
        usage("Can't go background if command execution is requested");
    }

    if (o_port == NULL) {
        o_port = strdup(COM_DEVICE);
    }

    // if (optind == argc) {
    //  usage("filename is required");
    // }

    if (o_killrunning) {
       int res = kill_running(PROG_NAME);
       exit(res);
    }

    if (o_daemonize) {
        daemonize(PROG_NAME);
    }

    if (_opts.syslog) {
#ifdef HAVE_SYSLOG
        openlog(PROG_NAME, LOG_PID, LOG_UUCP);
#else
        log_errno("Syslog disabled during compilation");
        exit(-1);
#endif
    }

    int res = com_open(o_port, &_fd);
    if (res < 0) {
        log_errno("Error open device %s", COM_DEVICE);
        exit(-1);
    }

    if (o_command != NULL) {
        // Execute command and exit
        // if (flow_setup(_fd, (notify_func_t *) send_to_display, o_destaddr) != 0) {
        //   log_err("Flow  {%s}", o_command);
        //    exit(-1);
        // }

        if (proceed_command_message(_fd, o_command) != 1) {
            log_err("Invalid command {%s}", o_command);
            usage(NULL);
        }

        exit(0);
    }

    // Main loop
    while(1) {
        if (flow_setup(_fd, (notify_func_t *) send_to_display, o_destaddr) == 0) {
            while(1) {
                if (flow(_fd, (notify_func_t *) send_to_display) != 0) {
                    break;
                }
            }
        }

        usleep(1000);
    }

    com_close(_fd);
}