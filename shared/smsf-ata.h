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

#ifndef _SMSF_ATA_H
#define _SMSF_ATA_H

#include "smsf-pdu.h"

 // https://wiki.iarduino.ru/page/a6_gprs_at/

 // Send AT to check response
 int ata_ping(int fd);
 int ata_echo(int fd, int onoff);
 // Warning! Running AT+COPS=2 will puth the nepwork to FPLMN (i.e. BAN list)
 int ata_cops(int fd, int mode, const char *network);
 int ata_clear_FPLNM(int fd);

 // Syn clock from the network
 int ata_sync_clock(int fd);

 // Send AT+CCALR?
 int ata_ready(int fd);
 int ata_network_status(int fd);
 int ata_power_status(int fd);

 // Send AT+COPS?
 int ata_op_info(int fd, char *info, int info_len);
 int ata_op_list(int fd); // for debugging only

 int ata_get_clock(int fd, char *info, int info_size);

 //
 int ata_set_pdu_mode(int fd);
 int ata_set_cset_UCS2(int fd);

 // Send/Read SMS
 int ata_send_message(int fd, const char *number, struct sms_message *msg);
 int ata_send_message_multipart(int fd, const char *number, struct sms_message *msg);

 int ata_msg_count(int fd, int *msgs_to_read);

 int ata_read_message(int fd, int msg_no, struct sms_message *msg);

 int ata_read_all_messages_fast(int fd, struct sms_message *msgs, int max_messages, int *msg_count);
 int ata_read_all_messages_slow(int fd, struct sms_message *msgs, int max_messages, int *msg_count);
 int ata_read_all_messages(int fd, struct sms_message *msgs, int max_messages, int *msg_count);

 int ata_delete_message(int fd, int msg_no);
 int ata_delete_all_messages(int fd);

 int ata_write_contact(int fd, int num, const char *name, const char *phone); // -1 mean first free slot
 int ata_read_contact(int fd, int num, char *name, int name_size, char *phone, int phone_size);

#endif