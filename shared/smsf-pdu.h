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

#ifndef _SMSF_PDU_H
#define _SMSF_PDU_H

// maximum number of bytes SMS can contain
#define MSG_TEXT_LIMIT 140

// the size of extra header (sender + TS) we append to message on forwarding
#define FORWARD_HEADER_SIZE 34

struct sms_message {
   char sender[14];
   char ts[24];
   uint16_t hash_id;
   uint8_t forwarded;
   uint8_t split_ref;
   uint8_t split_parts;
   uint8_t split_no;
   uint16_t text_size;
   char text[];        // Must be the last item, the size may vary and it's handled at malloc time
};

// Ensure that all pdu creation routines operate on the same size buffer
struct sms_pdu {
   char pdu[512]; // PDU can't exceed 2*255
   int len;
};

int create_pdu(const char* dest_addr, struct sms_message *msg, struct sms_pdu** output_pdu);
int create_pdu_multipart(const char *dest_addr, struct sms_message *msg, struct sms_pdu **output, int *parts);

int decode_pdu(const char *pdu,  int pdu_len, struct sms_message *msg);
int decode_contact(const char *name, int name_len, char *out_name, int out_size);

#ifdef _PDU_TEST
 int test_pdu();
#endif

#endif

