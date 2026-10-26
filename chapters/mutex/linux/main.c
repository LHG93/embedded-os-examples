/*
 * 뮤텍스 — Linux(pthread)
 *
 * 계좌 두 개 a, b (각 1000). 스레드 둘이 서로 1씩 100만 번 옮김.
 * 합계 a + b는 언제 봐도 2000이어야 함.
 * 검사 스레드가 옮기는 도중의 합계를 계속 읽어서, 2000이 아닌 순간을 셈.
 *
 * 1. 잠금 없이          : 증가가 사라져서 최종 합계부터 틀림
 * 2. 변수마다 원자 연산  : 최종 합계는 맞음. 하지만 a를 뺀 뒤 b를 더하기 전 순간이 보임
 * 3. 뮤텍스            : 두 변수를 한 묶음으로 바꿈. 검사도 잠그고 읽어서 틀린 순간 0
 */
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>

#define LOOPS 1000000
#define TOTAL 2000

/* 1. 잠금 없이 */
static volatile long ua, ub;
static void u_move(int dir) { if (dir) { ua--; ub++; } else { ub--; ua++; } }
static long u_total(void) { return ua + ub; }

/* 2. 변수마다 원자 연산 */
static atomic_long xa, xb;
static void x_move(int dir)
{
    if (dir) { atomic_fetch_sub(&xa, 1); atomic_fetch_add(&xb, 1); }
    else     { atomic_fetch_sub(&xb, 1); atomic_fetch_add(&xa, 1); }
}
static long x_total(void) { return atomic_load(&xa) + atomic_load(&xb); }

/* 3. 뮤텍스: 옮기기와 읽기 모두 같은 뮤텍스로 감쌈 */
static long ma, mb;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static void m_move(int dir)
{
    pthread_mutex_lock(&lock);
    if (dir) { ma--; mb++; } else { mb--; ma++; }
    pthread_mutex_unlock(&lock);
}
static long m_total(void)
{
    pthread_mutex_lock(&lock);
    long t = ma + mb;
    pthread_mutex_unlock(&lock);
    return t;
}

typedef struct {
    const char *name;
    void (*move)(int dir);
    long (*total)(void);
} kind_t;

static const kind_t *cur;
static atomic_bool running;
static long bad;   /* 검사 스레드가 본 틀린 합계 횟수 */

static void *mover(void *arg)
{
    int dir = (int)(long)arg;
    for (int i = 0; i < LOOPS; i++)
        cur->move(dir);
    return NULL;
}

static void *checker(void *arg)
{
    (void)arg;
    while (atomic_load(&running))
        if (cur->total() != TOTAL)
            bad++;
    return NULL;
}

static long run(const kind_t *k)
{
    pthread_t c, a, b;
    cur = k;
    bad = 0;
    atomic_store(&running, true);
    pthread_create(&c, NULL, checker, NULL);
    pthread_create(&a, NULL, mover, (void *)1L);
    pthread_create(&b, NULL, mover, (void *)0L);
    pthread_join(a, NULL);
    pthread_join(b, NULL);
    atomic_store(&running, false);
    pthread_join(c, NULL);

    long t = k->total();
    printf("%s: 최종 합계 %ld (기대값 %d), 도중에 본 틀린 합계 %ld번\n", k->name, t, TOTAL, bad);
    return t;
}

int main(void)
{
    static const kind_t unsafe = {"잠금 없이  ", u_move, u_total};
    static const kind_t atomic = {"원자 연산  ", x_move, x_total};
    static const kind_t mutex  = {"뮤텍스     ", m_move, m_total};

    ua = ub = 1000;
    atomic_store(&xa, 1000);
    atomic_store(&xb, 1000);
    ma = mb = 1000;

    run(&unsafe);
    run(&atomic);
    long t = run(&mutex);
    long mutex_bad = bad;

    /* 확인하는 것은 뮤텍스 쪽뿐: 최종 합계가 맞고, 도중에도 틀린 합계를 본 적이 없어야 함 */
    return (t == TOTAL && mutex_bad == 0) ? 0 : 1;
}
