/*
 * 스택과 힙 — Linux
 *
 * 1. 스택 크기     : 메인 스레드는 ulimit -s, 새 스레드는 기본 크기로 따로 받음 (Linux 8MB, macOS 512KB)
 * 2. 스택은 아래로 : 함수를 한 단계 부를 때마다 지역 변수 주소가 일정하게 줄어듦
 * 3. 힙의 큰 블록   : malloc(64MB)는 mmap으로 받음. 실제 메모리(RSS)는 쓰는 순간 늘고, free하면 바로 돌려줌 (Linux 전용)
 */
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <unistd.h>

static uintptr_t addr[4];

static void __attribute__((noinline)) depth(int n)
{
    volatile char local[64];            /* 한 단계마다 지역 변수 64바이트 + 저장할 레지스터 */
    local[0] = (char)n;
    addr[n] = (uintptr_t)local;
    if (n < 3)
        depth(n + 1);
}

#ifdef __linux__
static long rss_kb(void)
{
    long pages = 0, resident = 0;
    FILE *f = fopen("/proc/self/statm", "r");
    if (f) {
        fscanf(f, "%ld %ld", &pages, &resident);
        fclose(f);
    }
    return resident * (sysconf(_SC_PAGESIZE) / 1024);
}
#endif

int main(void)
{
    /* 1. 스택 크기 */
    struct rlimit rl;
    getrlimit(RLIMIT_STACK, &rl);
    pthread_attr_t a;
    size_t thread_stack = 0;
    pthread_attr_init(&a);
    pthread_attr_getstacksize(&a, &thread_stack);
    printf("스택 크기          : 메인 스레드 한도 %ldKB (ulimit -s), 새 스레드 기본 %zuKB\n",
           rl.rlim_cur == RLIM_INFINITY ? -1L : (long)(rl.rlim_cur / 1024), thread_stack / 1024);

    /* 2. 스택은 아래로 */
    depth(0);
    printf("스택은 아래로      : 한 단계 부를 때마다 지역 변수 주소가 %ld · %ld · %ld바이트씩 줄어듦\n",
           (long)(addr[0] - addr[1]), (long)(addr[1] - addr[2]), (long)(addr[2] - addr[3]));
    int ok = addr[1] < addr[0] && addr[0] - addr[1] >= 64;

#ifdef __linux__
    /* 3. 큰 블록 */
    long r0 = rss_kb();
    size_t n = 64u << 20;
    char *p = malloc(n);
    long r1 = rss_kb();
    memset(p, 1, n);                    /* 쓰는 순간 실제 메모리를 받음 */
    long r2 = rss_kb();
    free(p);
    long r3 = rss_kb();
    printf("malloc(64MB)       : 받기만 했을 때 +%ldMB, 다 쓰고 나서 +%ldMB, free 뒤 +%ldMB (RSS 기준)\n",
           (r1 - r0) / 1024, (r2 - r0) / 1024, (r3 - r0) / 1024);
    ok = ok && (r1 - r0) < 4096 && (r2 - r0) > 60 * 1024 && (r3 - r0) < 4096;
#else
    printf("malloc(64MB)       : Linux 전용 (/proc/self/statm). 도커 컨테이너 안에서 실행\n");
#endif
    return ok ? 0 : 1;
}
