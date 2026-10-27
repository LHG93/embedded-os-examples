/*
 * 실시간이란 — FreeRTOS
 *
 * 주기 태스크 P: 10틱마다 3틱 일하고, 다음 주기 전(10틱 안)에 끝내야 함 = 마감
 * 응답 시간 = 주기가 시작된 틱부터 일을 끝낸 틱까지. 틱 단위로 재서 PC · QEMU 어디서나 같음
 * 1. P 혼자
 * 2. 낮은 L이 쉬지 않음    : P는 그대로. 방해가 있어도 결과가 같음 = 결정성
 * 3. 높은 H가 끼어듦       : H(25틱마다 8틱)와 겹치는 주기에 P가 마감을 넘김
 * 4. 주기가 짧은 P를 높게  : 같은 일인데 둘 다 마감을 지킴 (rate monotonic)
 */
#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "board.h"

#define N         10            /* P 주기 수 */
#define P_PERIOD  10
#define P_WORK    3
#define H_PERIOD  25
#define H_WORK    8
#define H_OFFSET  5             /* H는 P보다 5틱 늦게 시작 */
#define H_N       4

static TickType_t t0;
static TickType_t p_resp[N], h_resp[H_N];

/* 자기가 CPU를 쓴 틱을 ticks개 채울 때까지 일함. 밀려나 있던 틱은 세지 않음 */
static void work(int ticks)
{
    TickType_t last = xTaskGetTickCount();
    int done = 0;
    while (done < ticks) {
        TickType_t t = xTaskGetTickCount();
        if (t != last) {                /* 틱 하나를 다 쓰고 다음 틱을 봄 */
            last = t;
            done++;
        }
    }
}

static void p_task(void *arg)
{
    (void)arg;
    TickType_t release = t0;
    vTaskDelay(release - xTaskGetTickCount());  /* 첫 주기까지. DelayUntil은 기준이 과거여야 해서 */
    for (int i = 0; i < N; i++) {
        if (i)
            xTaskDelayUntil(&release, P_PERIOD); /* 늦었으면 기다리지 않고 바로 */
        work(P_WORK);
        p_resp[i] = xTaskGetTickCount() - release;
    }
    vTaskSuspend(NULL);
}

static void h_task(void *arg)
{
    (void)arg;
    TickType_t release = t0 + H_OFFSET;
    vTaskDelay(release - xTaskGetTickCount());
    for (int i = 0; i < H_N; i++) {
        if (i)
            xTaskDelayUntil(&release, H_PERIOD);
        work(H_WORK);
        h_resp[i] = xTaskGetTickCount() - release;
    }
    vTaskSuspend(NULL);
}

static void l_task(void *arg)          /* 쉬지 않음 */
{
    (void)arg;
    for (;;) { }
}

typedef struct {
    int max, miss;
} stat_t;

static stat_t summarize(const TickType_t *r, int n, int deadline, char *out, size_t size)
{
    stat_t s = {0, 0};
    size_t k = 0;
    for (int i = 0; i < n; i++) {
        k += (size_t)snprintf(out + k, size - k, "%s%lu", i ? " " : "", (unsigned long)r[i]);
        if ((int)r[i] > s.max)
            s.max = (int)r[i];
        s.miss += (int)r[i] > deadline;
    }
    return s;
}

/* p_pri · h_pri · l_pri가 0이면 그 태스크는 안 만듦 */
static void scene(UBaseType_t p_pri, UBaseType_t h_pri, UBaseType_t l_pri, stat_t *ps, stat_t *hs,
                  char *pl, char *hl, size_t size)
{
    TaskHandle_t p, h = NULL, l = NULL;
    memset(p_resp, 0, sizeof p_resp);
    memset(h_resp, 0, sizeof h_resp);
    t0 = xTaskGetTickCount() + 20;      /* 다 만든 뒤 같이 출발 */
    xTaskCreate(p_task, "P", configMINIMAL_STACK_SIZE, NULL, p_pri, &p);
    if (h_pri)
        xTaskCreate(h_task, "H", configMINIMAL_STACK_SIZE, NULL, h_pri, &h);
    if (l_pri)
        xTaskCreate(l_task, "L", configMINIMAL_STACK_SIZE, NULL, l_pri, &l);
    vTaskDelay(20 + N * P_PERIOD + 10);
    vTaskDelete(p);
    if (h)
        vTaskDelete(h);
    if (l)
        vTaskDelete(l);
    *ps = summarize(p_resp, N, P_PERIOD, pl, size);
    if (h_pri)
        *hs = summarize(h_resp, H_N, H_PERIOD, hl, size);
}

static void runner(void *arg)
{
    (void)arg;
    static char pl[96], hl[64];
    stat_t ps, hs;
    int ok = 1;

    board_log("주기 태스크 P: %d틱마다 %d틱 일함, 마감 %d틱 (%d번). 숫자는 응답 시간(틱), 괄호는 우선순위",
              P_PERIOD, P_WORK, P_PERIOD, N);

    scene(3, 0, 0, &ps, &hs, pl, hl, sizeof pl);
    board_log("1. P(3) 혼자                          : %s  최대 %d, 마감 넘김 %d", pl, ps.max, ps.miss);
    ok &= ps.max == P_WORK && ps.miss == 0;

    scene(3, 0, 1, &ps, &hs, pl, hl, sizeof pl);
    board_log("2. P(3) + 낮은 L(1)이 쉬지 않음         : %s  최대 %d, 마감 넘김 %d", pl, ps.max, ps.miss);
    ok &= ps.max == P_WORK && ps.miss == 0;

    scene(3, 4, 0, &ps, &hs, pl, hl, sizeof pl);
    board_log("3. P(3) + 높은 H(4)가 %d틱마다 %d틱 일함 : %s  최대 %d, 마감 넘김 %d",
              H_PERIOD, H_WORK, pl, ps.max, ps.miss);
    /* H가 P와 같은 틱에 시작하는 주기(30 · 80틱)가 최악: P_WORK + H_WORK */
    ok &= ps.max == P_WORK + H_WORK && (ps.miss > 0) == (P_WORK + H_WORK > P_PERIOD);

    scene(4, 3, 0, &ps, &hs, pl, hl, sizeof pl);
    board_log("4. 주기가 짧은 P를 높게 P(4) · H(3)     : %s  최대 %d, 마감 넘김 %d", pl, ps.max, ps.miss);
    board_log("                                 H 응답 : %s  최대 %d, 마감 %d틱 넘김 %d", hl, hs.max, H_PERIOD, hs.miss);
    ok &= ps.miss == 0 && hs.miss == 0;

    board_finish(ok ? 0 : 1);
    vTaskDelete(NULL);
}

int main(void)
{
    board_init();
    xTaskCreate(runner, "runner", configMINIMAL_STACK_SIZE * 2, NULL, tskIDLE_PRIORITY + 5, NULL);
    vTaskStartScheduler();
    return board_exit_code();
}
