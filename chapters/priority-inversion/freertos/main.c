/*
 * 우선순위 역전 — FreeRTOS
 *
 * 태스크 셋: L(낮음) · M(중간) · H(높음). L과 H만 같은 잠금을 씀. M은 잠금 없이 CPU만 오래 씀
 *   0ms  L이 잠금을 잡고 20틱 분량 일을 시작
 *   2ms  H가 깨어나 잠금을 기다림 (차단)
 *   5ms  M이 깨어나 100틱 동안 CPU를 씀
 *
 * 1. 바이너리 세마포어로 잠금 : 우선순위 상속 없음. M이 L을 밀어내서 H가 M이 끝날 때까지 기다림
 * 2. 뮤텍스로 잠금           : H가 기다리는 동안 L이 H의 우선순위를 물려받아 M보다 먼저 끝냄
 */
#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "board.h"

#define L_PRI      ( tskIDLE_PRIORITY + 2 )
#define M_PRI      ( tskIDLE_PRIORITY + 3 )
#define H_PRI      ( tskIDLE_PRIORITY + 4 )
#define RUNNER_PRI ( tskIDLE_PRIORITY + 1 )
#define L_WORK     20      /* 틱 */
#define M_WORK     100     /* 틱 */

static SemaphoreHandle_t lock;
static volatile int done;
static volatile TickType_t h_wait;
static volatile UBaseType_t l_max_pri;

/* 자기가 실행된 틱 수만큼 CPU를 씀. 밀려나 있던 시간은 세지 않음 */
static void spin(int ticks, int watch_pri)
{
    TickType_t last = xTaskGetTickCount();
    for (int n = 0; n < ticks; ) {
        TickType_t now = xTaskGetTickCount();
        if (now != last) {
            n++;
            last = now;
        }
        if (watch_pri && uxTaskPriorityGet(NULL) > l_max_pri)
            l_max_pri = uxTaskPriorityGet(NULL);   /* 물려받은 우선순위도 보임 */
    }
}

static void finish(void)
{
    taskENTER_CRITICAL();
    done++;
    taskEXIT_CRITICAL();
    vTaskDelete(NULL);
}

static void task_l(void *arg)
{
    (void)arg;
    xSemaphoreTake(lock, portMAX_DELAY);
    spin(L_WORK, 1);
    xSemaphoreGive(lock);
    finish();
}

static void task_m(void *arg)
{
    (void)arg;
    vTaskDelay(5);
    spin(M_WORK, 0);                    /* 잠금과 상관없는 긴 일 */
    finish();
}

static void task_h(void *arg)
{
    (void)arg;
    vTaskDelay(2);
    TickType_t t0 = xTaskGetTickCount();
    xSemaphoreTake(lock, portMAX_DELAY);
    h_wait = xTaskGetTickCount() - t0;
    xSemaphoreGive(lock);
    finish();
}

static void run(int use_mutex)
{
    lock = use_mutex ? xSemaphoreCreateMutex() : xSemaphoreCreateBinary();
    configASSERT(lock != NULL);
    if (!use_mutex)
        xSemaphoreGive(lock);           /* 바이너리 세마포어는 비어서 시작하므로 한 번 채움 */
    done = 0;
    h_wait = 0;
    l_max_pri = 0;

    vTaskSuspendAll();
    xTaskCreate(task_l, "L", configMINIMAL_STACK_SIZE, NULL, L_PRI, NULL);
    xTaskCreate(task_m, "M", configMINIMAL_STACK_SIZE, NULL, M_PRI, NULL);
    xTaskCreate(task_h, "H", configMINIMAL_STACK_SIZE, NULL, H_PRI, NULL);
    xTaskResumeAll();

    while (done < 3)
        vTaskDelay(pdMS_TO_TICKS(1));
    vSemaphoreDelete(lock);
}

static void runner(void *arg)
{
    (void)arg;

    run(0);
    TickType_t sem_wait = h_wait;
    UBaseType_t sem_pri = l_max_pri;
    board_log("바이너리 세마포어 : H가 잠금을 기다린 시간 %3ums, 그동안 L의 우선순위 최대 %u (M이 끼어듦)",
              (unsigned)(sem_wait * portTICK_PERIOD_MS), (unsigned)sem_pri);

    run(1);
    board_log("뮤텍스            : H가 잠금을 기다린 시간 %3ums, 그동안 L의 우선순위 최대 %u (H의 우선순위를 물려받음)",
              (unsigned)(h_wait * portTICK_PERIOD_MS), (unsigned)l_max_pri);

    board_finish(sem_wait > M_WORK && sem_pri == L_PRI && h_wait < M_WORK / 2 && l_max_pri == H_PRI ? 0 : 1);
    vTaskDelete(NULL);
}

int main(void)
{
    board_init();
    xTaskCreate(runner, "runner", configMINIMAL_STACK_SIZE, NULL, RUNNER_PRI, NULL);
    vTaskStartScheduler();
    return board_exit_code();
}
