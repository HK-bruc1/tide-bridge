"""Storage regressions derived from the reproducible 22 mA business trace."""

CASES = r'''
int test_missing_dat_and_orphan_identity(void) {
    reset();
    current.sn=13;
    strcpy(current.filename,"f5062.raw");
    CHECK(!rdx_record_format_begin(&current,1));
    CHECK(!rdx_record_format_frame(&current,1,audio,40));
    CHECK(!rf_write(RF_ROOT "f5062.raw",audio,sizeof(audio)));
    CHECK(!recover());
    u32 budget=0;
    CHECK(!rdx_record_format_space(&budget) && budget>=8000);
    /* The legacy automatic-delete result observed in the trace. */
    CHECK(!fdelete_by_name(RF_ROOT "f5062.raw"));
    CHECK(!fdelete_by_name(RF_DAT));
    CHECK(lookup(RF_ROOT "f5062.mta"));
    CHECK(rdx_record_format_space(&budget)==-1 && !budget && !handles);
    CHECK(!recover());
    cJSON *root=read_dat();
    CHECK(root && cJSON_GetArraySize(root)==1);
    cJSON *entry=cJSON_GetArrayItem(root,0);
    CHECK(num(entry,"sn")==302993506u);
    cJSON *name=cJSON_GetObjectItem(entry,"name");
    CHECK(name && !strcmp(name->valuestring,"f5062.mta"));
    cJSON_Delete(root);
    CHECK(!handles);
    return 0;
}

static int auto_auth_calls, auto_auth_reject;
static int auto_authorize(void *context) {
    ++auto_auth_calls;
    return !auto_auth_reject || auto_auth_calls<auto_auth_reject;
}
int test_auto_delete_durable_authorization(void) {
    reset();
    vm_sn=7; vm_present=1;
    CHECK(!seed_mono() && !recover());
    u32 before=vm_sn;
    for(int reject=1;reject<=2;++reject) {
        auto_auth_calls=0; auto_auth_reject=reject;
        CHECK(rdx_record_format_delete_guarded(7,"abc123.raw",auto_authorize,NULL)==-2);
        CHECK(lookup(RF_ROOT "abc123.raw") && lookup(RF_ROOT "abc123.mta"));
        CHECK(!lookup(RF_DELETE) && !handles && vm_sn==before);
    }
    auto_auth_reject=auto_auth_calls=0;
    CHECK(!rdx_record_format_delete_guarded(7,"abc123.raw",auto_authorize,NULL));
    CHECK(auto_auth_calls==2 && !lookup(RF_ROOT "abc123.raw") && !lookup(RF_ROOT "abc123.mta"));
    CHECK(lookup(RF_DAT) && !lookup(RF_DELETE) && vm_sn==before);
    cJSON *root=read_dat();
    CHECK(root && !cJSON_GetArraySize(root));
    cJSON_Delete(root);
    u32 budget=0;
    CHECK(!rdx_record_format_space(&budget) && budget>=8000 && !handles);
    CHECK(!recover());
    root=read_dat();
    CHECK(root && !cJSON_GetArraySize(root));
    cJSON_Delete(root);
    u32 next=0;
    CHECK(!rdx_record_format_reserve_sn(&next) && next>before && next>7);
    return 0;
}
'''

NAMES = ['test_missing_dat_and_orphan_identity', 'test_auto_delete_durable_authorization']
