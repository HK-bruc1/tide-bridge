#include <assert.h>
#include <stdio.h>

#include "system/includes.h"
#include "rdx_err.h"
#include "rdx_uxfile.h"
#include "rdx_storage_format_compat.h"

static unsigned g_dut_request_count;
static unsigned g_unbind_request_count;
static int g_unbind_result;
static uxfile_format_cb g_last_callback;

static void reset_test(void)
{
    g_dut_request_count = 0;
    g_unbind_request_count = 0;
    g_unbind_result = 0;
    g_last_callback = NULL;
}

void rdx_uxfile_device_sd_format(uxfile_format_cb callback)
{
    g_dut_request_count++;
    g_last_callback = callback;
}

int rdx_uxfile_sd_format(uxfile_format_cb callback)
{
    g_unbind_request_count++;
    g_last_callback = callback;
    return g_unbind_result;
}

static void dut_callback(u8 result)
{
    (void)result;
}

static void unbind_callback(u8 result)
{
    (void)result;
}

static void test_result_mapping(void)
{
    assert(rdx_storage_format_compat_result_is_ok(MEM_FORMAT_RESULT_OK));
    assert(!rdx_storage_format_compat_result_is_ok(MEM_FORMAT_RESULT_FAIL));
    assert(!rdx_storage_format_compat_result_is_ok(0));
}

static void test_dut_thin_forward(void)
{
    reset_test();

    assert(rdx_storage_format_compat_for_dut(dut_callback) == RDX_OK);
    assert(g_dut_request_count == 1);
    assert(g_unbind_request_count == 0);
    assert(g_last_callback == dut_callback);
}

static void test_unbind_result_mapping(void)
{
    reset_test();
    assert(rdx_storage_format_compat_for_unbind(unbind_callback) == RDX_OK);
    assert(g_dut_request_count == 0);
    assert(g_unbind_request_count == 1);
    assert(g_last_callback == unbind_callback);

    reset_test();
    g_unbind_result = 7;
    assert(rdx_storage_format_compat_for_unbind(unbind_callback) == RDX_ERR_IO);
    assert(g_unbind_request_count == 1);
    assert(g_last_callback == unbind_callback);

    reset_test();
    g_unbind_result = -1;
    assert(rdx_storage_format_compat_for_unbind(unbind_callback) == RDX_ERR_IO);
    assert(g_unbind_request_count == 1);
    assert(g_last_callback == unbind_callback);
}

int main(void)
{
    test_result_mapping();
    test_dut_thin_forward();
    test_unbind_result_mapping();
    puts("P11 storage format compatibility Host tests passed.");
    return 0;
}
