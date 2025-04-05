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
#include <stdlib.h>

#include "smsf-hal.h"

#include "smsf-logging.h"
#include "smsf-ata.h"
#include "smsf-util.h"

#include "smsf-flow.h"

#define DA_CONTACT_NAME "PRIMARY NUMBER"
#define DA_CONTACT_NAME_UCS2 "005000520049004D0041005200590020004E0055004D004200450052" // UNICODE version of contact text above

#define SAVED_MESSAGES 32
#define EXPIRE (1 * (3600 * 24)) // 1 Day

extern struct smsf_options _opts;

char _dest_addr[32]; //! Destination phone number
struct sms_message *_saved_msgs[SAVED_MESSAGES]; //! List of read messages
time_t _latest_msg_time;

extern inline void fence();

static struct sms_message *new_msg(int text_size, const struct sms_message *tpl) {
    struct sms_message* msg = malloc(sizeof(struct sms_message) + text_size);
    if (msg == NULL) {
        log_err("Can't allocate %d bytes for message", sizeof(struct sms_message) + text_size);
        abort();   // This error is not recoverable, so abort the program. TODO: recover
        return NULL;
    }
    // Copy message header from template
    if (tpl != NULL) {
        memcpy(msg, tpl, sizeof(struct sms_message));
    }
    msg->text[0] = 0;
    msg->text_size = text_size;
    return msg;
};

static int compare_messages(const struct sms_message *a, const struct sms_message *b) {
    if (a->hash_id == b->hash_id) {
        if (a->split_ref == b->split_ref &&
            a->split_no == b->split_no &&
            a->split_parts == b->split_parts &&
            strcmp(a->ts, b->ts) == 0 &&
            strcmp(a->sender, b->sender) == 0) {
            return 1;
        }
    }

    return 0;
}

static int find_saved_message(const struct sms_message *msg) {
    // Check hashed slot first
    int idx = msg->hash_id % SAVED_MESSAGES;
    if (_saved_msgs[idx] != NULL && compare_messages(msg, _saved_msgs[idx])) {
        log_debug("Found MSG #%d %x: {%s} {%s} vs {%s} {%s}", idx, _saved_msgs[idx]->hash_id,\
                                           _saved_msgs[idx]->ts, _saved_msgs[idx]->text,  msg->ts, msg->text);
        return idx;
    }

    for (int i = 0; i < SAVED_MESSAGES; ++i) {
        if (i != idx && _saved_msgs[i] != NULL && compare_messages(msg, _saved_msgs[i])) {
            log_debug("Found MSG #%d %x: {%s} {%s} vs {%s} {%s}", i, _saved_msgs[i]->hash_id,\
                                           _saved_msgs[i]->ts, _saved_msgs[i]->text,  msg->ts, msg->text);
            return i;
        }
    }

    // Nothing found
    return -1;
}

static int find_free_slot() {
    for (int i = 0; i < SAVED_MESSAGES; ++i) {
        if (_saved_msgs[i] == NULL) {
            return i;
        }
    }
    // Nothing found
    return -1;
}

static void add_saved_message(struct sms_message *msg) {
    int idx = msg->hash_id % SAVED_MESSAGES;
    if (_saved_msgs[idx] != NULL) { // hashed slot already taken, linear scan for first free one
        idx = find_free_slot(NULL);
    }
    if (idx == -1) {
        log_err("Saved msgs buffer overflow");
        return;
    }
    _saved_msgs[idx] = msg;
}

static void remove_saved_message(int idx) {
    free(_saved_msgs[idx]);
    _saved_msgs[idx] = NULL;
}

static int slot_taken(int idx) {
    return _saved_msgs[idx] != NULL;
}

// Expire messages based on relative time, i.e. delta between oldest and newest message
static int message_expired(int device, struct sms_message *msg) {
    time_t msg_time = iso2time(msg->ts);
    time_t delta = _latest_msg_time - msg_time;

    if (delta > EXPIRE) {
        log_err("Message EXPIRED: %s {%s} %ld %ld - %ld", msg->sender, msg->ts, (long) msg_time, (long) _latest_msg_time,  (long) delta);
    }
    else {
        log_noise("Message actual: %s {%s} %ld %ld - %ld", msg->sender, msg->ts, (long) msg_time, (long) _latest_msg_time,  (long) delta);
    }

    if (_latest_msg_time < msg_time) {
        _latest_msg_time = msg_time;
    }

    return (delta > EXPIRE);
}

static int forward_message(int device, struct sms_message *msg, notify_func_t *notify) {
    int res = 0;
    if (! _opts.forward) {
        log_err("Forwarding disabled, all SMS is kept until expires");
        return -1;
    }

    // Add extra header and send message
    // If we are in multipart mode, gives priority to security and put the header in front of the other text.
    // If we are in truncate i.e. money-saving mode, append the header to the message - it will be shown only if
    //    the original message is short enough
    // TS is compacted, 2025-02-28T12:55:40Z+3 => 02-28T12:55
    int sender_len = strlen(msg->sender);
    int offs = 0;
    struct sms_message* eh_msg = new_msg(msg->text_size + sender_len + 14 /* extra header */, msg);

    if (_opts.multipart) {
        memcpy(eh_msg->text + offs, msg->sender, sender_len); offs += sender_len;
        *(eh_msg->text + offs) = ' '; offs += 1;
        memcpy(eh_msg->text + offs, msg->ts + 5, 11); offs += 11;
        *(eh_msg->text + offs) = ' '; offs += 1;
        memcpy(eh_msg->text + offs, msg->text, msg->text_size); offs += msg->text_size;
        *(eh_msg->text + offs) = 0;

        log_noise("Sending message (multipart): %s {%s}", eh_msg->sender, eh_msg->text);
        res = ata_send_message_multipart(device, _dest_addr, eh_msg);
    }
    else {
        memcpy(eh_msg->text, msg->text, msg->text_size); offs += msg->text_size;
        *(eh_msg->text + offs) = ' ';
        memcpy(eh_msg->text + offs, msg->sender, sender_len); offs += sender_len;
        memcpy(eh_msg->text + offs, msg->ts + 5, 11); offs += 11;
        *(eh_msg->text + offs) = 0;

        log_noise("Sending message (truncate): %s {%s}", eh_msg->sender, eh_msg->text);
        res = ata_send_message(device, _dest_addr, eh_msg);
    }

    notify((res != 0) ? "Forward error %s" : "Forwarded %s", msg->sender);
    free(eh_msg);

    return res;
}

static int delete_message(int device, int msg_no, notify_func_t *notify) {
    int res = 0;
    // Force overrides the option to be able to delete
    // command and expired messages ever if the user decided to keep forwarded ones
    if (!_opts.may_delete) {
        log_err("Message deletion is forbidden");
        return -1;
    }

    res = ata_delete_message(device, msg_no);
    if (res == -1) {
        log_err("Can't delete message #%d", msg_no);
    }
    else {
        log_debug("Deleted message #%d", msg_no);
        notify("Deleted #%d", msg_no);
    }
    return res;
}

// Set option, check for errors but ignore it
static void set_option(const char *name, int *option, int new_val, int max_val) {
    if (new_val < 0 || new_val > max_val) {
        log_err("Invalid value for %s (%d)", name, new_val);
        return;
    }

    *option = new_val;
    fence();
    log_write("Option %s set to %d by command SMS", name, new_val);
}

// Return 1 if it's a command message 0 - otherwise
int process_command_message(int device, const char *text) {
    if (*text != '+' && *(text+1) != '+') {
        // Not a command message
        return 0;
    }

    log_noise("Processing command message {%s} {%c}", text, text[2]);

    // Command messages
    // No guards for user errors, format must be ++<UPPERCASE COMMAND>\s<DIGIT>
    // Fence required after modification of global variable, but ESP get stuck on gcc built-in fences
    switch(text[2]) {
        case 'C': {
            if (strcmp(text, "++CLEAR") == 0) {
                // Delete all messages from SIM
                ata_delete_all_messages(device);
                return 1;
            }

            if (strcmp(text, "++CONTACTS") == 0) {
                // Dump first 25 contacts from SIM to console
                ata_set_cset_UCS2(device);

                for (int i = 1; i < 25; ++i) {
                    char name[128], phone[20];
                    if (ata_read_contact(device, i, name, sizeof(name), phone, sizeof(phone)) != 0) {
                        break;
                    }
                    char out_name[128];
                    decode_contact(name, strlen(name), out_name, sizeof(out_name));
                    log_write("Contact #%d Name: {%s} {%s} Phone: {%s}", i, out_name, name, phone);
                }
                return 1;
            }
            break;
        }
        case 'D': {
            if (strcmp(text, "++DUMP") == 0) {
                // Dump all messages from SIM to console
                static struct sms_message msgs[25];
                int n_msgs = 0;

                ata_read_all_messages(device, msgs, 10, &n_msgs);
                log_write("Found %d messages (SM)", n_msgs);

                for (int i = 0; i < n_msgs; ++i) {
                    log_write("Message #%d (%x): From: %s TS: %s {%s}", i, msgs[i].hash_id, msgs[i].sender, msgs[i].ts, msgs[i].text);
                }
                return 1;
            }

            if (strncmp(text, "++DELETE", 8) == 0) {
                // Enable/Disable deletion of received messages
                set_option("MAY_DELETE", &_opts.may_delete, atoi(text + 9), 1);
                return 1;
            }

            break;
        }
        case 'E': {
            if (strncmp(text, "++EXPIRE", 8) == 0) {
                // Enable/Disable multipart support
                set_option("EXPIRE", &_opts.expire, atoi(text + 9), 1);
                return 1;
            }
            break;
        }
        case 'F': {
            if (strncmp(text, "++FORWARD", 9) == 0) {
                // Enable/Disable multipart support
                set_option("FORWARD", &_opts.forward, atoi(text + 10), 1);
                return 1;
            }
            break;
        }
        case 'H': {
            if (strncmp(text, "++HEADER", 8) == 0) {
                // Enable/Disable additional header
                set_option("HEADER", &_opts.header, atoi(text + 9), 1);
                return 1;
            }
            break;
        }
        case 'L': {
            if (strncmp(text, "++LOG", 5) == 0) {
                // Set verbosity, ++LOG 7 enables debug output
                set_option("VERBOSITY", &_opts.verbosity, atoi(text + 6), 9);
                return 1;
            }
            break;
        }
        case 'M': {
            if (strncmp(text, "++MULTIPART", 11) == 0) {
                // Enable/Disable multipart support
                set_option("MULTIPART", &_opts.multipart, atoi(text + 12), 1);
                return 1;
            }
            break;
        }
        case 'S': {
            if (strcmp(text, "++SAVED") == 0) {
                // Dump all messages from hash table to console
                for (int i = 0; i < SAVED_MESSAGES; ++i) {
                    log_write("Message #%d (%x): From: %s TS: %s {%s}", i, \
                    _saved_msgs[i]->hash_id, _saved_msgs[i]->sender, _saved_msgs[i]->ts, _saved_msgs[i]->text);
                }
                return 1;
            }
            break;
        }
    }

    return 0;
}

int process_multipart_message(int device, const struct sms_message *msg,  notify_func_t *notify) {
    // Walk through cache and ensure, that all parts are available
    // Build L2 cache
    int parts_found = 0;
    int total_length = 0;
    struct sms_message *msgs_as[msg->split_parts];

    for (int j = 0; j < SAVED_MESSAGES; ++j) {
        if (slot_taken(j)) {
            if (_saved_msgs[j]->split_ref == msg->split_ref && _saved_msgs[j]->split_parts == msg->split_parts) {
                parts_found += 1;
                total_length += strlen(_saved_msgs[j]->text);
                msgs_as[_saved_msgs[j]->split_no - 1] = _saved_msgs[j];
            }
        }
    }

    // Not all parts already arrived
    if (parts_found != msg->split_parts) {
       log_debug("Not all messages has arrived: (%x %d/%d) From: %s TS: %s {%s}", msg->split_ref, msg->split_no, msg->split_parts, msg->sender, msg->ts, msg->text);
       return -1;
    }

    // Walk through L2 cache and extract text from parts,
    // Mark collected message as forwarded to delete it on the next cycle
    struct sms_message* mp_msg = new_msg(total_length + 1 /* trailing zero */, msg);
    int offs = 0;
    for (int j = 0; j < msg->split_parts; ++j) {
        log_debug("Extracting text from multipart message #%d: (%x %d/%d) From: %s TS: %s {%s}", j, msgs_as[j]->split_ref, msgs_as[j]->split_no, msgs_as[j]->split_parts, msgs_as[j]->sender, msgs_as[j]->ts, msgs_as[j]->text);
        strcpy(mp_msg->text + offs, msgs_as[j]->text);
        offs += strlen(msgs_as[j]->text);
    }
    mp_msg->text[offs] = 0;

    log_noise("Forwarding multipart message From: %s TS: %s {%s}", mp_msg->sender, mp_msg->ts, mp_msg->text);

    int res = forward_message(device, mp_msg, notify);
    if (res == 0) {
        // Long message sent successfully, mark all part as forwarded
        for (int j = 0; j < msg->split_parts; ++j) {
            msgs_as[j]->forwarded = 1;
        }
    }

    free(mp_msg);
    return res;
}

int flow_setup(int device, notify_func_t *notify, const char *da_override) {

    _latest_msg_time = 0;

    // Turn off echo and check modem is alive
    if (ata_echo(device, 0) != 0) {
        log_err("Modem error, can't set echo mode");
        notify("Modem error");
        return -1;
    }

    if (_opts.verbosity >= LOG_DEBUG) {
        ata_power_status(device);
        ata_network_status(device);
        //  ata_op_list(device);

        // Troubleshooting network registration issues
        //  ata_clear_FPLNM(device);
        //  ata_cops(device, 1, "Beeline"); // register manual mode
    }

    // Sync clock from network, not reliable
    // log_warn("Requesting clock from the network");
    // if (ata_sync_clock(device) != 0) {
    //    log_err("Modem error, can't sync clock from the network");
    // }

    if (ata_set_pdu_mode(device) != 0) {
        log_err("Modem error, can't set PDU mode");
    }

    // Check and display connection status
    char info[64] = {0};
    if (ata_op_info(device, info, sizeof(info)) != 0 || *info == 0) {
        log_err("Connection info reading error");
        notify("No connection");
        return -1;
    }

    log_warn("Connected to: %s", info);
    notify(info);

    // Load destination address
    memset(_dest_addr, 0, sizeof(_dest_addr));

    if (da_override == NULL) {
        // Forward number is not provided, read it from SIM card
        for (int i = 1; i < 10; ++i) {
            char name[64], phone[14];
            if (ata_read_contact(device, i, name, sizeof(name), phone, sizeof(phone)) != 0) {
                log_err("Contact #%d reading error", i);
                break;
            }

            log_noise("Contact #%d Name: {%s} Phone: {%s}", i, name, phone);
            // Huawei modem uses UCS2 and bin2hex for contact names.
            // We need the only contact, so no reason to decode.
            if (strcmp(name, DA_CONTACT_NAME) == 0 || strcmp(name, DA_CONTACT_NAME_UCS2) == 0) {
                const char *s_phone = (*phone == '+') ? phone + 1 : phone;
                strncpy(_dest_addr, s_phone, sizeof(_dest_addr) - 2);
                break;
            }
        }
    }
    else {
        const char *s_phone = (*da_override == '+') ? da_override + 1 : da_override;
        strncpy(_dest_addr, s_phone, sizeof(_dest_addr) - 2);
    }

    if (_dest_addr[0] == 0) {
        // Destination address is not set. Bail out.
        return -1;
    }

    log_warn("Forward set to phone: %s", _dest_addr);
    notify(_dest_addr);

    return 0;
}

int flow(int device, notify_func_t *notify) {
    int n_msgs = 0;

    if (ata_msg_count(device, &n_msgs) == 0) {
        if (n_msgs > 0) {
           notify("Messages: %-4d", n_msgs);
        }

        // Re-check connection status
        char info[64] = {0};
        if (ata_op_info(device, info, sizeof(info)) != 0) {
            log_err("Connection info reading error");
            return -1;
        }
        log_noise("Connected to: %s messages %d", info, n_msgs);

        // Using soft expire instead
        //
        //  ata_get_clock(device, info, sizeof(info));
        //  _today = gsm2time(info);
        //  log_noise("Read GSM time as {%s} (%ld)", info, (long) _today);

        for (int i = 1; i < n_msgs+1; ++i) {
            // Read messages one by one
            struct sms_message* msg = new_msg(MSG_TEXT_LIMIT + 1, NULL /* no template*/);

            if (ata_read_message(device, i, msg) != 0) {
                // Ignore message reading error
                log_err("Message #%d reading error", i); // Report error, delete bad message
                free(msg);
                continue;
            }

            log_debug("Found message #%d (%x): From: %s TS: %s {%s}", i, msg->hash_id, msg->sender, msg->ts, msg->text);

            int idx = find_saved_message(msg);

            // 1. Message was not seen before
            if (idx == -1) {
                log_noise("Received new message #%d (%d/%d): From: {%s} TS: {%s} {%s}", i, msg->split_no, msg->split_parts, msg->sender, msg->ts, msg->text);

                // Ignore leading "+""
                char *s_sender = (*msg->sender == '+') ? msg->sender + 1 : msg->sender;

                if (strcmp(s_sender, _dest_addr) == 0) {
                    // Message come from DA_CONTACT_NAME, it could be a command message.
                    // Command message can affect SMS list, so re-read after processing command.
                    if (process_command_message(device, msg->text) == 1) {
                        // It was recognised command message, don't forward
                        // Command message may alter message sequence, so can't delete it immediately
                        msg->forwarded = 1;
                    }
                }

                // Non-processed messages from DA will be forwarded as usual
                // Multipart messages are not forwarded but saved for further processing
                if (msg->forwarded == 0 && msg->split_no == 0) {
                    if (forward_message(device, msg, notify) == 0) {
                        msg->forwarded = 1;
                    }
                }

                if (msg->forwarded == 0 && msg->split_no != 0) {
                    log_noise("Saving multipart message #%d: (%x %d/%d) From: %s TS: %s {%s}", i, msg->split_ref, msg->split_no, msg->split_parts, msg->sender, msg->ts, msg->text);
                }

                add_saved_message(msg);
                continue;
            }

            // 2. Message was seen before
            if (idx != -1) {
                struct sms_message *c_msg = _saved_msgs[idx]; // shortcut

                // 2.0 Message expired
                if (message_expired(device, c_msg)) {
                    log_noise("Deleting expired message #%d: From: %s TS: %s {%s}", i, c_msg->sender, c_msg->ts, c_msg->text);
                    if (delete_message(device, i, notify) == 0) {
                        // Remove message from seen list only if it's successfully deleted
                        remove_saved_message(idx);
                    }
                    continue;
                }

                // 2.1 Message was not forwarded and is not a part of multipart message
                if (c_msg->forwarded == 0 && c_msg->split_no == 0) {
                    if (forward_message(device, msg, notify) == 0) {
                        c_msg->forwarded = 1;
                    }
                    continue;
                }

                // 2.1 Message was already forwarded
                if (c_msg->forwarded == 1) {
                    log_noise("Deleting forwarded message #%d: From: %s TS: %s {%s}", i, c_msg->sender, c_msg->ts, c_msg->text);
                    if (delete_message(device, i, notify) == 0) {
                        // Remove message from seen list only if it's successfully deleted
                        remove_saved_message(idx);
                    }
                    continue;
                }

                // 2.3 Message is the last part of multipart messages
                // All previous parts shall be already cached, forwarded messages are already handled
                if (c_msg->split_no > 0 && c_msg->split_no == c_msg->split_parts) {
                    log_noise("Found last part of multipart message #%d: (%x %d/%d) From: %s TS: %s {%s}", i, c_msg->split_ref, c_msg->split_no, c_msg->split_parts, c_msg->sender, c_msg->ts, c_msg->text);
                    process_multipart_message (device, c_msg, notify);
                }
            }
        } // End For
    }

    return 0;
}
