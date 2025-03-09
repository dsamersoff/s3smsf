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
#include <stdint.h>

#include "smsf-util.h"
#include "smsf-logging.h"
#include "smsf-pdu.h"

// https://en.wikipedia.org/wiki/GSM_03.40

extern struct smsf_options _opts;

// Convert a phone number to semi-octet format, keep hex representation
static int encode_semi_octets(const char *input, int input_len, unsigned char *output) {
    int i, j;
    for (i = 0, j = 0; i < input_len; i += 2) {
        if (i + 1 < input_len) {
            output[j++] = input[i + 1];
        } else {
            output[j++] = 'F';
        }
        output[j++] = input[i];
    }
    output[j] = 0;
    return j; // output_len
}

// Function to decode a semi-octet encoded phone number
static int decode_semi_octets(const unsigned char* input, int input_len, char* output, int output_size) {
    int j = 0;
    for (int i = 0; i < input_len; ++i) {
        output[j] = '0' + (input[i] & 0x0F);
        output[j + 1] = ((input[i] >> 4) == 0xF) ? 0 : '0' + (input[i] >> 4);
        if (j >= output_size) {
            log_err("decode_semi_octet output truncated to %d", output_size);
            break;
        }
        j += 2;
    }
    output[j] = 0;
    return j; // output_len
}

// Function to encode a message text into 7-bit GSM default alphabet
static int encode_7bit(const char *input, int input_len, unsigned char *output) {
    int bit_offset = 0;

    for (int i = 0; i < input_len; i++) {
        int current_char = input[i] & 0x7F;
        int shift = bit_offset % 8;
        if (shift == 0) {
            output[bit_offset / 8] = current_char;
        } else {
            output[bit_offset / 8] |= current_char << shift;
            output[bit_offset / 8 + 1] = current_char >> (8 - shift);
        }
        bit_offset += 7;
    }
    int output_len = (bit_offset + 7) / 8;
    output[output_len] = 0;
    return output_len;
}

// Function to decode GSM 7-bit encoded string
static int decode_7bit(const unsigned char *input, int input_length, char *output, int output_size) {
    int i = 0, j = 0, shift = 0;
    unsigned char prev = 0;

    while(i < input_length) {
        output[j] = ((input[i] << shift) & 0x7F) | (prev >> (8 - shift));
        prev = input[i];
        shift++;
        if (j >= output_size) {
            log_err("decode_7bit output truncated to %d", output_size);
            break;
        }
        j += 1;
        if (shift == 7) { // flash the accum
            output[j++] = prev >> 1;
            shift = 0;
            prev = 0;
        }
        i += 1;
    }
    output[i] = 0;  // Null-terminate the output string
    return j; // output_len
}

// Function to encode UTF-8 string to UCS2
static int encode_ucs2(const char *input, int input_len, unsigned char *output) {
    int i = 0, j = 0;
    unsigned char c;

    while ((c = input[i]) != 0) {
        if (i >= input_len) { // guard against invalid utf8 input
            break;
        }
        if ((c & 0x80) == 0) {
            // 1-byte UTF-8 character
            output[j++] = 0x00;
            output[j++] = c;
            i++;
        } else if ((c & 0xE0) == 0xC0) {
            // 2-byte UTF-8 character
            unsigned short code_point = ((input[i] & 0x1F) << 6) | (input[i + 1] & 0x3F);
            output[j++] = (code_point >> 8) & 0xFF;
            output[j++] = code_point & 0xFF;
            i += 2;
        } else if ((c & 0xF0) == 0xE0) {
            // 3-byte UTF-8 character
            unsigned short code_point = ((input[i] & 0x0F) << 12) | ((input[i + 1] & 0x3F) << 6) | (input[i + 2] & 0x3F);
            output[j++] = (code_point >> 8) & 0xFF;
            output[j++] = code_point & 0xFF;
            i += 3;
        } else {
            // Unsupported character (outside UCS2 range)
            i++;
        }
    }

    return j;  // output_len
}

// Function to convert UCS2 string to UTF-8 string
static int decode_ucs2(const unsigned char *input, int input_length, char *output, int output_size) {
    int i, j = 0;
    for (i = 0; i < input_length; i += 2) {
        unsigned short code_point = (input[i] << 8) | input[i + 1];

        if (code_point < 0x80) {
            output[j++] = code_point;
        } else if (code_point < 0x800) {
            output[j++] = 0xC0 | (code_point >> 6);
            output[j++] = 0x80 | (code_point & 0x3F);
        } else {
            output[j++] = 0xE0 | (code_point >> 12);
            output[j++] = 0x80 | ((code_point >> 6) & 0x3F);
            output[j++] = 0x80 | (code_point & 0x3F);
        }
        if (j >= output_size) {
            j -= 3; // correct index to avoid overflow, TODO: do it better
            log_err("decode_ucs2 output truncated to %d", output_size);
            break;
        }
    }
    output[j] = 0;  // Null-terminate the UTF-8 output
    return j; // output_len
}

static void decode_ts(const unsigned char *pdu, char* out_ts) {
    char ts[14];
    decode_semi_octets(pdu, 7, ts, 14);

    // Decode timezone
    unsigned int tz = pdu[6];
    int is_neg = tz & ~0xF7; // Extract bit 3 that means negative timezone
    int chunks = tz & 0xF7; // TZ is defines in 15 minutes chunks
    int btz = ((((chunks & 0xF) * 10) + (chunks >> 4)) * 15)/60;

    // YY-MM-DD hh:mm:ss+zzz
    int offs = 0;
    out_ts[offs ++] = '2'; out_ts[offs ++] = '0'; // Must be updated by 2100 y
    out_ts[offs ++] = ts[0];  out_ts[offs ++] = ts[1]; out_ts[offs ++] = '-';
    out_ts[offs ++] = ts[2];  out_ts[offs ++] = ts[3]; out_ts[offs ++] = '-';
    out_ts[offs ++] = ts[4];  out_ts[offs ++] = ts[5]; out_ts[offs ++] = 'T';
    out_ts[offs ++] = ts[6];  out_ts[offs ++] = ts[7]; out_ts[offs ++] = ':';
    out_ts[offs ++] = ts[8];  out_ts[offs ++] = ts[9]; out_ts[offs ++] = ':';
    out_ts[offs ++] = ts[10]; out_ts[offs ++] = ts[11];

    out_ts[offs ++] = 'Z'; out_ts[offs ++] = (is_neg) ? '-' : '+';
    ui_to_str(btz, out_ts+offs);
}

static int need_ucs2(const char *input, int input_len) {
    for(int i = 0; i < input_len; ++i) {
        if ((input[i] & 0x80) != 0) {
            return 1; // Has non- ascii characters, need UCS2 encoding
        }
    }
    return 0; // 7-bit is enough
}

// Function to create a PDU string
int create_pdu(const char *dest_addr, struct sms_message *msg, struct sms_pdu *output) {
    memcpy(output, "0011000B91", 10);
    int offs = 10;

    int ds_len = strlen(dest_addr);
    int text_len = strlen(msg->text);
    int coding = need_ucs2(msg->text, text_len) ? 8 : 0;

   // Validation, bail out if DA is too long but truncate the text
   // TODO: Support multipart messages:
   //      https://en.wikipedia.org/wiki/Concatenated_SMS
    if (ds_len > 12) {
        log_err("Destination address too long %d (should be less than 12)", ds_len);
        return -1;
    }

    //TODO: Make in precise
    const int text_limit = (coding == 8) ? 80 : 160;
    if (text_len > text_limit) {
        log_err("Text is too long for message %d, truncated to %d", text_len, text_limit);
        text_len = text_limit;
    }

    // Encode destination address
    const char *da_tmp = (*dest_addr == '+') ? dest_addr + 1 : dest_addr;
    offs += encode_semi_octets(da_tmp, strlen(da_tmp), (unsigned char*) output + offs);

    ui_to_hex(0, output->pdu + offs); offs += 2; // PID
    ui_to_hex(coding, output->pdu + offs); offs += 2; // coding
    ui_to_hex(0, output->pdu + offs); offs += 2; // TS (default)

    // Max PDU size is 255 char, calc size to not overflow
    // TODO: Support SMS splitting
    unsigned char enc_tmp[text_len * 2];
    int enc_len = 0;
    if (coding == 8 /*ucs2*/) {
        enc_len = encode_ucs2(msg->text, text_len, enc_tmp);
        ui_to_hex(enc_len, output->pdu + offs); offs += 2; // length of the encoded message

    }
    else {
        enc_len = encode_7bit(msg->text, text_len, enc_tmp);
        ui_to_hex(text_len, output->pdu + offs); offs += 2; // length of the original text
    }

    // TODO: check output size
    offs += bin2hex(enc_tmp, enc_len, output->pdu + offs);
    output->pdu[offs] = 0;
    return offs-1; // output len
}

// Function to decode a PDU message
int decode_pdu(const char* pdu, int pdu_len, struct sms_message *msg) {
    // assert_ret((pdu_len & 0x1) == 0, "PDU len %d should be even", pdu_len);
    unsigned char pdu_bin[pdu_len/2];
    hex2bin(pdu, pdu_len, pdu_bin);

    // Initialize fields msg structure
    msg->hash_id = crc16(pdu, pdu_len);
    msg->forwarded = 0;
    msg->split_ref = 0;
    msg->split_parts = 0;
    msg->split_no = 0;

    size_t offs = 0;

    // Message starts with SMSC number, it's optional, but almost always here
    int smsc_len = pdu_bin[offs];
    offs += smsc_len + 1;

    int pdu_header = pdu_bin[offs];
    int msg_type = pdu_header & 0x3;     // Two less significant bits indicate message type, should be 0
    int udhi = (pdu_header >> 6) & 0x1;  // User data has additional header
    offs += 1;

    int sa_len = (pdu_bin[offs++] + 1) / 2; // sender address len in bytes
    int ton = (pdu_bin[offs] >> 4) & 0x7; // type of number
    // int npi = pdu_bin[offs] & 0xF; // numbering plan, ignored for now
    offs += 1;

    switch (ton) {
        case 1: // International number
            *msg->sender = '+';
            decode_semi_octets(pdu_bin + offs, sa_len, msg->sender+1, sizeof(msg->sender) - 1);
            break;
        case 5: // Alpha-numeric sender
            decode_7bit(pdu_bin + offs, sa_len, msg->sender, sizeof(msg->sender));
            break;
        case 4: // Subscriber number, fail through
            decode_semi_octets(pdu_bin + offs, sa_len, msg->sender, sizeof(msg->sender));
            break;
        default: // Unknow
            strncpy(msg->sender,"Unknown", sizeof(msg->sender)); // TODO: zero termination
            break;
    }

    offs += sa_len;
    offs ++; // skip protocol identifier

    int dcs = pdu_bin[offs++]; //data coding scheme, TODO: handle 8 bit used in provisioning messages

    if (dcs >= 4 && dcs <= 7) { // 4,5,6,7 - means 8bit encoding
       log_err("DCS %x (8bit) is not supported", dcs);
       return -1;
    }

    decode_ts(pdu_bin+offs, msg->ts); // time stamp
    offs += 7;
    int data_len = pdu_bin[offs++];

    log_debug("Received message: %x hdr: %x type: %x ton: %x dcs: %x udhi: %x len/sa/da %d/%d/%d", msg->hash_id, pdu_header, msg_type, ton, dcs, udhi, pdu_len, sa_len, data_len);

    char *msg_out = msg->text;
    int msg_out_len = sizeof(msg->text);

    if (udhi == 1) {
        // We currently doen't support message spitting, but able to process data with UDHI
        int udh_len = pdu_bin[offs++];
        if (pdu_bin[offs] == 0) { // Concatenated SMS
            msg->split_ref = pdu_bin[offs + 2]; // Reference number
            msg->split_parts = pdu_bin[offs + 3]; // Total split parts
            msg->split_no = pdu_bin[offs + 4]; // Number of part in split, starting from 1.
        }

        offs += udh_len;
        data_len -= (udh_len+1);
        // In case of 7 bits encoding last byte of UDH contains 7bit of first character.
        if (dcs < 4) {
            *msg_out = pdu_bin[offs++] >> 1;
            msg_out += 1;
            msg_out_len -= 1;
            data_len -= 1;
        }
        log_debug("Received message: hdr: 0x%x UHDI dlen/ulen: %d/%d 0x%X Split: %x/%x", pdu_header, data_len, udh_len, pdu_bin+offs, msg->split_ref, msg->split_no);
    }

    if (dcs < 4 ) { // DCS 0,1,2,3 - means 7bit
         decode_7bit(pdu_bin + offs, data_len, msg_out, msg_out_len);
    } else { // 8,9,10,11 means UCS2
         decode_ucs2(pdu_bin + offs, data_len, msg_out, msg_out_len);
    }

    return 0; // 0 - success, -1 - error
}

int decode_contact(const char *name, int name_len, char *out_name, int out_size) {
    int name_bin_len = name_len/2;
    unsigned char name_tmp[name_len];
    hex2bin(name, name_len, name_tmp);
    return decode_ucs2(name_tmp, name_bin_len, out_name, out_size);
}

#ifdef _PDU_TEST
// TODO: TS parser incorrectly decode timezone
static struct pdu_test _pdu_w_test[] = {
    {"0011000B919712890064F900000008D4F29C0E4ABEA9", {"79219800469", "", "Test IoT"}},
    {"0011000B919712890064F90008002A041F0440043E043204350440043A043000200440044304410441043A043E0433043E00200049006F0054", {"79219800469", "", "Проверка русского IoT"}},
    {"", {"", "", ""}}
};

static struct pdu_test _pdu_r_test[] = {
    {"0791448720003023240DD0E474D81C0EBB010000111011315214000BE474D81C0EBB5DE3771B", {"diafaan", "2011-01-11T13:25:41Z+0", "diafaan.com"}},
    {"07919712690080F8000B919712890064F90000522090022174210CD4F29C0E1287C76B50D109", {"+79219800469", "2025-02-09T20:12:47Z+3", "Test back EN"}},
    {"07919712690080F8040B919712890064F900085220212193332124041F0440043E04320435044"\
        "0043A0430002004410432044F043704380020004D00490058", {"+79219800469", "2025-02-12T12:39:33Z+3", "Проверка связи MIX"}},
    {"07919736799499F8640DD0E272999D76971B000852207212329221370608045C250202002F006D0079006200650"\
        "065002E0070006100670065002E006C0069006E006B002F0074006F007000750070000D000A", {"beeline", "2025-02-27T21:23:29Z+3", "/mybee.page.link/topup\r\n"}},
    {"07919736799499F86409D1D2E910390500085220822155042143060804070B020204350020043E043F0430044104"\
        "35043D0021002004110435044004350433043804420435002004410432043E044E0020043604380437043D044C0021", {"RSCHS", "2025-02-28T12:55:40Z+3", "е опасен! Берегите свою жизнь!"}},
    {"07919712690080F8440B919712890064F9000052303041138521A0050003E10201"\
        "986F79B90D4AC3E7F53688FC66BFE5A0799A0E0AB7CB741668FC76CFCB637A995E9783C2E4343C3D1FA7DD6750999DA6B340F33219447E83CAE9FABCFD268"\
        "3E8E536FC2D07A5DDE334394DAEBBE9A03A1DC40E8BDFF232A84C0791DFECB7BC0C6A87CFEE3028CC4EC7EB6117A84"\
        "A0795DDE936284C06B5D3EE741B642FBBD3E1360B14AFA7E7", {"+79219800469", "2025-03-03T14:31:58Z+3", "Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor incididunt ut labore"\
                                                                                                     " et dolore magna aliqua. Ut enim ad minim veniam, quis"}},
    {"07919712690080F8440B919712890064F9000052303041138521A0050003E10201"\
        "986F79B90D4AC3E7F53688FC66BFE5A0799A0E0AB7CB741668FC76CFCB637A995E9783C2E4343C3D1FA7DD6750999DA6B340F33219447E83CAE9FABCFD268"\
        "3E8E536FC2D07A5DDE334394DAEBBE9A03A1DC40E8BDFF232A84C0791DFECB7BC0C6A87CFEE3028CC4EC7EB6117A84"\
        "A0795DDE936284C06B5D3EE741B642FBBD3E1360B14AFA7E7", {"+79219800469", "2025-03-03T14:31:58Z+3", "Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor incididunt ut labore"\
                                                                                                     " et dolore magna aliqua. Ut enim ad minim veniam, quis"}},
    {"07919712690080F8440B919712890064F900085230300213232150" \
        "050003E20303"\
        "044B0020044104420430044004300435043C0441044F002004380020043F043504470430044204300435043C0020044004300437043D0443044E0020043504400443043D04340443002E"
        , {"+79219800469", "2025-03-03T20:31:32Z+3", "ы стараемся и печатаем разную ерунду."}},
    {"", {"", "", ""}}
};

#define STATUS ((ok) ? "+OK " : "!ERR")

static int print_w_test_results(struct pdu_test *pdut, const char *new_pdu) {
    int ok = 0;

    ok = strcmp(pdut->pdu, new_pdu) == 0 ? 1 : 0;
    printf("%s PDU.pdu: {{%s}} vs {{%s}} \n", STATUS, pdut->pdu, new_pdu);
    return !ok;
}

static int print_r_test_results(struct pdu_test *pdut, const struct sms_message *msg) {
    int ok = 0;
    int errors = 0;

    ok = strcmp(pdut->msg.sender, msg->sender) == 0 ? 1 : 0;
    errors += !ok;
    printf("%s PDU.sender: {{%s}} vs {{%s}}\n", STATUS, pdut->msg.sender, msg->sender);

    ok = strcmp(pdut->msg.ts, msg->ts) == 0 ? 1 : 0;
    errors += !ok;
    printf("%s PDU.ts: {{%s}} vs {{%s}}\n", STATUS, pdut->msg.ts, msg->ts);

    ok = strcmp(pdut->msg.text, msg->text) == 0 ? 1 : 0;
    errors += !ok;
    printf("%s PDU.text: {{%s}} vs {{%s}}\n", STATUS, pdut->msg.text, msg->text);

    return errors;
}
#undef STATUS

int test_pdu() {
    struct sms_message msg;
    char pdu[1024];

    int errors = 0;

    printf("\nTesting Contact decoding.\n");
    char tmp[]="005000520049004D0041005200590020004E0055004D004200450052";
    char decoded[512];
    decode_contact(tmp, strlen(tmp), decoded, sizeof(decoded));
    if (strcmp(decoded, "PRIMARY NUMBER") != 0) {
        printf("Contact decoding error {PRIMARY NUMBER} vs {%s}\n");
        errors += 1;
    }

    printf("\nTesting PDU creation.\n");
    for (int i = 0; _pdu_w_test[i].pdu[0] != 0; ++i) {
        memset(pdu, 0, sizeof(pdu));
        printf("PDU.pdu: {{%s}}\n", _pdu_w_test[i].pdu);
        create_pdu(_pdu_w_test[i].msg.sender, _pdu_w_test[i].msg.text, pdu);
        errors += print_w_test_results(&(_pdu_w_test[i]), pdu);
    }

    printf("\nTesting PDU parsing.\n");
    for (int i = 0; _pdu_r_test[i].pdu[0] != 0; ++i) {
        memset(&msg, 0, sizeof(msg));
        printf("PDU.pdu: {{%s}}\n", _pdu_r_test[i].pdu);
        int res = decode_pdu(_pdu_r_test[i].pdu, strlen(_pdu_r_test[i].pdu), &msg);
        if (res != 0) {
            printf("PDU decoding error.\n");
        }
        errors += print_r_test_results(&(_pdu_r_test[i]), &msg);
    }

    printf("Total results: %d errors\n\n", errors);
    return errors;
}

#endif
