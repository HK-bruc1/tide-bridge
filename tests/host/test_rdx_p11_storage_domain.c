#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "../../SDK/apps/common/third_party_profile/rdx_protocol/internal/rdx_storage_domain.c"

static uxfile_data_t *g_operate_file;
static unsigned g_query_count;
static unsigned g_log_count;
static char g_log[160];

uxfile_data_t *rdx_uxfile_get_operateFile_info(void)
{
    g_query_count++;
    return g_operate_file;
}

int p11_test_y_printf(const char *format, ...)
{
    va_list args;
    int result;

    g_log_count++;
    va_start(args, format);
    result = vsnprintf(g_log, sizeof(g_log), format, args);
    va_end(args);
    return result;
}

static void reset_test(uxfile_data_t *operate_file)
{
    g_operate_file = operate_file;
    g_query_count = 0;
    g_log_count = 0;
    g_log[0] = '\0';
}

static void test_null_operate_file(void)
{
    reset_test(NULL);
    assert(rdx_storage_domain_adjust_active_record_time(10) == RDX_OK);
    assert(g_query_count == 1);
    assert(g_log_count == 0);
}

static void test_zero_start_time(void)
{
    uxfile_data_t file = { .start_time = 0 };

    reset_test(&file);
    assert(rdx_storage_domain_adjust_active_record_time(10) == RDX_OK);
    assert(g_query_count == 1);
    assert(g_log_count == 0);
    assert(file.start_time == 0);
}

static void test_positive_delta(void)
{
    uxfile_data_t file = { .start_time = 100 };

    reset_test(&file);
    assert(rdx_storage_domain_adjust_active_record_time(25) == RDX_OK);
    assert(g_query_count == 1);
    assert(g_log_count == 1);
    assert(file.start_time == 125);
    assert(strcmp(g_log,
                  "[RTC_SYNC] Recording active, fix start_time: 100 -> 125 (delta=25)\r") == 0);
}

static void test_negative_delta(void)
{
    uxfile_data_t file = { .start_time = 100 };

    reset_test(&file);
    assert(rdx_storage_domain_adjust_active_record_time(-30) == RDX_OK);
    assert(g_query_count == 1);
    assert(g_log_count == 1);
    assert(file.start_time == 70);
    assert(strcmp(g_log,
                  "[RTC_SYNC] Recording active, fix start_time: 100 -> 70 (delta=-30)\r") == 0);
}

int main(void)
{
    test_null_operate_file();
    test_zero_start_time();
    test_positive_delta();
    test_negative_delta();
    puts("P11 storage domain host tests passed.");
    return 0;
}
