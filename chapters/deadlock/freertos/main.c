/*
 * 데드락 — FreeRTOS
 *
 * 우유 · 시럽 뮤텍스 두 개. 직원 A · B 태스크(같은 우선순위)가 둘 다 쥐고 음료를 ROUNDS잔씩 만듦.
 * 첫 잔에서 첫 번째 뮤텍스를 잡은 뒤 vTaskDelay(1)로 상대에게 차례를 넘김
 *
 * 1. 순서 반대, 무한 대기      : 데드락. 러너가 500ms 지켜보고 판정, 두 태스크를 지움
 * 2. 순서 반대, 100ms 시간 제한 : 못 잡으면 쥔 것을 놓고 조금 쉬었다 다시. 완료
 * 3. 순서 고정 (항상 우유 먼저) : 완료
 */
#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "board.h"

#define ROUNDS     1000
#define STAFF_PRI  ( tskIDLE_PRIORITY + 2 )
#define RUNNER_PRI ( tskIDLE_PRIORITY + 1 )

enum { OPPOSITE, TIMEOUT, ORDERED };

static SemaphoreHandle_t milk, syrup;
static TaskHandle_t ta, tb;
static volatile int mode, done;
static volatile uint32_t drinks, timeouts;

static void staff(void *arg)
{
    int is_a = (int)(intptr_t)arg;     /* A는 우유 먼저, B는 시럽 먼저 */
    SemaphoreHandle_t first  = is_a ? milk  : syrup;
    SemaphoreHandle_t second = is_a ? syrup : milk;
    TickType_t wait = mode == TIMEOUT ? pdMS_TO_TICKS(100) : portMAX_DELAY;
    int tried = 0;

    if (mode == ORDERED) {             /* 규칙: 항상 우유 먼저 */
        first  = milk;
        second = syrup;
    }

    for (int i = 0; i < ROUNDS; ) {
        xSemaphoreTake(first, portMAX_DELAY);
        if (!tried++)
            vTaskDelay(1);             /* 첫 잔: 상대가 다른 쪽을 잡을 틈 */

        if (xSemaphoreTake(second, wait) != pdTRUE) {
            xSemaphoreGive(first);     /* 시간 초과: 쥔 것을 놓고 */
            taskENTER_CRITICAL();
            timeouts++;
            taskEXIT_CRITICAL();
            vTaskDelay(is_a ? 1 : 3);  /* 조금 쉬었다 다시. 둘이 같은 박자로 부딪치지 않게 다르게 */
            continue;
        }

        taskENTER_CRITICAL();
        drinks++;
        taskEXIT_CRITICAL();
        xSemaphoreGive(second);
        xSemaphoreGive(first);
        i++;
    }
    taskENTER_CRITICAL();
    done++;
    taskEXIT_CRITICAL();
    vTaskDelete(NULL);
}

/* 500ms 안에 둘 다 끝나면 1, 아니면 0 (데드락) */
static int run(int m)
{
    mode = m;
    done = 0;
    drinks = timeouts = 0;
    milk = xSemaphoreCreateMutex();
    syrup = xSemaphoreCreateMutex();
    configASSERT(milk != NULL && syrup != NULL);

    vTaskSuspendAll();
    xTaskCreate(staff, "A", configMINIMAL_STACK_SIZE, (void *)1, STAFF_PRI, &ta);
    xTaskCreate(staff, "B", configMINIMAL_STACK_SIZE, (void *)0, STAFF_PRI, &tb);
    xTaskResumeAll();

    for (int ms = 0; ms < 500 && done < 2; ms++)
        vTaskDelay(pdMS_TO_TICKS(1));
    if (done < 2)
        return 0;
    vSemaphoreDelete(milk);
    vSemaphoreDelete(syrup);
    return 1;
}

static void runner(void *arg)
{
    (void)arg;

    int r1 = run(OPPOSITE);
    if (r1) {
        board_log("순서 반대, 무한 대기  : 완료 (이번엔 운 좋게 엇갈리지 않음)");
    } else {
        int milk_a = xSemaphoreGetMutexHolder(milk) == ta;
        int syrup_b = xSemaphoreGetMutexHolder(syrup) == tb;
        board_log("순서 반대, 무한 대기  : 데드락. 500ms 동안 둘 다 못 끝냄 (우유 주인 %s, 시럽 주인 %s)",
                  milk_a ? "A" : "?", syrup_b ? "B" : "?");
        vTaskDelete(ta);               /* 멈춘 두 태스크 정리. 쥐고 있던 뮤텍스는 그대로 버림 */
        vTaskDelete(tb);
    }

    int r2 = run(TIMEOUT);
    uint32_t d2 = drinks, t2 = timeouts;
    board_log("순서 반대, 100ms 제한 : %s, 음료 %u잔, 시간 초과로 놓고 다시 한 횟수 %u번",
              r2 ? "완료" : "멈춤", (unsigned)d2, (unsigned)t2);

    int r3 = run(ORDERED);
    board_log("순서 고정 (우유 먼저) : %s, 음료 %u잔", r3 ? "완료" : "멈춤", (unsigned)drinks);

    board_finish(!r1 && r2 && t2 > 0 && d2 == 2 * ROUNDS && r3 && drinks == 2 * ROUNDS ? 0 : 1);
    vTaskDelete(NULL);
}

int main(void)
{
    board_init();
    xTaskCreate(runner, "runner", configMINIMAL_STACK_SIZE, NULL, RUNNER_PRI, NULL);
    vTaskStartScheduler();
    return board_exit_code();
}
