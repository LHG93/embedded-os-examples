/*
 * 트레이싱 — FreeRTOS on Cortex-M3 (QEMU mps2-an385 전용)
 *
 * 트레이스 훅 traceTASK_SWITCHED_IN으로 "누가 언제 CPU를 받았나"를 25MHz 시계와 함께 기록하고,
 * 타이머 ISR은 직접 표시를 남김. 30ms 동안 기록한 뒤 한꺼번에 출력 (기록 중에는 출력하지 않음)
 *   sensor  (3) : 5ms마다 0.3ms 일 (xTaskDelayUntil)
 *   handler (4) : 타이머 ISR(7ms마다)이 알림을 주면 0.2ms 일
 *   logger  (1) : 3ms 일하고 2ms 쉼
 * 1. 기록 목록  2. 타임라인  3. CPU 사용 · 확인
 * TRACE_JSON 1이면 Perfetto(ui.perfetto.dev)에 바로 올릴 수 있는 JSON도 출력
 */
#include <stdint.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "board.h"
#include "mps2.h"

#define WINDOW_MS  30
#define MAXEV      256
#define TRACE_JSON 0
#define US(c)      ((c) / (CPU_HZ / 1000000u))

typedef struct {
    uint32_t t;                         /* 25MHz 시계 */
    const char *name;
    char kind;                          /* 'S' 전환, 'I' ISR 표시 */
} ev_t;

static ev_t ev[MAXEV];
static volatile int nev, tracing;
static uint32_t t0;
static TaskHandle_t handler_h;

/* board.c의 빈 함수를 덮어씀. 커널이 태스크를 바꿀 때마다 부름. 짧게: 기록만 */
void board_trace_switch_in(void)
{
    if (tracing && nev < MAXEV) {
        ev[nev].t = clock_now();
        ev[nev].name = pcTaskGetName(NULL);
        ev[nev].kind = 'S';
        nev++;
    }
}

static void busy_us(uint32_t us)
{
    uint32_t s = clock_now();
    while (clock_now() - s < us * 25u) { }
}

void TIMER0_Handler(void)
{
    TIMER_INTCLR(TIMER0_BASE) = 1u;
    if (tracing && nev < MAXEV) {           /* 사용자 표시 */
        ev[nev].t = clock_now();
        ev[nev].name = "TIMER0";
        ev[nev].kind = 'I';
        nev++;
    }
    BaseType_t woken = pdFALSE;
    vTaskNotifyGiveFromISR(handler_h, &woken);
    portYIELD_FROM_ISR(woken);
}

static void sensor(void *arg)
{
    (void)arg;
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        xTaskDelayUntil(&last, pdMS_TO_TICKS(5));
        busy_us(300);
    }
}

static void handler(void *arg)
{
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        busy_us(200);
    }
}

static void logger(void *arg)
{
    (void)arg;
    for (;;) {
        busy_us(3000);
        vTaskDelay(pdMS_TO_TICKS(2));
    }
}

static const char *NAMES[] = {"handler", "sensor", "logger", "IDLE"};
#define NN 4

static int name_idx(const char *n)
{
    for (int i = 0; i < NN; i++)
        if (strcmp(n, NAMES[i]) == 0)
            return i;
    return -1;
}

static void runner(void *arg)
{
    (void)arg;
    int ok = 1;
    clock_start();
    xTaskCreate(handler, "handler", configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY + 4, &handler_h);
    xTaskCreate(sensor, "sensor", configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY + 3, NULL);
    xTaskCreate(logger, "logger", configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY + 1, NULL);
    timer_start(TIMER0_BASE, TIMER0_IRQn, 7000, 0xC0);
    vTaskDelay(pdMS_TO_TICKS(10));          /* 자리 잡을 때까지 */

    t0 = clock_now();
    tracing = 1;
    vTaskDelay(pdMS_TO_TICKS(WINDOW_MS));   /* 이 동안 기록 */
    tracing = 0;
    uint32_t t_end = clock_now();
    int n = nev;
    while (n > 0 && ev[n - 1].kind == 'S' && strcmp(ev[n - 1].name, "runner") == 0)
        n--;                                /* 끝에 runner가 돌아온 기록은 뺌 */

    /* 1. 기록 목록 */
    board_log("1. 기록 (처음 14개, 시간은 기록 시작부터, 전체 %d개)", n);
    for (int i = 0; i < n && i < 14; i++)
        board_log("   %6lu.%03lums  %s %s", (unsigned long)(US(ev[i].t - t0) / 1000), (unsigned long)(US(ev[i].t - t0) % 1000),
                  ev[i].kind == 'I' ? "!" : "→", ev[i].name);

    /* 2. 타임라인: 0.5ms 한 칸. 칸마다 그 시각에 CPU를 쥐고 있던 태스크 */
    enum { COLS = WINDOW_MS * 2 };
    static char row[NN + 1][COLS + 1];
    memset(row, '.', sizeof row);
    uint32_t busy_us[NN] = {0};
    int runs[NN] = {0}, isr = 0, handler_after_isr = 0;
    uint32_t worst_isr_to_handler = 0;
    for (int i = 0; i < n; i++) {
        if (ev[i].kind == 'I') {
            isr++;
            int c = (int)(US(ev[i].t - t0) / 500);
            if (c >= 0 && c < COLS)
                row[NN][c] = '|';
            for (int j = i + 1; j < n; j++)             /* ISR 다음 handler가 CPU를 받기까지 */
                if (ev[j].kind == 'S' && strcmp(ev[j].name, "handler") == 0) {
                    uint32_t d = US(ev[j].t - ev[i].t);
                    if (d > worst_isr_to_handler)
                        worst_isr_to_handler = d;
                    handler_after_isr++;
                    break;
                }
            continue;
        }
        int k = name_idx(ev[i].name);
        uint32_t start = ev[i].t, end = t_end;
        for (int j = i + 1; j < n; j++)
            if (ev[j].kind == 'S') {
                end = ev[j].t;
                break;
            }
        if (k < 0)
            continue;
        runs[k]++;
        busy_us[k] += US(end - start);
        for (uint32_t c = US(start - t0) / 500; c < COLS && c * 500 < US(end - t0); c++)
            row[k][c] = '#';
    }
    board_log("2. 타임라인 (0.5ms 한 칸 · %dms, # = CPU 사용, | = 타이머 ISR)", WINDOW_MS);
    for (int k = 0; k < NN; k++) {
        row[k][COLS] = '\0';
        board_log("   %-8s %s", NAMES[k], row[k]);
    }
    row[NN][COLS] = '\0';
    board_log("   %-8s %s", "ISR", row[NN]);

    /* 3. 요약 */
    /* 보드 printf는 소수를 못 찍어서 천분율로 계산해 x.y%로 */
    uint32_t pm[NN];
    for (int k = 0; k < NN; k++)
        pm[k] = busy_us[k] * 1000u / (WINDOW_MS * 1000u);
    board_log("3. CPU 사용 (%dms): handler %lu.%lu%% · sensor %lu.%lu%% · logger %lu.%lu%% · IDLE %lu.%lu%%", WINDOW_MS,
              (unsigned long)(pm[0] / 10), (unsigned long)(pm[0] % 10), (unsigned long)(pm[1] / 10), (unsigned long)(pm[1] % 10),
              (unsigned long)(pm[2] / 10), (unsigned long)(pm[2] % 10), (unsigned long)(pm[3] / 10), (unsigned long)(pm[3] % 10));
    board_log("   CPU를 받은 횟수: handler %d · sensor %d · logger %d, 타이머 ISR %d번, ISR → handler 최대 %luus",
              runs[0], runs[1], runs[2], isr, (unsigned long)worst_isr_to_handler);
    ok &= n > 10 && n < MAXEV && isr >= 3 && handler_after_isr == isr && worst_isr_to_handler < 100 &&
          runs[1] >= 5 && runs[2] >= 3;

#if TRACE_JSON
    /* Chrome trace 형식: 이 사이 줄만 파일로 저장해 ui.perfetto.dev에 올리면 그림으로 보임 */
    board_log("---- trace.json 시작 ----");
    board_log("[");
    for (int i = 0; i < n; i++) {
        uint32_t s = US(ev[i].t - t0), e = US(t_end - t0);
        if (ev[i].kind == 'I') {
            board_log("{\"name\":\"%s\",\"ph\":\"i\",\"s\":\"g\",\"ts\":%lu,\"pid\":1,\"tid\":0},", ev[i].name, (unsigned long)s);
            continue;
        }
        for (int j = i + 1; j < n; j++)
            if (ev[j].kind == 'S') {
                e = US(ev[j].t - t0);
                break;
            }
        board_log("{\"name\":\"%s\",\"ph\":\"X\",\"ts\":%lu,\"dur\":%lu,\"pid\":1,\"tid\":%d},", ev[i].name, (unsigned long)s,
                  (unsigned long)(e - s), name_idx(ev[i].name) + 1);
    }
    board_log("{\"name\":\"end\",\"ph\":\"i\",\"s\":\"g\",\"ts\":%d,\"pid\":1,\"tid\":0}", WINDOW_MS * 1000);
    board_log("]");
    board_log("---- trace.json 끝 ----");
#endif

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
