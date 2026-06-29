#include "test_minimal.h"
#include "rdx_ops.h"
#include "rdx_err.h"

static int test_validate_ok(void)
{
    const rdx_time_ops_t *ops = rdx_time_ops_get();
    TEST_ASSERT(ops != NULL);
    TEST_ASSERT(rdx_time_ops_validate(ops) == RDX_OK);
    TEST_PASS();
}

static int test_leap_year(void)
{
    const rdx_time_ops_t *ops = rdx_time_ops_get();
    TEST_ASSERT(ops->is_leap_year(2000) == 1);
    TEST_ASSERT(ops->is_leap_year(1900) == 0);
    TEST_ASSERT(ops->is_leap_year(2024) == 1);
    TEST_ASSERT(ops->is_leap_year(2023) == 0);
    TEST_PASS();
}

static int test_days_in_month(void)
{
    const rdx_time_ops_t *ops = rdx_time_ops_get();
    TEST_ASSERT(ops->days_in_month(2023, 1) == 31);
    TEST_ASSERT(ops->days_in_month(2023, 2) == 28);
    TEST_ASSERT(ops->days_in_month(2024, 2) == 29);
    TEST_ASSERT(ops->days_in_month(2023, 4) == 30);
    TEST_PASS();
}

static int test_invalid_ops(void)
{
    rdx_time_ops_t bad = { 0 };
    TEST_ASSERT(rdx_time_ops_validate(&bad) == RDX_ERR_INVAL);
    TEST_PASS();
}

int main(void)
{
    TEST_RUN(test_validate_ok);
    TEST_RUN(test_leap_year);
    TEST_RUN(test_days_in_month);
    TEST_RUN(test_invalid_ops);

    printf("All time ops tests passed.\n");
    return 0;
}
