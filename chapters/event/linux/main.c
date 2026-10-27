/*
 * 이벤트 — Linux(pthread)
 *
 * pthread에는 이벤트 그룹이 없어서 뮤텍스 + 조건 변수 + 비트 묶음으로 직접 만듦 (ev_set · ev_wait · ev_sync)
 *
 * 1. 모두 기다리기 (AND)  : 우유 10ms · 포스기 20ms · 머신 30ms에 비트를 세움. 오픈 담당은 셋 다 설 때 깨어남
 * 2. 아무거나 (OR)        : 배달 15ms · 매장 40ms에 주문. 바리스타는 먼저 온 쪽에 깨어남
 * 3. 모여서 출발 (Sync)   : 직원 셋이 준비를 마치는 시각이 달라도 다 같이 출발
 * 4. 비트는 세지 않음      : 같은 비트를 5번 세워도 받을 수 있는 건 한 번
 */
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define BIT_MILK     (1u << 0)
#define BIT_POS      (1u << 1)
#define BIT_MACHINE  (1u << 2)
#define BIT_HALL     (1u << 0)
#define BIT_DELIVERY (1u << 1)
#define BIT_ORDER    (1u << 0)

struct events {
    pthread_mutex_t m;
    pthread_cond_t c;
    unsigned bits;
};

static void ev_init(struct events *e)
{
    pthread_mutex_init(&e->m, NULL);
    pthread_cond_init(&e->c, NULL);
    e->bits = 0;
}

static void ev_set(struct events *e, unsigned b)
{
    pthread_mutex_lock(&e->m);
    e->bits |= b;
    pthread_cond_broadcast(&e->c);      /* 기다리는 쪽 모두 깨워서 각자 조건을 다시 봄 */
    pthread_mutex_unlock(&e->m);
}

static int met(unsigned bits, unsigned mask, int all)
{
    return all ? (bits & mask) == mask : (bits & mask) != 0;
}

/* all: 1이면 mask 전부, 0이면 하나라도. 깨어나면 mask 비트를 지움. timeout_ms < 0이면 무한 대기
 * 돌려주는 값: 깨어날 때(지우기 전) 비트. 시간 초과면 그때 비트 */
static unsigned ev_wait(struct events *e, unsigned mask, int all, int timeout_ms)
{
    struct timespec ts;
    if (timeout_ms >= 0) {
        struct timeval now;
        gettimeofday(&now, NULL);       /* 조건 변수 시간 제한은 실제 시각 기준 */
        long ns = now.tv_usec * 1000L + timeout_ms * 1000000L;
        ts.tv_sec = now.tv_sec + ns / 1000000000L;
        ts.tv_nsec = ns % 1000000000L;
    }
    pthread_mutex_lock(&e->m);
    while (!met(e->bits, mask, all)) {
        if (timeout_ms < 0)
            pthread_cond_wait(&e->c, &e->m);
        else if (pthread_cond_timedwait(&e->c, &e->m, &ts) == ETIMEDOUT)
            break;
    }
    unsigned got = e->bits;
    if (met(got, mask, all))
        e->bits &= ~mask;
    pthread_mutex_unlock(&e->m);
    return got;
}

/* 내 비트를 세우고 all이 다 설 때까지 기다림 (FreeRTOS xEventGroupSync와 같음, 한 번 쓰고 버리는 용도) */
static void ev_sync(struct events *e, unsigned mine, unsigned all)
{
    pthread_mutex_lock(&e->m);
    e->bits |= mine;
    pthread_cond_broadcast(&e->c);
    while ((e->bits & all) != all)
        pthread_cond_wait(&e->c, &e->m);
    pthread_mutex_unlock(&e->m);
}

static struct events ev;
static struct timespec t0;

static int ms_since_start(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (int)((t.tv_sec - t0.tv_sec) * 1000 + (t.tv_nsec - t0.tv_nsec) / 1000000);
}

static void sleep_until_ms(int at)
{
    while (ms_since_start() < at)
        usleep(200);
}

struct setter { unsigned bit; int at_ms; };

static void *setter(void *arg)
{
    const struct setter *s = arg;
    sleep_until_ms(s->at_ms);
    ev_set(&ev, s->bit);
    return NULL;
}

static unsigned wait_mask;
static int wait_all, woke_ms;
static unsigned woke_bits;

static void *waiter(void *arg)
{
    (void)arg;
    woke_bits = ev_wait(&ev, wait_mask, wait_all, -1);
    woke_ms = ms_since_start();
    return NULL;
}

static int leave_ms[3];

static void *staff(void *arg)
{
    int i = (int)(long)arg;
    static const int ready_ms[3] = {5, 15, 25};
    sleep_until_ms(ready_ms[i]);
    ev_sync(&ev, 1u << i, 0x7);
    leave_ms[i] = ms_since_start();
    return NULL;
}

static void run_waiter(unsigned mask, int all, const struct setter *s, int n)
{
    pthread_t w, t[3];
    ev_init(&ev);
    wait_mask = mask;
    wait_all = all;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    pthread_create(&w, NULL, waiter, NULL);
    for (int i = 0; i < n; i++)
        pthread_create(&t[i], NULL, setter, (void *)&s[i]);
    pthread_join(w, NULL);
    for (int i = 0; i < n; i++)
        pthread_join(t[i], NULL);
}

int main(void)
{
    static const struct setter open_list[] = {{BIT_MILK, 10}, {BIT_POS, 20}, {BIT_MACHINE, 30}};
    static const struct setter orders[] = {{BIT_DELIVERY, 15}, {BIT_HALL, 40}};

    run_waiter(BIT_MILK | BIT_POS | BIT_MACHINE, 1, open_list, 3);
    int and_ms = woke_ms;
    unsigned and_bits = woke_bits;
    printf("모두 기다리기 (AND) : 우유 10ms · 포스기 20ms · 머신 30ms에 체크, 오픈 담당은 %dms에 깨어남 (비트 0x%x)\n",
           and_ms, and_bits);

    run_waiter(BIT_HALL | BIT_DELIVERY, 0, orders, 2);
    int or_ms = woke_ms;
    unsigned or_bits = woke_bits;
    printf("아무거나 (OR)       : 배달 15ms · 매장 40ms에 주문, 바리스타는 %dms에 깨어남 (깨운 비트: %s)\n",
           or_ms, (or_bits & BIT_DELIVERY) ? "배달" : "매장");

    pthread_t t[3];
    ev_init(&ev);
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (long i = 0; i < 3; i++)
        pthread_create(&t[i], NULL, staff, (void *)i);
    for (int i = 0; i < 3; i++)
        pthread_join(t[i], NULL);
    printf("모여서 출발 (Sync)  : 준비 5 · 15 · 25ms, 세 명 모두 %d · %d · %dms에 출발\n",
           leave_ms[0], leave_ms[1], leave_ms[2]);

    ev_init(&ev);
    for (int i = 0; i < 5; i++)
        ev_set(&ev, BIT_ORDER);
    int got = 0;
    while (ev_wait(&ev, BIT_ORDER, 1, 10) & BIT_ORDER)
        got++;
    printf("비트는 세지 않음    : 주문 비트를 5번 세움, 기다려서 받은 횟수 %d번\n", got);

    int ok = and_ms >= 30 && and_ms < 40 && and_bits == 0x7 && or_ms >= 15 && or_ms < 40 &&
             (or_bits & BIT_DELIVERY) && got == 1;
    for (int i = 0; i < 3; i++)
        ok = ok && leave_ms[i] >= 25 && leave_ms[i] < 35;
    return ok ? 0 : 1;
}
