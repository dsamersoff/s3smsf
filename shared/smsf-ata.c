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
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>

#include "smsf-logging.h"
#include "smsf-hal.h"
#include "smsf-pdu.h"
#include "smsf-util.h"
#include "smsf-ata.h"

#define TIMEOUT 10
#define CRLF "\r\n"
#define RD_BUF_SIZE 4096

char _rd_buf[RD_BUF_SIZE];

extern struct smsf_options _opts;

/**
 * @brief send AT command to modem, CRLF is added
 *
 * @param fd - descriptor to write to
 * @param str - null-terminated string to write
 * @param ending - extra characters to send
 * @return int - error code, 0 - success, -1 - error
 */
static int send_command(int fd, const char *str, ... ) {
    va_list(args);
    int ds = strlen(str);
    int bw = 0, res = 0;

    log_debug("SENDING C (%d): {{%s}}", ds, str);

    res = com_write(fd, str, ds, &bw);
    if (res == -1 || bw != ds) {
        log_errno("Error sending comand {{%s}}", str);
    }

    va_start(args, str);
    while(1) {
        const char *ending = va_arg(args, char *);
        if (ending == NULL) {
            break;
        }

        log_debug("SENDING E (%d): {{%s}}", (int) strlen(ending), (*ending == '\r') ? "CRLF" : ending);

        res = com_write(fd, ending, strlen(ending), &bw);
        if (res == -1) {
            log_errno("Error sending comand {{%s}}", ending);
        }
    }

    va_end(args);
    return res;
}

// Append CRLF
static int send_command_cr(int fd, const char *str) {
    return send_command(fd, str, CRLF, NULL);
}

// Append ^Z
static int send_command_z(int fd, const char *str) {
    return send_command(fd, str, "\x1A", NULL);
}

//Append digit and CRLF
static int send_command_dig_cr(int fd, const char *str, int dig) {
    char s_dig[6] = {0};
    ui_to_str(dig, s_dig);
    return send_command(fd, str, s_dig, CRLF, NULL);
}

/**
 * @brief Read response from modem
 *
 * @param fd - descriptor to read from
 * @param buf - destination buffer
 * @param buf_size - size of destination buffer
 * @return int - 0 if success, -1 if error occur
 */
static int read_response(int fd, char *buf, int buf_size) {
    int br;
    int res = com_read(fd, buf, buf_size, TIMEOUT, &br);
    if (res == -1) {
        log_errno("Error reading response");
        return -1;
    }

    log_debug("RESPONSE BEGIN (%d):", br);
    dump(_rd_buf, br);
    log_debug("RESPONSE END");

    return res; // ATA error or noise
}

static int read_response_gb(int fd) {
    return read_response(fd, _rd_buf, RD_BUF_SIZE);
}

/**
 * @brief read "OK" from modem
 *
 * @param fd - descriptor to read from
 * @return int - OK found, -1 noise or ERROR
 */
static int read_ok(int fd) {
    CHECK(read_response_gb(fd));
    int pos = 0;
    const char *line;
    int line_len;
    while(pos != -1) {
        read_line(_rd_buf, &pos, &line, &line_len);
        // Check for OK, safe because last char of line is either 0 or \r
        if (*line == 'O' && *(line+1) == 'K') {
            return 0;
        }
    }
    return -1;
}

 // Ping modem
int ata_ping(int fd) {
    CHECK(send_command_cr(fd, "AT"));
    return read_ok(fd);
}

int ata_echo(int fd, int onoff) { // 0 off, 1 on
    CHECK(send_command_dig_cr(fd, "ATE", onoff));
    return read_ok(fd);
}

// Warning! Running AT+COPS=2 will puth the nepwork to FPLMN (i.e. BAN list)
int ata_cops(int fd, int mode, const char *network) {
    if (mode == 1 || mode == 4) {
        char s_mode[6] = {0};
        ui_to_str(mode, s_mode);
        CHECK(send_command(fd, "AT+COPS=", s_mode, ",1,\"", network, "\"", CRLF, NULL));
    }
    else {
        CHECK(send_command_dig_cr(fd, "AT+COPS=", mode));
    }
    return read_ok(fd);
}

int ata_clear_FPLNM(int fd) {
    CHECK(send_command_cr(fd, "AT+CRSM=214,28539,0,0,12,\"FFFFFFFFFFFFFFFFFFFFFFFF\""));
    return read_ok(fd);
}

int ata_sync_clock(int fd) {
 //   Warning, it puts network to the FPLNM (BAN) list so you will need to reconnect manually.
 //   CHECK(send_command_cr(fd, "AT+COPS=2"));
 //   CHECK(read_ok(fd));

    CHECK(send_command_cr(fd, "AT+CLTS=1"));
    CHECK(read_ok(fd));

    CHECK(send_command_cr(fd, "AT+COPS=0"));
    CHECK(read_response_gb(fd));
    return 0;
}

// AT+CCLK?
int ata_get_clock(int fd, char *info, int info_size) {
    CHECK(send_command_cr(fd, "AT+CCLK?"));
    CHECK(read_response_gb(fd));

    int pos = 0;
    const char *line;
    int line_len;
    while(pos != -1) {
        read_line(_rd_buf, &pos, &line, &line_len);
        if (line_len > 6 && (memcmp(line, "+CCLK:", 6) == 0)) {
            copy_quoted(info, info_size, line, line_len);
            return 0;
        }
    }
    return -1;
}

// Send AT+CCALR?
// TODO: parse response and get status
int ata_ready(int fd) {
    CHECK(send_command_cr(fd, "AT+CCALR?"));
    CHECK(read_response_gb(fd));
    return 0;
}

int ata_network_status(int fd) {
    CHECK(send_command_cr(fd, "AT+CREG?"));
    CHECK(read_response_gb(fd));
    return 0;
}

int ata_power_status(int fd) {
    CHECK(send_command_cr(fd, "AT+CBC"));
    CHECK(read_response_gb(fd));
    return 0;
}

 // Send AT+COPS?
 // +COPS: 0,0,"Bee Line GSM"
 int ata_op_info(int fd, char *info, int info_size) {
    CHECK(send_command_cr(fd, "AT+COPS?"));
    CHECK(read_response_gb(fd));

    int pos = 0;
    const char *line;
    int line_len;
    while(pos != -1) {
        read_line(_rd_buf, &pos, &line, &line_len);
        if (line_len > 7 && (memcmp(line, "+COPS:", 6) == 0)) {
            copy_quoted(info, info_size, line, line_len);
            return 0;
        }
    }

    return -1;
}

int ata_op_list(int fd) {
    CHECK(send_command_cr(fd, "AT+COPS=?"));
    CHECK(read_response_gb(fd));
    return 0;
}

int ata_set_pdu_mode(int fd) {
    CHECK(send_command_cr(fd, "AT+CMGF=0")); // Set PDU mode
    return read_ok(fd);
}

int ata_set_cset_UCS2(int fd) {
    CHECK(send_command_cr(fd, "AT+CSCS=\"UCS2\""));
    return read_ok(fd);
}

static int ata_send_message_impl(int fd, struct sms_pdu *spdu) {
    CHECK(send_command_dig_cr(fd, "AT+CMGS=", spdu->len/2)); // Max size here is 255
    // Modem should return > but we don't care. Try to send and check modem error later
    // So only os error is checked here
    CHECK(read_response_gb(fd));
    CHECK(send_command_z(fd, spdu->pdu));
    CHECK(read_response_gb(fd));

    int pos = 0;
    const char *line;
    int line_len;
    while(pos != -1) {
        read_line(_rd_buf, &pos, &line, &line_len);
        if ((line_len > 10 && memcmp(line, "+CMS ERROR", 10) == 0) ||
            (line_len > 5 && memcmp(line, "ERROR", 5) == 0)) {

            log_err("Not able to send message %d {%s}", spdu->len, spdu->pdu);
            dump_by_line(_rd_buf);
            return -1;
        }
    }
    return 0;
}

int ata_send_message(int fd, const char *number, struct sms_message *msg) {
    int res = 0;
    struct sms_pdu *spdu = NULL;
    CHECK(create_pdu(number, msg, &spdu))
    if (spdu->len > 255*2) {
        log_err("PDU length error %d for {%s} {%s}", spdu->len, number, msg->text);
        free(spdu);
        return -1;
    }
    res = ata_send_message_impl(fd, spdu);
    free(spdu);
    return res;
}

int ata_send_message_multipart(int fd, const char *number, struct sms_message *msg) {
    int res = 0;
    struct sms_pdu *spdu = NULL;
    int split_parts = 0;
    CHECK(create_pdu_multipart(number, msg, &spdu, &split_parts))
    for(int i = 0; i < split_parts; ++i) {
        if (spdu[i].len > 255*2) {
            log_err("PDU length error %d for {%s} {%s}", spdu[i].len, number, msg->text);
            free(spdu);
            return -1;
        }
        log_noise("Sending PDU %d {%s}", spdu[i].len, spdu[i].pdu);
        res = ata_send_message_impl(fd, &(spdu[i]));
    }
    free(spdu);
    return res;
}

// Get memory source and number of messages
int ata_msg_count(int fd, int *msgs_to_read) {
    CHECK(send_command_cr(fd, "AT+CPMS?"));
    CHECK(read_response_gb(fd));
    // +CPMS: "SM",3,10,"SM",3,0,"SM",3,10

    int pos = 0;
    const char *line;
    int line_len;
    int res = -1;
    int messages = 0;
    while(pos != -1) {
        read_line(_rd_buf, &pos, &line, &line_len);
        if (line_len > 6 && memcmp(line, "+CPMS:", 6) == 0) {
            const char *s = line;
            while(*s != ',' && s - line < line_len) ++s;
            messages = atoi(s+1);
        }
        if (line_len > 2 && memcmp(line, "OK", 2) == 0) {
            res = 0;
        }
    }

    if (res == 0) {
        *msgs_to_read = messages;
    }
    else {
        dump_by_line(_rd_buf);
    }
    return res;
}

int ata_read_message(int fd, int msg_no, struct sms_message *msg) {
    int res;
    CHECK(send_command_dig_cr(fd, "AT+CMGR=", msg_no)); // Read the message
    CHECK(read_response_gb(fd));

    int pos = 0;
    const char *line;
    int line_len;
    res = -1;
    while(pos != -1) {
        read_line(_rd_buf, &pos, &line, &line_len);
        if (line_len > 6 && memcmp(line, "+CMGR:", 6) == 0) {
            read_line(_rd_buf, &pos, &line, &line_len);
            res = decode_pdu(line, line_len, msg);
            break;
        }
    }

    if (res == -1) {
        dump_by_line(_rd_buf);
    }

    return res;
}

int ata_read_all_messages_fast(int fd, struct sms_message *msgs, int max_messages, int *msg_count) {
    CHECK(send_command_cr(fd, "AT+CMGL=4")); // Read all messages \"ALL\" in text mode
    CHECK(read_response_gb(fd));

    int pos = 0;
    const char *line;
    int line_len;
    int res = 0;
    int i = 0;
    while(pos != -1) {
        read_line(_rd_buf, &pos, &line, &line_len);
        if (line_len > 6 && memcmp(line, "+CMGL:", 6) == 0) {
            read_line(_rd_buf, &pos, &line, &line_len);
            res = decode_pdu(line, line_len, &(msgs[i++]));
            if (res != 0) {
                i -= 1;
                log_debug("Invalid pdu at line %d", i);
                dump(line, line_len);
                continue;
            }
            if (i == max_messages) {
                res = -1;
                break;
            }
        }
    }

    *msg_count = i;
    return res;
}

int ata_read_all_messages_slow(int fd, struct sms_message *msgs, int max_messages, int *msg_count) {
    int res;
    CHECK(ata_msg_count(fd, msg_count));

    if (*msg_count  > 0) {
        int j = 0;
        for(int i = 0; i < *msg_count; ++i) {
            res = ata_read_message(fd, i, &(msgs[j]));
            if (res == -1) {
                log_debug("Error reading message #%d", i);
                continue;
            }
            if (j == max_messages) {
                res = -1;
                break;
            }
            j += 1;
        }
    }
    return 0;
}

int ata_read_all_messages(int fd, struct sms_message *msgs, int max_messages, int *msg_count) {
    if (_opts.slow_read == 1) {
        return ata_read_all_messages_slow(fd, msgs, max_messages, msg_count);
    }
    return ata_read_all_messages_fast(fd, msgs, max_messages, msg_count);
}

int ata_delete_message(int fd, int msg_no) {
    CHECK(send_command_dig_cr(fd, "AT+CMGD=", msg_no)); // Read the message
    return read_ok(fd);
}

int ata_delete_all_messages(int fd) {
    // 4 mean delete all messages, 1 - index, ignored
    CHECK(send_command_cr(fd, "AT+CMGD=1,4"));
    return read_ok(fd);
}

// AT+CPBW=,”6187759088",129,”Adam”
// ATT! Used global buffer _rd_buf
int ata_write_contact(int fd, int num, const char *name, const char *phone) {
    if (num == -1) {
       snprintf(_rd_buf, RD_BUF_SIZE, "AT+CPBW=,\"%s\",129,\"%s\"", phone, name);
    }
    else {
       snprintf(_rd_buf, RD_BUF_SIZE, "AT+CPBW=%d,\"%s\",129,\"%s\"", num, phone, name);
    }
    CHECK(send_command_cr(fd, _rd_buf));
    return read_ok(fd);
}

int ata_read_contact(int fd, int num, char *name, int name_size, char *phone, int phone_size) {
    CHECK(send_command_dig_cr(fd, "AT+CPBR=", num)); // Read the message
    CHECK(read_response_gb(fd));

    int pos = 0;
    const char *line;
    int line_len;
    while(pos != -1) {
        read_line(_rd_buf, &pos, &line, &line_len);
        if (line_len > 7 && (memcmp(line, "+CPBR:", 6) == 0)) {
            int end = copy_quoted(phone, phone_size, line, line_len);
            copy_quoted(name, name_size, line + end + 1, line_len - end - 1);
            return 0;
        }
    }

    return -1;
}