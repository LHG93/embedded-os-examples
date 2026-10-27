/*
 * 큐 — FreeRTOS
 *
 * 주문서 꽂이: struct order를 길이 3짜리 큐로 주고받음
 * 1. 순서 · 꽉 차면 기다림  : 캐셔가 주문 8개를 한꺼번에 넣고, 바리스타는 5ms마다 하나씩 뺌
 * 2. 값 복사 vs 포인터      : 보낸 뒤 원래 메모를 고치면? 값으로 보낸 건 그대로, 포인터로 보낸 건 바뀜
 * 3. 기다리지 않고 넣기      : 대기 시간 0으로 넣으면 꽉 찼을 때 바로 실패 (errQUEUE_FULL)
 * 4. 최신 값 하나 (메일박스) : 길이 1 큐에 xQueueOverwrite로 덮어쓰고 xQueuePeek로 읽음
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "board.h"

#define LEN         3
#define ORDERS      8
#define CASHIER_PRI ( tskIDLE_PRIORITY + 2 )
#define BARISTA_PRI ( tskIDLE_PRIORITY + 3 )
#define RUNNER_PRI  ( tskIDLE_PRIORITY + 1 )

struct order {
    int no;
    char drink[24];
};

static QueueHandle_t q;
static volatile int done, blocked;
static char got[64];

/* 1: 캐셔 · 바리스타 */
static void cashier(void *arg)
{
    (void)arg;
    for (int i = 1; i <= ORDERS; i++) {
        struct order o = { i, "라테" };
        if (uxQueueSpacesAvailable(q) == 0)
            blocked++;                          /* 꽂이가 차서 여기서 기다리게 됨 */
        xQueueSend(q, &o, portMAX_DELAY);       /* o의 내용이 큐 안으로 복사됨 */
    }
    done++;
    vTaskDelete(NULL);
}

static void barista(void *arg)
{
    (void)arg;
    struct order o;
    for (int i = 0; i < ORDERS; i++) {
        xQueueReceive(q, &o, portMAX_DELAY);    /* 비어 있으면 여기서 기다림 */
        snprintf(got + strlen(got), sizeof got - strlen(got), "%s%d", i ? " " : "", o.no);
        vTaskDelay(pdMS_TO_TICKS(5));           /* 음료 만드는 시간 */
    }
    done++;
    vTaskDelete(NULL);
}

static void runner(void *arg)
{
    (void)arg;

    /* 1. 순서 · 꽉 차면 기다림 */
    q = xQueueCreate(LEN, sizeof(struct order));
    configASSERT(q != NULL);
    vTaskSuspendAll();
    xTaskCreate(cashier, "cashier", configMINIMAL_STACK_SIZE, NULL, CASHIER_PRI, NULL);
    xTaskCreate(barista, "barista", configMINIMAL_STACK_SIZE, NULL, BARISTA_PRI, NULL);
    xTaskResumeAll();
    while (done < 2)
        vTaskDelay(pdMS_TO_TICKS(1));
    vQueueDelete(q);
    board_log("순서 · 꽉 차면 기다림 : 받은 순서 %s, 캐셔가 꽂이가 차서 기다린 횟수 %d번", got, blocked);

    /* 2. 값 복사 vs 포인터 */
    struct order memo = { 9, "라테" }, by_value;
    q = xQueueCreate(LEN, sizeof(struct order));
    xQueueSend(q, &memo, 0);
    strcpy(memo.drink, "아메리카노");           /* 보낸 뒤 원래 메모를 고침 */
    xQueueReceive(q, &by_value, 0);
    vQueueDelete(q);

    struct order memo2 = { 10, "라테" }, *by_pointer;
    struct order *p = &memo2;
    q = xQueueCreate(LEN, sizeof(struct order *));
    xQueueSend(q, &p, 0);                       /* 주소만 복사됨 */
    strcpy(memo2.drink, "아메리카노");
    xQueueReceive(q, &by_pointer, 0);
    vQueueDelete(q);
    board_log("값 복사              : 보낸 뒤 메모를 아메리카노로 고쳐도 받은 주문은 %s", by_value.drink);
    board_log("포인터로 보내면       : 받은 주문이 %s (보낸 뒤 고친 내용이 보임)", by_pointer->drink);

    /* 3. 기다리지 않고 넣기 */
    q = xQueueCreate(LEN, sizeof(struct order));
    int ok = 0, dropped = 0;
    for (int i = 1; i <= 5; i++) {
        struct order o = { i, "라테" };
        if (xQueueSend(q, &o, 0) == pdPASS)     /* 대기 시간 0: 꽉 차 있으면 바로 실패 */
            ok++;
        else
            dropped++;
    }
    vQueueDelete(q);
    board_log("기다리지 않고 넣기    : 5개 중 %d개 들어감, %d개 버림 (errQUEUE_FULL)", ok, dropped);

    /* 4. 최신 값 하나 (메일박스) */
    QueueHandle_t box = xQueueCreate(1, sizeof(int));
    for (int t = 20; t <= 24; t++)
        xQueueOverwrite(box, &t);               /* 꽉 차 있어도 덮어씀 */
    int v1 = 0, v2 = 0;
    xQueuePeek(box, &v1, 0);                    /* 꺼내지 않고 읽기 */
    xQueuePeek(box, &v2, 0);
    UBaseType_t left = uxQueueMessagesWaiting(box);
    vQueueDelete(box);
    board_log("최신 값 하나 (메일박스): 20~24를 덮어쓰고 읽은 값 %d, 한 번 더 읽어도 %d (남은 개수 %u)", v1, v2, (unsigned)left);

    int pass = strcmp(got, "1 2 3 4 5 6 7 8") == 0 && blocked > 0 &&
               strcmp(by_value.drink, "라테") == 0 && strcmp(by_pointer->drink, "아메리카노") == 0 &&
               ok == LEN && dropped == 5 - LEN && v1 == 24 && v2 == 24 && left == 1;
    board_finish(pass ? 0 : 1);
    vTaskDelete(NULL);
}

int main(void)
{
    board_init();
    xTaskCreate(runner, "runner", configMINIMAL_STACK_SIZE, NULL, RUNNER_PRI, NULL);
    vTaskStartScheduler();
    return board_exit_code();
}
