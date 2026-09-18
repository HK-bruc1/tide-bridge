#ifdef SUPPORT_MS_EXTENSIONS
#pragma bss_seg(".dev_flow_player.data.bss")
#pragma data_seg(".dev_flow_player.data")
#pragma const_seg(".dev_flow_player.text.const")
#pragma code_seg(".dev_flow_player.text")
#endif
#include "jlstream.h"
#include "app_config.h"
#include "node_uuid.h"
#include "audio_config_def.h"
#include "dev_flow_player.h"
#include "os/os_api.h"

struct dev_flow_player {
    struct jlstream *stream;
    OS_SEM stop_done;
    u8 stopping;
    u8 stopped;
};

static struct dev_flow_player *g_dev_flow_player = NULL;


static void dev_flow_player_callback(void *private_data, int event)
{
    struct dev_flow_player *player = g_dev_flow_player;
    struct jlstream *stream = (struct jlstream *)private_data;

    switch (event) {
    case STREAM_EVENT_START:
        break;
    }
}

/**
 * @brief 自定义数据流播放器 启动
 * 
 * @param pipeline_name 启动的场景，固定为"translation_ear_player"
 * @param source_uuid 播放数据源，固定为NODE_UUID_SOURCE_DEV0
 * @return int 
 */
int dev_flow_player_open(u8 ch_num, u16 source_uuid)
{
    int err;
    struct dev_flow_player *player;

    dev_flow_player_close();

    u16 uuid = ch_num == 2 ? PIPELINE_UUID_TRANSLATION : PIPELINE_UUID_TRANSLATION_MONO;//jlstream_event_notify(STREAM_EVENT_GET_PIPELINE_UUID, (int)"dev_flow");
#if TCFG_RDX_LOCAL_PLAYBACK_ENABLE
    /* SourceDev0 raw Opus is decoded to 48 kHz LR for either file profile. */
    if (source_uuid == NODE_UUID_SOURCE_DEV0) {
        uuid = PIPELINE_UUID_TRANSLATION;
    }
#endif
    if (uuid == 0) {
        return -EFAULT;
    }

    player = zalloc(sizeof(*player));
    if (!player) {
        return -ENOMEM;
    }

    /*
       导入自定义音频流的源节点UUID，如下自定义源节点为NODE_UUID_SOURCE_DEV,
       若是其他源节点，需使用对应节点的UUID，如ADC节点则为NODE_UUID_ADC
     */
    player->stream = jlstream_pipeline_parse(uuid, source_uuid);
    /* player->stream = jlstream_pipeline_parse(uuid, NODE_UUID_SOURCE_DEV1); */

    if (!player->stream) {
        err = -ENOMEM;
        goto __exit0;
    }

    //设置当前数据对应节点的特性
    /* err = jlstream_node_ioctl(player->stream, NODE_UUID_SOURCE, NODE_IOC_SET_PRIV_FMT, arg); */

    void app_audio_init_volume(void);
    app_audio_init_volume();

    jlstream_node_ioctl(player->stream, NODE_UUID_SOURCE, NODE_IOC_SET_PRIV_FMT, ch_num);
    jlstream_set_callback(player->stream, player->stream, dev_flow_player_callback);
    jlstream_set_scene(player->stream, STREAM_SCENE_DEV_FLOW);
    jlstream_set_coexist(player->stream, STREAM_COEXIST_AUTO);
    err = jlstream_start(player->stream);
    if (err) {
        goto __exit1;
    }

    g_dev_flow_player = player;

    return 0;

__exit1:
    jlstream_release(player->stream);
__exit0:
    free(player);
    return err;
}

//自定义数据流播放器 查询是否活动
bool dev_flow_player_runing(void)
{
    return g_dev_flow_player != NULL;
}

static void dev_flow_stop_worker(void *priv)
{
    struct dev_flow_player *player = priv;
    jlstream_stop(player->stream, 50);
    os_sem_post(&player->stop_done);
    while (1) {
        os_time_dly(0xffff);
    }
}

/* Called only by the playback owner (app_core). Keep the player published
 * until close: recording/storage admission must not see a false idle window.
 * The worker stops audio only; release and notifications stay on app_core. */
int dev_flow_player_drain_close(void)
{
    struct dev_flow_player *player = g_dev_flow_player;
    if (!player || player->stopped) {
        return 1;
    }
    if (!player->stopping) {
        if (os_sem_create(&player->stop_done, 0)) {
            return -ENOMEM;
        }
        player->stopping = 1;
        if (os_task_create(dev_flow_stop_worker, player, 1, 1024, 0, "pb_close")) {
            player->stopping = 0;
            os_sem_del(&player->stop_done, 0);
            return -ENOMEM;
        }
    }
    if (os_sem_query(&player->stop_done) > 0) {
        player->stopped = 1;
        return 1;
    }
    return 0;
}


/**
 * @brief 自定义数据流播放器 关闭
 * 
 */
void dev_flow_player_close(void)
{
    struct dev_flow_player *player = g_dev_flow_player;

    if (!player) {
        return;
    }
    if (player->stopping) {
        /* Explicit stop/pause/switch may arrive during natural EOF cleanup.
         * Join before release, never stop the same stream concurrently. */
        os_sem_pend(&player->stop_done, 0);
        os_task_del("pb_close");
        os_sem_del(&player->stop_done, 0);
    } else {
        jlstream_stop(player->stream, 50);
    }
    jlstream_release(player->stream);

    free(player);
    g_dev_flow_player = NULL;

    jlstream_event_notify(STREAM_EVENT_CLOSE_PLAYER, (int)"dev_flow");
}
