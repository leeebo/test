#include <stdio.h>

#define DEBUG_LOAD_STORE_PROHIBITED 1
#define DEBUG_INSTR_FETCH_FAILED 0
#define DEBUG_INSTR_FETCH_PROHIBITED 0
#define DEBUG_ILLEGAL_INSTRUCTION 0

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

int a = 1000;

void app_main(void)
{
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
        printf("%d\n", buf[i]);
    }


}
