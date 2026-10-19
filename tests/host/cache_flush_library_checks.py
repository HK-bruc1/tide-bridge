"""对实际补丁库检查全部脏位通知、保存准入、互斥与故障保留。"""
import ctypes
import re


def run_cache_library_checks(ir, extract, globals_for, types_for, engine_for):
    stores = re.findall(r'^  store i8 [^\n]*i8\* @g_dat_cache\.4[^\n]*\n([^\n]*)', ir, re.M)
    assert len(stores) == 14
    assert all('call void @rdx_cache_flush_changed()' in line for line in stores)
    assert not re.search(r'^define[^\n]*@rdx_uxfile_flush_cache_timer_cb', ir, re.M)
    assert not re.search(r'call[^\n]*@sys_timeout_add[^\n]*flush_cache', ir)
    assert '@rdx_cache_flush_start()' in extract('rdx_uxfile_init')
    stop = extract('rdx_uxfile_task_free')
    assert stop.index('@rdx_cache_flush_stop()') < stop.index('@os_task_del(')

    source = types_for(['OS_SEM']) + '\n' + globals_for([
        'g_dat_cache.3', 'g_dat_cache.4', 'is_uxfile_task_created',
        'g_dat_need_upgrade_rebuild', 'rdx_storage_quiet',
        'rdx_storage_refresh', 'rdx_storage_io_error', 'dat_opera_mutex']) + '''
@service = global i32 0
@admission = global i32 0
@users = global i32 0
@lock_result = global i32 0
@lock_calls = global i32 0
@lock_wait = global i32 0
@unlock_calls = global i32 0
@lock_changes_service = global i32 0
@business_result = global i32 0
@business_calls = global i32 0
@business_exits = global i32 0
@save_result = global i32 0
@saved = global i32 0
define i32 @rdx_record_format_service_status() {
 %s = load i32, i32* @service
 ret i32 %s
}
define i32 @rdx_record_format_flush_enter() {
 %s = load i32, i32* @admission
 %ready = icmp eq i32 %s, 0
 br i1 %ready, label %enter, label %done
enter:
 %old = load i32, i32* @users
 %next = add i32 %old, 1
 store i32 %next, i32* @users
 br label %done
done:
 ret i32 %s
}
define void @rdx_record_format_command_leave() {
 %old = load i32, i32* @users
 %next = sub i32 %old, 1
 store i32 %next, i32* @users
 ret void
}
define i32 @os_mutex_pend(%struct.OS_SEM* %m, i32 %ticks) {
 store i32 %ticks, i32* @lock_wait
 %n = load i32, i32* @lock_calls
 %next = add i32 %n, 1
 store i32 %next, i32* @lock_calls
 %changed = load i32, i32* @lock_changes_service
 store i32 %changed, i32* @service
 %result = load i32, i32* @lock_result
 ret i32 %result
}
define i32 @os_mutex_post(%struct.OS_SEM* %m) {
 %n = load i32, i32* @unlock_calls
 %next = add i32 %n, 1
 store i32 %next, i32* @unlock_calls
 ret i32 0
}
define i32 @rdx_adv_policy_business_enter() {
 %n = load i32, i32* @business_calls
 %next = add i32 %n, 1
 store i32 %next, i32* @business_calls
 %result = load i32, i32* @business_result
 ret i32 %result
}
define void @rdx_adv_policy_business_exit() {
 %n = load i32, i32* @business_exits
 %next = add i32 %n, 1
 store i32 %next, i32* @business_exits
 ret void
}
define fastcc i32 @rdx_dat_cache_save_adv_impl() {
 %n = load i32, i32* @saved
 %next = add i32 %n, 1
 store i32 %next, i32* @saved
 %result = load i32, i32* @save_result
 %failed = icmp slt i32 %result, 0
 %dirty = zext i1 %failed to i8
 store i8 %dirty, i8* @g_dat_cache.4
 %error = select i1 %failed, i32 -1, i32 0
 store i32 %error, i32* @rdx_storage_io_error
 ret i32 %result
}
'''
    engine = engine_for(source + '\n' + '\n'.join(extract(name) for name in (
        'rdx_uxfile_cache_flush_state', 'rdx_uxfile_cache_flush_once', 'rdx_dat_cache_save')))
    state = ctypes.CFUNCTYPE(ctypes.c_int)(engine.get_function_address('rdx_uxfile_cache_flush_state'))
    flush = ctypes.CFUNCTYPE(ctypes.c_int)(engine.get_function_address('rdx_uxfile_cache_flush_once'))

    def value(name):
        kind = ctypes.c_ubyte if name in (
            'g_dat_cache.3', 'g_dat_cache.4', 'is_uxfile_task_created',
            'g_dat_need_upgrade_rebuild') else ctypes.c_int
        return kind.from_address(engine.get_global_value_address(name))

    value('is_uxfile_task_created').value = 1
    value('g_dat_cache.3').value = 1
    assert state() == 0 and flush() == 0
    assert value('saved').value == value('business_calls').value == 0
    value('g_dat_cache.4').value = 1
    assert state() == 1
    for name, setting, expected, original in [
        ('rdx_storage_quiet', 1, -1, 0), ('rdx_storage_quiet', 2, -1, 0),
        ('rdx_storage_refresh', -1, -1, 0), ('rdx_storage_refresh', 1, 2, 0),
        ('rdx_storage_io_error', -1, -1, 0), ('service', -1, -1, 0),
        ('service', 1, 2, 0), ('g_dat_need_upgrade_rebuild', 1, 2, 0),
        ('is_uxfile_task_created', 0, -1, 1), ('g_dat_cache.3', 0, 2, 1),
    ]:
        value(name).value = setting
        assert state() == expected and flush() == expected, name
        assert value('users').value == value('saved').value == value('lock_calls').value == 0
        assert value('g_dat_cache.4').value == 1
        value(name).value = original
    for name, setting, expected in [
        ('admission', 1, 2), ('admission', -1, -1),
        ('lock_result', 1, 2), ('lock_changes_service', 1, 2),
        ('business_result', -1, 2),
    ]:
        value(name).value = setting
        assert flush() == expected, name
        assert value('users').value == value('saved').value == 0
        assert value('g_dat_cache.4').value == 1
        value(name).value = 0
        value('service').value = 0
    assert value('lock_calls').value == 3 and value('unlock_calls').value == 2
    assert value('lock_wait').value == 1
    assert value('business_calls').value == 1 and value('business_exits').value == 0
    assert flush() == 0 and value('saved').value == 1 and state() == 0
    assert value('users').value == 0 and value('business_exits').value == 1
    value('g_dat_cache.4').value = 1
    value('save_result').value = -1
    assert flush() == -1 and value('saved').value == 2
    assert value('g_dat_cache.4').value == 1 and value('users').value == 0
    assert flush() == -1 and value('saved').value == 2
    print('Cache library: dirty hooks, admission, mutex, clean idle and sticky failure checks passed.')
