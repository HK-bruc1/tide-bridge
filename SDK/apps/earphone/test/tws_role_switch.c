#include "classic/tws_api.h"
#include "app_msg.h"
#include "system/timer.h"


#if 0

static u16 g_timer = 0;

void tws_role_switch_timer(void *p)
{
    g_timer = 0;
    if (tws_api_get_role() == TWS_ROLE_MASTER) {
        tws_api_role_switch();
    }
}

int tws_role_switch_test_msg_handler(int *msg)
{
    struct tws_event *evt = (struct tws_event *)msg;
    int role = evt->args[0];

    switch (evt->event) {
    case TWS_EVENT_CONNECTED:
    case TWS_EVENT_ROLE_SWITCH:
        if (role == TWS_ROLE_MASTER) {
            if (g_timer == 0) {
                g_timer = sys_timeout_add(NULL, tws_role_switch_timer, 10000);
            } else {
                sys_timer_modify(g_timer, 10000);
            }
        } else {
            if (g_timer) {
                sys_timeout_del(g_timer);
                g_timer = 0;
            }
        }
        break;
    }

    return 0;
}

APP_MSG_HANDLER(tws_role_switch_test_entry) = {
    .owner = 0xff,
    .from = MSG_FROM_TWS,
    .handler = tws_role_switch_test_msg_handler,
};

#endif
