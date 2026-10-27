/*
 * 컨텍스트 스위칭과 틱 — Linux
 *
 * 1. 전환 횟수   : 스레드마다 스스로 비켜 준 횟수(voluntary)와 뺏긴 횟수(nonvoluntary)
 *                  잠을 자는 스레드는 voluntary, CPU 하나를 다투는 바쁜 스레드는 nonvoluntary가 늘어남
 * 2. 전환 비용   : CPU 하나에 고정한 스레드 둘이 파이프로 핑퐁. 왕복 한 번 = 전환 2번 + 시스템 콜
 * 3. 시간 해상도 : Linux는 틱이 아니라 고해상도 타이머로 깨움. 1ms · 100us 잠의 실제 길이
 *
 * 1과 CPU 고정은 Linux 전용(/proc, sched_setaffinity). macOS에서는 2 · 3만 실행
 */
#define _GNU_SOURCE
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#ifdef __linux__
#include <sched.h>
#endif

#define ROUNDS 50000

static int pinned;                      /* CPU 0 하나에 고정했는지 */

static double now_us(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e6 + t.tv_nsec / 1e3;
}

static void pin_cpu0(void)
{
#ifdef __linux__
    cpu_set_t one;
    CPU_ZERO(&one);
    CPU_SET(0, &one);
    pinned = sched_setaffinity(0, sizeof one, &one) == 0;  /* 이후 만드는 스레드도 CPU 0만 씀 */
#endif
}

/* ---------- 1. 전환 횟수 ---------- */
#ifdef __linux__
typedef struct {
    int busy;
    long vol, nonvol;
} counter_t;

/* 이 스레드의 전환 횟수 (/proc/thread-self/status) */
static void read_switches(long *vol, long *nonvol)
{
    char line[128];
    FILE *f = fopen("/proc/thread-self/status", "r");
    *vol = *nonvol = -1;
    if (!f)
        return;
    while (fgets(line, sizeof line, f)) {
        sscanf(line, "voluntary_ctxt_switches: %ld", vol);
        sscanf(line, "nonvoluntary_ctxt_switches: %ld", nonvol);
    }
    fclose(f);
}

static void *count_fn(void *arg)
{
    counter_t *c = arg;
    long v0, n0;
    read_switches(&v0, &n0);
    if (c->busy) {
        double end = now_us() + 300000;         /* 300ms 동안 쉬지 않음 */
        while (now_us() < end) { }
    } else {
        struct timespec ms = { 0, 1000000 };
        for (int i = 0; i < 100; i++)           /* 1ms씩 100번 잠 */
            nanosleep(&ms, NULL);
    }
    read_switches(&c->vol, &c->nonvol);
    c->vol -= v0;
    c->nonvol -= n0;
    return NULL;
}
#endif

/* ---------- 2. 전환 비용 ---------- */
static int ab[2], ba[2];

static void *pong_fn(void *arg)
{
    (void)arg;
    char b;
    for (int i = 0; i < ROUNDS; i++) {
        if (read(ab[0], &b, 1) != 1 || write(ba[1], &b, 1) != 1)
            break;
    }
    return NULL;
}

/* ---------- 3. 시간 해상도 ---------- */
static void sleep_stats(long req_us, double *avg, double *max)
{
    struct timespec r = { 0, req_us * 1000 };
    *avg = 0;
    *max = 0;
    for (int i = 0; i < 50; i++) {
        double t = now_us();
        nanosleep(&r, NULL);
        double late = now_us() - t;
        *avg += late / 50;
        if (late > *max)
            *max = late;
    }
}

int main(void)
{
    int ok = 1;
    pin_cpu0();

#ifdef __linux__
    counter_t sleeper = { .busy = 0 }, b1 = { .busy = 1 }, b2 = { .busy = 1 };
    pthread_t t1, t2, t3;
    pthread_create(&t1, NULL, count_fn, &sleeper);
    pthread_join(t1, NULL);
    pthread_create(&t2, NULL, count_fn, &b1);
    pthread_create(&t3, NULL, count_fn, &b2);
    pthread_join(t2, NULL);
    pthread_join(t3, NULL);
    printf("1. 전환 횟수 (스스로 비켜 줌 / 뺏김)\n");
    printf("   1ms씩 100번 자는 스레드          : %4ld / %ld\n", sleeper.vol, sleeper.nonvol);
    printf("   %s : %4ld / %ld, %ld / %ld\n", pinned ? "CPU 하나를 300ms 다투는 스레드 둘" : "300ms 바쁜 스레드 둘 (CPU 고정 없음)",
           b1.vol, b1.nonvol, b2.vol, b2.nonvol);
    ok &= sleeper.vol >= 100 && (!pinned || (b1.nonvol > 5 && b2.nonvol > 5));
#else
    printf("1. 전환 횟수 : Linux 전용 (/proc). 건너뜀\n");
#endif

    pthread_t tp;
    char b = 'x';
    if (pipe(ab) || pipe(ba))
        return 1;
    pthread_create(&tp, NULL, pong_fn, NULL);
    double t0 = now_us();
    for (int i = 0; i < ROUNDS; i++) {
        if (write(ab[1], &b, 1) != 1 || read(ba[0], &b, 1) != 1)
            break;
    }
    double el = now_us() - t0;
    pthread_join(tp, NULL);
    printf("2. 전환 비용 (파이프 핑퐁 %d번%s)\n", ROUNDS, pinned ? ", CPU 0 하나" : "");
    printf("   왕복 한 번 %.2fus → 전환 한 번 약 %.2fus (시스템 콜 포함)\n", el / ROUNDS, el / ROUNDS / 2);
    ok &= el > 0;

    struct timespec res;
    clock_getres(CLOCK_MONOTONIC, &res);
    double a1, m1, a2, m2;
    sleep_stats(1000, &a1, &m1);
    sleep_stats(100, &a2, &m2);
    printf("3. 시간 해상도 (CLOCK_MONOTONIC 해상도 %ldns)\n", res.tv_nsec);
    printf("   1ms 잠   → 실제 평균 %6.0fus, 최대 %6.0fus\n", a1, m1);
    printf("   100us 잠 → 실제 평균 %6.0fus, 최대 %6.0fus\n", a2, m2);
    ok &= a1 >= 1000 && a2 >= 100 && a2 < 1000;   /* 틱(1ms)에 묶였다면 100us 잠도 1ms 가까이 걸림 */
    return ok ? 0 : 1;
}
