/*
 * ISR에서 태스크로 넘기기 — FreeRTOS on Cortex-M3 (QEMU mps2-an385 전용)
 *
 * 타이머 인터럽트가 약 10ms마다 100번 옴. ISR은 짧게 알리기만 하고, 일은 처리 태스크가 함
 * ISR이 알린 순간부터 처리 태스크가 시작할 때까지 걸린 시간을 25MHz 시계로 잼
 * 1. 세마포어 + 바로 전환    : xSemaphoreGiveFromISR(&woken) 뒤 portYIELD_FROM_ISR(woken)
 * 2. 세마포어, 전환 요청 없이 : 깨운 태스크가 다음 틱(최대 1ms)까지 기다림
 * 3. 태스크 알림 + 바로 전환  : vTaskNotifyGiveFromISR. 세마포어보다 가벼움
 * 4. 큐로 값 넘기기          : xQueueSendFromISR로 측정값을 넘김. 빠짐 · 순서 확인
 */
#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "queue.h"
#include "board.h"
#include "mps2.h"

#define EVENTS     100
#define PERIOD_US  10300                /* 틱(1ms)과 박자가 어긋나게 */
#define WORKER_PRI ( tskIDLE_PRIORITY + 3 )
#define RUNNER_PRI ( tskIDLE_PRIORITY + 1 )

enum { SEM_YIELD, SEM_NOYIELD, NOTIFY, QUEUE };

static SemaphoreHandle_t sem;
static QueueHandle_t q;
static TaskHandle_t worker;
static volatile int mode, fired, handled, in_order;
static volatile uint32_t stamp, lat_sum, lat_max;

void TIMER0_Handler(void)
{
    BaseType_t woken = pdFALSE;
    TIMER_INTCLR(TIMER0_BASE) = 1u;
    if (++fired == EVENTS)
        TIMER_CTRL(TIMER0_BASE) = 0;    /* 100번 채우면 타이머 끔 */
    stamp = clock_now();

    switch (mode) {
    case SEM_YIELD:
        xSemaphoreGiveFromISR(sem, &woken);
        portYIELD_FROM_ISR(woken);      /* 더 높은 태스크가 깼으면 ISR 끝나자마자 그 태스크로 */
        break;
    case SEM_NOYIELD:
        xSemaphoreGiveFromISR(sem, NULL);   /* 전환 요청 없음: 다음 틱에서야 전환 */
        break;
    case NOTIFY:
        vTaskNotifyGiveFromISR(worker, &woken);
        portYIELD_FROM_ISR(woken);
        break;
    case QUEUE: {
        uint32_t sample = (uint32_t)fired;  /* 측정값 대신 순번 */
        xQueueSendFromISR(q, &sample, &woken);
        portYIELD_FROM_ISR(woken);
        break;
    }
    }
}

static void worker_task(void *arg)
{
    (void)arg;
    uint32_t expect = 1;
    for (;;) {
        if (mode == NOTIFY) {
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        } else if (mode == QUEUE) {
            uint32_t sample;
            xQueueReceive(q, &sample, portMAX_DELAY);
            if (sample != expect++)
                in_order = 0;
        } else {
            xSemaphoreTake(sem, portMAX_DELAY);
        }
        uint32_t lat = clock_now() - stamp;     /* ISR이 알린 순간부터 여기까지 */
        lat_sum += lat;
        if (lat > lat_max)
            lat_max = lat;
        handled++;
    }
}

static void run(int m)
{
    mode = m;
    fired = handled = 0;
    lat_sum = lat_max = 0;
    in_order = 1;
    /* 방식마다 처리 태스크를 새로 만듦 (앞 방식의 세마포어에서 기다리는 채로 남지 않게) */
    xTaskCreate(worker_task, "worker", configMINIMAL_STACK_SIZE, NULL, WORKER_PRI, &worker);
    timer_start(TIMER0_BASE, TIMER0_IRQn, PERIOD_US, 0xC0);   /* 0x40 이상이라 FromISR API 가능 */
    while (fired < EVENTS)
        vTaskDelay(pdMS_TO_TICKS(10));
    for (int i = 0; i < 20 && handled < EVENTS; i++)
        vTaskDelay(pdMS_TO_TICKS(10));                        /* 남은 처리를 최대 200ms 더 기다림 */
    timer_stop(TIMER0_BASE, TIMER0_IRQn);
    vTaskDelete(worker);
}

#define US(c) ((unsigned long)((c) / 25u))

static void runner(void *arg)
{
    (void)arg;
    static const char *names[] = {
        "세마포어 + 바로 전환    ", "세마포어, 전환 요청 없이", "태스크 알림 + 바로 전환 ", "큐로 값 넘기기         "
    };
    unsigned long avg[4], max[4];
    int order = 0, done[4];

    clock_start();
    sem = xSemaphoreCreateBinary();
    q = xQueueCreate(8, sizeof(uint32_t));

    for (int m = SEM_YIELD; m <= QUEUE; m++) {
        run(m);
        done[m] = handled;
        avg[m] = handled ? US(lat_sum / (uint32_t)handled) : 0;
        max[m] = US(lat_max);
        if (m == QUEUE)
            order = in_order;
        board_log("%s: ISR → 처리 태스크 시작까지 평균 %4luus, 최대 %4luus (%d번 중 %d번 처리)",
                  names[m], avg[m], max[m], EVENTS, done[m]);
    }
    board_log("큐로 넘긴 값          : 순서 %s", order ? "그대로" : "빠지거나 뒤섞임");

    int ok = avg[SEM_YIELD] < 100 && avg[SEM_NOYIELD] > 100 && max[SEM_NOYIELD] > 700 &&
             avg[NOTIFY] < 100 && avg[QUEUE] < 100 && order;
    for (int m = SEM_YIELD; m <= QUEUE; m++)
        ok = ok && done[m] == EVENTS;
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
