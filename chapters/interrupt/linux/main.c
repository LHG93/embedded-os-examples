/*
 * 인터럽트 — Linux (사용자 공간에서 보기)
 *
 * 사용자 프로그램은 인터럽트를 직접 받지 못함. 커널이 처리하고, 프로그램에는 시그널로 알려 줌
 * 1. 시그널 = 사용자 공간의 인터럽트 : 1kHz SIGALRM. 처리기는 하던 일 중간에 끼어들어 실행됨
 * 2. 시그널 막기 = 인터럽트 끄기      : 50ms 동안 막으면 그사이 50번 울려도 1번만 남음 (걸림 표시가 하나)
 * 3. /proc/interrupts              : 커널이 처리한 진짜 인터럽트 수 (Linux 전용)
 */
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t ticks;

/* 처리기 안에서는 printf · malloc 같은 함수를 부르지 않음 (ISR과 같은 규칙) */
static void on_alarm(int sig)
{
    (void)sig;
    ticks++;
}

static void busy_ms(int ms)
{
    struct timespec s, n;
    clock_gettime(CLOCK_MONOTONIC, &s);
    do
        clock_gettime(CLOCK_MONOTONIC, &n);
    while ((n.tv_sec - s.tv_sec) * 1000 + (n.tv_nsec - s.tv_nsec) / 1000000 < ms);
}

#ifdef __linux__
/* /proc/interrupts에서 CPU 타이머 줄의 숫자를 모두 더함
 * x86은 "LOC:"(Local timer interrupts), arm64는 "arch_timer" */
static long timer_irqs(void)
{
    FILE *f = fopen("/proc/interrupts", "r");
    char line[4096];
    long total = -1;
    if (!f)
        return -1;
    while (fgets(line, sizeof line, f)) {
        char *p = strchr(line, ':');
        int is_loc = strncmp(line + strspn(line, " "), "LOC:", 4) == 0;
        if (!p || !(is_loc || strstr(line, "arch_timer")))
            continue;
        if (total < 0)
            total = 0;
        p++;
        long v;
        int n;
        while (sscanf(p, "%ld%n", &v, &n) == 1) {
            total += v;
            p += n;
        }
    }
    fclose(f);
    return total;
}
#endif

int main(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_alarm;
    sigaction(SIGALRM, &sa, NULL);

    struct itimerval it = {{0, 1000}, {0, 1000}};      /* 1ms마다 */
    setitimer(ITIMER_REAL, &it, NULL);

    /* 1. 바쁘게 일하는 중에도 처리기가 끼어듦 */
    ticks = 0;
    busy_ms(100);
    int t1 = ticks;
    printf("시그널 (사용자 공간의 인터럽트): 1kHz SIGALRM, 100ms 일하는 동안 처리기 %d번 끼어듦\n", t1);

    /* 2. 막아 두면 걸림 표시 하나만 남음 */
    sigset_t block;
    sigemptyset(&block);
    sigaddset(&block, SIGALRM);
    sigprocmask(SIG_BLOCK, &block, NULL);
    ticks = 0;
    busy_ms(50);
    int during = ticks;
    sigprocmask(SIG_UNBLOCK, &block, NULL);     /* 푸는 순간 걸려 있던 것이 들어옴 */
    int after = ticks;
    printf("시그널 막기 50ms              : 막은 동안 %d번, 풀자마자 %d번 (50번 울렸지만 표시는 하나)\n", during, after);

    struct itimerval off = {{0, 0}, {0, 0}};
    setitimer(ITIMER_REAL, &off, NULL);

    /* 3. 커널이 처리한 진짜 인터럽트 */
#ifdef __linux__
    long a = timer_irqs(), b;
    sleep(1);
    b = timer_irqs();
    if (a >= 0)
        printf("/proc/interrupts              : 1초 동안 커널이 처리한 CPU 타이머 인터럽트 %ld번 (모든 CPU 합)\n", b - a);
#else
    printf("/proc/interrupts              : Linux 전용. 도커 컨테이너 안에서 실행\n");
#endif

    return t1 >= 50 && during == 0 && after == 1 ? 0 : 1;
}
