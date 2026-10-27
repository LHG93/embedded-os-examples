/*
 * Linux 실시간 — Linux 전용
 *
 * 0. 커널 확인     : 선점 모델(PREEMPT · PREEMPT_RT)
 * 1. 페이지 폴트   : 처음 쓰는 메모리는 그 순간 커널이 페이지를 마련함(폴트) → 그만큼 늦어짐
 *                    미리 건드려 두거나(prefault) mlockall로 잠그면 주기 루프 안에서 폴트가 없음
 * 2. 실시간 레시피 : 1ms 주기 루프가 매번 새 메모리 16KB를 씀. 같은 CPU에 바쁜 스레드 2개
 *                    대책 없음 vs SCHED_FIFO + mlockall + 미리 건드림
 *
 * 2의 실시간 쪽은 권한 필요(SCHED_FIFO · mlockall). 없으면 건너뜀
 * Linux 전용 (CPU 고정 · SCHED_FIFO · /sys). 다른 OS에서는 안내만 출력
 */
#define _GNU_SOURCE
#include <stdio.h>

#ifndef __linux__
int main(void)
{
    printf("Linux 전용 예제 (SCHED_FIFO · mlockall · CPU 고정). 도커 컨테이너 안에서 실행\n");
    return 0;
}
#else

#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

#define PAGE      4096
#define TOUCH     (16 * 1024)       /* 한 번에 쓰는 새 메모리 */
#define LOOPS     1000
#define PERIOD_NS 1000000L          /* 1ms */

static double now_us(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e6 + t.tv_nsec / 1e3;
}

static long minor_faults(void)
{
    struct rusage u;
    getrusage(RUSAGE_THREAD, &u);
    return u.ru_minflt;
}

/* 페이지마다 한 바이트씩 써서 실제 메모리를 쓰게 함 */
/* 4KB 페이지로 받음. 리눅스는 큰 영역을 2MB 페이지(THP)로 주기도 하는데, 그러면 폴트는 적지만 한 번이 큼 */
static char *map(size_t n)
{
    char *p = mmap(NULL, n, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED)
        return NULL;
    madvise(p, n, MADV_NOHUGEPAGE);
    return p;
}

static void touch(char *p, size_t n)
{
    for (size_t i = 0; i < n; i += PAGE)
        p[i] = 1;
}

/* ---------- 1. 페이지 폴트 ---------- */
static void fault_cost(const char *name, int prefault)
{
    size_t total = (size_t)TOUCH * 200;
    char *buf = map(total);
    if (!buf)
        return;
    if (prefault)
        touch(buf, total);                  /* 미리 한 번 다 건드림 */
    double worst = 0, sum = 0;
    long f0 = minor_faults();
    for (int i = 0; i < 200; i++) {
        double t = now_us();
        touch(buf + (size_t)i * TOUCH, TOUCH);
        double d = now_us() - t;
        sum += d;
        if (d > worst)
            worst = d;
    }
    long faults = minor_faults() - f0;
    printf("   %s : 평균 %5.1fus, 최대 %6.1fus, 페이지 폴트 %ld번\n", name, sum / 200, worst, faults);
    munmap(buf, total);
}

/* ---------- 2. 실시간 레시피 ---------- */
/* 늦음 분포 칸: ~20 · ~50 · ~100 · ~500 · ~1000 · 그 이상 (us) */
static const int EDGE[] = {20, 50, 100, 500, 1000};
#define BINS 6

typedef struct {
    int rt;                             /* 1이면 SCHED_FIFO + mlockall + 미리 건드림 */
    double avg, max;
    long faults;
    int over;
    int hist[BINS];
} run_t;

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
    run_t *r = arg;
    char *buf = map((size_t)TOUCH * LOOPS);             /* 매 주기 새 16KB를 씀 */
    if (!buf)
        return NULL;
    if (r->rt)
        touch(buf, (size_t)TOUCH * LOOPS);
    struct timespec next, now;
    long f0 = minor_faults();
    clock_gettime(CLOCK_MONOTONIC, &next);
    for (int i = 0; i < LOOPS; i++) {
        add_ns(&next, PERIOD_NS);
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);
        touch(buf + (size_t)i * TOUCH, TOUCH);           /* 일: 새 메모리 쓰기 */
        clock_gettime(CLOCK_MONOTONIC, &now);
        double late = (now.tv_sec - next.tv_sec) * 1e6 + (now.tv_nsec - next.tv_nsec) / 1e3;  /* 깨어남 + 일 */
        r->avg += late / LOOPS;
        if (late > r->max)
            r->max = late;
        r->over += late > 500;
        int b = 0;
        while (b < BINS - 1 && late >= EDGE[b])
            b++;
        r->hist[b]++;
    }
    r->faults = minor_faults() - f0;
    munmap(buf, (size_t)TOUCH * LOOPS);
    return NULL;
}

static void print_hist(const run_t *r)
{
    printf("     분포: <20us %d · <50 %d · <100 %d · <500 %d · <1ms %d · 1ms 이상 %d\n",
           r->hist[0], r->hist[1], r->hist[2], r->hist[3], r->hist[4], r->hist[5]);
}

static int recipe(run_t *r)
{
    pthread_t load[2], t;
    pthread_attr_t a;
    r->avg = r->max = 0;
    r->over = 0;
    r->faults = 0;
    memset(r->hist, 0, sizeof r->hist);
    if (r->rt && mlockall(MCL_CURRENT | MCL_FUTURE) != 0)
        return errno;                   /* 지금 있는 메모리와 앞으로 받을 메모리를 모두 잠금 */
    stop_load = 0;
    for (int i = 0; i < 2; i++)
        pthread_create(&load[i], NULL, busy, NULL);
    pthread_attr_init(&a);
    if (r->rt) {
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
    for (int i = 0; i < 2; i++)
        pthread_join(load[i], NULL);
    if (r->rt)
        munlockall();
    return err;
}

int main(void)
{
    int ok = 1;
    cpu_set_t one;
    CPU_ZERO(&one);
    CPU_SET(0, &one);
    sched_setaffinity(0, sizeof one, &one);     /* 모두 CPU 0 하나에서 */

    /* 0. 커널 */
    struct utsname u;
    uname(&u);
    int is_rt = strstr(u.version, "PREEMPT_RT") != NULL || access("/sys/kernel/realtime", F_OK) == 0;
    printf("0. 커널 %s : %s → %s\n", u.release, strstr(u.version, "PREEMPT") ? u.version : "(선점 정보 없음)",
           is_rt ? "PREEMPT_RT (실시간 커널)" : "PREEMPT_RT 아님 (일반 커널)");

    /* 1. 페이지 폴트 */
    printf("1. 새 메모리 16KB 쓰기 200번 (페이지 4KB × 4)\n");
    fault_cost("처음 쓰는 메모리     ", 0);
    fault_cost("미리 건드려 둔 메모리", 1);

    /* 2. 레시피 */
    printf("2. 1ms 주기 루프 %d번, 매번 새 16KB 쓰기, 같은 CPU에 바쁜 스레드 2개 (늦음 = 깨어남 + 일)\n", LOOPS);
    run_t plain = { .rt = 0 }, rt = { .rt = 1 };
    recipe(&plain);
    printf("   일반 정책 · 대책 없음    : 평균 %5.0fus, 최대 %6.0fus, 500us 넘음 %d번, 루프 안 폴트 %ld번\n",
           plain.avg, plain.max, plain.over, plain.faults);
    print_hist(&plain);
    ok &= plain.faults > 0;
    int err = recipe(&rt);
    if (err == EPERM || err == EINVAL || err == ENOMEM) {
        printf("   FIFO + mlockall + 미리   : 건너뜀 (%s). sudo · rtprio · memlock 제한 필요 (README 참고)\n",
               err == ENOMEM ? "메모리 잠금 한도 부족" : "실시간 우선순위 권한 없음");
    } else {
        printf("   FIFO + mlockall + 미리   : 평균 %5.0fus, 최대 %6.0fus, 500us 넘음 %d번, 루프 안 폴트 %ld번\n",
               rt.avg, rt.max, rt.over, rt.faults);
        print_hist(&rt);
        ok &= rt.faults < plain.faults && rt.max < plain.max;
    }
    return ok ? 0 : 1;
}
#endif
