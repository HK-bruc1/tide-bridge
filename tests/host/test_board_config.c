#include "test_minimal.h"
#include "rdx_board_config.h"

static int test_config_not_null(void)
{
    const rdx_board_config_t *cfg = rdx_board_get_config();
    TEST_ASSERT(cfg != NULL);
    TEST_PASS();
}

static int test_board_name(void)
{
    const rdx_board_config_t *cfg = rdx_board_get_config();
    TEST_ASSERT(cfg->board_name != NULL);
    TEST_ASSERT(cfg->board_name[0] != '\0');
    TEST_PASS();
}

static int test_chip_family(void)
{
    const rdx_board_config_t *cfg = rdx_board_get_config();
    TEST_ASSERT(cfg->chip_family != NULL);
    TEST_ASSERT(cfg->chip_family[0] != '\0');
    TEST_PASS();
}

static int test_spi_pins_valid(void)
{
    const rdx_board_config_t *cfg = rdx_board_get_config();
    TEST_ASSERT(cfg->spi_clk_hz > 0);
    TEST_PASS();
}

int main(void)
{
    TEST_RUN(test_config_not_null);
    TEST_RUN(test_board_name);
    TEST_RUN(test_chip_family);
    TEST_RUN(test_spi_pins_valid);

    printf("All board config tests passed.\n");
    return 0;
}
