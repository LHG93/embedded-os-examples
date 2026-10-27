/*
 * 스케줄링 — Linux
 *
 * 쉬지 않는 스레드 둘을 CPU 0 하나에 고정하고 1초 동안 돌림
 * 1. 같은 nice   : CPU를 반씩 나눔. 한 번에 몇 ms씩 번갈아 도는지(타임 슬라이스) 측정
 * 2. nice 0 · 5  : 가중치 1024 : 335라서 약 75% : 25%
 * 3. SCHED_FIFO  : 실시간 스레드가 쉬지 않으면 일반 스레드는 거의 못 돎 (굶주림)
 *
 * 3은 권한이 필요함: sudo, 또는 rtprio 제한(compose.yaml에 설정됨). 없으면 건너뜀
 * Linux 전용. 다른 OS에서는 안내만 출력
 */
#define _GNU_SOURCE
#include <stdio.h>

#ifndef __linux__
int main(void)
{
    printf("Linux 전용 예제 (CPU 고정 · nice · SCHED_FIFO). 도커 컨테이너 안에서 실행\n");
    return 0;
}
#else

#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#define RUN_MS 1000.0
#define GAP_MS 0.2      /* 시계가 이만큼 건너뛰면 그동안 다른 스레드가 돈 것 */

typedef struct {
    int nice, fifo;
    double cpu_ms, run_ms;
    int slices;
} worker_t;

/* nice 0 ~ 10의 가중치 (커널 sched_prio_to_weight). nice 1 차이 ≈ CPU 10% 차이 */
static const int weight[] = {1024, 820, 655, 526, 423, 335, 272, 215, 172, 137, 110};

static pthread_barrier_t bar;
static double end_at;

static double ms(clockid_t c)
{
    struct timespec t;
    clock_gettime(c, &t);
    return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

static void *busy(void *arg)
{
    worker_t *w = arg;
    if (!w->fifo)
        setpriority(PRIO_PROCESS, (id_t)syscall(SYS_gettid), w->nice);  /* Linux에선 스레드마다 nice */
    pthread_barrier_wait(&bar);

    double c0 = ms(CLOCK_THREAD_CPUTIME_ID), prev = ms(CLOCK_MONOTONIC), seg = prev;
    for (;;) {
        double t = ms(CLOCK_MONOTONIC);
        if (t - prev > GAP_MS) {        /* 밀려났다가 돌아옴: 한 조각 끝 */
            w->slices++;
            w->run_ms += prev - seg;
            seg = t;
        }
        prev = t;
        if (t >= end_at)
            break;
    }
    w->slices++;
    w->run_ms += prev - seg;
    w->cpu_ms = ms(CLOCK_THREAD_CPUTIME_ID) - c0;
    return NULL;
}

static int spawn(pthread_t *t, worker_t *w)
{
    pthread_attr_t a;
    pthread_attr_init(&a);
    if (w->fifo) {
        struct sched_param sp = { .sched_priority = 10 };
        pthread_attr_setinheritsched(&a, PTHREAD_EXPLICIT_SCHED);
        pthread_attr_setschedpolicy(&a, SCHED_FIFO);
        pthread_attr_setschedparam(&a, &sp);
    }
    int r = pthread_create(t, &a, busy, w);
    pthread_attr_destroy(&a);
    return r;
}

/* 두 스레드를 같이 출발시켜 RUN_MS 동안 돌림 */
static int run(worker_t *a, worker_t *b)
{
    pthread_t ta, tb;
    a->cpu_ms = a->run_ms = b->cpu_ms = b->run_ms = 0;
    a->slices = b->slices = 0;
    pthread_barrier_init(&bar, NULL, 2);
    end_at = ms(CLOCK_MONOTONIC) + 20 + RUN_MS;   /* 만드는 시간 20ms 여유 */
    int r = spawn(&ta, a);
    if (r) {
        pthread_barrier_destroy(&bar);
        return r;
    }
    r = spawn(&tb, b);
    if (r) {
        pthread_cancel(ta);
        pthread_join(ta, NULL);
        pthread_barrier_destroy(&bar);
        return r;
    }
    pthread_join(ta, NULL);
    pthread_join(tb, NULL);
    pthread_barrier_destroy(&bar);
    return 0;
}

static double share(const worker_t *w, const worker_t *other)
{
    return 100.0 * w->cpu_ms / (w->cpu_ms + other->cpu_ms);
}

int main(void)
{
    cpu_set_t one;
    CPU_ZERO(&one);
    CPU_SET(0, &one);
    sched_setaffinity(0, sizeof one, &one);     /* 이후 만드는 스레드도 CPU 0만 씀 */
    int ok = 1;

    printf("쉬지 않는 스레드 둘을 CPU 0에서 %.0f초 동안 돌림\n", RUN_MS / 1000);

    worker_t a = { .nice = 0 }, b = { .nice = 0 };
    run(&a, &b);
    printf("1. 같은 nice 0 · 0  : CPU %2.0f%% · %2.0f%%, 한 번에 평균 %.1fms · %.1fms씩 번갈아 돎 (조각 %d · %d개)\n",
           share(&a, &b), share(&b, &a), a.run_ms / a.slices, b.run_ms / b.slices, a.slices, b.slices);
    ok &= share(&a, &b) > 40 && share(&a, &b) < 60 && a.slices > 5;

    a.nice = 0;
    b.nice = 5;
    run(&a, &b);
    double expect = 100.0 * weight[0] / (weight[0] + weight[b.nice]);
    printf("2. nice 0 · %-2d      : CPU %2.0f%% · %2.0f%% (가중치 %d : %d → %.0f%% : %.0f%%)\n",
           b.nice, share(&a, &b), share(&b, &a), weight[0], weight[b.nice], expect, 100 - expect);
    ok &= share(&a, &b) > expect - 10 && share(&a, &b) < expect + 10;

    worker_t rt = { .fifo = 1 }, normal = { .nice = 0 };
    int r = run(&rt, &normal);
    if (r == EPERM || r == EINVAL) {
        printf("3. SCHED_FIFO       : 실시간 우선순위를 쓸 수 없어 건너뜀 (권한 없음). sudo 또는 rtprio 제한 필요 (README 참고)\n");
    } else {
        printf("3. SCHED_FIFO · 일반 : CPU %2.0f%% · %2.0f%% (실시간 스레드가 쉬지 않으면 일반은 굶음. 남은 몫은 커널의 RT 제한)\n",
               share(&rt, &normal), share(&normal, &rt));
        ok &= share(&normal, &rt) < 20;
    }
    return ok ? 0 : 1;
}
#endif
