/*
 * 태스크 — FreeRTOS
 *
 * 1. 상태       : 태스크를 실행 · 준비 · 차단 · 일시 정지 상태로 만들고 eTaskGetState로 확인
 * 2. 태스크 목록 : vTaskListTasks. ps처럼 이름 · 상태 · 우선순위 · 스택 여유 · 번호
 * 3. 메모리 공유 : 모든 태스크가 같은 전역 변수를 봄 (주소 공간이 하나뿐)
 */
#include <stdint.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "board.h"

static SemaphoreHandle_t never;
static volatile int shared;
static volatile uintptr_t seen_addr;

static void ready_task(void *arg)       /* 우선순위가 낮아 runner가 도는 동안 줄만 섬 */
{
    (void)arg;
    for (;;) { }
}

static void sleep_task(void *arg)       /* 1초 잠 = 차단 */
{
    (void)arg;
    for (;;)
        vTaskDelay(pdMS_TO_TICKS(1000));
}

static void wait_task(void *arg)        /* 오지 않는 신호를 기다림 = 차단 */
{
    (void)arg;
    xSemaphoreTake(never, portMAX_DELAY);
    vTaskDelete(NULL);
}

static void writer_task(void *arg)
{
    (void)arg;
    shared = 42;                        /* 다른 태스크가 그대로 봄 */
    seen_addr = (uintptr_t)&shared;
    vTaskSuspend(NULL);                 /* 스스로 일시 정지 */
}

static const char *state_name(eTaskState s)
{
    switch (s) {
    case eRunning:   return "실행 (Running)";
    case eReady:     return "준비 (Ready)";
    case eBlocked:   return "차단 (Blocked)";
    case eSuspended: return "일시 정지 (Suspended)";
    case eDeleted:   return "지워짐 (Deleted)";
    default:         return "?";
    }
}

static void runner(void *arg)
{
    (void)arg;
    TaskHandle_t me = xTaskGetCurrentTaskHandle(), ready, sleeper, waiter, writer;
    never = xSemaphoreCreateBinary();

    xTaskCreate(ready_task, "ready", configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY + 1, &ready);
    xTaskCreate(sleep_task, "sleeper", configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY + 3, &sleeper);
    xTaskCreate(wait_task, "waiter", configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY + 3, &waiter);
    xTaskCreate(writer_task, "writer", configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY + 3, &writer);
    /* 우선순위 3인 셋이 먼저 돌고 각자 잠들거나 멈춤. 그동안 runner(2)는 준비 상태로 기다림 */

    /* 1. 상태 */
    board_log("태스크 상태 (eTaskGetState):");
    board_log("  runner  : %s", state_name(eTaskGetState(me)));
    board_log("  ready   : %s  ← 우선순위가 낮아 줄만 섬", state_name(eTaskGetState(ready)));
    board_log("  sleeper : %s  ← vTaskDelay로 잠", state_name(eTaskGetState(sleeper)));
    board_log("  waiter  : %s  ← 세마포어를 기다림", state_name(eTaskGetState(waiter)));
    board_log("  writer  : %s  ← vTaskSuspend로 멈춤", state_name(eTaskGetState(writer)));

    /* 2. 태스크 목록 */
    static char list[512];
    vTaskListTasks(list, sizeof list);
    board_log("태스크 목록 (vTaskListTasks, 상태 X 실행 · R 준비 · B 차단 · S 일시 정지):");
    board_log("이름\t상태\t우선\t스택여유\t번호\n%s", list);

    /* 3. 메모리 공유 */
    board_log("메모리 공유 : writer가 shared = 42로 바꿈 → runner가 본 값 %d (주소 0x%lx로 같음: %s)",
              shared, (unsigned long)(uintptr_t)&shared, seen_addr == (uintptr_t)&shared ? "예" : "아니오");

    int ok = eTaskGetState(me) == eRunning && eTaskGetState(ready) == eReady &&
             eTaskGetState(sleeper) == eBlocked && eTaskGetState(waiter) == eBlocked &&
             eTaskGetState(writer) == eSuspended && shared == 42 && seen_addr == (uintptr_t)&shared &&
             strstr(list, "runner") != NULL;
    board_finish(ok ? 0 : 1);
    vTaskDelete(NULL);
}

int main(void)
{
    board_init();
    xTaskCreate(runner, "runner", configMINIMAL_STACK_SIZE * 2, NULL, tskIDLE_PRIORITY + 2, NULL);
    vTaskStartScheduler();
    return board_exit_code();
}
