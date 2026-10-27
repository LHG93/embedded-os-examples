/*
 * 데드락 — Linux(pthread)
 *
 * 우유 · 시럽 뮤텍스 두 개. 직원 A · B 스레드가 둘 다 쥐고 음료를 ROUNDS잔씩 만듦.
 * 첫 잔에서 첫 번째 뮤텍스를 잡은 뒤 10ms 쉬어서, 상대가 다른 쪽을 잡을 틈을 줌
 *
 * 1. 순서 반대 (A: 우유 → 시럽, B: 시럽 → 우유) : 둘 다 영원히 기다림. 1초 지켜보고 데드락으로 판정
 * 2. 순서 고정 (둘 다 우유 → 시럽)             : 끝까지 완료
 * 3. 순서 반대 + trylock                        : 두 번째를 못 잡으면 쥔 것을 놓고 다시. 완료
 * 4. 같은 뮤텍스를 두 번 잠금                    : 혼자서도 데드락. 오류 검사 뮤텍스는 EDEADLK를 돌려줌
 */
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define ROUNDS 10000

enum { OPPOSITE, ORDERED, TRYLOCK };

struct shop {
    pthread_mutex_t milk, syrup;
    int mode;
    atomic_int done;        /* 끝난 직원 수 */
    atomic_long retries;    /* 놓고 다시 한 횟수 */
    long drinks;            /* 두 뮤텍스를 다 쥔 동안만 바꿈 */
};

struct staff {
    struct shop *s;
    int milk_first;         /* A는 우유 먼저, B는 시럽 먼저 */
};

static void *staff(void *arg)
{
    struct staff *w = arg;
    struct shop *s = w->s;
    pthread_mutex_t *first  = w->milk_first ? &s->milk  : &s->syrup;
    pthread_mutex_t *second = w->milk_first ? &s->syrup : &s->milk;

    if (s->mode == ORDERED) {           /* 규칙: 항상 우유 먼저 */
        first  = &s->milk;
        second = &s->syrup;
    }

    for (int i = 0; i < ROUNDS; i++) {
        pthread_mutex_lock(first);
        if (i == 0)
            usleep(10000);              /* 첫 잔: 상대가 다른 쪽을 잡을 틈 */

        if (s->mode == TRYLOCK) {
            while (pthread_mutex_trylock(second) != 0) {
                pthread_mutex_unlock(first);    /* 못 잡으면 쥔 것을 놓고 */
                atomic_fetch_add(&s->retries, 1);
                sched_yield();                  /* 상대에게 양보한 뒤 */
                pthread_mutex_lock(first);      /* 처음부터 다시 */
            }
        } else {
            pthread_mutex_lock(second);         /* 순서 반대면 여기서 영원히 기다림 */
        }

        s->drinks++;
        pthread_mutex_unlock(second);
        pthread_mutex_unlock(first);
    }
    atomic_fetch_add(&s->done, 1);
    return NULL;
}

/* 1초 안에 둘 다 끝나면 1, 아니면 0 (데드락) */
static int run(struct shop *s, int mode)
{
    static struct staff a, b;
    pthread_t ta, tb;

    pthread_mutex_init(&s->milk, NULL);
    pthread_mutex_init(&s->syrup, NULL);
    s->mode = mode;
    a = (struct staff){s, 1};
    b = (struct staff){s, 0};
    pthread_create(&ta, NULL, staff, &a);
    pthread_create(&tb, NULL, staff, &b);

    for (int ms = 0; ms < 1000 && atomic_load(&s->done) < 2; ms++)
        usleep(1000);
    if (atomic_load(&s->done) < 2)
        return 0;                       /* 멈춘 스레드는 프로그램이 끝날 때 함께 사라짐 */
    pthread_join(ta, NULL);
    pthread_join(tb, NULL);
    return 1;
}

int main(void)
{
    static struct shop opposite, ordered, trylock;
    int ok = 1;

    int r1 = run(&opposite, OPPOSITE);
    if (r1)
        printf("순서 반대         : 완료 (이번엔 운 좋게 엇갈리지 않음)\n");
    else
        printf("순서 반대         : 데드락. 1초 동안 둘 다 못 끝냄 (A는 우유, B는 시럽을 쥐고 기다림)\n");

    int r2 = run(&ordered, ORDERED);
    printf("순서 고정         : %s, 음료 %ld잔\n", r2 ? "완료" : "멈춤", ordered.drinks);

    int r3 = run(&trylock, TRYLOCK);
    printf("순서 반대 + trylock: %s, 음료 %ld잔, 놓고 다시 한 횟수 %ld번\n",
           r3 ? "완료" : "멈춤", trylock.drinks, atomic_load(&trylock.retries));

    /* 같은 뮤텍스를 두 번: 기본 뮤텍스면 영원히 멈추므로 오류 검사 뮤텍스로 */
    pthread_mutexattr_t at;
    pthread_mutex_t m;
    pthread_mutexattr_init(&at);
    pthread_mutexattr_settype(&at, PTHREAD_MUTEX_ERRORCHECK);
    pthread_mutex_init(&m, &at);
    pthread_mutex_lock(&m);
    int r4 = pthread_mutex_lock(&m);
    printf("같은 뮤텍스 두 번 : %s\n", r4 == EDEADLK ? "EDEADLK (오류 검사 뮤텍스가 데드락을 알려 줌)" : strerror(r4));
    pthread_mutex_unlock(&m);

    ok = !r1 && r2 && r3 && ordered.drinks == 2 * ROUNDS && trylock.drinks == 2 * ROUNDS && r4 == EDEADLK;
    return ok ? 0 : 1;
}
