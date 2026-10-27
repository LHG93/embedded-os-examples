/*
 * 실시간이란 — Linux
 *
 * 1ms마다 깨어나는 주기 스레드가 "깨어나야 할 시각"보다 얼마나 늦게 깨어나는지 잼 (cyclictest와 같은 방식)
 * 모두 CPU 0 하나에 고정
 * 1. 일반 정책 · 한가함
 * 2. 일반 정책 · 같은 CPU에 바쁜 스레드 2개 : 가끔 크게 늦음
 * 3. SCHED_FIFO · 바쁜 스레드 2개          : 바쁜 스레드를 바로 밀어내서 거의 안 늦음
 *
 * 3은 실시간 우선순위 권한 필요. 없으면 건너뜀
 * Linux 전용 (CPU 고정 · SCHED_FIFO). 다른 OS에서는 안내만 출력
 */
#define _GNU_SOURCE
#include <stdio.h>

#ifndef __linux__
int main(void)
{
    printf("Linux 전용 예제 (CPU 고정 · SCHED_FIFO). 도커 컨테이너 안에서 실행\n");
    return 0;
}
#else

#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <time.h>

#define LOOPS     1000
#define PERIOD_NS 1000000L      /* 1ms */
#define LATE_US   500           /* 이만큼 넘게 늦으면 "마감 넘김"으로 셈 */

typedef struct {
    double avg, max;
    int over;
} lat_t;

static volatile int stop_load;

static void *busy(void *arg)
{
    (void)arg;
    while (!stop_load) { }
    return NULL;
}

static void add_ns(struct timespec *t, long ns)
{
    t->tv_nsec += ns;
    while (t->tv_nsec >= 1000000000L) {
        t->tv_nsec -= 1000000000L;
        t->tv_sec++;
    }
}

static void *cyclic(void *arg)
{
    lat_t *r = arg;
    struct timespec next, now;
    clock_gettime(CLOCK_MONOTONIC, &next);
    add_ns(&next, PERIOD_NS);
    for (int i = 0; i < LOOPS; i++) {
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);  /* 절대 시각까지 잠 */
        clock_gettime(CLOCK_MONOTONIC, &now);
        double late = (now.tv_sec - next.tv_sec) * 1e6 + (now.tv_nsec - next.tv_nsec) / 1e3;
        r->avg += late / LOOPS;
        if (late > r->max)
            r->max = late;
        r->over += late > LATE_US;
        add_ns(&next, PERIOD_NS);
    }
    return NULL;
}

/* fifo면 SCHED_FIFO 우선순위 80으로. load만큼 바쁜 스레드를 같이 돌림 */
static int run(int fifo, int load, lat_t *r)
{
    pthread_t loaders[2], t;
    pthread_attr_t a;
    *r = (lat_t){0, 0, 0};
    stop_load = 0;
    for (int i = 0; i < load; i++)
        pthread_create(&loaders[i], NULL, busy, NULL);

    pthread_attr_init(&a);
    if (fifo) {
        struct sched_param sp = { .sched_priority = 80 };
        pthread_attr_setinheritsched(&a, PTHREAD_EXPLICIT_SCHED);
        pthread_attr_setschedpolicy(&a, SCHED_FIFO);
        pthread_attr_setschedparam(&a, &sp);
    }
    int err = pthread_create(&t, &a, cyclic, r);
    pthread_attr_destroy(&a);
    if (!err)
        pthread_join(t, NULL);
    stop_load = 1;
    for (int i = 0; i < load; i++)
        pthread_join(loaders[i], NULL);
    return err;
}

int main(void)
{
    cpu_set_t one;
    CPU_ZERO(&one);
    CPU_SET(0, &one);
    sched_setaffinity(0, sizeof one, &one);     /* 이후 만드는 스레드도 CPU 0만 씀 */
    lat_t idle, loaded, rt;
    int ok = 1;

    printf("1ms 주기 스레드가 깨어나야 할 시각보다 늦은 정도 (%d번, CPU 0, %dus 넘게 늦으면 마감 넘김)\n", LOOPS, LATE_US);
    run(0, 0, &idle);
    printf("1. 일반 · 한가함                : 평균 %5.0fus, 최대 %6.0fus, 마감 넘김 %d\n", idle.avg, idle.max, idle.over);
    run(0, 2, &loaded);
    printf("2. 일반 · 바쁜 스레드 2개        : 평균 %5.0fus, 최대 %6.0fus, 마감 넘김 %d\n", loaded.avg, loaded.max, loaded.over);
    ok &= loaded.max > 0;

    int err = run(1, 2, &rt);
    if (err == EPERM || err == EINVAL) {
        printf("3. SCHED_FIFO · 바쁜 스레드 2개  : 실시간 우선순위를 쓸 수 없어 건너뜀 (권한 없음). README 참고\n");
    } else {
        printf("3. SCHED_FIFO · 바쁜 스레드 2개  : 평균 %5.0fus, 최대 %6.0fus, 마감 넘김 %d\n", rt.avg, rt.max, rt.over);
        ok &= rt.max < loaded.max;
    }
    return ok ? 0 : 1;
}
#endif
