
#ifdef SUPPORT_MS_EXTENSIONS
#pragma bss_seg(".gpadc_battery.data.bss")
#pragma data_seg(".gpadc_battery.data")
#pragma const_seg(".gpadc_battery.text.const")
#pragma code_seg(".gpadc_battery.text")
#endif
#include "typedef.h"
#include "gpadc.h"
#include "timer.h"

#define BATTERY_SAMPLE_TIMES 10
#define BATTERY_DATA_BUF_SIZE   20
#define BATTERY_DATA_BUF_MID    10

extern const u8 adc_data_res; //adc 采样精度

static u32 battery_data_buf[BATTERY_DATA_BUF_SIZE];
static struct battery_data_fifo {
    u32 voltage;
    u32 offset;
    u32 *buf;
} battery_fifo;


_WEAK_
u32 get_vddiom_voltage()
{
    return get_vddiom_vol();
}

_WEAK_
u32 efuse_get_vbat_3700()
{
    u32 vddiom_vol = 2800;
    u32 adc_value_max = BIT(adc_data_res) - 1;
    u32 voltage = 3700 / AD_CH_PMU_VBAT_DIV;

    return 8 * voltage * adc_value_max / vddiom_vol;
}

static void quickSortDescending(u32 arr[], u32 left, u32 right)
{
    if (left >= right) {
        return;
    }

    int pivot = arr[left];
    int i = left;
    int j = right;

    while (i < j) {
        // 从右往左找到第一个小于 pivot 的元素
        while (i < j && arr[j] <= pivot) {
            j--;
        }
        if (i < j) {
            arr[i++] = arr[j];
        }

        // 从左往右找到第一个大于 pivot 的元素
        while (i < j && arr[i] > pivot) {
            i++;
        }
        if (i < j) {
            arr[j--] = arr[i];
        }
    }
    arr[i] = pivot;

    // 递归排序左半部分和右半部分
    quickSortDescending(arr, left, i - 1);
    quickSortDescending(arr, i + 1, right);
}
static void sortDescending(u32 arr[], u32 n)
{
    for (int i = 0; i < n - 1; i++) {
        for (int j = 0; j < n - i - 1; j++) {
            if (arr[j] < arr[j + 1]) {  // 降序排列
                int temp = arr[j];
                arr[j] = arr[j + 1];
                arr[j + 1] = temp;
            }
        }
    }
}
static u32 gpadc_battery_get_average(u32 *data, u32 size, u32 mid)
{
    u32 sum_v = 0;
    u32 start = (size - mid) / 2;
    u32 end = start + mid;
    for (u8 i = start; i < end; i++) {
        sum_v += data[i];
    }
    return sum_v / mid;
}
static u32 gpadc_battery_get_vbat_voltage()
{
    int sum_v = adc_get_value_blocking_filter_dma(AD_CH_PMU_VBAT, NULL, BATTERY_SAMPLE_TIMES);

    int K = efuse_get_vbat_3700();
    int Dg = get_vddiom_vol() - 2800; //当前IOVDD档位-2800
    int V = 3700.0f * sum_v / K + (Dg * sum_v * 1.0f / 4096);
    /* printf("voltage:%dmv, value:%d, Dg:%d, K:%d\n", V, sum_v, Dg, K); */
    return (u32)V;
}

static void gpadc_battery_callback(void *priv)
{
    u32 voltage = gpadc_battery_get_vbat_voltage();
    battery_fifo.buf[battery_fifo.offset] = voltage;
    battery_fifo.offset++;
    if (battery_fifo.offset >= BATTERY_DATA_BUF_SIZE) {
        battery_fifo.offset = 0;
    }
    /* quickSortDescending(battery_fifo.buf, 0, BATTERY_DATA_BUF_SIZE-1); */
    sortDescending(battery_fifo.buf, BATTERY_DATA_BUF_SIZE);
    battery_fifo.voltage = gpadc_battery_get_average(battery_fifo.buf, BATTERY_DATA_BUF_SIZE, BATTERY_DATA_BUF_MID);
    /* printf("vbat_voltage:%dmv\n", battery_fifo.voltage); */
    /* printf("$vbat, %d, %d, %d, %d, %d \n",A,B,VV, adc_get_voltage_blocking(AD_CH_PMU_VBAT), gpadc_battery_get_voltage(AD_CH_PMU_VBAT)); */
}

extern const u8 gpadc_battery_mode;
int gpadc_battery_init()
{
    if (gpadc_battery_mode != MEAN_FILTERING_MODE) {
        return 0;
    }
    printf("func:%s(), line:%d\n", __func__, __LINE__);

    memset(battery_data_buf, 0, sizeof(battery_data_buf));
    battery_fifo.buf = battery_data_buf;
    battery_fifo.voltage = 0;
    battery_fifo.offset = 0;

    for (u8 i = 0; i < BATTERY_DATA_BUF_SIZE; i++) {
        battery_fifo.buf[i] = gpadc_battery_get_vbat_voltage();
    }
    /* quickSortDescending(battery_fifo.buf, 0, BATTERY_DATA_BUF_SIZE-1); */
    sortDescending(battery_fifo.buf, BATTERY_DATA_BUF_SIZE);
    battery_fifo.voltage = gpadc_battery_get_average(battery_fifo.buf, BATTERY_DATA_BUF_SIZE, BATTERY_DATA_BUF_MID);
    printf("gpadc_battery_init voltage:%dmv\n", battery_fifo.voltage);

    usr_timer_add(NULL, gpadc_battery_callback, 100, 0);
    /* adc_internal_signal_to_io(AD_CH_PMU_VBAT, 16+2); //将内部通道信号，接到IO口上，输出 */
    /* local_irq_disable(); */
    /* extern void wdt_clear(); */
    /* wdt_clear(); */
    return 0;
}

u32 gpadc_battery_get_voltage()
{
    if (gpadc_battery_mode == MEAN_FILTERING_MODE) {
        return battery_fifo.voltage;
    } else if (gpadc_battery_mode == WEIGHTING_MODE) {
        return adc_get_voltage(AD_CH_PMU_VBAT) * AD_CH_PMU_VBAT_DIV;
    } else {
        return 0;
    }
}

/* #include "init.h" */
/* platform_initcall(gpadc_battery_init); */
