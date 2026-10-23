#include "rdx_factory_cdc.h"
#include "usb/device/usb_factory_cdc.h"
#include "system/includes.h"
#include "rdx_app.h"
#include "rdx_vm.h"
#include "rdx_app_config.h"
#include "rdx_dut.h"
#include "rdx_factory_audio.h"
#include "rdx_cdc_file.h"
#include "rdx_cdc_source.h"

#if TCFG_T2620_FACTORY_USB_CDC_ENABLE && !TCFG_T2620_FACTORY_USB_CDC_TEST_ENABLE
extern char *rdx_ble_server_get_local_name(void);
extern int le_controller_get_mac(u8 *addr);

/* usb_stack only copies whole packets. app_core owns framing and responses.
 * Four 64-byte packets (288 bytes including metadata), a 129-byte line and
 * one 258-byte response: bounded independently of the host's input rate.
 * File streaming adds coalesced app_core wakeups; the timer is fallback. */
#define FACTORY_RX_DEPTH 4
#define FACTORY_RX_PACKET 64
#define FACTORY_LINE_MAX 128
#define FACTORY_RESPONSE_TIMEOUT 2000
struct factory_packet {
    u32 generation;
    u8 length, offset;
    u8 bytes[FACTORY_RX_PACKET];
};
static struct factory_packet factory_mailbox[FACTORY_RX_DEPTH];
static u8 factory_head, factory_count;
static struct {
    volatile u32 generation;
    u32 response_since;
    volatile u16 timer;
    u16 length, response_length;
    u8 cr, error;
    char line[FACTORY_LINE_MAX + 1];
    char response[FACTORY_CDC_TX_MAX];
} factory_protocol;

static volatile u8 factory_event_posted, factory_pending, factory_initialized;
static void factory_service(void *priv);
static void factory_event(void *priv)
{
    local_irq_disable();
    factory_event_posted = 0;
    local_irq_enable();
    factory_service(NULL);
}

static void factory_wake(void)
{
    int message[] = { (int)factory_event, 1, 0 };
    local_irq_disable();
    factory_pending = 1;
    if (factory_initialized && !factory_event_posted) {
        factory_event_posted = 1;
        if (os_taskq_post_type("app_core", Q_CALLBACK, 3, message)) factory_event_posted = 0;
    }
    local_irq_enable();
}

#if TCFG_T2620_CDC_FILE_ENABLE
static void factory_tx_wake(void)
{
    if (rdx_cdc_file_active()) factory_wake();
}
#endif

void rdx_factory_cdc_wake(void)
{
    factory_wake();
}

static int factory_receive(u32 generation, const u8 *bytes, u32 length)
{
    if (!length || length > FACTORY_RX_PACKET) return -1;
    local_irq_disable();
    if (factory_cdc_generation() != generation || !generation) {
        local_irq_enable();
        return -1;
    }
    /* A new session can replace old queued packets, never a current packet. */
    if (factory_count && factory_mailbox[factory_head].generation != generation) {
        factory_head = factory_count = 0;
    }
    if (factory_count == FACTORY_RX_DEPTH) {
        local_irq_enable();
        return -1;
    }
    struct factory_packet *packet = &factory_mailbox[(factory_head + factory_count) % FACTORY_RX_DEPTH];
    packet->generation = generation;
    packet->length = length;
    packet->offset = 0;
    memcpy(packet->bytes, bytes, length);
    ++factory_count;
    local_irq_enable();
    factory_wake();
    return 0;
}

static int factory_pop_byte(u32 generation, u8 *byte)
{
    int found = 0;
    local_irq_disable();
    if (factory_cdc_generation() != generation) {
        local_irq_enable();
        return 0;
    }
    while (factory_count) {
        struct factory_packet *packet = &factory_mailbox[factory_head];
        if (packet->generation == generation) {
            *byte = packet->bytes[packet->offset++];
            found = 1;
        }
        if (packet->generation != generation || packet->offset == packet->length) {
            factory_head = (factory_head + 1) % FACTORY_RX_DEPTH;
            --factory_count;
        }
        if (found) break;
    }
    local_irq_enable();
    return found;
}

static int factory_safe_field(const char *text, unsigned int maximum)
{
    if (!text || !*text) return 0;
    for (unsigned int i = 0; i <= maximum; ++i) {
        unsigned char c = text[i];
        if (!c) return i <= maximum;
        if (c < 0x20 || c > 0x7e || c == ';' || c == '=') return 0;
    }
    return 0;
}

static const char *factory_info(void)
{
    u8 addr[6];
    char auth[25];
    int nonzero = 0, all_ff = 1, any_addr = 0;
    if (!rdx_app_initialized()) return "ERR STATE\r\n";
    const char *name = rdx_ble_server_get_local_name();
    const u8 *key = rdx_vm_get_auth_info()->AuthKey;
    if (!factory_safe_field(name, 24) || !factory_safe_field(FIRMWARE_VERSION, 32)) {
        return "ERR DATA\r\n";
    }
    for (int i = 0; i < 24; ++i) nonzero |= key[i];
    for (int i = 0; i < 24; ++i) {
        u8 c = nonzero ? key[i] : '0';
        if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))) {
            return "ERR DATA\r\n";
        }
        auth[i] = c;
    }
    auth[24] = 0;
    if (le_controller_get_mac(addr)) return "ERR STATE\r\n";
    for (int i = 0; i < 6; ++i) {
        any_addr |= addr[i];
        all_ff &= addr[i] == 0xff;
    }
    if (!any_addr || all_ff) return "ERR DATA\r\n";
    snprintf(factory_protocol.response, sizeof(factory_protocol.response),
             "OK PROTO=1;NAME=%s;AUTH=%s;ADDR=%02X%02X%02X%02X%02X%02X;FW=%s\r\n",
             name, auth, addr[5], addr[4], addr[3], addr[2], addr[1], addr[0], FIRMWARE_VERSION);
    return NULL;
}

static const char *factory_custom_command(char *name)
{
    char *separator = strchr(name, '#');
    if (!separator || separator == name) return "ERR ARG\r\n";
    *separator = 0;
    if (strncmp(name, "FT_", 3)) return "ERR CMD\r\n";
    for (const char *cursor = name; *cursor; ++cursor) {
        if (!((*cursor >= 'A' && *cursor <= 'Z') ||
              (*cursor >= '0' && *cursor <= '9') || *cursor == '_'))
            return "ERR ARG\r\n";
    }
    const char *value = separator + 1;
    u8 enabled;
    if (!strcmp(name, "FT_KEY_LAYOUT")) {
        if (value[0]) return "ERR ARG\r\n";
        enabled = 1;
    } else if (!value[0] && (!strcmp(name, "FT_FINALPACK_END") ||
                             !strcmp(name, "FT_KEY_DUT_ENABLE"))) {
        enabled = 1;
    } else if ((value[0] == '0' || value[0] == '1') && value[1] == '#' && !value[2]) {
        enabled = value[0] - '0';
    } else {
        return "ERR ARG\r\n";
    }
    rdx_dut_cdc_request(factory_protocol.generation, name, enabled,
                        factory_protocol.response, sizeof(factory_protocol.response));
    return NULL;
}

static void factory_dispatch(void)
{
    const char *reply = "ERR CMD\r\n";
    char *line = factory_protocol.line;
    line[factory_protocol.length] = 0;
#if TCFG_T2620_FACTORY_USB_CDC_LOG_LEVEL >= 1
    if (factory_protocol.error) {
        printf("[CDC_RX] FRAME_ERROR=%s\n", factory_protocol.error == 2 ? "OVERFLOW" : "INVALID");
    } else {
        printf("[CDC_RX] %s\n", line);
    }
#endif
#if TCFG_T2620_CDC_FILE_ENABLE
    if (rdx_cdc_file_command(factory_protocol.generation, line, factory_protocol.error,
                             factory_protocol.response, sizeof(factory_protocol.response))) {
        factory_protocol.response_length = strlen(factory_protocol.response);
        factory_protocol.response_since = sys_timer_get_ms();
        factory_protocol.length = factory_protocol.cr = factory_protocol.error = 0;
        return;
    }
#endif
    /* Keywords and action names are case insensitive; values stay strict. */
    for (u16 i = 0; i < factory_protocol.length; ++i) {
        if (line[i] >= 'a' && line[i] <= 'z') line[i] -= 'a' - 'A';
    }
    if (factory_protocol.error) {
        reply = factory_protocol.error == 2 ? "ERR ARG\r\n" : "ERR CMD\r\n";
    } else if (!strcmp(line, "PING")) {
        reply = "OK PONG\r\n";
    } else if (!strcmp(line, "GET_INFO")) {
        reply = factory_info();
    } else if (!strcmp(line, "GET_STATUS") || !strcmp(line, "GET_KEY_STATUS")) {
        rdx_dut_cdc_status(factory_protocol.response, sizeof(factory_protocol.response), !strcmp(line, "GET_KEY_STATUS"));
        reply = NULL;
    } else if (rdx_factory_audio_command(factory_protocol.generation, line,
                 factory_protocol.response, sizeof(factory_protocol.response))) {
        reply = NULL;
    } else if (!strncmp(line, "*APP#CUSTOM#", 12)) {
        reply = factory_custom_command(line + 12);
    }
    if (reply) strcpy(factory_protocol.response, reply);
    factory_protocol.response_length = strlen(factory_protocol.response);
    factory_protocol.response_since = sys_timer_get_ms();
    factory_protocol.length = factory_protocol.cr = factory_protocol.error = 0;
}

static void factory_byte(u8 byte)
{
    if (factory_protocol.cr && byte == '\n') {
        factory_dispatch();
        return;
    }
    if (factory_protocol.cr && !factory_protocol.error) factory_protocol.error = 1;
    factory_protocol.cr = byte == '\r';
    if (factory_protocol.error || factory_protocol.cr) return;
    /* Invalid character takes precedence over overflow at the same byte. */
    if (byte < 0x20 || byte > 0x7e) factory_protocol.error = 1;
    else if (factory_protocol.length == FACTORY_LINE_MAX) factory_protocol.error = 2;
    else factory_protocol.line[factory_protocol.length++] = byte;
}

static void factory_service(void *priv)
{
    if (!factory_initialized) return;
    local_irq_disable();
    factory_pending = 0;
    local_irq_enable();
    /* At most 256 bytes and four responses per tick, including error frames. */
    unsigned int bytes = 0, responses = 0;
    u32 generation = factory_cdc_generation();
    if (generation) {
        if (!factory_protocol.timer) {
            factory_protocol.timer = sys_timer_add(NULL, factory_service, 10);
        }
    } else if (factory_protocol.timer) {
        sys_timer_del(factory_protocol.timer);
        factory_protocol.timer = 0;
    }
    if (generation != factory_protocol.generation) {
        factory_protocol.generation = generation;
        factory_protocol.length = factory_protocol.response_length = 0;
        factory_protocol.cr = factory_protocol.error = 0;
        rdx_dut_service_wake();
    }
    rdx_factory_audio_service(generation);
#if TCFG_T2620_CDC_FILE_ENABLE
    rdx_cdc_file_session(generation);
#endif
    if (!generation) {
        local_irq_disable();
        if (!factory_cdc_generation()) factory_head = factory_count = 0;
        local_irq_enable();
        return;
    }
    while (bytes < 256 && responses < 4) {
        if (factory_cdc_generation() != generation) return;
        if (factory_protocol.response_length) {
            int result = factory_cdc_send(generation, (const u8 *)factory_protocol.response,
                                          factory_protocol.response_length);
            if (result != FACTORY_CDC_OK) {
                if (result != FACTORY_CDC_FULL ||
                    (u32)(sys_timer_get_ms() - factory_protocol.response_since) >= FACTORY_RESPONSE_TIMEOUT) {
                    factory_cdc_abort(generation);
                }
                return;
            }
            factory_protocol.response_length = 0;
            ++responses;
        } else {
            u8 byte;
            if (!factory_pop_byte(generation, &byte)) {
#if TCFG_T2620_CDC_FILE_ENABLE
                rdx_cdc_file_service(generation);
                if (rdx_cdc_file_reply(factory_protocol.response, sizeof(factory_protocol.response))) {
                    factory_protocol.response_length = strlen(factory_protocol.response);
                    factory_protocol.response_since = sys_timer_get_ms();
                    continue;
                }
                if (rdx_cdc_file_active()) return;
#endif
                rdx_factory_audio_send(generation);
                return;
            }
            ++bytes;
            factory_byte(byte);
        }
    }
#if TCFG_T2620_CDC_FILE_ENABLE
    if (!factory_protocol.response_length) {
        rdx_cdc_file_service(generation);
        if (rdx_cdc_file_reply(factory_protocol.response, sizeof(factory_protocol.response))) {
            factory_protocol.response_length = strlen(factory_protocol.response);
            factory_protocol.response_since = sys_timer_get_ms();
        }
    }
#endif
}

void rdx_factory_cdc_init(void)
{
    factory_initialized = 1;
    factory_cdc_set_rx_handler(factory_receive);
    factory_cdc_set_state_handler(factory_wake);
#if TCFG_T2620_CDC_FILE_ENABLE
    factory_cdc_set_tx_handler(factory_tx_wake);
#endif
    factory_wake();
}

int rdx_factory_cdc_needs_poll(void)
{
    if (!factory_initialized || factory_event_posted) return 0;
    u32 generation = factory_cdc_generation();
    int cleanup = rdx_factory_audio_needs_service();
#if TCFG_T2620_CDC_FILE_ENABLE
    cleanup |= rdx_cdc_source_busy() && rdx_cdc_source_closed() != 2;
#endif
    /* 只读状态，供优先级 0 恢复入口判断；不得在此执行协议或文件操作。 */
    return factory_pending || generation != factory_protocol.generation ||
           !!generation != !!factory_protocol.timer || (!generation && cleanup);
}

void rdx_factory_cdc_poll(void)
{
    if (rdx_factory_cdc_needs_poll()) factory_service(NULL);
}
#endif
