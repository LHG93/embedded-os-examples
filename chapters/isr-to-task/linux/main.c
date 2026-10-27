/*
 * 처리기에서 스레드로 넘기기 — Linux (사용자 공간)
 *
 * 시그널 처리기 안에서는 할 수 있는 일이 적음(비동기 시그널 안전 함수만). 그래서 알리기만 하고,
 * 일은 스레드가 함. 알리는 방법: 자기 자신에게 쓰는 파이프(self-pipe). write()는 처리기에서 써도 됨
 * 10ms마다 SIGALRM 100번. 처리기가 알린 순간부터 스레드가 깨어날 때까지 시간을 잼
 */
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define EVENTS 100

static int pfd[2];
static struct timespec stamp;

static void on_alarm(int sig)
{
    (void)sig;
    clock_gettime(CLOCK_MONOTONIC, &stamp); /* clock_gettime · write는 처리기에서 써도 되는 함수 */
    char c = 1;
    write(pfd[1], &c, 1);
}

static long lat_sum, lat_max;

static void *worker(void *arg)
{
    (void)arg;
    for (int i = 0; i < EVENTS; i++) {
        char c;
        read(pfd[0], &c, 1);            /* 알림이 올 때까지 잠 */
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        long us = (now.tv_sec - stamp.tv_sec) * 1000000L + (now.tv_nsec - stamp.tv_nsec) / 1000;
        lat_sum += us;
        if (us > lat_max)
            lat_max = us;
        /* 여기서는 printf · malloc · 잠금 등 무엇이든 해도 됨 */
    }
    return NULL;
}

int main(void)
{
    pipe(pfd);
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_alarm;
    sa.sa_flags = SA_RESTART;           /* 처리기 때문에 read()가 중간에 끊기지 않게 */
    sigaction(SIGALRM, &sa, NULL);

    /* 스레드는 시그널을 막고, 메인 스레드만 받게 */
    sigset_t block;
    sigemptyset(&block);
    sigaddset(&block, SIGALRM);
    pthread_sigmask(SIG_BLOCK, &block, NULL);
    pthread_t t;
    pthread_create(&t, NULL, worker, NULL);
    pthread_sigmask(SIG_UNBLOCK, &block, NULL);

    struct itimerval it = {{0, 10000}, {0, 10000}};
    setitimer(ITIMER_REAL, &it, NULL);
    pthread_join(t, NULL);
    struct itimerval off = {{0, 0}, {0, 0}};
    setitimer(ITIMER_REAL, &off, NULL);

    printf("시그널 처리기 → 작업 스레드 (self-pipe): 평균 %ldus, 최대 %ldus (%d번)\n",
           lat_sum / EVENTS, lat_max, EVENTS);
    return lat_sum / EVENTS < 5000 ? 0 : 1;
}
