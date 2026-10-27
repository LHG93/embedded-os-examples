/*
 * 인터럽트 기초 — FreeRTOS on Cortex-M3 (QEMU mps2-an385 전용)
 *
 * CMSDK 타이머 두 개(T0 · T1)의 인터럽트로 확인. 시간은 25MHz 자유 실행 시계로 잼
 * 1. 타이머 인터럽트      : T0를 1kHz로. 태스크가 100ms 자는 동안 ISR이 몇 번 도는지
 * 2. 중첩                : T0 처리기가 2ms 동안 일함. T1이 더 급하면(숫자가 작으면) 그 도중에 끼어듦
 * 3. 중첩 없음            : T1이 덜 급하면 T0 처리기가 끝날 때까지 기다림 → 지연이 2ms 가까이
 * 4. 임계 구역이 막는 범위 : taskENTER_CRITICAL()은 우선순위 0x40 이상(덜 급한 쪽)만 막음
 *                          0x20처럼 더 급한 인터럽트는 계속 돎 (대신 FreeRTOS API를 부르면 안 됨)
 */
#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"
#include "board.h"
#include "mps2.h"

#define US(cycles) ((cycles) / (CPU_HZ / 1000000u))

static volatile uint32_t t0_count, t1_count, nested, t1_max_lat;
static volatile uint32_t t0_busy_us;
static volatile int in_t0;

void TIMER0_Handler(void)
{
    TIMER_INTCLR(TIMER0_BASE) = 1u;     /* 지우지 않으면 끝나자마자 다시 들어옴 */
    t0_count++;
    if (t0_busy_us) {
        in_t0 = 1;
        uint32_t start = clock_now();
        while (clock_now() - start < t0_busy_us * 25u) { }   /* 처리기 안에서 오래 일함 */
        in_t0 = 0;
    }
}

void TIMER1_Handler(void)
{
    /* 0이 된 순간부터 지금까지 흐른 시간 = 들어오기까지 걸린 지연 */
    uint32_t lat = TIMER_RELOAD(TIMER1_BASE) - TIMER_VALUE(TIMER1_BASE);
    TIMER_INTCLR(TIMER1_BASE) = 1u;
    t1_count++;
    if (in_t0)
        nested++;                       /* T0 처리기 도중에 끼어들었음 */
    if (lat > t1_max_lat)
        t1_max_lat = lat;
}

static void reset_counts(void)
{
    t0_count = t1_count = nested = t1_max_lat = 0;
}

static void runner(void *arg)
{
    (void)arg;
    clock_start();

    /* 1. 타이머 인터럽트 */
    reset_counts();
    t0_busy_us = 0;
    timer_start(TIMER0_BASE, TIMER0_IRQn, 1000, 0xC0);
    vTaskDelay(pdMS_TO_TICKS(100));
    timer_stop(TIMER0_BASE, TIMER0_IRQn);
    uint32_t c1 = t0_count;
    board_log("타이머 인터럽트       : 1kHz 타이머, 태스크가 100ms 자는 동안 ISR %lu번 실행", (unsigned long)c1);

    /* 2 · 3. 중첩: T0(0xC0)는 5ms마다 2ms 일함, T1은 3ms마다 */
    uint32_t nest[2], lat[2];
    const uint8_t t1_prio[2] = { 0x80, 0xE0 };  /* 0x80: T0보다 급함, 0xE0: T0보다 덜 급함 */
    for (int i = 0; i < 2; i++) {
        reset_counts();
        t0_busy_us = 2000;
        timer_start(TIMER0_BASE, TIMER0_IRQn, 5000, 0xC0);
        timer_start(TIMER1_BASE, TIMER1_IRQn, 3000, t1_prio[i]);
        vTaskDelay(pdMS_TO_TICKS(100));
        timer_stop(TIMER0_BASE, TIMER0_IRQn);
        timer_stop(TIMER1_BASE, TIMER1_IRQn);
        nest[i] = nested;
        lat[i] = US(t1_max_lat);
    }
    board_log("중첩 (T1이 더 급함)   : T0 처리기(2ms) 도중 T1이 끼어든 횟수 %lu번, T1 최대 지연 %luus",
              (unsigned long)nest[0], (unsigned long)lat[0]);
    board_log("중첩 없음 (T1이 덜 급함): 끼어든 횟수 %lu번, T1 최대 지연 %luus (T0 처리기가 끝날 때까지 기다림)",
              (unsigned long)nest[1], (unsigned long)lat[1]);

    /* 4. 임계 구역이 막는 범위 */
    reset_counts();
    t0_busy_us = 0;
    timer_start(TIMER0_BASE, TIMER0_IRQn, 1000, 0xC0);   /* 커널이 막는 쪽 */
    timer_start(TIMER1_BASE, TIMER1_IRQn, 1000, 0x20);   /* 커널이 안 막는 쪽 */
    vTaskDelay(pdMS_TO_TICKS(2));
    uint32_t a0 = t0_count, a1 = t1_count;
    taskENTER_CRITICAL();                                 /* BASEPRI = 0x40 */
    uint32_t start = clock_now();
    while (clock_now() - start < 5000u * 25u) { }        /* 5ms 동안 */
    uint32_t in0 = t0_count - a0, in1 = t1_count - a1;
    taskEXIT_CRITICAL();
    uint32_t after0 = t0_count - a0 - in0;               /* 풀자마자 밀려 있던 것 */
    timer_stop(TIMER0_BASE, TIMER0_IRQn);
    timer_stop(TIMER1_BASE, TIMER1_IRQn);
    board_log("임계 구역 5ms 동안    : 0xC0 타이머 %lu번(막힘, 풀자마자 %lu번), 0x20 타이머 %lu번(안 막힘)",
              (unsigned long)in0, (unsigned long)after0, (unsigned long)in1);

    int ok = c1 >= 95 && c1 <= 105 && nest[0] > 0 && lat[0] < 1000 && nest[1] == 0 && lat[1] > 1000 &&
             in0 == 0 && after0 == 1 && in1 >= 4;
    board_finish(ok ? 0 : 1);
    vTaskDelete(NULL);
}

int main(void)
{
    board_init();
    xTaskCreate(runner, "runner", configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY + 2, NULL);
    vTaskStartScheduler();
    return board_exit_code();
}
