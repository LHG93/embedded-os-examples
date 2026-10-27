/*
 * 스케줄링 — FreeRTOS
 *
 * 틱마다 그 틱에 CPU를 쓰던 태스크의 글자를 적어서 타임라인으로 출력
 * 1. 선점          : L(1)이 바쁜 동안 H(3)가 깨어나면 그 틱에 바로 L을 밀어냄
 * 2. 타임 슬라이스 : 같은 우선순위 A · B(2)는 틱마다 번갈아 돎 (configUSE_TIME_SLICING)
 * 3. 굶주림        : H(3)가 쉬지 않으면 L(1)은 한 틱도 못 받음. H가 1틱씩 쉬면 L도 돎
 */
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "board.h"

#define TICKS 20

static char trace[TICKS + 1];
static volatile int n;
static volatile TickType_t last;

/* 새 틱을 처음 본 태스크가 그 틱의 주인. t는 호출한 쪽이 읽은 틱 */
static void mark(char me, TickType_t t)
{
    if (t == last)
        return;
    taskENTER_CRITICAL();
    if (t > last) {                     /* 읽고 나서 밀려난 사이 남이 더 뒤 틱을 적었으면 무시 */
        last = t;
        if (n < TICKS)
            trace[n++] = me;
    }
    taskEXIT_CRITICAL();
}

/* ticks 동안 쉬지 않고 일함. 틱은 한 번만 읽어서 끝낼지와 적을지를 같은 값으로 정함 */
static void spin(char me, TickType_t ticks)
{
    TickType_t start = xTaskGetTickCount();
    for (;;) {
        TickType_t t = xTaskGetTickCount();
        if (t - start >= ticks)
            return;
        mark(me, t);
    }
}

static void busy(void *arg)             /* 쉬지 않음 */
{
    spin((char)(uintptr_t)arg, portMAX_DELAY);
}

static void sleep3_work2(void *arg)     /* 3틱 자고 2틱 일함 */
{
    (void)arg;
    for (;;) {
        vTaskDelay(3);
        spin('H', 2);
    }
}

static void work1_sleep1(void *arg)     /* 1틱 일하고 1틱 쉼 */
{
    (void)arg;
    for (;;) {
        spin('H', 1);
        vTaskDelay(1);
    }
}

typedef struct {
    TaskFunction_t fn;
    char name;
    UBaseType_t pri;
} job_t;

/* 태스크를 만들고 TICKS틱 동안 재운 뒤 지움. runner(5)가 가장 높아서 만드는 동안엔 아무도 안 돎 */
static void scene(const job_t *jobs, int count)
{
    TaskHandle_t h[3];
    memset(trace, 0, sizeof trace);
    n = 0;
    last = xTaskGetTickCount();         /* 지금 틱은 세지 않음 */
    for (int i = 0; i < count; i++)
        xTaskCreate(jobs[i].fn, "job", configMINIMAL_STACK_SIZE, (void *)(uintptr_t)jobs[i].name, jobs[i].pri, &h[i]);
    vTaskDelay(TICKS + 2);
    for (int i = 0; i < count; i++)
        vTaskDelete(h[i]);
}

static int count_of(char c)
{
    int k = 0;
    for (int i = 0; i < n; i++)
        k += trace[i] == c;
    return k;
}

static int switches(void)
{
    int k = 0;
    for (int i = 1; i < n; i++)
        k += trace[i] != trace[i - 1];
    return k;
}

static void runner(void *arg)
{
    (void)arg;
    int ok = 1;

    board_log("틱마다 CPU를 쓰던 태스크 (%d틱, 숫자는 우선순위)", TICKS);

    const job_t preempt[] = {{busy, 'L', 1}, {sleep3_work2, 'H', 3}};
    scene(preempt, 2);
    board_log("1. 선점          L(1) 바쁨, H(3) 3틱 자고 2틱 일함 : %s", trace);
    ok &= n == TICKS && count_of('H') >= 6 && count_of('H') <= 10 && strstr(trace, "HHH") == NULL;

    const job_t slice[] = {{busy, 'A', 2}, {busy, 'B', 2}};
    scene(slice, 2);
    board_log("2. 타임 슬라이스 A(2) · B(2) 둘 다 바쁨            : %s  (바뀐 횟수 %d)", trace, switches());
#if configUSE_TIME_SLICING
    ok &= n == TICKS && switches() >= TICKS - 3;
#else
    ok &= n == TICKS && switches() == 0;    /* 끄면 먼저 잡은 쪽이 계속 돎 */
#endif

    const job_t starve[] = {{busy, 'L', 1}, {busy, 'H', 3}};
    scene(starve, 2);
    board_log("3. 굶주림        H(3)가 쉬지 않음                  : %s  L이 받은 틱 %d", trace, count_of('L'));
    ok &= n == TICKS && count_of('L') == 0;

    const job_t share[] = {{busy, 'L', 1}, {work1_sleep1, 'H', 3}};
    scene(share, 2);
    board_log("                 H(3)가 1틱 일하고 1틱 쉼          : %s  L이 받은 틱 %d", trace, count_of('L'));
    ok &= n == TICKS && count_of('L') >= 8 && count_of('L') <= 12;

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
