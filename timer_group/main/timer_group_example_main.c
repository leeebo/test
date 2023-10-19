/* General Purpose Timer example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/timer.h"
#include "driver/gpio.h"
#include "driver/dedic_gpio.h"
#include "hal/gpio_hal.h"
#include "soc/gpio_struct.h"
#include "driver/dedic_gpio.h"

#define TIMER_DIVIDER         (80)  //  Hardware timer clock divider
#define TIMER_SCALE           (TIMER_BASE_CLK / TIMER_DIVIDER / 1000000)  // convert counter value to seconds
#define FLAP_GPIO_NUM         2
#define FLAP_DEDICATED_GPIO   3
#define USING_DEDICATED_GPIO  1

static dedic_gpio_bundle_handle_t dedicated_gpio_bundle = NULL;

typedef struct {
    int timer_group;
    int timer_idx;
    int alarm_interval;
    bool auto_reload;
    bool enqueue;
} example_timer_info_t;

/**
 * @brief A sample structure to pass events from the timer ISR to task
 *
 */
typedef struct {
    example_timer_info_t info;
    uint64_t timer_counter_value;
} example_timer_event_t;

static xQueueHandle s_timer_queue;

/*
 * A simple helper function to print the raw timer counter value
 * and the counter value converted to seconds
 */
static void inline print_timer_counter(uint64_t counter_value)
{
    printf("Counter: 0x%08x%08x\r\n", (uint32_t) (counter_value >> 32),
           (uint32_t) (counter_value));
    printf("Time   : %.8f us\r\n", (double) counter_value / TIMER_SCALE);
}

#if !USING_DEDICATED_GPIO
static void IRAM_ATTR my_gpio_ll_set_level(uint32_t gpio_num, uint32_t level)
{
    gpio_dev_t *hw = GPIO_HAL_GET_HW(GPIO_PORT_0);
    if (level) {
        hw->out_w1ts.out_w1ts = (1 << gpio_num);
    } else {
        hw->out_w1tc.out_w1tc = (1 << gpio_num);
    }
}

static int IRAM_ATTR my_gpio_ll_get_level(uint32_t gpio_num)
{
    gpio_dev_t *hw = GPIO_HAL_GET_HW(GPIO_PORT_0);
    return (hw->in.data >> gpio_num) & 0x1;
}
#endif

static bool IRAM_ATTR timer_group_isr_callback(void *args)
{
    BaseType_t high_task_awoken = pdFALSE;
    example_timer_info_t *info = (example_timer_info_t *) args;

#if USING_DEDICATED_GPIO
    uint32_t mask = dedic_gpio_bundle_read_out(dedicated_gpio_bundle);
    dedic_gpio_bundle_write(dedicated_gpio_bundle, 0x01, mask ^ 1);
#else
    my_gpio_ll_set_level(FLAP_GPIO_NUM, my_gpio_ll_get_level(FLAP_GPIO_NUM) ^ 1);
#endif

    uint64_t timer_counter_value = timer_group_get_counter_value_in_isr(info->timer_group, info->timer_idx);

    /* Prepare basic event data that will be then sent back to task */
    example_timer_event_t evt = {
        .info.timer_group = info->timer_group,
        .info.timer_idx = info->timer_idx,
        .info.auto_reload = info->auto_reload,
        .info.alarm_interval = info->alarm_interval,
        .timer_counter_value = timer_counter_value
    };

    if (!info->auto_reload) {
        timer_counter_value += info->alarm_interval * TIMER_SCALE;
        timer_group_set_alarm_value_in_isr(info->timer_group, info->timer_idx, timer_counter_value);
    }

    /* Now just send the event data back to the main program task */
    if (info->enqueue) {
        xQueueSendFromISR(s_timer_queue, &evt, &high_task_awoken);
    }

    return high_task_awoken == pdTRUE; // return whether we need to yield at the end of ISR
}

/**
 * @brief Initialize selected timer of timer group
 *
 * @param group Timer Group number, index from 0
 * @param timer timer ID, index from 0
 * @param auto_reload whether auto-reload on alarm event
 * @param timer_interval_us interval of alarm
 */
static void example_tg_timer_init(int group, int timer, bool auto_reload, bool enqueue, int timer_interval_us)
{
    /* Select and initialize basic parameters of the timer */
    timer_config_t config = {
        .divider = TIMER_DIVIDER,
        .counter_dir = TIMER_COUNT_UP,
        .counter_en = TIMER_PAUSE,
        .alarm_en = TIMER_ALARM_EN,
        .auto_reload = auto_reload,
    }; // default clock source is APB
    timer_init(group, timer, &config);

    /* Timer's counter will initially start fr
    
    om value below.
       Also, if auto_reload is set, this value will be automatically reload on alarm */
    timer_set_counter_value(group, timer, 0);

    /* Configure the alarm value and the interrupt on alarm. */
    timer_set_alarm_value(group, timer, timer_interval_us * TIMER_SCALE);
    timer_enable_intr(group, timer);

    example_timer_info_t *timer_info = calloc(1, sizeof(example_timer_info_t));
    timer_info->timer_group = group;
    timer_info->timer_idx = timer;
    timer_info->auto_reload = auto_reload;
    timer_info->alarm_interval = timer_interval_us;
    timer_info->enqueue = enqueue;
    timer_isr_callback_add(group, timer, timer_group_isr_callback, timer_info, ESP_INTR_FLAG_IRAM);

    timer_start(group, timer);
}

static int trigger_times = 0;

static void timer_example_evt_task(void *arg)
{
    while (1) {
        example_timer_event_t evt;
        xQueueReceive(s_timer_queue, &evt, portMAX_DELAY);
        printf("\n");
        ++trigger_times;
        /* Print information that the timer reported an event */
        if (evt.info.auto_reload) {
            printf("Timer Group with auto reload\n");
        } else {
            printf("Timer Group without auto reload\n");
        }
        printf("Group[%d], timer[%d] alarm event\n", evt.info.timer_group, evt.info.timer_idx);

        /* Print the timer values passed by event */
        printf("------- EVENT TIME --------\n");
        print_timer_counter(evt.timer_counter_value);

        /* Print the timer values as visible by this task */
        printf("-------- TASK TIME --------\n");
        uint64_t task_counter_value;
        timer_get_counter_value(evt.info.timer_group, evt.info.timer_idx, &task_counter_value);
        print_timer_counter(task_counter_value);
    }
}

void app_main(void)
{
    s_timer_queue = xQueueCreate(10, sizeof(example_timer_event_t));

    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_INPUT_OUTPUT,
        .pin_bit_mask = (1ULL << FLAP_GPIO_NUM | 1ULL << FLAP_DEDICATED_GPIO),
        .pull_down_en = 0,
        .pull_up_en = 1,
    };
    gpio_config(&io_conf);
    gpio_set_level(FLAP_GPIO_NUM, 1);
    gpio_set_level(FLAP_DEDICATED_GPIO, 1);

    // configure GPIO
    const int bundleA_gpios[] = {FLAP_DEDICATED_GPIO};
    // Create bundleA, output only
    dedic_gpio_bundle_config_t bundleA_config = {
        .gpio_array = bundleA_gpios,
        .array_size = sizeof(bundleA_gpios) / sizeof(bundleA_gpios[0]),
        .flags = {
            .out_en = 1,
        },
    };
    ESP_ERROR_CHECK(dedic_gpio_new_bundle(&bundleA_config, &dedicated_gpio_bundle));
    dedic_gpio_bundle_write(dedicated_gpio_bundle, 0x01, 0x01);
    example_tg_timer_init(TIMER_GROUP_0, TIMER_0, true, false, 50);
    example_tg_timer_init(TIMER_GROUP_1, TIMER_0, true, true, 1000000);
    xTaskCreate(timer_example_evt_task, "timer_evt_task", 2048, NULL, 5, NULL);
    int count = 0;
    while (1) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        if (++count > 5) {
            if (trigger_times < 5) {
                abort();
            }
            esp_restart();
        }
        printf("count: %d, trigger_times: %d\n", count, trigger_times);
    }
}
