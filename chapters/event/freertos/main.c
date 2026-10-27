/*
 * 이벤트 그룹 — FreeRTOS
 *
 * 1. 모두 기다리기 (AND)  : 오픈 체크리스트. 우유 10ms · 포스기 20ms · 머신 30ms에 비트를 세움.
 *                          오픈 담당은 셋 다 설 때 깨어남
 * 2. 아무거나 (OR)        : 배달 15ms · 매장 40ms에 주문. 바리스타는 먼저 온 쪽에 깨어남
 * 3. 모여서 출발 (Sync)   : 직원 셋이 준비를 마치는 시각이 달라도 다 같이 출발
 * 4. 비트는 세지 않음      : 같은 비트를 5번 세워도 받을 수 있는 건 한 번
 */
#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"
#include "event_groups.h"
#include "board.h"

#define BIT_MILK     ( 1u << 0 )
#define BIT_POS      ( 1u << 1 )
#define BIT_MACHINE  ( 1u << 2 )
#define BIT_HALL     ( 1u << 0 )
#define BIT_DELIVERY ( 1u << 1 )
#define BIT_ORDER    ( 1u << 0 )

#define WORKER_PRI ( tskIDLE_PRIORITY + 2 )
#define RUNNER_PRI ( tskIDLE_PRIORITY + 1 )

static EventGroupHandle_t ev;
static volatile int done;
static TickType_t t0;

static int ms_since(TickType_t t) { return (int)((xTaskGetTickCount() - t) * portTICK_PERIOD_MS); }

static void finish(void)
{
    taskENTER_CRITICAL();
    done++;
    taskEXIT_CRITICAL();
    vTaskDelete(NULL);
}

/* 정해진 시각에 비트 하나를 세우는 태스크 */
struct setter { EventBits_t bit; int at_ms; };

static void setter(void *arg)
{
    const struct setter *s = arg;
    vTaskDelayUntil(&(TickType_t){ t0 }, pdMS_TO_TICKS(s->at_ms));
    xEventGroupSetBits(ev, s->bit);
    finish();
}

/* 1 · 2: 기다리는 쪽 */
static volatile int woke_ms;
static volatile EventBits_t woke_bits;
static volatile EventBits_t wait_mask;
static volatile BaseType_t wait_all;

static void waiter(void *arg)
{
    (void)arg;
    woke_bits = xEventGroupWaitBits(ev, wait_mask, pdTRUE, wait_all, portMAX_DELAY);
    woke_ms = ms_since(t0);
    finish();
}

/* 3: 모여서 출발 */
static volatile int leave_ms[3];

static void staff(void *arg)
{
    int i = (int)(intptr_t)arg;
    static const int ready_ms[3] = { 5, 15, 25 };
    vTaskDelayUntil(&(TickType_t){ t0 }, pdMS_TO_TICKS(ready_ms[i]));
    xEventGroupSync(ev, 1u << i, 0x7, portMAX_DELAY);   /* 내 비트를 세우고, 셋 다 설 때까지 기다림 */
    leave_ms[i] = ms_since(t0);
    finish();
}

static void start(int n, void (*create)(void))
{
    ev = xEventGroupCreate();
    configASSERT(ev != NULL);
    done = 0;
    vTaskSuspendAll();
    t0 = xTaskGetTickCount();
    create();
    xTaskResumeAll();
    while (done < n)
        vTaskDelay(pdMS_TO_TICKS(1));
    vEventGroupDelete(ev);
}

static const struct setter open_list[] = { { BIT_MILK, 10 }, { BIT_POS, 20 }, { BIT_MACHINE, 30 } };
static const struct setter orders[] = { { BIT_DELIVERY, 15 }, { BIT_HALL, 40 } };

static void create_and(void)
{
    wait_mask = BIT_MILK | BIT_POS | BIT_MACHINE;
    wait_all = pdTRUE;
    xTaskCreate(waiter, "opener", configMINIMAL_STACK_SIZE, NULL, WORKER_PRI, NULL);
    for (int i = 0; i < 3; i++)
        xTaskCreate(setter, "set", configMINIMAL_STACK_SIZE, (void *)&open_list[i], WORKER_PRI, NULL);
}

static void create_or(void)
{
    wait_mask = BIT_HALL | BIT_DELIVERY;
    wait_all = pdFALSE;
    xTaskCreate(waiter, "barista", configMINIMAL_STACK_SIZE, NULL, WORKER_PRI, NULL);
    for (int i = 0; i < 2; i++)
        xTaskCreate(setter, "order", configMINIMAL_STACK_SIZE, (void *)&orders[i], WORKER_PRI, NULL);
}

static void create_sync(void)
{
    for (int i = 0; i < 3; i++)
        xTaskCreate(staff, "staff", configMINIMAL_STACK_SIZE, (void *)(intptr_t)i, WORKER_PRI, NULL);
}

static void runner(void *arg)
{
    (void)arg;

    start(4, create_and);
    int and_ms = woke_ms;
    EventBits_t and_bits = woke_bits;
    board_log("모두 기다리기 (AND) : 우유 10ms · 포스기 20ms · 머신 30ms에 체크, 오픈 담당은 %dms에 깨어남 (비트 0x%x)",
              and_ms, (unsigned)and_bits);

    start(3, create_or);
    int or_ms = woke_ms;
    EventBits_t or_bits = woke_bits;
    board_log("아무거나 (OR)       : 배달 15ms · 매장 40ms에 주문, 바리스타는 %dms에 깨어남 (깨운 비트: %s)",
              or_ms, (or_bits & BIT_DELIVERY) ? "배달" : "매장");

    start(3, create_sync);
    board_log("모여서 출발 (Sync)  : 준비 5 · 15 · 25ms, 세 명 모두 %d · %d · %dms에 출발",
              leave_ms[0], leave_ms[1], leave_ms[2]);

    /* 4: 비트는 세지 않음. 다섯 번 세워도 1 */
    ev = xEventGroupCreate();
    for (int i = 0; i < 5; i++)
        xEventGroupSetBits(ev, BIT_ORDER);
    int got = 0;
    while (xEventGroupWaitBits(ev, BIT_ORDER, pdTRUE, pdTRUE, pdMS_TO_TICKS(10)) & BIT_ORDER)
        got++;
    vEventGroupDelete(ev);
    board_log("비트는 세지 않음    : 주문 비트를 5번 세움, 기다려서 받은 횟수 %d번", got);

    int ok = and_ms == 30 && and_bits == 0x7 && or_ms == 15 && (or_bits & BIT_DELIVERY) &&
             leave_ms[0] == 25 && leave_ms[1] == 25 && leave_ms[2] == 25 && got == 1;
    board_finish(ok ? 0 : 1);
    vTaskDelete(NULL);
}

int main(void)
{
    board_init();
    xTaskCreate(runner, "runner", configMINIMAL_STACK_SIZE, NULL, RUNNER_PRI, NULL);
    vTaskStartScheduler();
    return board_exit_code();
}
