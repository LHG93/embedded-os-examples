/*
 * 세마포어 — FreeRTOS
 *
 * 1. 주차장 (카운팅 세마포어): 자리 3개에 차(태스크) 6대.
 *    세마포어 없이 / xSemaphoreCreateCounting(3, 3)으로 동시에 주차한 최대 수 비교
 * 2. 진동벨 (신호 보내기): 바리스타 태스크가 벨을 5번 연달아 울리고, 손님 태스크가 받은 만큼 처리.
 *    바이너리 세마포어는 "울렸다/안 울렸다"만 기억해서 1번만 받음.
 *    카운팅 세마포어는 횟수를 세서 5번 다 받음
 */
#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "board.h"

#define CARS        6
#define SLOTS       3
#define RINGS       5
#define CAR_PRI     ( tskIDLE_PRIORITY + 2 )
#define CUSTOMER_PRI ( tskIDLE_PRIORITY + 2 )
#define BARISTA_PRI ( tskIDLE_PRIORITY + 3 )
#define RUNNER_PRI  ( tskIDLE_PRIORITY + 1 )

static volatile int finished;

static void finish(void)
{
    taskENTER_CRITICAL();
    finished++;
    taskEXIT_CRITICAL();
}

/* 1. 주차장 */
static SemaphoreHandle_t slots;
static volatile int use_sem, parked, max_parked;

static void car(void *arg)
{
    (void)arg;
    if (use_sem)
        xSemaphoreTake(slots, portMAX_DELAY);   /* 빈자리 하나 차지, 없으면 기다림 */

    taskENTER_CRITICAL();
    parked++;
    if (parked > max_parked)
        max_parked = parked;
    taskEXIT_CRITICAL();

    vTaskDelay(pdMS_TO_TICKS(20));              /* 20ms 주차 */

    taskENTER_CRITICAL();
    parked--;
    taskEXIT_CRITICAL();

    if (use_sem)
        xSemaphoreGive(slots);                  /* 자리 반납 */
    finish();
    vTaskDelete(NULL);
}

static int parking(int with_sem)
{
    use_sem = with_sem;
    parked = max_parked = 0;
    finished = 0;
    vTaskSuspendAll();
    for (int i = 0; i < CARS; i++)
        xTaskCreate(car, "car", configMINIMAL_STACK_SIZE, NULL, CAR_PRI, NULL);
    xTaskResumeAll();
    while (finished < CARS)
        vTaskDelay(pdMS_TO_TICKS(1));
    return max_parked;
}

/* 2. 진동벨 */
static SemaphoreHandle_t bell;
static volatile int handled;

static void barista(void *arg)
{
    (void)arg;
    for (int i = 0; i < RINGS; i++)
        xSemaphoreGive(bell);           /* 바이너리는 이미 울린 상태면 실패(무시)됨 */
    finish();
    vTaskDelete(NULL);
}

static void customer(void *arg)
{
    (void)arg;
    /* 벨이 50ms 동안 더 안 울리면 끝 */
    while (xSemaphoreTake(bell, pdMS_TO_TICKS(50)) == pdTRUE)
        handled++;
    finish();
    vTaskDelete(NULL);
}

static int rings(SemaphoreHandle_t s)
{
    bell = s;
    handled = 0;
    finished = 0;
    vTaskSuspendAll();
    xTaskCreate(customer, "customer", configMINIMAL_STACK_SIZE, NULL, CUSTOMER_PRI, NULL);
    xTaskCreate(barista, "barista", configMINIMAL_STACK_SIZE, NULL, BARISTA_PRI, NULL);
    xTaskResumeAll();                   /* 우선순위가 높은 바리스타가 먼저 5번 울림 */
    while (finished < 2)
        vTaskDelay(pdMS_TO_TICKS(1));
    vSemaphoreDelete(s);
    return handled;
}

static void runner(void *arg)
{
    (void)arg;
    slots = xSemaphoreCreateCounting(SLOTS, SLOTS);
    configASSERT(slots != NULL);

    int a = parking(0);
    int b = parking(1);
    board_log("세마포어 없이 : 동시에 주차한 차 최대 %d대 (자리 %d개)", a, SLOTS);
    board_log("카운팅 세마포어: 동시에 주차한 차 최대 %d대 (자리 %d개)", b, SLOTS);

    int bin = rings(xSemaphoreCreateBinary());
    int cnt = rings(xSemaphoreCreateCounting(RINGS, 0));
    board_log("바이너리 벨   : %d번 울림, 받은 신호 %d번 (%d번 사라짐)", RINGS, bin, RINGS - bin);
    board_log("카운팅 벨     : %d번 울림, 받은 신호 %d번", RINGS, cnt);

    board_finish(b <= SLOTS && bin == 1 && cnt == RINGS ? 0 : 1);
    vTaskDelete(NULL);
}

int main(void)
{
    board_init();
    xTaskCreate(runner, "runner", configMINIMAL_STACK_SIZE, NULL, RUNNER_PRI, NULL);
    vTaskStartScheduler();
    return board_exit_code();
}
