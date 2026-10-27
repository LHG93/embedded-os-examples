/*
 * 타이머 — Linux
 *
 * 1. 상대 대기 vs 절대 기한 : 3ms 일한 뒤 "10ms 자기"를 10번 하면 130ms. 다음 기한을 10ms씩 더해 가며
 *                            그 시각까지 자면 100ms. (FreeRTOS의 vTaskDelay와 vTaskDelayUntil)
 * 2. timerfd 주기 타이머     : 10ms마다 울림. read()가 "지난번 이후 몇 번 울렸는지"를 돌려줘서
 *                            처리가 늦으면 놓친 횟수를 알 수 있음 (Linux 전용)
 */
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/timerfd.h>
#endif

static long now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

static void busy_ms(int ms)
{
    long end = now_ms() + ms;
    while (now_ms() < end) { }
}

static void sleep_ms(long ms)
{
    if (ms <= 0)
        return;
    struct timespec t = {ms / 1000, (ms % 1000) * 1000000};
    nanosleep(&t, NULL);
}

int main(void)
{
    /* 1. 상대 대기 */
    long start = now_ms();
    for (int i = 0; i < 10; i++) {
        busy_ms(3);
        sleep_ms(10);                   /* 지금부터 10ms */
    }
    long rel = now_ms() - start;

    /* 절대 기한 */
    start = now_ms();
    long deadline = start;
    for (int i = 0; i < 10; i++) {
        busy_ms(3);
        deadline += 10;                 /* 지난 기한부터 10ms 뒤 */
        sleep_ms(deadline - now_ms());
    }
    long abs_ = now_ms() - start;
    printf("주기 작업 10번      : 3ms 일 + 10ms 자기 = %ldms, 기한을 10ms씩 더하기 = %ldms\n", rel, abs_);

    int ok = rel >= 125 && abs_ <= 105;

#ifdef __linux__
    /* 2. timerfd */
    int fd = timerfd_create(CLOCK_MONOTONIC, 0);
    struct itimerspec its = {{0, 10000000}, {0, 10000000}};     /* 10ms마다 */
    timerfd_settime(fd, 0, &its, NULL);
    uint64_t n;
    read(fd, &n, sizeof n);                                    /* 첫 번째 */
    read(fd, &n, sizeof n);
    uint64_t fast = n;
    busy_ms(35);                                               /* 처리가 늦음 */
    read(fd, &n, sizeof n);
    uint64_t late = n;
    close(fd);
    printf("timerfd 10ms 주기   : 제때 읽으면 한 번에 %llu번, 35ms 늦게 읽으면 한 번에 %llu번 (놓친 주기를 알려 줌)\n",
           (unsigned long long)fast, (unsigned long long)late);
    ok = ok && fast == 1 && late >= 3;
#else
    printf("timerfd 10ms 주기   : Linux 전용. 도커 컨테이너 안에서 실행\n");
#endif
    return ok ? 0 : 1;
}
