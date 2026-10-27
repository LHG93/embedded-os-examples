/*
 * 컨텍스트 스위칭과 틱 — FreeRTOS on Cortex-M3 (QEMU mps2-an385 전용)
 *
 * 1. 저장된 문맥 : 비켜 준 태스크는 자기 스택에 레지스터 16개(64바이트)를 적어 둠
 *                  아직 한 번도 안 돈 태스크는 "함수 처음부터 이어서 하기"로 꾸며 둔 문맥을 가짐
 * 2. 틱          : 1ms마다 틱 인터럽트. vTaskDelay(1)은 1ms가 아니라 "다음 틱까지"
 * 3. 전환 비용   : 같은 우선순위 태스크 둘이 알림으로 핑퐁
 *
 * 시간은 25MHz 시계(듀얼 타이머)로 잼. QEMU는 실제 칩 속도를 흉내 내지 않아서
 * 3의 숫자는 PC 성능에 따라 달라짐 (참고용)
 */
#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"
#include "board.h"
#include "mps2.h"

#define PARAM     0x1234ABCDu
#define ROUNDS    10000
#define US(c)     ((c) / (CPU_HZ / 1000000u))

#define TRIALS    15
#define DELAY     1                     /* vTaskDelay에 줄 틱 수 */
#define TICK_US   (1000000u / configTICK_RATE_HZ)

static TaskHandle_t ping, pong, runner_h;
static volatile uint32_t pong_count;

static void worker(void *arg)           /* 알림이 올 때까지 잠 */
{
    (void)arg;
    for (;;)
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
}

/* TCB 첫 칸은 저장된 스택 위치(pxTopOfStack). FreeRTOS가 지키는 배치 */
static uint32_t *saved_sp(TaskHandle_t h)
{
    return *(uint32_t **)h;
}

/* Cortex-M3 포트가 스택에 쌓는 순서 (낮은 주소부터)
 *   [0..7]  R4 ~ R11            ← PendSV 처리기가 소프트웨어로 저장
 *   [8..15] R0 R1 R2 R3 R12 LR PC xPSR  ← 예외에 들어갈 때 하드웨어가 저장 */
enum { R4 = 0, R0 = 8, LR = 13, PC = 14, XPSR = 15, WORDS = 16 };

static uint32_t median(uint32_t *v, int n)  /* 작은 배열이라 삽입 정렬 */
{
    for (int i = 1; i < n; i++)
        for (int j = i; j > 0 && v[j - 1] > v[j]; j--) {
            uint32_t t = v[j];
            v[j] = v[j - 1];
            v[j - 1] = t;
        }
    return v[n / 2];
}

static void pong_task(void *arg)
{
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        pong_count++;
        xTaskNotifyGive(ping);
    }
}

static void ping_task(void *arg)
{
    (void)arg;
    for (int i = 0; i < ROUNDS; i++) {
        xTaskNotifyGive(pong);                  /* 같은 우선순위라 바로 넘어가진 않음 */
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY); /* 여기서 잠들며 pong으로 전환 */
    }
    xTaskNotifyGive(runner_h);
    vTaskSuspend(NULL);
}

static void runner(void *arg)
{
    (void)arg;
    int ok = 1;
    clock_start();

    /* 1. 저장된 문맥 */
    TaskHandle_t w;
    xTaskCreate(worker, "worker", configMINIMAL_STACK_SIZE, (void *)PARAM, tskIDLE_PRIORITY + 1, &w);
    uint32_t *f = saved_sp(w);
    uint32_t entry = (uint32_t)(uintptr_t)worker & ~1u;   /* 함수 주소의 마지막 비트는 Thumb 표시 */
    board_log("1. 저장된 문맥 (Cortex-M3: 레지스터 %d개 = %d바이트)", WORDS, WORDS * 4);
    board_log("   만들자마자 : 위치 0x%08lx, PC 0x%08lx (worker 함수 처음: %s), R0 0x%08lx (넘긴 인자: %s), xPSR 0x%08lx",
              (unsigned long)(uintptr_t)f, (unsigned long)f[PC], f[PC] == entry ? "예" : "아니오",
              (unsigned long)f[R0], f[R0] == PARAM ? "예" : "아니오", (unsigned long)f[XPSR]);
    board_log("                LR 0x%08lx (함수가 끝나면 갈 곳: 오류 처리), R4 ~ R11 0x%08lx (아직 빈 값)",
              (unsigned long)f[LR], (unsigned long)f[R4]);
    ok &= f[PC] == entry && f[R0] == PARAM && f[XPSR] == 0x01000000u;

    vTaskDelay(2);                      /* 그동안 worker가 처음 돌고 알림을 기다리며 잠듦 */
    uint32_t *g = saved_sp(w);
    board_log("   잠든 뒤    : 위치 0x%08lx (%ld바이트 더 씀), PC 0x%08lx (커널 안, 기다리던 자리), xPSR 0x%08lx",
              (unsigned long)(uintptr_t)g, (long)((uintptr_t)f - (uintptr_t)g), (unsigned long)g[PC],
              (unsigned long)g[XPSR]);
    ok &= eTaskGetState(w) == eBlocked && g[PC] != entry && g < f;
    vTaskDelete(w);

    /* 2. 틱 */
    board_log("2. 틱 (configTICK_RATE_HZ %d → 1틱 = %luus)", configTICK_RATE_HZ, (unsigned long)TICK_US);
    TickType_t k0 = xTaskGetTickCount();
    uint32_t c0 = clock_now();
    while (clock_now() - c0 < 100u * 1000u * 25u) { }        /* 시계로 100ms 바쁘게 기다림 */
    TickType_t ticks = xTaskGetTickCount() - k0;
    board_log("   시계로 100ms 동안 틱 %lu번", (unsigned long)ticks);
    ok &= ticks + 2 >= 100000u / TICK_US && ticks <= 100000u / TICK_US + 2;

    /* QEMU는 틱 인터럽트가 PC 사정에 따라 조금씩 늦게 와서, 위치마다 15번 재고 가운데 값을 씀 */
    static const uint32_t phase[] = {0, 300, 600, 900};
    for (unsigned i = 0; i < sizeof phase / sizeof phase[0]; i++) {
        uint32_t w[TRIALS];
        for (int k = 0; k < TRIALS; k++) {
            vTaskDelay(1);                                    /* 틱 바로 뒤에서 출발 */
            uint32_t s = clock_now();
            while (clock_now() - s < phase[i] * 25u) { }      /* 틱 경계에서 phase us 지난 뒤 */
            uint32_t t = clock_now();
            vTaskDelay(DELAY);
            w[k] = US(clock_now() - t);
        }
        uint32_t waited = median(w, TRIALS);
        board_log("   틱 경계 +%3luus에 vTaskDelay(%d) → 실제로 %4luus 기다림",
                  (unsigned long)phase[i], DELAY, (unsigned long)waited);
        uint32_t expect = DELAY * TICK_US - phase[i];      /* 다음 틱까지 남은 시간 + (DELAY - 1)틱 */
        ok &= waited + 150u > expect && waited < expect + 150u;
    }

    /* 3. 전환 비용 */
    runner_h = xTaskGetCurrentTaskHandle();
    pong_count = 0;
    uint32_t s = clock_now();
    k0 = xTaskGetTickCount();
    /* runner(2)보다 높아서 만들자마자 돎. ping을 만드는 순간 핑퐁이 시작돼 끝날 때까지 이어짐 */
    xTaskCreate(pong_task, "pong", configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY + 3, &pong);
    xTaskCreate(ping_task, "ping", configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY + 3, &ping);
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);  /* ping이 끝났다고 알려 줌 */
    uint32_t el = clock_now() - s;
    board_log("3. 전환 비용 (알림 핑퐁 %d번 = 전환 %d번, 틱 %lu번 동안)",
              ROUNDS, ROUNDS * 2, (unsigned long)(xTaskGetTickCount() - k0));
    board_log("   전환 한 번 평균 %lu.%02luus (QEMU 값, 참고용)",
              (unsigned long)(US(el) / (ROUNDS * 2)), (unsigned long)(US(el) * 100u / (ROUNDS * 2) % 100u));
    ok &= pong_count == ROUNDS;
    vTaskDelete(ping);
    vTaskDelete(pong);

    board_finish(ok ? 0 : 1);
    vTaskDelete(NULL);
}

int main(void)
{
    board_init();
    xTaskCreate(runner, "runner", configMINIMAL_STACK_SIZE * 2, NULL, tskIDLE_PRIORITY + 2, NULL);
    vTaskStartScheduler();
    return board_exit_code();
}
