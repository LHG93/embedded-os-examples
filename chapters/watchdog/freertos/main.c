/*
 * 워치독 — FreeRTOS on Cortex-M3 (QEMU mps2-an385 전용)
 *
 * CMSDK 워치독: 100ms 안에 킥(카운터 다시 채우기)이 없으면 NMI로 경고, 한 번 더 100ms 지나면 리셋
 * 태스크 A · B가 50ms마다 "살아 있음"을 체크인하고, 감시 태스크는 둘 다 체크인했을 때만 킥
 * 1. 정상      : 500ms 동안 킥 10번, 경고 0번
 * 2. B가 멈춤  : B가 무한 루프에 빠짐 → 감시 태스크가 킥을 멈춤 → 100ms 뒤 NMI 경고 → 100ms 뒤 리셋
 *               (QEMU는 -no-reboot라 리셋되면 끝남. 실제 보드는 처음부터 다시 시작)
 */
#include <stdint.h>
#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"
#include "board.h"
#include "mps2.h"

#define CHECK_MS   50
#define TIMEOUT_MS 100
#define WORKER_PRI ( tskIDLE_PRIORITY + 2 )
#define WATCH_PRI  ( tskIDLE_PRIORITY + 3 )

static volatile uint32_t checkin;           /* bit0 = A, bit1 = B */
static volatile int b_hang, kicks, nmi_count;
static volatile TickType_t b_last, warned_at;

static void wdog_start(uint32_t timeout_ms)
{
    WDOG_LOCK = WDOG_UNLOCK_KEY;            /* 잠금을 풀어야 설정 가능 */
    WDOG_LOAD = timeout_ms * (CPU_HZ / 1000u);
    WDOG_CTRL = (1u << 0) | (1u << 1);      /* 0이 되면 인터럽트, 한 번 더 0이 되면 리셋 */
    WDOG_LOCK = 0;
}

static void wdog_kick(void)
{
    WDOG_LOCK = WDOG_UNLOCK_KEY;
    WDOG_INTCLR = 1u;                       /* 카운터를 LOAD부터 다시 */
    WDOG_LOCK = 0;
}

/* 워치독 경고. 커널 API는 못 씀. 기록만 남기고 리셋을 기다림 */
void NMI_Handler(void)
{
    char buf[120];
    nmi_count++;
    snprintf(buf, sizeof buf, "[워치독 경고] 킥 없이 %dms 지남 (B 마지막 체크인 뒤 %lums) → %dms 뒤 리셋",
             TIMEOUT_MS, (unsigned long)((xTaskGetTickCountFromISR() - b_last) * portTICK_PERIOD_MS), TIMEOUT_MS);
    board_raw_log(buf);
    for (;;) { }                            /* 실제 제품은 여기서 원인 기록(플래시 등) */
}

static void worker(void *arg)
{
    uint32_t bit = (uint32_t)(uintptr_t)arg;
    for (;;) {
        if (bit == 2u && b_hang)
            for (;;) { }                    /* B가 멈춤 (무한 루프, 데드락 등) */
        taskENTER_CRITICAL();
        checkin |= bit;                     /* 살아 있음 */
        taskEXIT_CRITICAL();
        if (bit == 2u)
            b_last = xTaskGetTickCount();
        vTaskDelay(pdMS_TO_TICKS(CHECK_MS));
    }
}

static void watcher(void *arg)
{
    (void)arg;
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        vTaskDelayUntil(&last, pdMS_TO_TICKS(CHECK_MS));
        taskENTER_CRITICAL();
        uint32_t seen = checkin;
        checkin = 0;
        taskEXIT_CRITICAL();
        if (seen == 3u) {
            wdog_kick();                    /* 둘 다 살아 있을 때만 */
            kicks++;
        } else if (!warned_at) {
            warned_at = xTaskGetTickCount();
            board_log("감시 태스크          : B 체크인 없음 → 킥을 멈춤");
        }
    }
}

static void runner(void *arg)
{
    (void)arg;
    xTaskCreate(worker, "A", configMINIMAL_STACK_SIZE, (void *)1u, WORKER_PRI, NULL);
    xTaskCreate(worker, "B", configMINIMAL_STACK_SIZE, (void *)2u, WORKER_PRI, NULL);
    xTaskCreate(watcher, "watch", configMINIMAL_STACK_SIZE, NULL, WATCH_PRI, NULL);
    wdog_start(TIMEOUT_MS);

    /* 1. 정상 */
    vTaskDelay(pdMS_TO_TICKS(500));
    board_log("정상 500ms           : 킥 %d번, 워치독 경고 %d번", kicks, nmi_count);
    if (kicks < 9 || nmi_count)
        board_finish(1);

    /* 2. B가 멈춤 */
    board_log("B가 멈춤             : 무한 루프에 빠짐");
    b_hang = 1;
    vTaskDelay(pdMS_TO_TICKS(1000));
    board_log("리셋이 일어나지 않음");     /* 여기까지 오면 실패 */
    board_finish(1);
    vTaskDelete(NULL);
}

int main(void)
{
    board_init();
    board_log("부팅");
    xTaskCreate(runner, "runner", configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY + 1, NULL);
    vTaskStartScheduler();
    return board_exit_code();
}
