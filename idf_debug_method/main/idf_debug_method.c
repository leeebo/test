#include <stdio.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "esp_heap_task_info.h"
#include "esp_heap_caps.h"
#include "esp_heap_trace.h"

#define DEBUG_MEMORY_TASK_STACK_STOMPING 0

#define DEBUG_MEMORY_STOMPING 0
#define DEBUG_MEMORY_STOMPING_TRACE 0

#define DEBUG_MEMORY_LEAK 0
#define DEBUG_MEMORY_LEAK_STEP1 1
#define DEBUG_MEMORY_LEAK_STEP2 0

#define DEBUG_LOAD_STORE_PROHIBITED 0
#define DEBUG_INSTR_FETCH_FAILED 0
#define DEBUG_INSTR_FETCH_PROHIBITED 0
#define DEBUG_ILLEGAL_INSTRUCTION 0

#if DEBUG_MEMORY_LEAK_STEP1 || DEBUG_MEMORY_STOMPING_TRACE
#define NUM_RECORDS 100
static heap_trace_record_t trace_record[NUM_RECORDS]; // This buffer must be in internal RAM
#endif

#if DEBUG_MEMORY_LEAK
#if DEBUG_MEMORY_LEAK_STEP2
#define MAX_TASK_NUM 20                         // Max number of per tasks info that it can store
#define MAX_BLOCK_NUM 20                        // Max number of per block info that it can store
static size_t s_prepopulated_num = 0;
static heap_task_totals_t s_totals_arr[MAX_TASK_NUM];
static heap_task_block_t s_block_arr[MAX_BLOCK_NUM];
#endif
#endif
//Function to reproduce illegal instruction exception

typedef void (*init_buf_func)(void *buf, int len);

typedef struct {
    int tigger;
    int lion;
    int monkey;
} zoo_t;

static void new_monkey_born(zoo_t *zoo) {
    asm("               nop;");
    asm("               nop;");
    zoo->monkey++;
}

static void init_int_to_zero(void *buf, int len)
{
    int i;
    for (i = 0; i < len; i++) {
        ((int *)buf)[i] = 0;
    }
}

static void init_int_to_sequence(void *buf, int len)
{
    int i;
    for (i = 0; i < len; i++) {
        ((int *)buf)[i] = i;
    }
}

void init_buf_templete(init_buf_func func, int *buf, int len)
{
    func(buf, len);
}

static zoo_t* zoo_create(int num)
{
    zoo_t *zoo = (zoo_t *)malloc(num * sizeof(zoo_t));
    memset(zoo, 0, num * sizeof(zoo_t));
    return zoo;
}

static void zoo_destroy(zoo_t *zoo)
{
    free(zoo);
}

static void zoo_print(zoo_t *zoo)
{
    printf("zoo: %d, %d, %d\n", zoo->tigger, zoo->lion, zoo->monkey);
}

static void zoo_set_tigger(zoo_t *zoo, int tigger)
{
    zoo->tigger = tigger;
}

static void mem_leak_task(void *arg)
{
    //printf("mem_leak_task start %ld\n", random() % 10);
#if DEBUG_MEMORY_LEAK_STEP1
    heap_trace_start(HEAP_TRACE_LEAKS);
#endif
    size_t before_ram = heap_caps_get_free_size(MALLOC_CAP_DEFAULT);
    zoo_t *zoo = NULL;
    for (int i = 0; i < 3; i++) {
        zoo = zoo_create(random() % 10);
    }
    zoo_destroy(zoo);
    size_t after_ram = heap_caps_get_free_size(MALLOC_CAP_DEFAULT);

#if DEBUG_MEMORY_LEAK_STEP2
    //enabel CONFIG_HEAP_TASK_TRACKING=y
    heap_task_info_params_t heap_info = {0};
    heap_info.caps[0] = MALLOC_CAP_8BIT;        // Gets heap with CAP_8BIT capabilities
    heap_info.mask[0] = MALLOC_CAP_8BIT;
    heap_info.caps[1] = MALLOC_CAP_32BIT;       // Gets heap info with CAP_32BIT capabilities
    heap_info.mask[1] = MALLOC_CAP_32BIT;
    heap_info.tasks = NULL;                     // Passing NULL captures heap info for all tasks
    heap_info.num_tasks = 0;
    heap_info.totals = s_totals_arr;            // Gets task wise allocation details
    heap_info.num_totals = &s_prepopulated_num;
    heap_info.max_totals = MAX_TASK_NUM;        // Maximum length of "s_totals_arr"
    heap_info.blocks = s_block_arr;             // Gets block wise allocation details. For each block, gets owner task, address and size
    heap_info.max_blocks = MAX_BLOCK_NUM;       // Maximum length of "s_block_arr"

    heap_caps_get_per_task_info(&heap_info);

    for (int i = 0 ; i < *heap_info.num_totals; i++) {
        printf("Task: %s -> CAP_8BIT: %d CAP_32BIT: %d\n",
                heap_info.totals[i].task ? pcTaskGetName(heap_info.totals[i].task) : "Pre-Scheduler allocs" ,
                heap_info.totals[i].size[0],    // Heap size with CAP_8BIT capabilities
                heap_info.totals[i].size[1]);   // Heap size with CAP32_BIT capabilities
    }
    printf("\n\n");
#endif

#if DEBUG_MEMORY_LEAK_STEP1
    heap_trace_stop();
    heap_trace_dump();
#endif
    printf("before : %d, after : %d\n", before_ram, after_ram);
    printf("memory leak = %d\n", before_ram - after_ram);
    vTaskDelete(NULL);
}

#if DEBUG_MEMORY_TASK_STACK_STOMPING

static void stack_stomp(void *arg)
{
    int buf[10] = {0};
    char str[10] = "hello";
    char *p_str = str;
    printf("&str[9]: %p\n", &str[9]);
    printf("&p_str: %p\n\n", &p_str);

    printf("p_str before %p: %s\n", p_str, p_str);
    init_buf_templete(init_int_to_sequence, buf, 11);
    printf("p_str after %p: %s\n", p_str, p_str);

    for(int i = 0; i < 10; i++) {
        printf("%d ", buf[i]);
    }
    printf("\n");
    /*write a bug code of stack stomp, it will stop the return address of stack
    cause return address of stack is changed, so it will cause crash*/
    vTaskDelete(NULL);
}
#endif

int a = 1000;

void app_main(void)
{
#if DEBUG_MEMORY_TASK_STACK_STOMPING
    xTaskCreate(stack_stomp, "stack_stomp", 4096, NULL, 2, NULL);
    vTaskDelay(2000 / portTICK_PERIOD_MS);
#endif

#if DEBUG_MEMORY_STOMPING
    //malloc random size
#if DEBUG_MEMORY_STOMPING_TRACE
    heap_trace_init_standalone(trace_record, NUM_RECORDS);
    heap_trace_start(HEAP_TRACE_ALL);
#endif

    int *buf2[3] = {NULL};
    for (int i = 0; i < 3; i++) {
        int size = random() % 1000;
        buf2[i] = (int *)malloc(size);
        printf("buf2: %p, size: %d\n", buf2[i], size);
    }
    //esp_cpu_set_watchpoint(0, (void *)0x3fc9b75c, 4, ESP_CPU_WATCHPOINT_STORE);
#if DEBUG_MEMORY_STOMPING_TRACE
    heap_trace_stop();
    heap_trace_dump();
#endif
    for (int i = 0; i < 3; i++) {
        int size = 1000;
        memset(buf2[i], i, size);
        printf("buf2-%d: %p, write size: %d\n", i, buf2[i], size);
#if DEBUG_MEMORY_STOMPING_TRACE
        //heap_caps_check_integrity_all(true);
#endif
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        //heap_caps_dump_all();
        free(buf2[i+1]);
    }
#endif

#if DEBUG_MEMORY_LEAK
#if DEBUG_MEMORY_LEAK_STEP1
    heap_trace_init_standalone(trace_record, NUM_RECORDS);
#endif
    xTaskCreate(mem_leak_task, "mem_leak_t1", 4096, NULL, 2, NULL);
    vTaskDelay(2000 / portTICK_PERIOD_MS);
#endif

    printf("Hello world!\n");
    int *buf = (int *)malloc(10 * sizeof(int));
    assert(buf != NULL);
    printf("buf: %p\n", buf);
    a = 1;

#if DEBUG_LOAD_STORE_PROHIBITED
    zoo_t *zoo = NULL;
    zoo_t zoo2;
    new_monkey_born(zoo);
    new_monkey_born(&zoo2);
    printf("monkey: %d\n", zoo->monkey);
#endif

#if DEBUG_INSTR_FETCH_FAILED
    //rst:0x8 (TG1WDT_SYS_RST),boot:0x8 (SPI_FAST_FLASH_BOOT)
    //Can not enter panic handler!
    init_buf_templete(buf, buf, 10);
    printf("buf: init done\n");
#endif

#if DEBUG_INSTR_FETCH_PROHIBITED
    init_buf_templete(NULL, buf, 10);
    printf("buf: init done\n");
#endif

    init_buf_templete(init_int_to_sequence, buf, 10);
    printf("buf: init tp sequence done\n");
    for(int i = 0; i < 10; i++) {
        printf("%d ", buf[i]);
    }
    printf("\n");
    while (1) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}
