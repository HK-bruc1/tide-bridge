#ifdef SUPPORT_MS_EXTENSIONS
#pragma bss_seg(".app_msg.data.bss")
#pragma data_seg(".app_msg.data")
#pragma const_seg(".app_msg.text.const")
#pragma code_seg(".app_msg.text")
#endif
#include "app_msg.h"
#include "key_driver.h"



void app_send_message(int _msg, int arg)
{
    int msg[2];

    msg[0] = _msg;
    msg[1] = arg;
    os_taskq_post_type("app_core", MSG_FROM_APP, 2, msg);
}

void app_send_message2(int _msg, int arg1, int arg2)
{
    int msg[3];

    msg[0] = _msg;
    msg[1] = arg1;
    msg[2] = arg2;
    os_taskq_post_type("app_core", MSG_FROM_APP, 3, msg);
}

void app_send_message_from(int from, int argc, int *msg)
{
    os_taskq_post_type("app_core", from, (argc + 3) / 4, msg);
}

int app_key_event_remap(const struct key_remap_table *table, int *event)
{
    u8 key_value = APP_MSG_KEY_VALUE(event[0]);
    u8 key_action = APP_MSG_KEY_ACTION(event[0]);
    g_printf("%s key_value = %d, key_action = %d\n", __FUNCTION__, key_value, key_action);
    if (table) {
        for (int i = 0; table[i].key_value != 0xff; i++) {
            if (table[i].key_value == key_value) {
                if (table[i].remap_table) {
                    return table[i].remap_table[key_action];
                }
                if (table[i].remap_func) {
                    return table[i].remap_func(event);
                }
                break;
            }
        }
    }

    return APP_MSG_NULL;
}
