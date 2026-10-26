/*
 * 레이스 컨디션 — FreeRTOS
 *
 * 같은 우선순위 태스크 두 개가 공유 변수 counter를 각각 1000번씩 1 증가.
 * 기대값 2000.
 *
 * MCU는 보통 코어가 하나라 두 태스크가 "동시에" 돌지 않음.
 * 그래도 레이스 발생. 읽기와 쓰기 사이에 문맥 전환(틱 인터럽트, 더 높은 우선순위 태스크,
 * 직접 양보)이 끼면 다른 태스크가 같은 값을 읽음.
 * 여기선 그 틈을 taskYIELD()로 일부러 만들어 결과가 매번 같게 함.
 *
 * 해결 하나: 임계 구역(taskENTER_CRITICAL ~ taskEXIT_CRITICAL).
 * 그 사이에선 문맥 전환 없음. 뮤텍스 해결은 다음 장.
 */
#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"
#include "board.h"

#define LOOPS     1000
#define WORKER_PRI ( tskIDLE_PRIORITY + 2 )
#define RUNNER_PRI ( tskIDLE_PRIORITY + 1 )

static volatile uint32_t counter;

/* 잠금 없이: 읽고, 틈을 주고, 씀 */
static void add_unsafe(void *arg)
{
    (void)arg;
    for (int i = 0; i < LOOPS; i++) {
        uint32_t v = counter;   /* 읽기 */
        taskYIELD();            /* 이 틈에 다른 태스크가 같은 값을 읽음 */
        counter = v + 1;        /* 쓰기: 상대가 더한 1을 덮어씀 */
    }
    vTaskDelete(NULL);
}

/* 임계 구역: 읽기부터 쓰기까지 문맥 전환 없음. 양보는 임계 구역 밖에서 */
static void add_safe(void *arg)
{
    (void)arg;
    for (int i = 0; i < LOOPS; i++) {
        taskENTER_CRITICAL();
        uint32_t v = counter;
        counter = v + 1;
        taskEXIT_CRITICAL();
        taskYIELD();
    }
    vTaskDelete(NULL);
}

/* 같은 함수로 태스크 두 개 생성, 둘 다 끝나면 counter 반환 */
static uint32_t run_two(TaskFunction_t fn)
{
    counter = 0;

    /* 두 태스크를 다 만든 뒤 함께 출발시키려고 스케줄러 잠시 정지.
     * 안 하면 먼저 만든 태스크가 혼자 끝까지 실행됨 */
    vTaskSuspendAll();
    xTaskCreate(fn, "worker-a", configMINIMAL_STACK_SIZE, NULL, WORKER_PRI, NULL);
    xTaskCreate(fn, "worker-b", configMINIMAL_STACK_SIZE, NULL, WORKER_PRI, NULL);
    xTaskResumeAll();

    /* 작업 태스크가 우선순위가 더 높아서, 둘 다 지워져야 여기로 돌아옴 */
    return counter;
}

static void runner(void *arg)
{
    (void)arg;
    const uint32_t expected = 2 * LOOPS;

    uint32_t unsafe = run_two(add_unsafe);
    board_log("잠금 없이   : %u (기대값 %u, 잃어버린 증가 %u번)",
              (unsigned)unsafe, (unsigned)expected, (unsigned)(expected - unsafe));

    uint32_t safe = run_two(add_safe);
    board_log("임계 구역   : %u (기대값 %u)", (unsigned)safe, (unsigned)expected);

    board_finish(safe == expected ? 0 : 1);
    vTaskDelete(NULL);
}

int main(void)
{
    board_init();
    xTaskCreate(runner, "runner", configMINIMAL_STACK_SIZE, NULL, RUNNER_PRI, NULL);
    vTaskStartScheduler();
    return board_exit_code();
}
