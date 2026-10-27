/*
 * 우선순위 역전 — Linux (SCHED_FIFO, CPU 하나에 고정)
 *
 * 스레드 셋: L(10) · M(20) · H(30). 실시간 우선순위라 높은 쪽이 항상 먼저 돎
 * 모두 CPU 0에서만 돌게 고정해서 코어 하나짜리 RTOS와 같은 상황을 만듦
 *   0ms  L이 뮤텍스를 잡고 CPU 20ms 분량 일을 시작
 *   2ms  H가 깨어나 뮤텍스를 기다림
 *   5ms  M이 깨어나 CPU 100ms를 씀 (뮤텍스와 상관없음)
 *
 * 1. PTHREAD_PRIO_NONE    : M이 L을 밀어내서 H가 M이 끝날 때까지 기다림
 * 2. PTHREAD_PRIO_INHERIT : H가 기다리는 동안 L이 H의 우선순위(30)를 물려받음
 * 3. PTHREAD_PRIO_PROTECT : 잡는 순간 천장 우선순위(30)로 올라감
 *
 * 실시간 우선순위는 권한이 필요함: sudo, 또는 rtprio 제한(compose.yaml에 설정됨)
 * Linux 전용. 다른 OS에서는 안내만 출력
 */
#define _GNU_SOURCE
#include <stdio.h>

#ifndef __linux__
int main(void)
{
    printf("Linux 전용 예제 (SCHED_FIFO · CPU 고정). 도커 컨테이너 안에서 실행\n");
    return 0;
}
#else

#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <string.h>
#include <time.h>

#define L_PRI  10
#define M_PRI  20
#define H_PRI  30
#define L_WORK 20.0    /* CPU ms */
#define M_WORK 100.0   /* CPU ms */

static pthread_mutex_t lock;
static struct timespec start;
static double h_wait;
static int l_max_pri;

static double ms(clockid_t c)
{
    struct timespec t;
    clock_gettime(c, &t);
    return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

static double since_start(void)
{
    return ms(CLOCK_MONOTONIC) - (start.tv_sec * 1e3 + start.tv_nsec / 1e6);
}

static void sleep_until(double off_ms)
{
    struct timespec t = start;
    long ns = t.tv_nsec + (long)(off_ms * 1e6);
    t.tv_sec += ns / 1000000000L;
    t.tv_nsec = ns % 1000000000L;
    while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &t, NULL) == EINTR) { }
}

/* 지금 실제로 적용된 실시간 우선순위. 물려받은 값도 보임 (/proc의 priority = -1 - 우선순위) */
static int effective_pri(void)
{
    char buf[512];
    FILE *f = fopen("/proc/thread-self/stat", "r");
    if (!f)
        return -1;
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = '\0';
    char *p = strrchr(buf, ')');
    int prio = 0;
    /* ')' 뒤로 state부터 세어 18번째 필드가 priority */
    if (p && sscanf(p + 2, "%*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %*u %*u %*d %*d %d", &prio) == 1)
        return -1 - prio;
    return -1;
}

/* 자기 CPU 시간으로 ms만큼 일함. 밀려나 있던 시간은 세지 않음 */
static void spin(double work, int watch_pri)
{
    double end = ms(CLOCK_THREAD_CPUTIME_ID) + work, next = 0;
    while (ms(CLOCK_THREAD_CPUTIME_ID) < end) {
        if (watch_pri && ms(CLOCK_THREAD_CPUTIME_ID) >= next) {
            int p = effective_pri();
            if (p > l_max_pri)
                l_max_pri = p;
            next = ms(CLOCK_THREAD_CPUTIME_ID) + 1;
        }
    }
}

static void *task_l(void *arg)
{
    (void)arg;
    sleep_until(0);
    pthread_mutex_lock(&lock);
    spin(L_WORK, 1);
    pthread_mutex_unlock(&lock);
    return NULL;
}

static void *task_m(void *arg)
{
    (void)arg;
    sleep_until(5);
    spin(M_WORK, 0);
    return NULL;
}

static void *task_h(void *arg)
{
    (void)arg;
    sleep_until(2);
    pthread_mutex_lock(&lock);
    h_wait = since_start() - 2;         /* 깨어나야 할 시각부터 잡을 때까지 */
    pthread_mutex_unlock(&lock);
    return NULL;
}

static int spawn(pthread_t *t, void *(*fn)(void *), int pri)
{
    pthread_attr_t a;
    struct sched_param sp = { .sched_priority = pri };
    pthread_attr_init(&a);
    pthread_attr_setinheritsched(&a, PTHREAD_EXPLICIT_SCHED);
    pthread_attr_setschedpolicy(&a, SCHED_FIFO);
    pthread_attr_setschedparam(&a, &sp);
    int r = pthread_create(t, &a, fn, NULL);
    pthread_attr_destroy(&a);
    return r;
}

static int run(int protocol)
{
    pthread_mutexattr_t ma;
    pthread_t tl, tm, th;

    pthread_mutexattr_init(&ma);
    pthread_mutexattr_setprotocol(&ma, protocol);
    if (protocol == PTHREAD_PRIO_PROTECT)
        pthread_mutexattr_setprioceiling(&ma, H_PRI);   /* 천장 = 이 뮤텍스를 쓰는 가장 높은 우선순위 */
    pthread_mutex_init(&lock, &ma);
    pthread_mutexattr_destroy(&ma);
    h_wait = 0;
    l_max_pri = 0;

    clock_gettime(CLOCK_MONOTONIC, &start);
    start.tv_nsec += 10 * 1000000L;     /* 스레드를 다 만들고 10ms 뒤 함께 출발 */
    if (start.tv_nsec >= 1000000000L) {
        start.tv_sec++;
        start.tv_nsec -= 1000000000L;
    }
    int r = spawn(&tl, task_l, L_PRI);
    if (r)
        return r;
    spawn(&tm, task_m, M_PRI);
    spawn(&th, task_h, H_PRI);

    /* 출발 직후까지 이 스레드가 CPU를 깨워 둠. CPU가 쉬고 있으면 깨어나는 데 시간이 걸려
     * (도커 데스크톱 같은 가상 머신은 수 ms) 출발이 늦어짐. 우선순위가 가장 낮아서 일에는 안 끼어듦 */
    while (since_start() < 6) { }
    pthread_join(tl, NULL);
    pthread_join(tm, NULL);
    pthread_join(th, NULL);
    pthread_mutex_destroy(&lock);
    return 0;
}

int main(void)
{
    cpu_set_t one;
    CPU_ZERO(&one);
    CPU_SET(0, &one);
    sched_setaffinity(0, sizeof one, &one);     /* 이후 만드는 스레드도 CPU 0만 씀 */

    static const struct { int protocol; const char *name, *note; } modes[] = {
        {PTHREAD_PRIO_NONE,    "PRIO_NONE   ", "M이 끼어듦"},
        {PTHREAD_PRIO_INHERIT, "PRIO_INHERIT", "H의 우선순위를 물려받음"},
        {PTHREAD_PRIO_PROTECT, "PRIO_PROTECT", "잡는 순간 천장으로 올라감"},
    };
    double wait[3];
    int pri[3];

    for (int i = 0; i < 3; i++) {
        int r = run(modes[i].protocol);
        if (r == EPERM) {
            printf("실시간 우선순위를 쓸 수 없어 건너뜀 (EPERM). sudo 또는 rtprio 제한 필요, 도커 데스크톱은 막혀 있을 수 있음 (README 참고)\n");
            return 0;
        }
        wait[i] = h_wait;
        pri[i] = l_max_pri;
        printf("%s : H가 뮤텍스를 기다린 시간 %5.1fms, 그동안 L의 우선순위 최대 %d (%s)\n",
               modes[i].name, wait[i], pri[i], modes[i].note);
    }
    return wait[0] > M_WORK && wait[1] < M_WORK / 2 && wait[2] < M_WORK / 2 &&
           pri[0] == L_PRI && pri[1] == H_PRI && pri[2] == H_PRI ? 0 : 1;
}
#endif
