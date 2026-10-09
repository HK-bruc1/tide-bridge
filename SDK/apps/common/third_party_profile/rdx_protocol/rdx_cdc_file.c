#include "rdx_cdc_file.h"
#include "rdx_factory_audio.h"
#include "rdx_dut.h"
#include "usb/device/usb_factory_cdc.h"
#include "system/includes.h"

#if TCFG_T2620_CDC_FILE_ENABLE
#define CDC_FILE_MAX 16777216u
#define CDC_FILE_BLOCK 8192u
#define CDC_FILE_TIMEOUT 110000u
static const u32 cdc_crc_table[256] = {
    0x00000000u, 0x77073096u, 0xee0e612cu, 0x990951bau, 0x076dc419u, 0x706af48fu, 0xe963a535u, 0x9e6495a3u,
    0x0edb8832u, 0x79dcb8a4u, 0xe0d5e91eu, 0x97d2d988u, 0x09b64c2bu, 0x7eb17cbdu, 0xe7b82d07u, 0x90bf1d91u,
    0x1db71064u, 0x6ab020f2u, 0xf3b97148u, 0x84be41deu, 0x1adad47du, 0x6ddde4ebu, 0xf4d4b551u, 0x83d385c7u,
    0x136c9856u, 0x646ba8c0u, 0xfd62f97au, 0x8a65c9ecu, 0x14015c4fu, 0x63066cd9u, 0xfa0f3d63u, 0x8d080df5u,
    0x3b6e20c8u, 0x4c69105eu, 0xd56041e4u, 0xa2677172u, 0x3c03e4d1u, 0x4b04d447u, 0xd20d85fdu, 0xa50ab56bu,
    0x35b5a8fau, 0x42b2986cu, 0xdbbbc9d6u, 0xacbcf940u, 0x32d86ce3u, 0x45df5c75u, 0xdcd60dcfu, 0xabd13d59u,
    0x26d930acu, 0x51de003au, 0xc8d75180u, 0xbfd06116u, 0x21b4f4b5u, 0x56b3c423u, 0xcfba9599u, 0xb8bda50fu,
    0x2802b89eu, 0x5f058808u, 0xc60cd9b2u, 0xb10be924u, 0x2f6f7c87u, 0x58684c11u, 0xc1611dabu, 0xb6662d3du,
    0x76dc4190u, 0x01db7106u, 0x98d220bcu, 0xefd5102au, 0x71b18589u, 0x06b6b51fu, 0x9fbfe4a5u, 0xe8b8d433u,
    0x7807c9a2u, 0x0f00f934u, 0x9609a88eu, 0xe10e9818u, 0x7f6a0dbbu, 0x086d3d2du, 0x91646c97u, 0xe6635c01u,
    0x6b6b51f4u, 0x1c6c6162u, 0x856530d8u, 0xf262004eu, 0x6c0695edu, 0x1b01a57bu, 0x8208f4c1u, 0xf50fc457u,
    0x65b0d9c6u, 0x12b7e950u, 0x8bbeb8eau, 0xfcb9887cu, 0x62dd1ddfu, 0x15da2d49u, 0x8cd37cf3u, 0xfbd44c65u,
    0x4db26158u, 0x3ab551ceu, 0xa3bc0074u, 0xd4bb30e2u, 0x4adfa541u, 0x3dd895d7u, 0xa4d1c46du, 0xd3d6f4fbu,
    0x4369e96au, 0x346ed9fcu, 0xad678846u, 0xda60b8d0u, 0x44042d73u, 0x33031de5u, 0xaa0a4c5fu, 0xdd0d7cc9u,
    0x5005713cu, 0x270241aau, 0xbe0b1010u, 0xc90c2086u, 0x5768b525u, 0x206f85b3u, 0xb966d409u, 0xce61e49fu,
    0x5edef90eu, 0x29d9c998u, 0xb0d09822u, 0xc7d7a8b4u, 0x59b33d17u, 0x2eb40d81u, 0xb7bd5c3bu, 0xc0ba6cadu,
    0xedb88320u, 0x9abfb3b6u, 0x03b6e20cu, 0x74b1d29au, 0xead54739u, 0x9dd277afu, 0x04db2615u, 0x73dc1683u,
    0xe3630b12u, 0x94643b84u, 0x0d6d6a3eu, 0x7a6a5aa8u, 0xe40ecf0bu, 0x9309ff9du, 0x0a00ae27u, 0x7d079eb1u,
    0xf00f9344u, 0x8708a3d2u, 0x1e01f268u, 0x6906c2feu, 0xf762575du, 0x806567cbu, 0x196c3671u, 0x6e6b06e7u,
    0xfed41b76u, 0x89d32be0u, 0x10da7a5au, 0x67dd4accu, 0xf9b9df6fu, 0x8ebeeff9u, 0x17b7be43u, 0x60b08ed5u,
    0xd6d6a3e8u, 0xa1d1937eu, 0x38d8c2c4u, 0x4fdff252u, 0xd1bb67f1u, 0xa6bc5767u, 0x3fb506ddu, 0x48b2364bu,
    0xd80d2bdau, 0xaf0a1b4cu, 0x36034af6u, 0x41047a60u, 0xdf60efc3u, 0xa867df55u, 0x316e8eefu, 0x4669be79u,
    0xcb61b38cu, 0xbc66831au, 0x256fd2a0u, 0x5268e236u, 0xcc0c7795u, 0xbb0b4703u, 0x220216b9u, 0x5505262fu,
    0xc5ba3bbeu, 0xb2bd0b28u, 0x2bb45a92u, 0x5cb36a04u, 0xc2d7ffa7u, 0xb5d0cf31u, 0x2cd99e8bu, 0x5bdeae1du,
    0x9b64c2b0u, 0xec63f226u, 0x756aa39cu, 0x026d930au, 0x9c0906a9u, 0xeb0e363fu, 0x72076785u, 0x05005713u,
    0x95bf4a82u, 0xe2b87a14u, 0x7bb12baeu, 0x0cb61b38u, 0x92d28e9bu, 0xe5d5be0du, 0x7cdcefb7u, 0x0bdbdf21u,
    0x86d3d2d4u, 0xf1d4e242u, 0x68ddb3f8u, 0x1fda836eu, 0x81be16cdu, 0xf6b9265bu, 0x6fb077e1u, 0x18b74777u,
    0x88085ae6u, 0xff0f6a70u, 0x66063bcau, 0x11010b5cu, 0x8f659effu, 0xf862ae69u, 0x616bffd3u, 0x166ccf45u,
    0xa00ae278u, 0xd70dd2eeu, 0x4e048354u, 0x3903b3c2u, 0xa7672661u, 0xd06016f7u, 0x4969474du, 0x3e6e77dbu,
    0xaed16a4au, 0xd9d65adcu, 0x40df0b66u, 0x37d83bf0u, 0xa9bcae53u, 0xdebb9ec5u, 0x47b2cf7fu, 0x30b5ffe9u,
    0xbdbdf21cu, 0xcabac28au, 0x53b39330u, 0x24b4a3a6u, 0xbad03605u, 0xcdd70693u, 0x54de5729u, 0x23d967bfu,
    0xb3667a2eu, 0xc4614ab8u, 0x5d681b02u, 0x2a6f2b94u, 0xb40bbe37u, 0xc30c8ea1u, 0x5a05df1bu, 0x2d02ef8du,
};
static struct {
    u32 generation, id, size, block, offset, crc, since;
    u32 length, sent, payload, next_crc;
    u8 active, terminal, error, deferred;
    u8 frame[CDC_FILE_BLOCK + 24];
} file_stream;
static u32 file_next_id;

u32 rdx_cdc_file_crc(u32 crc, const u8 *bytes, u32 length)
{
    crc ^= 0xffffffffu;
    while (length--) crc = cdc_crc_table[(crc ^ *bytes++) & 255] ^ (crc >> 8);
    return crc ^ 0xffffffffu;
}

static void file_put32(u8 *bytes, u32 value)
{
    for (unsigned index = 0; index < 4; ++index) bytes[index] = value >> (index * 8);
}

static int file_number(const char **cursor, u32 *value)
{
    const char *text = *cursor;
    u32 result = 0;
    if (*text < '0' || *text > '9') return 0;
    do {
        u32 digit = *text++ - '0';
        if (result > (0xffffffffu - digit) / 10) return 0;
        result = result * 10 + digit;
    } while (*text >= '0' && *text <= '9');
    if (*text++ != '#') return 0;
    *cursor = text;
    *value = result;
    return 1;
}

int rdx_cdc_file_active(void)
{
    return file_stream.active;
}

void rdx_cdc_file_session(u32 generation)
{
    if (file_stream.generation != generation) {
        memset(&file_stream, 0, sizeof(file_stream));
        file_stream.generation = generation;
    }
}

static void file_stop(u8 error)
{
    if (file_stream.terminal && file_stream.sent) return;
    file_stream.terminal = 0;
    file_stream.error = error;
    if (!file_stream.sent) file_stream.length = 0;
}

int rdx_cdc_file_command(u32 generation, const char *line, int invalid, char *out, unsigned capacity)
{
    const char *cursor;
    u32 size, block, id;
    rdx_cdc_file_session(generation);
    if (file_stream.active) {
        cursor = line + (strlen(line) >= 17 ? 17 : 0);
        if (!invalid && !strncmp(line, "*APP#file#cancel#", 17) &&
            file_number(&cursor, &id) && !*cursor) {
            if (id == file_stream.id) file_stop(6);
            else if (!file_stream.deferred) file_stream.deferred = 9;
        } else if (!file_stream.deferred) {
            file_stream.deferred = invalid ? 1 : 2;
        }
        out[0] = 0;
        return 1;
    }
    if (strncmp(line, "*APP#file#", 10)) return 0;
    int error = 1;
    if (!invalid && !strcmp(line, "*APP#file#caps#")) {
        snprintf(out, capacity, "*DEV#file#caps#1#8192#16777216#IDLE#\r\n");
        return 1;
    }
    cursor = line + (strlen(line) >= 16 ? 16 : 0);
    if (!invalid && !strncmp(line, "*APP#file#bench#", 16) &&
        file_number(&cursor, &size) && file_number(&cursor, &block) && !*cursor &&
        size && size <= CDC_FILE_MAX &&
        (block == 1024 || block == 2048 || block == 4096 || block == 8192)) {
        if (rdx_dut_is_in_mode() || rdx_factory_audio_owned() || rdx_factory_audio_file_busy() ||
            rdx_factory_audio_tx_pending(generation)) error = 2;
        else if (!generation || file_next_id == 0xffffffffu) error = 8;
        else {
            memset(&file_stream, 0, sizeof(file_stream));
            file_stream.generation = generation;
            file_stream.id = ++file_next_id;
            file_stream.size = size;
            file_stream.block = block;
            file_stream.since = sys_timer_get_ms();
            file_stream.active = 1;
            snprintf(out, capacity, "*DEV#file#accepted#%u#1#\r\n", file_stream.id);
#if TCFG_T2620_FACTORY_USB_CDC_LOG_LEVEL
            printf("[CDC_FILE] bench id=%u size=%u block=%u gen=%u\n", file_stream.id, size, block, generation);
#endif
            return 1;
        }
    } else if (!invalid && !strncmp(line, "*APP#file#get#", 14)) error = 8;
    else if (!invalid && !strncmp(line, "*APP#file#cancel#", 17)) error = 9;
    snprintf(out, capacity, "*DEV#file#error#%d#\r\n", error);
    return 1;
}

static void file_frame(void)
{
    u8 *frame = file_stream.frame;
    u32 payload = 0;
    u8 type = 1;
    if (file_stream.error) {
        type = 3;
        payload = 4;
        file_put32(frame + 20, file_stream.error);
    } else if (file_stream.offset == file_stream.size) {
        type = 2;
        payload = 8;
        file_put32(frame + 20, file_stream.size);
        file_put32(frame + 24, file_stream.crc);
    } else {
        payload = file_stream.size - file_stream.offset;
        if (payload > file_stream.block) payload = file_stream.block;
        for (u32 index = 0; index < payload; ++index) frame[20 + index] = (u8)(file_stream.offset + index);
        file_stream.next_crc = rdx_cdc_file_crc(file_stream.crc, frame + 20, payload);
    }
    memcpy(frame, "CDF1", 4);
    frame[4] = 1;
    frame[5] = type;
    frame[6] = 20;
    frame[7] = 0;
    file_put32(frame + 8, file_stream.id);
    file_put32(frame + 12, file_stream.offset);
    file_put32(frame + 16, payload);
    file_put32(frame + 20 + payload, rdx_cdc_file_crc(0, frame, 20 + payload));
    file_stream.payload = type == 1 ? payload : 0;
    file_stream.terminal = type != 1;
    file_stream.length = payload + 24;
    file_stream.sent = 0;
}

void rdx_cdc_file_service(u32 generation)
{
    rdx_cdc_file_session(generation);
    if (!generation || !file_stream.active) return;
    if ((u32)(sys_timer_get_ms() - file_stream.since) >= CDC_FILE_TIMEOUT) file_stop(7);
    for (unsigned budget = 0; budget < 32; ++budget) {
        if (!file_stream.length) file_frame();
        u32 length = file_stream.length - file_stream.sent;
        if (length > 256) length = 256;
        int result = factory_cdc_stream_send(generation, file_stream.frame + file_stream.sent, length);
        if (result == FACTORY_CDC_FULL) return;
        if (result != FACTORY_CDC_OK) {
            factory_cdc_abort(generation);
            rdx_cdc_file_session(0);
            return;
        }
        file_stream.sent += length;
        if (file_stream.sent != file_stream.length) continue;
        if (file_stream.terminal) {
#if TCFG_T2620_FACTORY_USB_CDC_LOG_LEVEL
            printf("[CDC_FILE] queued id=%u bytes=%u crc=%08x ms=%u error=%u\n", file_stream.id,
                   file_stream.offset, file_stream.crc, (u32)(sys_timer_get_ms() - file_stream.since), file_stream.error);
#endif
            file_stream.active = 0;
            file_stream.length = file_stream.sent = 0;
            return;
        }
        file_stream.offset += file_stream.payload;
        file_stream.crc = file_stream.next_crc;
        file_stream.length = file_stream.sent = 0;
    }
}

int rdx_cdc_file_reply(char *out, unsigned capacity)
{
    if (file_stream.active || !file_stream.deferred) return 0;
    snprintf(out, capacity, "*DEV#file#error#%u#\r\n", file_stream.deferred);
    file_stream.deferred = 0;
    return 1;
}
#endif
