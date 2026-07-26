#include "test_minimal.h"
#include "rdx_record_service.h"
#include "rdx_record.h"

#include <string.h>

static RecordStatus g_status;
static int g_status_available = 1;

RecordStatus *rdx_record_get_status(void)
{
    return g_status_available ? &g_status : NULL;
}

static int test_activity_mapping(void)
{
    rdx_record_activity_t activity;

    g_status.run = RECORD_STATE_STOP;
    TEST_ASSERT(rdx_record_service_get_activity(&activity) == RDX_OK);
    TEST_ASSERT(activity == RDX_RECORD_ACTIVITY_IDLE);

    g_status.run = RECORD_STATE_START;
    TEST_ASSERT(rdx_record_service_get_activity(&activity) == RDX_OK);
    TEST_ASSERT(activity == RDX_RECORD_ACTIVITY_ACTIVE);

    g_status.run = RECORD_STATE_RESUME;
    TEST_ASSERT(rdx_record_service_get_activity(&activity) == RDX_OK);
    TEST_ASSERT(activity == RDX_RECORD_ACTIVITY_ACTIVE);

    g_status.run = RECORD_STATE_PAUSE;
    TEST_ASSERT(rdx_record_service_get_activity(&activity) == RDX_OK);
    TEST_ASSERT(activity == RDX_RECORD_ACTIVITY_PAUSED);
    TEST_PASS();
}

static int test_legacy_query_compatibility(void)
{
    g_status.run = RECORD_STATE_STOP;
    TEST_ASSERT(!rdx_record_service_is_running());
    TEST_ASSERT(rdx_record_service_can_auto_shutdown());
    TEST_ASSERT(rdx_record_service_is_active() == 0);

    g_status.run = RECORD_STATE_START;
    TEST_ASSERT(rdx_record_service_is_running());
    TEST_ASSERT(!rdx_record_service_can_auto_shutdown());
    TEST_ASSERT(rdx_record_service_is_active() == 0);

    g_status.run = RECORD_STATE_RESUME;
    TEST_ASSERT(rdx_record_service_is_running());
    TEST_ASSERT(rdx_record_service_is_active() == 0);

    g_status.run = RECORD_STATE_PAUSE;
    TEST_ASSERT(!rdx_record_service_is_running());
    TEST_ASSERT(!rdx_record_service_can_auto_shutdown());
    TEST_ASSERT(rdx_record_service_is_active() == 0);
    TEST_PASS();
}

static int test_scene_and_path_mapping(void)
{
    rdx_record_scene_t scene;
    rdx_record_path_t path;

    g_status.scene = RECORD_SCENE_CHAT;
    TEST_ASSERT(rdx_record_service_get_scene(&scene) == RDX_OK);
    TEST_ASSERT(scene == RDX_RECORD_SCENE_CHAT);
    g_status.scene = RECORD_SCENE_CALL;
    TEST_ASSERT(rdx_record_service_get_scene(&scene) == RDX_OK);
    TEST_ASSERT(scene == RDX_RECORD_SCENE_CALL);

    g_status.mode = RECORD_MODE_OFFLINE;
    TEST_ASSERT(rdx_record_service_get_path(&path) == RDX_OK);
    TEST_ASSERT(path == RDX_RECORD_PATH_OFFLINE);
    g_status.mode = RECORD_MODE_ONLINE;
    TEST_ASSERT(rdx_record_service_get_path(&path) == RDX_OK);
    TEST_ASSERT(path == RDX_RECORD_PATH_ONLINE);

    g_status.run = RECORD_STATE_START;
    g_status.orig_mode = RECORD_MODE_OFFLINE;
    TEST_ASSERT(rdx_record_service_is_offline_active());
    g_status.run = RECORD_STATE_RESUME;
    TEST_ASSERT(rdx_record_service_is_offline_active());
    g_status.run = RECORD_STATE_PAUSE;
    TEST_ASSERT(!rdx_record_service_is_offline_active());
    g_status.run = RECORD_STATE_STOP;
    TEST_ASSERT(!rdx_record_service_is_offline_active());
    g_status.run = RECORD_STATE_START;
    g_status.orig_mode = RECORD_MODE_ONLINE;
    TEST_ASSERT(!rdx_record_service_is_offline_active());
    TEST_PASS();
}

static int test_queries_are_read_only(void)
{
    RecordStatus before;
    rdx_record_activity_t activity;
    rdx_record_scene_t scene;
    rdx_record_path_t path;

    memset(&g_status, 0x5a, sizeof(g_status));
    g_status.run = RECORD_STATE_START;
    g_status.scene = RECORD_SCENE_CALL;
    g_status.mode = RECORD_MODE_ONLINE;
    g_status.orig_mode = RECORD_MODE_OFFLINE;
    before = g_status;

    TEST_ASSERT(rdx_record_service_get_activity(&activity) == RDX_OK);
    TEST_ASSERT(rdx_record_service_get_scene(&scene) == RDX_OK);
    TEST_ASSERT(rdx_record_service_get_path(&path) == RDX_OK);
    TEST_ASSERT(rdx_record_service_is_running());
    TEST_ASSERT(rdx_record_service_is_active() == 0);
    TEST_ASSERT(rdx_record_service_is_offline_active());
    TEST_ASSERT(memcmp(&before, &g_status, sizeof(g_status)) == 0);
    TEST_PASS();
}

static int test_invalid_legacy_values_fail_closed(void)
{
    rdx_record_activity_t activity = RDX_RECORD_ACTIVITY_ACTIVE;
    rdx_record_scene_t scene = RDX_RECORD_SCENE_CALL;
    rdx_record_path_t path = RDX_RECORD_PATH_ONLINE;

    g_status.run = 0xff;
    g_status.scene = 0xff;
    g_status.mode = 0xff;
    g_status.orig_mode = RECORD_MODE_OFFLINE;

    TEST_ASSERT(rdx_record_service_get_activity(&activity) == RDX_ERR_INVAL);
    TEST_ASSERT(activity == RDX_RECORD_ACTIVITY_ACTIVE);
    TEST_ASSERT(rdx_record_service_get_scene(&scene) == RDX_ERR_INVAL);
    TEST_ASSERT(scene == RDX_RECORD_SCENE_CALL);
    TEST_ASSERT(rdx_record_service_get_path(&path) == RDX_ERR_INVAL);
    TEST_ASSERT(path == RDX_RECORD_PATH_ONLINE);
    TEST_ASSERT(!rdx_record_service_is_running());
    TEST_ASSERT(!rdx_record_service_can_auto_shutdown());
    TEST_ASSERT(rdx_record_service_is_active() == 0);
    TEST_ASSERT(!rdx_record_service_is_offline_active());
    TEST_PASS();
}

static int test_query_validation(void)
{
    rdx_record_activity_t activity;

    TEST_ASSERT(rdx_record_service_get_activity(NULL) == RDX_ERR_INVAL);
    TEST_ASSERT(rdx_record_service_get_scene(NULL) == RDX_ERR_INVAL);
    TEST_ASSERT(rdx_record_service_get_path(NULL) == RDX_ERR_INVAL);
    g_status_available = 0;
    TEST_ASSERT(rdx_record_service_get_activity(&activity) == RDX_ERR_INVAL);
    TEST_ASSERT(!rdx_record_service_is_running());
    TEST_ASSERT(!rdx_record_service_can_auto_shutdown());
    TEST_ASSERT(rdx_record_service_is_active() == 0);
    TEST_ASSERT(!rdx_record_service_is_offline_active());
    {
        rdx_record_scene_t scene;
        rdx_record_path_t path;
        TEST_ASSERT(rdx_record_service_get_scene(&scene) == RDX_ERR_INVAL);
        TEST_ASSERT(rdx_record_service_get_path(&path) == RDX_ERR_INVAL);
    }
    g_status_available = 1;
    TEST_PASS();
}

int main(void)
{
    TEST_RUN(test_activity_mapping);
    TEST_RUN(test_scene_and_path_mapping);
    TEST_RUN(test_legacy_query_compatibility);
    TEST_RUN(test_queries_are_read_only);
    TEST_RUN(test_invalid_legacy_values_fail_closed);
    TEST_RUN(test_query_validation);
    return 0;
}
