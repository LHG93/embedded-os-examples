/*
 * 뮤텍스 — FreeRTOS
 *
 * 계좌 두 개 a, b (각 1000). 같은 우선순위 태스크 둘이 서로 1씩 1000번 옮김.
 * 합계 a + b는 언제 봐도 2000이어야 함.
 * 옮기는 태스크는 한쪽을 빼고 taskYIELD() 뒤 다른 쪽을 더함 (두 변수 사이의 틈).
 * 검사 태스크가 그 틈에 합계를 읽어서 2000이 아닌 순간을 셈.
 *
 * 1. 잠금 없이          : 10장 문제(증가가 사라짐)까지 겹쳐서 최종 합계부터 틀림
 * 2. 변수마다 임계 구역  : 변수 하나하나는 안전. 하지만 두 변수 사이의 틈이 보임
 * 3. 뮤텍스            : 빼기부터 더하기까지 한 묶음. 검사도 잠그고 읽어서 틀린 순간 0
 */
#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "board.h"

#define LOOPS      1000
#define TOTAL      2000
#define WORKER_PRI ( tskIDLE_PRIORITY + 2 )
#define RUNNER_PRI ( tskIDLE_PRIORITY + 1 )

enum { UNSAFE, CRITICAL, MUTEX };

static volatile int32_t a, b;
static volatile int mode;
static volatile int done;       /* 끝난 옮기기 태스크 수 */
static volatile int finished;   /* 끝난 태스크 수 (옮기기 둘 + 검사) */
static volatile uint32_t bad;
static SemaphoreHandle_t lock;

static void take(void) { if (mode == MUTEX) xSemaphoreTake(lock, portMAX_DELAY); }
static void give(void) { if (mode == MUTEX) xSemaphoreGive(lock); }

static void add(volatile int32_t *v, int32_t d)
{
    if (mode == UNSAFE) {
        int32_t t = *v;
        taskYIELD();            /* 10장의 틈: 읽기와 쓰기 사이 */
        *v = t + d;
    } else if (mode == CRITICAL) {
        taskENTER_CRITICAL();
        *v += d;
        taskEXIT_CRITICAL();
    } else {
        *v += d;                /* 이미 뮤텍스 안 */
    }
}

static void mover(void *arg)
{
    int dir = (int)(intptr_t)arg;
    volatile int32_t *from = dir ? &a : &b;
    volatile int32_t *to   = dir ? &b : &a;

    for (int i = 0; i < LOOPS; i++) {
        take();
        add(from, -1);
        taskYIELD();            /* 두 변수 사이의 틈: 검사 태스크가 끼어들 수 있음 */
        add(to, +1);
        give();
        taskYIELD();
    }
    taskENTER_CRITICAL();
    done++;
    finished++;
    taskEXIT_CRITICAL();
    vTaskDelete(NULL);
}

static void checker(void *arg)
{
    (void)arg;
    while (done < 2) {
        take();
        if (a + b != TOTAL)
            bad++;
        give();
        taskYIELD();
    }
    taskENTER_CRITICAL();
    finished++;
    taskEXIT_CRITICAL();
    vTaskDelete(NULL);
}

static int32_t run(int m)
{
    mode = m;
    a = b = 1000;
    done = 0;
    finished = 0;
    bad = 0;

    /* 세 태스크를 다 만든 뒤 함께 출발 */
    vTaskSuspendAll();
    xTaskCreate(mover, "mover-ab", configMINIMAL_STACK_SIZE, (void *)1, WORKER_PRI, NULL);
    xTaskCreate(mover, "mover-ba", configMINIMAL_STACK_SIZE, (void *)0, WORKER_PRI, NULL);
    xTaskCreate(checker, "checker", configMINIMAL_STACK_SIZE, NULL, WORKER_PRI, NULL);
    xTaskResumeAll();

    /* 세 태스크가 모두 끝날 때까지 기다림.
     * 뮤텍스를 안 풀고 끝내면 다른 태스크가 영원히 기다려서 여기서 멈춤 */
    while (finished < 3)
        vTaskDelay(pdMS_TO_TICKS(1));
    return a + b;
}

static void runner(void *arg)
{
    (void)arg;
    static const char *names[] = {"잠금 없이  ", "임계 구역  ", "뮤텍스     "};
    int32_t total = 0;

    lock = xSemaphoreCreateMutex();
    configASSERT(lock != NULL);

    for (int m = UNSAFE; m <= MUTEX; m++) {
        total = run(m);
        board_log("%s: 최종 합계 %d (기대값 %d), 도중에 본 틀린 합계 %u번",
                  names[m], (int)total, TOTAL, (unsigned)bad);
    }

    board_finish(total == TOTAL && bad == 0 ? 0 : 1);
    vTaskDelete(NULL);
}

int main(void)
{
    board_init();
    xTaskCreate(runner, "runner", configMINIMAL_STACK_SIZE, NULL, RUNNER_PRI, NULL);
    vTaskStartScheduler();
    return board_exit_code();
}
