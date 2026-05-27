#include "app_config.h"
#include "cig.h"
#include "wireless_trans_manager.h"


#if (TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_UNICAST_SINK_EN | LE_AUDIO_JL_UNICAST_SINK_EN))

static int cig_perip_init(void *priv)
{
    return cig_perip_ops.init();
}

static int cig_perip_uninit(void *priv)
{
    return cig_perip_ops.uninit();
}

static int cig_perip_open(void *priv)
{
    return cig_perip_ops.open((cig_parameter_t *)priv);
}

static int cig_perip_close(void *priv)
{
    uint8_t cig_hdl = *((uint8_t *)priv);

    return cig_perip_ops.close(cig_hdl);
}

static int cig_perip_ioctrl(int op, ...)
{
    va_list argptr;
    int res = CIG_OPST_SUCC;

    va_start(argptr, op);

    switch (op) {
    case WIRELESS_DEV_OP_SEND_PACKET:
        res = cig_perip_ops.send_packet((const uint8_t *)va_arg(argptr, int), va_arg(argptr, int), (cig_stream_param_t *)va_arg(argptr, int));
        break;

    case WIRELESS_DEV_OP_GET_BLE_CLK:
        cig_perip_ops.get_ble_clk((uint32_t *)va_arg(argptr, uint32_t));
        break;

    case WIRELESS_DEV_OP_GET_TX_SYNC:
        res = cig_perip_ops.get_tx_sync((void *)va_arg(argptr, int));
        break;

    case WIRELESS_DEV_OP_SET_SYNC:
        cig_perip_ops.sync_set((uint16_t)va_arg(argptr, int), (uint8_t)va_arg(argptr, int));
        break;

    case WIRELESS_DEV_OP_GET_SYNC:
        cig_perip_ops.sync_get((uint16_t)va_arg(argptr, int), (uint16_t *)va_arg(argptr, int), (uint32_t *)va_arg(argptr, int), (uint32_t *)va_arg(argptr, int));
        break;

    default :
        break;
    }

    va_end(argptr);

    return res;
}

REGISTER_WIRELESS_DEV(cig_perip_op) = {
    .name           = "cig_perip",
    .init           = cig_perip_init,
    .uninit         = cig_perip_uninit,
    .open           = cig_perip_open,
    .close          = cig_perip_close,
    .ioctrl         = cig_perip_ioctrl,
};

#endif /* LEA_CIG_PERIPHERAL_EN */

