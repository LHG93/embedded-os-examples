/*
 * 코드 리뷰 연습 (29장) — 일부러 문제를 넣은 코드. 빌드 목록에 없고, 실행하지 않음
 *
 * 컴파일러 검사만 해 본다:
 *   gcc -Wall -Wextra -fanalyzer -c chapters/review/review_me.c -o /dev/null
 * 도구가 잡는 것과, 사람이 리뷰에서 잡아야 하는 것을 비교 (답은 장 페이지)
 */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int count;                               /* 수신 처리와 로거가 같이 씀 */
static volatile int ready;
static pthread_mutex_t lock_a = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t lock_b = PTHREAD_MUTEX_INITIALIZER;

void on_byte(char c)                            /* 수신될 때마다 불림 */
{
    (void)c;
    count++;
}

void *logger(void *arg)
{
    (void)arg;
    for (;;) {
        while (!ready) { }
        pthread_mutex_lock(&lock_a);
        pthread_mutex_lock(&lock_b);
        printf("%d\n", count);
        pthread_mutex_unlock(&lock_b);
        pthread_mutex_unlock(&lock_a);
    }
}

void *sender(void *arg)
{
    (void)arg;
    for (;;) {
        pthread_mutex_lock(&lock_b);
        pthread_mutex_lock(&lock_a);
        char *buf = malloc(64);
        strcpy(buf, "hello");
        puts(buf);
        pthread_mutex_unlock(&lock_a);
        pthread_mutex_unlock(&lock_b);
    }
}
