#include "test_minimal.h"
#include "rdx_util.h"

static int test_check_sum8(void)
{
	u8 data[] = { 1, 2, 3, 4, 5 };
	TEST_ASSERT(rdx_util_check_sum8(data, 5) == 15);
	TEST_ASSERT(rdx_util_check_sum8(data, 0) == 0);
	TEST_PASS();
}

static int test_check_sum16(void)
{
	u8 data[] = { 0xFF, 0x01, 0xFF, 0x01 };
	TEST_ASSERT(rdx_util_check_sum16(data, 4) == (0xFF+0x01+0xFF+0x01));
	TEST_ASSERT(rdx_util_check_sum16(data, 0) == 0);
	TEST_PASS();
}

static int test_hexchar2int(void)
{
	TEST_ASSERT(rdx_util_str_hexchar2int('0') == 0);
	TEST_ASSERT(rdx_util_str_hexchar2int('9') == 9);
	TEST_ASSERT(rdx_util_str_hexchar2int('A') == 10);
	TEST_ASSERT(rdx_util_str_hexchar2int('F') == 15);
	TEST_ASSERT(rdx_util_str_hexchar2int('a') == 10);
	TEST_ASSERT(rdx_util_str_hexchar2int('f') == 15);
	TEST_PASS();
}

static int test_int2hexchar(void)
{
	TEST_ASSERT(rdx_util_str_int2hexchar(1, 10) == 'A');
	TEST_ASSERT(rdx_util_str_int2hexchar(1, 15) == 'F');
	TEST_ASSERT(rdx_util_str_int2hexchar(0, 10) == 'a');
	TEST_ASSERT(rdx_util_str_int2hexchar(0, 0)  == '0');
	TEST_PASS();
}

static int test_count_one_in_num(void)
{
	TEST_ASSERT(rdx_util_count_one_in_num(0) == 0);
	TEST_ASSERT(rdx_util_count_one_in_num(0xFF) == 8);
	TEST_ASSERT(rdx_util_count_one_in_num(0x80000001) == 2);
	TEST_PASS();
}

static int test_buffer_value_is_all_x(void)
{
	u8 all_zero[4] = { 0, 0, 0, 0 };
	u8 mixed[4]    = { 0, 0, 1, 0 };
	u8 all_ff[4]   = { 0xFF, 0xFF, 0xFF, 0xFF };

	TEST_ASSERT(rdx_util_buffer_value_is_all_x(all_zero, 4, 0) == 1);
	TEST_ASSERT(rdx_util_buffer_value_is_all_x(mixed, 4, 0) == 0);
	TEST_ASSERT(rdx_util_buffer_value_is_all_x(all_ff, 4, 0xFF) == 1);
	TEST_PASS();
}

static int test_reverse_byte(void)
{
	u8 data[4] = { 1, 2, 3, 4 };
	rdx_util_reverse_byte(data, 4);
	TEST_ASSERT(data[0] == 4);
	TEST_ASSERT(data[1] == 3);
	TEST_ASSERT(data[2] == 2);
	TEST_ASSERT(data[3] == 1);
	/* odd size: middle stays */
	u8 data3[3] = { 0xA, 0xB, 0xC };
	rdx_util_reverse_byte(data3, 3);
	TEST_ASSERT(data3[0] == 0xC);
	TEST_ASSERT(data3[1] == 0xB);
	TEST_ASSERT(data3[2] == 0xA);
	TEST_PASS();
}

static int test_intarray2int(void)
{
	u8 arr[6] = { 1, 2, 3, 4, 5, 6 };
	TEST_ASSERT(rdx_util_intarray2int(arr, 0, 6) == 123456);
	TEST_ASSERT(rdx_util_intarray2int(arr, 1, 5) == 23456);
	TEST_PASS();
}

static int test_int2intarray(void)
{
	u8 out[4] = { 0 };
	u32 ret = rdx_util_int2intarray(1234, out, 4);
	TEST_ASSERT(ret == 4);
	TEST_ASSERT(out[0] == 1);
	TEST_ASSERT(out[1] == 2);
	TEST_ASSERT(out[2] == 3);
	TEST_ASSERT(out[3] == 4);
	TEST_PASS();
}

int main(void)
{
	TEST_RUN(test_check_sum8);
	TEST_RUN(test_check_sum16);
	TEST_RUN(test_hexchar2int);
	TEST_RUN(test_int2hexchar);
	TEST_RUN(test_count_one_in_num);
	TEST_RUN(test_buffer_value_is_all_x);
	TEST_RUN(test_reverse_byte);
	TEST_RUN(test_intarray2int);
	TEST_RUN(test_int2intarray);

	printf("All util tests passed.\n");
	return 0;
}
