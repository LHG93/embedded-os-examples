/*
 * 타이머 — FreeRTOS 소프트웨어 타이머
 *
 * 1. 원샷 · 주기     : 100ms 동안 원샷(50ms)은 한 번, 주기(20ms)는 다섯 번
 * 2. 콜백이 도는 곳   : 타이머 서비스(데몬) 태스크. 인터럽트가 아니라 태스크라서 우선순위가 있음
 * 3. 느린 콜백 하나   : 콜백은 모두 같은 데몬 태스크에서 차례로 돎. 30ms 걸리는 콜백이 끼면
 *                      10ms 주기 타이머의 간격이 그만큼 벌어짐
 * 4. 주기 태스크      : 3ms 일한 뒤 vTaskDelay(10)은 주기가 13ms로 밀림. vTaskDelayUntil은 10ms 그대로
 */
#include <stdint.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
#include "board.h"

#define RUNNER_PRI ( tskIDLE_PRIORITY + 2 )

enum { ONESHOT, PERIODIC, TICKER, SLOW };

static volatile int fired[4];
static volatile TickType_t last_tick, max_gap;
static char cb_task[16];
static volatile UBaseType_t cb_pri;

/* 틱 수만큼 CPU를 씀 */
static void spin_ticks(TickType_t n)
{
    TickType_t start = xTaskGetTickCount();
    while (xTaskGetTickCount() - start < n) { }
}

static void on_timer(TimerHandle_t t)
{
    int id = (int)(intptr_t)pvTimerGetTimerID(t);
    fired[id]++;
    if (id == ONESHOT) {
        strncpy(cb_task, pcTaskGetName(NULL), sizeof cb_task - 1);  /* 콜백을 실행한 태스크 */
        cb_pri = uxTaskPriorityGet(NULL);
    } else if (id == TICKER) {
        TickType_t now = xTaskGetTickCount();
        if (last_tick && now - last_tick > max_gap)
            max_gap = now - last_tick;
        last_tick = now;
    } else if (id == SLOW) {
        spin_ticks(pdMS_TO_TICKS(30));  /* 콜백 안에서 30ms 일함 */
    }
}

static TickType_t periodic_task(int until)
{
    TickType_t start = xTaskGetTickCount(), last = start;
    for (int i = 0; i < 10; i++) {
        spin_ticks(pdMS_TO_TICKS(3));   /* 3ms 일 */
        if (until)
            vTaskDelayUntil(&last, pdMS_TO_TICKS(10));  /* 지난 기한부터 10ms 뒤 */
        else
            vTaskDelay(pdMS_TO_TICKS(10));              /* 지금부터 10ms 뒤 */
    }
    return xTaskGetTickCount() - start;
}

static void runner(void *arg)
{
    (void)arg;

    /* 1. 원샷 · 주기 */
    TimerHandle_t one = xTimerCreate("oven", pdMS_TO_TICKS(50), pdFALSE, (void *)ONESHOT, on_timer);
    TimerHandle_t per = xTimerCreate("hourly", pdMS_TO_TICKS(20), pdTRUE, (void *)PERIODIC, on_timer);
    xTimerStart(one, 0);
    xTimerStart(per, 0);
    vTaskDelay(pdMS_TO_TICKS(105));
    xTimerStop(per, 0);
    vTaskDelay(pdMS_TO_TICKS(5));
    board_log("원샷 · 주기 타이머  : 100ms 동안 원샷(50ms) %d번, 주기(20ms) %d번 실행", fired[ONESHOT], fired[PERIODIC]);

    /* 2. 콜백이 도는 곳 */
    board_log("콜백이 도는 곳      : 타이머 서비스 태스크 \"%s\" (우선순위 %u)", cb_task, (unsigned)cb_pri);

    /* 3. 느린 콜백 하나 */
    TimerHandle_t tick = xTimerCreate("ticker", pdMS_TO_TICKS(10), pdTRUE, (void *)TICKER, on_timer);
    last_tick = max_gap = 0;
    xTimerStart(tick, 0);
    vTaskDelay(pdMS_TO_TICKS(200));
    TickType_t gap_alone = max_gap;
    TimerHandle_t slow = xTimerCreate("slow", pdMS_TO_TICKS(50), pdTRUE, (void *)SLOW, on_timer);
    last_tick = max_gap = 0;
    xTimerStart(slow, 0);
    vTaskDelay(pdMS_TO_TICKS(200));
    xTimerStop(slow, 0);
    xTimerStop(tick, 0);
    TickType_t gap_slow = max_gap;
    board_log("느린 콜백 하나       : 10ms 주기 타이머 간격 최대 %ums → 30ms 걸리는 콜백이 끼면 최대 %ums",
              (unsigned)(gap_alone * portTICK_PERIOD_MS), (unsigned)(gap_slow * portTICK_PERIOD_MS));

    /* 4. 주기 태스크 */
    TickType_t rel = periodic_task(0), abs_ = periodic_task(1);
    board_log("주기 태스크 10번     : 3ms 일 + vTaskDelay(10) = %ums, vTaskDelayUntil(10) = %ums",
              (unsigned)(rel * portTICK_PERIOD_MS), (unsigned)(abs_ * portTICK_PERIOD_MS));

    int ok = fired[ONESHOT] == 1 && fired[PERIODIC] == 5 && cb_pri == configTIMER_TASK_PRIORITY &&
             gap_alone <= pdMS_TO_TICKS(11) && gap_slow >= pdMS_TO_TICKS(30) &&
             rel >= pdMS_TO_TICKS(125) && abs_ <= pdMS_TO_TICKS(101);
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
