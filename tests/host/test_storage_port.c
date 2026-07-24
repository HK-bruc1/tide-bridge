#include <string.h>

#include "test_minimal.h"
#include "rdx_jl_storage.h"
#include "syscfg_id.h"

static int mock_read_result;
static int mock_write_result;
static int mock_read_string_result;
static int mock_last_id;
static u8 mock_read_data[32];

int syscfg_read(u16 id, void *buf, u16 len)
{
    int copy_len = mock_read_result;

    mock_last_id = id;
    if (copy_len > len) {
        copy_len = len;
    }
    if (copy_len > 0) {
        memcpy(buf, mock_read_data, (size_t)copy_len);
    }
    return mock_read_result;
}

int syscfg_write(u16 id, const void *buf, u16 len)
{
    (void)buf;
    (void)len;
    mock_last_id = id;
    return mock_write_result;
}

int syscfg_read_string(u16 id, void *buf, u16 len, u8 ver)
{
    (void)buf;
    (void)len;
    (void)ver;
    mock_last_id = id;
    return mock_read_string_result;
}

static void reset_mock(void)
{
    mock_read_result = 0;
    mock_write_result = 0;
    mock_read_string_result = 0;
    mock_last_id = -1;
    memset(mock_read_data, 0, sizeof(mock_read_data));
}

static int test_exact_transfer_contract(void)
{
    u8 value = 0;

    reset_mock();
    mock_read_data[0] = 0x5a;
    mock_read_result = 1;
    TEST_ASSERT(rdx_storage_read(RDX_STORAGE_KEY_BOUND_STATUS,
                                 &value, sizeof(value)) == RDX_OK);
    TEST_ASSERT(value == 0x5a);
    TEST_ASSERT(mock_last_id == VM_RDX_NOTTA_BOUND_STATUS);

    mock_read_result = 0;
    TEST_ASSERT(rdx_storage_read(RDX_STORAGE_KEY_BOUND_STATUS,
                                 &value, sizeof(value)) == RDX_ERR_NOENT);

    mock_read_result = 1;
    TEST_ASSERT(rdx_storage_read(RDX_STORAGE_KEY_BLE_MAC,
                                 mock_read_data, 6) == RDX_ERR_IO);

    mock_write_result = 1;
    TEST_ASSERT(rdx_storage_write(RDX_STORAGE_KEY_BOUND_STATUS,
                                  &value, sizeof(value)) == RDX_OK);
    mock_write_result = 0;
    TEST_ASSERT(rdx_storage_write(RDX_STORAGE_KEY_BOUND_STATUS,
                                  &value, sizeof(value)) == RDX_ERR_IO);
    TEST_PASS();
}

static int test_invalid_arguments(void)
{
    u8 value = 0;

    reset_mock();
    TEST_ASSERT(rdx_storage_read(RDX_STORAGE_KEY_BOUND_STATUS,
                                 NULL, 1) == RDX_ERR_INVAL);
    TEST_ASSERT(rdx_storage_write(RDX_STORAGE_KEY_BOUND_STATUS,
                                  &value, 0) == RDX_ERR_INVAL);
    TEST_ASSERT(rdx_storage_read((rdx_storage_key_t)99,
                                 &value, 1) == RDX_ERR_NOTSUP);
    TEST_PASS();
}

static int test_legacy_ble_name_blob(void)
{
    u8 name[24] = {0};
    u16 actual_len = 0;

    reset_mock();
    memcpy(mock_read_data, "RDX CC", 6);
    mock_read_result = 6;
    TEST_ASSERT(rdx_storage_read_blob(RDX_STORAGE_KEY_BLE_NAME,
                                      name, sizeof(name),
                                      &actual_len) == RDX_OK);
    TEST_ASSERT(actual_len == 6);
    TEST_ASSERT(memcmp(name, "RDX CC", 6) == 0);
    TEST_ASSERT(mock_last_id == VM_RDX_BLE_NAME);

    TEST_ASSERT(rdx_storage_read_blob(RDX_STORAGE_KEY_BLE_MAC,
                                      name, sizeof(name),
                                      &actual_len) == RDX_ERR_NOTSUP);

    mock_write_result = 6;
    TEST_ASSERT(rdx_storage_write_blob(RDX_STORAGE_KEY_BLE_NAME,
                                       name, 6) == RDX_OK);
    mock_write_result = 5;
    TEST_ASSERT(rdx_storage_write_blob(RDX_STORAGE_KEY_BLE_NAME,
                                       name, 6) == RDX_ERR_IO);
    TEST_PASS();
}

static int test_factory_config_accessors(void)
{
    u8 name[32] = {0};
    u8 mac[6] = {0};

    reset_mock();
    mock_read_string_result = 8;
    TEST_ASSERT(rdx_storage_read_factory_bt_name(name, sizeof(name)) == RDX_OK);
    TEST_ASSERT(mock_last_id == CFG_BT_NAME);

    mock_write_result = 0;
    TEST_ASSERT(rdx_storage_write_factory_bt_name(name, sizeof(name)) == RDX_OK);
    TEST_ASSERT(mock_last_id == CFG_BT_NAME);

    mock_read_result = 6;
    TEST_ASSERT(rdx_storage_read_factory_bt_mac(mac) == RDX_OK);
    TEST_ASSERT(mock_last_id == CFG_BT_MAC_ADDR);
    TEST_PASS();
}

int main(void)
{
    TEST_RUN(test_exact_transfer_contract);
    TEST_RUN(test_invalid_arguments);
    TEST_RUN(test_legacy_ble_name_blob);
    TEST_RUN(test_factory_config_accessors);
    return 0;
}
