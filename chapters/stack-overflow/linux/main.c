/*
 * 스택 넘침과 메모리 보호 — Linux
 *
 * 1. 가드 페이지     : 스레드 스택 아래에는 접근 금지 페이지가 있음. 스택 256KB 스레드가 깊이 재귀하면
 *                     가드에 닿는 순간 SIGSEGV. 처리기는 넘친 스택 대신 따로 마련한 스택(sigaltstack)에서 돎
 *                     (프로세스가 죽으므로 자식 프로세스에서 실행)
 * 2. 읽기 전용 페이지 : mprotect로 읽기 전용으로 바꾼 뒤 쓰면 막힘 (MPU와 같은 일을 MMU가 함)
 */
#include <pthread.h>
#include <setjmp.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

/* ---------- 1. 가드 페이지 ---------- */
static char *stack_top;
static volatile int max_depth;

static void on_segv(int sig, siginfo_t *si, void *ctx)
{
    (void)ctx;
    char buf[200];
    long used = (long)(stack_top - (char *)si->si_addr);
    int n = snprintf(buf, sizeof buf, "  깊이 %d단계에서 %s, 닿은 주소는 스택 시작에서 %ldKB 아래 (가드 페이지)\n",
                     max_depth, sig == SIGSEGV ? "SIGSEGV" : "SIGBUS", used / 1024);
    write(1, buf, (size_t)n);           /* 처리기에서는 printf 대신 write */
    _exit(used > 200 * 1024 && used < 320 * 1024 ? 0 : 1);
}

static int __attribute__((noinline)) deep(int n)
{
    volatile char buf[1024];
    buf[0] = (char)n;
    max_depth = n;
    return deep(n + 1) + buf[0];
}

static void *overflow(void *arg)
{
    (void)arg;
    char top;
    stack_top = &top;                   /* 이 스레드 스택의 시작(가장 높은 쪽) */
    /* 처리기는 따로 마련한 스택에서 (넘친 스택에서는 처리기도 못 돎) */
    stack_t ss = {.ss_sp = malloc(SIGSTKSZ * 4), .ss_size = SIGSTKSZ * 4};
    sigaltstack(&ss, NULL);
    deep(1);
    return NULL;
}

static int guard_demo(void)
{
    pid_t pid = fork();
    if (pid == 0) {
        struct sigaction sa;
        memset(&sa, 0, sizeof sa);
        sa.sa_sigaction = on_segv;
        sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
        sigaction(SIGSEGV, &sa, NULL);
        sigaction(SIGBUS, &sa, NULL);   /* macOS는 SIGBUS로 오기도 함 */

        pthread_attr_t a;
        pthread_attr_init(&a);
        /* 스택 256KB. 아래에 가드 페이지가 붙음 (arm64 Linux는 최소가 128KB라 64KB는 거절됨) */
        pthread_attr_setstacksize(&a, 256 * 1024);
        pthread_t t;
        pthread_create(&t, &a, overflow, NULL);
        pthread_join(t, NULL);
        _exit(2);                       /* 여기 오면 넘침을 못 잡은 것 */
    }
    int status;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : 3;
}

/* ---------- 2. 읽기 전용 페이지 ---------- */
static sigjmp_buf back;

static void on_write(int sig)
{
    (void)sig;
    siglongjmp(back, 1);                /* 막힌 쓰기를 건너뛰고 돌아감 */
}

int main(void)
{
    pthread_attr_t a;
    size_t guard = 0;
    pthread_attr_init(&a);
    pthread_attr_getguardsize(&a, &guard);
    printf("가드 페이지          : 새 스레드 스택 아래 %zu바이트 (pthread 기본)\n", guard);

    printf("256KB 스택 넘침      :\n");
    fflush(stdout);
    int r1 = guard_demo();

    long page = sysconf(_SC_PAGESIZE);
    volatile uint32_t *settings = mmap(NULL, (size_t)page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    settings[0] = 7;
    mprotect((void *)settings, (size_t)page, PROT_READ);   /* 이제부터 읽기만 */
    signal(SIGSEGV, on_write);
    signal(SIGBUS, on_write);
    int blocked = 0;
    if (sigsetjmp(back, 1) == 0)
        settings[0] = 99;
    else
        blocked = 1;
    printf("읽기 전용 페이지 쓰기 : %s, 값은 그대로 %u\n", blocked ? "막힘(시그널)" : "막히지 않음", settings[0]);

    return r1 == 0 && blocked && settings[0] == 7 ? 0 : 1;
}
