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

#include "smsf-logging.h"
#include "smsf-ata.h"
#include "smsf-util.h"

#include "smsf-flow.h"

#define DA_CONTACT_NAME "PRIMARY NUMBER"
#define DA_CONTACT_NAME_UCS2 "005000520049004D0041005200590020004E0055004D004200450052" // UNICODE version of contact text above

#define SAVED_MESSAGES 32
#define EXPIRE (3 * (3600 * 24)) // 3 Days

extern struct smsf_options _opts;

char _dest_addr[32]; //! Destination phone number
struct sms_message _saved_msgs[SAVED_MESSAGES]; //! List of read messages

static int compare_messages(const struct sms_message *a, const struct sms_message *b) {
    if (a->hash_id == b->hash_id) {
        if (strcmp(a->ts, b->ts) == 0 && strcmp(a->text, b->text) == 0) { // Ignore sender
            return 1;
        }
    }
    return 0;
}

static int find_saved_message(const struct sms_message *msg) {
    // Check hashed slot first
    int idx = msg->hash_id % SAVED_MESSAGES;
    if (compare_messages(msg, &(_saved_msgs[idx]))) {
        log_debug("Found MSG #%d %x: {%s} {%s} vs {%s} {%s}", idx, _saved_msgs[idx].hash_id,\
                                           _saved_msgs[idx].ts, _saved_msgs[idx].text,  msg->ts, msg->text);
        return idx;
    }

    for (int i = 0; i < SAVED_MESSAGES; ++i) {
        if (i != idx && compare_messages(msg, &(_saved_msgs[i]))) {
            log_debug("Found MSG #%d %x: {%s} {%s} vs {%s} {%s}", i, _saved_msgs[i].hash_id,\
                                           _saved_msgs[i].ts, _saved_msgs[i].text,  msg->ts, msg->text);
            return i;
        }
    }

    // Nothing found
    return -1;
}

static int find_free_slot() {
    for (int i = 0; i < SAVED_MESSAGES; ++i) {
        if (_saved_msgs[i].hash_id == 0) {
            return i;
        }
    }
    // Nothing found
    return -1;
}

static void add_saved_message(const struct sms_message *msg) {
    int idx = msg->hash_id % SAVED_MESSAGES;
    if (_saved_msgs[idx].hash_id != 0) { // hashed slot already taken, linear scan for first free one
        idx = find_free_slot(NULL);
    }
    if (idx == -1) {
        log_err("Saved msgs buffer overflow");
        return;
    }
    memcpy(&(_saved_msgs[idx]), msg, sizeof(struct sms_message));
}

static void remove_saved_message(int idx) {
    _saved_msgs[idx].hash_id = 0;
}

static int message_expired(struct sms_message *msg) {
    time_t now = time(0);
    time_t msg_time = iso2time(msg->ts);

    return (now - msg_time) > EXPIRE; // 3 Days
}

static int forward_message(int device, struct sms_message *msg, notify_func_t *notify) {
    int res = 0;
    struct  sms_message nmsg;

    // Compact TS: 2025-02-28T12:55:40Z+3 => 02-28 12:55
    memcpy(nmsg.ts, msg->ts + 5, 11);
    nmsg.ts[5] = ' '; nmsg.ts[11] = 0;

    res = snprintf(nmsg.text, sizeof(nmsg.text), "%s %s %s", msg->text,  nmsg.ts, msg->sender);

    log_noise("Sending: %s (%d/%d)", nmsg.text, res, sizeof(nmsg.text));
    res = ata_send_message(device, _dest_addr, &nmsg);
    if (res == -1) {
        log_err("Can't send message id#%x to %s", msg->hash_id, _dest_addr);
        notify("Forward error %x", msg->hash_id);
    }
    else {
        log_debug("Forwarded message id#%x to %s", msg->hash_id, _dest_addr);
        notify("Forwarded %x", msg->hash_id);
    }
    return res;
}

static int delete_message(int device, int msg_no, notify_func_t *notify) {
    int res = 0;
    res = ata_delete_message(device, msg_no);
    if (res == -1) {
        log_err("Can't delete message #%d", msg_no);
        notify("Delete error #%d", msg_no);
    }
    else {
        log_debug("Deleted message #%d", msg_no);
        notify("Deleted #%d", msg_no);
    }
    return res;
}

// Return 1 if it's a command message 0 - otherwise
int proceed_command_message(int device, const char *text) {
    if (*text != '+' && *(text+1) != '+') {
        // Not a command message
        return 0;
    }

    log_noise("Processing command message {%s}", text);

    if (strcmp(text, "++CLEAR") == 0) {
        // Delete all messages from SIM
        ata_delete_all_messages(device);
        return 1;
    }

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

    if (strcmp(text, "++CONTACTS") == 0) {
        // Dump all contacts from SIM to console
        // Bail out on error
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

    if (strcmp(text, "++SAVED") == 0) {
        // Dump all messages from hash table to console
        for (int i = 0; i < SAVED_MESSAGES; ++i) {
            log_write("Message #%d (%x): From: %s TS: %s {%s}", i, \
                 _saved_msgs[i].hash_id, _saved_msgs[i].sender, _saved_msgs[i].ts, _saved_msgs[i].text);
        }
        return 1;
    }

    if (strncmp(text, "++LOG", 5) == 0) {
        // Set verbosity, ++LOG 7 enables debug output
        int level = atoi(text + 6);
        if (level > 0) {
            _opts.verbosity = level;
            log_write("Verbosity set to {%d} by SMS", level);
        }
        return 1;
    }

    return 0;
}

int flow_setup(int device, notify_func_t *notify, const char *da_override) {

    // Turn off echo and check modem is alive
    if (ata_echo(device, 0) != 0) {
        log_err("Modem error, can't set echo mode");
        notify("Modem error");
        return -1;
    }

   //  if (ata_set_cset_UCS2(device) !=0) {
   //     log_err("Modem error, can't set UCS2 mode");
   //     notify("Modem error");
   //     return -1;
   // }

    // Load destination address
    memset(_dest_addr, 0, sizeof(_dest_addr));

    if (da_override == NULL) {
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
                strncpy(_dest_addr, phone, sizeof(_dest_addr) - 2);
                break;
            }
        }
    }
    else {
        strncpy(_dest_addr, da_override, sizeof(_dest_addr) - 2);
    }

    if (_dest_addr[0] == 0) {
        // Destination address is not set. Bail out.
        return -1;
    }

    log_info("Forward set to phone: %s", _dest_addr);
    notify(_dest_addr);

    if (ata_set_pdu_mode(device) != 0) {
        log_err("Modem error, can't set PDU mode");
        notify("Modem error");
        return -1;
    }

    // Check and display connection status
    char info[64];
    if (ata_op_info(device, info, sizeof(info)) != 0) {
        log_err("Connection info reading error");
        notify("No connection");
        return -1;
    }

    log_info("Connected to: %s", info);
    notify(info);

    return 0;
}

int flow(int device, notify_func_t *notify) {
    int n_msgs = 0;

    // ata_delete_all_messages(device);

    // Read the messages one by one and remove any messages we already saw
    // or expired.
    // Do this in a separate cycle because new messages may have arrived
    // while we were sending the message and it mess up slots

    if (ata_msg_count(device, &n_msgs) == 0) {
        log_debug("Found %d messages to consider", n_msgs);
        notify("Messages: %-3d", n_msgs);

        struct sms_message msg;

        for (int i = 1; i < n_msgs+1; ++i) {
            // Read messages one by one
            if (ata_read_message(device, i, &msg) != 0) {
                // Ignore message reading error
                log_debug("Message #%d reading error", i);
                continue;
            }

            log_debug("Found message #%d (%x): From: %s TS: %s {%s}", i, msg.hash_id, msg.sender, msg.ts, msg.text);

            int idx = find_saved_message(&msg);

            // 1. Message was not seen before
            if (idx == -1) {
                log_noise("Received new message #%d (%d): From: {%s} TS: {%s} {%s}", i, msg.split_no, msg.sender, msg.ts, msg.text);

                // Ignore + on both ends
                char *ask_sender = (*msg.sender == '+') ? msg.sender + 1 : msg.sender;
                char *req_sender = (*_dest_addr == '+') ? _dest_addr + 1 : _dest_addr;

                if (strcmp(ask_sender, req_sender) == 0) {
                    // Message come from DA_CONTACT_NAME, no reason to forward.
                    // But it could be a command message.
                    // Command message can affect SMS list, so re-read after processing command.
                    if (proceed_command_message(device, msg.text) == 1) {
                        // It was recognised command message, don't forward
                        msg.forwarded = 1;
                    }
                }

                // Non-processed messages from DA will be forwarded as usual
                // Only first part of splitted message will be forwarded
                if (msg.forwarded == 0 && msg.split_no <= 1) {
                    if (forward_message(device, &msg, notify) == 0) {
                        msg.forwarded = 1;
                    }
                }

                add_saved_message(&msg);
                continue;
            }

            // 2. Message was seen before
            if (idx != -1) {
                // 2.0 Message was not forwarded
                if (_saved_msgs[idx].forwarded == 0 && _saved_msgs[idx].split_no <= 1) {
                    if (forward_message(device, &msg, notify) == 0) {
                        _saved_msgs[idx].forwarded = 1;
                    }
                    continue;
                }

                // 2.1 Message was forwarded or it's parts of multipart message
                if (_saved_msgs[idx].forwarded == 1 || _saved_msgs[idx].split_no > 1) {
                    log_noise("Deleting %s message #%d (%d/%d): From: %s TS: %s {%s}",
                        (_saved_msgs[idx].split_no > 1) ? "SPARE PART" : "FORWARDED", i, _saved_msgs[idx].split_no, _saved_msgs[idx].split_parts,
                        _saved_msgs[idx].sender, _saved_msgs[idx].ts, _saved_msgs[idx].text);

                    if (delete_message(device, i, notify) == 0) {
                        // Remove message from seen list only if it's successfully deleted
                        remove_saved_message(idx);
                    }
                    continue;
                }

                // 2.2 Message expired
                if (message_expired(&(_saved_msgs[idx]))) {
                    log_noise("Deleting expired message #%d: From: %s TS: %s {%s}", i, _saved_msgs[idx].sender, _saved_msgs[idx].ts, _saved_msgs[idx].text);
                    if (delete_message(device, i, notify) == 0) {
                        // Remove message from seen list only if it's successfully deleted
                        remove_saved_message(idx);
                    }
                    continue;
                }
            }
        }
    }
    return 0;
}
