/*
 * 레이스 컨디션 — Linux(pthread)
 *
 * 스레드 두 개가 공유 변수 counter를 각각 100만 번씩 1 증가.
 * 기대값 200만, 잠금 없이 하면 그보다 작게 나옴.
 *
 * counter++ 는 한 줄이지만 CPU에선 "읽기 → 1 더하기 → 쓰기" 세 단계.
 * 두 스레드가 같은 값을 읽고 각자 쓰면 증가 한 번이 사라짐.
 *
 * volatile로는 해결 안 됨. "매번 메모리에서 읽고 쓰기"일 뿐,
 * 세 단계를 한 번에(원자적으로) 하라는 뜻이 아님.
 *
 * 해결 하나: C11 원자 연산(atomic_fetch_add). 뮤텍스 해결은 다음 장.
 */
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>

#define LOOPS 1000000

static volatile long unsafe_counter;
static atomic_long safe_counter;

static void *add_unsafe(void *arg)
{
    (void)arg;
    for (int i = 0; i < LOOPS; i++)
        unsafe_counter++;           /* 읽기 → 더하기 → 쓰기, 중간에 끼어들기 가능 */
    return NULL;
}

static void *add_safe(void *arg)
{
    (void)arg;
    for (int i = 0; i < LOOPS; i++)
        atomic_fetch_add(&safe_counter, 1);   /* 세 단계를 한 번에 */
    return NULL;
}

static void run_two(void *(*fn)(void *))
{
    pthread_t a, b;
    pthread_create(&a, NULL, fn, NULL);
    pthread_create(&b, NULL, fn, NULL);
    pthread_join(a, NULL);
    pthread_join(b, NULL);
}

int main(void)
{
    const long expected = 2L * LOOPS;

    run_two(add_unsafe);
    printf("잠금 없이   : %ld (기대값 %ld, 잃어버린 증가 %ld번)\n",
           unsafe_counter, expected, expected - unsafe_counter);

    run_two(add_safe);
    printf("원자 연산   : %ld (기대값 %ld)\n", (long)atomic_load(&safe_counter), expected);

    /* CPU 1개 환경에선 잠금 없이도 우연히 맞을 수 있어서 안전한 쪽만 확인 */
    return atomic_load(&safe_counter) == expected ? 0 : 1;
}
