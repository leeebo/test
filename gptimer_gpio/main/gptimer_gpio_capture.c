/*
 * SPDX-FileCopyrightText: 2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gptimer.h"
#include "driver/gpio.h"
#include "driver/gpio_etm.h"
#include "driver/gptimer_etm.h"
#include "esp_log.h"

static const char *TAG = "example";

#define TIMMER_INTR_ENABLE 0

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////// Please update the following configuration according to your board spec ////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#define ETM_TRIG_GPIO  5
#define CPU_TRIG_GPIO  6

#define EXAMPLE_GPTIMER_RESOLUTION_HZ  40000000 // 1MHz, 1 tick = 1us

/**
 * @brief User defined context, to be passed to GPIO ISR callback function.
 */
typedef struct {
    gptimer_handle_t gptimer;
    TaskHandle_t task_to_notify;
    gpio_num_t echo_gpio;
} gpio_callback_user_data_t;

/**
 * @brief generate single pulse on Trig pin to start a new sample
 */
static void gen_trig_output(void)
{
    gpio_set_level(ETM_TRIG_GPIO, 1); // set high
    esp_rom_delay_us(10);
    gpio_set_level(ETM_TRIG_GPIO, 0); // set low
}

#if TIMMER_INTR_ENABLE
static bool on_gptimer_alarm_cb(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *user_ctx)
{
    return false;
}
#endif

void app_main(void)
{
    ESP_LOGI(TAG, "Configure trig gpio");
    gpio_config_t trig_io_conf = {
        .mode = GPIO_MODE_OUTPUT,
        //.pull_up_en = true,
        .pin_bit_mask = (1ULL << ETM_TRIG_GPIO | 1ULL << CPU_TRIG_GPIO),
    };
    ESP_ERROR_CHECK(gpio_config(&trig_io_conf));
    // drive low by default
    ESP_ERROR_CHECK(gpio_set_level(ETM_TRIG_GPIO, 0));
    ESP_ERROR_CHECK(gpio_set_level(CPU_TRIG_GPIO, 0));

    ESP_LOGI(TAG, "Create gptimer handle");
    gptimer_handle_t gptimer = NULL;
    gptimer_config_t timer_config = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = EXAMPLE_GPTIMER_RESOLUTION_HZ,
    };
    ESP_ERROR_CHECK(gptimer_new_timer(&timer_config, &gptimer));

    gptimer_etm_event_config_t gptimer_etm_event_conf = {
        .event_type = GPTIMER_ETM_EVENT_ALARM_MATCH,
    };
    esp_etm_event_handle_t gptimer_etm_event = NULL;
    ESP_ERROR_CHECK(gptimer_new_etm_event(gptimer, &gptimer_etm_event_conf, &gptimer_etm_event));

    esp_etm_task_handle_t gpio_task = NULL;
    gpio_etm_task_config_t gpio_task_config = {
        .action = GPIO_ETM_TASK_ACTION_TOG,
    };
    ESP_ERROR_CHECK(gpio_new_etm_task(&gpio_task_config, &gpio_task));
    ESP_ERROR_CHECK(gpio_etm_task_add_gpio(gpio_task, ETM_TRIG_GPIO));

    ESP_LOGI(TAG, "Create ETM channel then connect gpio event and gptimer task");
    esp_etm_channel_handle_t etm_chan = NULL;
    esp_etm_channel_config_t etm_chan_config = {};
    ESP_ERROR_CHECK(esp_etm_new_channel(&etm_chan_config, &etm_chan));
    // GPIO any edge ==> ETM channel ==> GPTimer capture task
    ESP_ERROR_CHECK(esp_etm_channel_connect(etm_chan, gptimer_etm_event, gpio_task));

    gptimer_alarm_config_t alarm_config = {
        .alarm_count = 2,
        .reload_count = 0,
        .flags.auto_reload_on_alarm = true,
    };
    ESP_ERROR_CHECK(gptimer_set_alarm_action(gptimer, &alarm_config));

#if TIMMER_INTR_ENABLE
    ESP_LOGI(TAG, "Enable etm channel and gptimer");
    gptimer_event_callbacks_t cbs = {
        .on_alarm = on_gptimer_alarm_cb,
    };
    ESP_ERROR_CHECK(gptimer_register_event_callbacks(gptimer, &cbs, NULL));
#else
    esp_etm_task_handle_t gptimer_task = NULL;
    gptimer_etm_task_config_t gptimer_etm_task_conf = {
        .task_type = GPTIMER_ETM_TASK_EN_ALARM,
    };
    ESP_ERROR_CHECK(gptimer_new_etm_task(gptimer, &gptimer_etm_task_conf, & gptimer_task));
    // link the gptimer etm task to the gptimer
    esp_etm_channel_handle_t etm_chan2 = NULL;
    esp_etm_channel_config_t etm_chan2_config = {};
    ESP_ERROR_CHECK(esp_etm_new_channel(&etm_chan2_config, &etm_chan2));
    ESP_ERROR_CHECK(esp_etm_channel_connect(etm_chan2, gptimer_etm_event, gptimer_task));
    ESP_ERROR_CHECK(esp_etm_channel_enable(etm_chan2));
#endif

    ESP_ERROR_CHECK(esp_etm_channel_enable(etm_chan));
    ESP_ERROR_CHECK(gptimer_enable(gptimer));

    // Print the ETM channel usage
    ESP_ERROR_CHECK(esp_etm_dump(stdout));

    ESP_LOGI(TAG, "Start gptimer");
    ESP_ERROR_CHECK(gptimer_start(gptimer));

    int counter = 0;

    // while (1) {
    //     gpio_set_level(CPU_TRIG_GPIO, 0);
    //     gpio_set_level(CPU_TRIG_GPIO, 1);
    // }

    //uint32_t tof_ticks;
    while (1) {
        // trigger the sensor to start a new sample
        //gen_trig_output();
        // // wait for echo done signal
        // if (xTaskNotifyWait(0x00, ULONG_MAX, &tof_ticks, pdMS_TO_TICKS(1000)) == pdTRUE) {
        //     if (tof_ticks > 35000) {
        //         // out of range
        //         continue;
        //     }
        //     // convert the pulse width into measure distance
        //     float distance = (float) tof_ticks / 58.0f;
        //     ESP_LOGI(TAG, "Measured distance: %.2fcm", distance);
        // }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
