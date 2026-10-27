/*
 * 프로세스와 스레드 — Linux
 *
 * 1. 프로세스 (fork)  : 자식이 전역 변수를 바꿔도 부모는 그대로. 주소는 같아 보여도 메모리는 따로 (가상 메모리)
 * 2. 스레드 (pthread) : 스레드가 바꾼 값을 메인이 그대로 봄. 주소 공간을 함께 씀
 * 3. ID              : 스레드마다 TID가 다르고 PID는 같음. 커널에는 둘 다 "태스크" (Linux 전용)
 * 4. 상태             : 잠든 자식은 S, 끝났는데 부모가 거두지 않은 자식은 Z(좀비) (Linux 전용, /proc)
 */
#define _GNU_SOURCE
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/syscall.h>
#endif

static volatile int global;

static void *thread_fn(void *arg)
{
    (void)arg;
    global = 100;
    return NULL;
}

#ifdef __linux__
static long tid_of_thread;

static void *tid_fn(void *arg)
{
    (void)arg;
    tid_of_thread = syscall(SYS_gettid);
    return NULL;
}

/* /proc/<pid>/stat의 세 번째 칸 = 상태 글자 */
static char state_of(pid_t pid)
{
    char path[64], buf[256];
    snprintf(path, sizeof path, "/proc/%d/stat", (int)pid);
    FILE *f = fopen(path, "r");
    if (!f)
        return '?';
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = '\0';
    char *p = strrchr(buf, ')');
    return p ? p[2] : '?';
}
#endif

int main(void)
{
    /* 1. 프로세스 */
    global = 0;
    pid_t pid = fork();
    if (pid == 0) {
        global = 100;
        printf("  (자식) global = %d, 주소 %p\n", global, (void *)&global);
        fflush(stdout);
        _exit(0);
    }
    waitpid(pid, NULL, 0);
    printf("프로세스 (fork)    : 자식이 global = 100으로 바꿈 → 부모가 본 값 %d, 주소 %p (같은 주소, 다른 메모리)\n",
           global, (void *)&global);
    int fork_ok = global == 0;

    /* 2. 스레드 */
    pthread_t t;
    pthread_create(&t, NULL, thread_fn, NULL);
    pthread_join(t, NULL);
    printf("스레드 (pthread)   : 스레드가 global = 100으로 바꿈 → 메인이 본 값 %d\n", global);
    int thread_ok = global == 100;
    int ok = fork_ok && thread_ok;

#ifdef __linux__
    /* 3. ID */
    pthread_create(&t, NULL, tid_fn, NULL);
    pthread_join(t, NULL);
    long main_tid = syscall(SYS_gettid);
    printf("ID                 : PID %d 하나에 메인 TID %ld, 스레드 TID %ld\n", (int)getpid(), main_tid, tid_of_thread);
    ok = ok && main_tid == getpid() && tid_of_thread != main_tid;

    /* 4. 상태 */
    pid_t sleeper = fork();
    if (sleeper == 0) {
        sleep(5);
        _exit(0);
    }
    pid_t quitter = fork();
    if (quitter == 0)
        _exit(0);
    usleep(200000);                     /* 둘 다 자리 잡을 때까지 */
    char s1 = state_of(sleeper), s2 = state_of(quitter);
    printf("상태 (/proc)       : 잠든 자식 %c, 끝났지만 아직 거두지 않은 자식 %c (좀비)\n", s1, s2);
    waitpid(quitter, NULL, 0);          /* 거두면 사라짐 */
    printf("                     waitpid로 거둔 뒤 %s\n", state_of(quitter) == '?' ? "/proc에서 사라짐" : "아직 남음");
    kill(sleeper, 9);
    waitpid(sleeper, NULL, 0);
    ok = ok && s1 == 'S' && s2 == 'Z';
#else
    printf("ID · 상태          : Linux 전용 (gettid, /proc). 도커 컨테이너 안에서 실행\n");
#endif
    return ok ? 0 : 1;
}
