/*
 * 스트림 · 메시지, 공유 메모리 — Linux (프로세스끼리)
 *
 * 자식 프로세스가 "HEL" · "LO\nWO" · "RLD\n"을 3번에 나눠 보냄. 부모는 10ms 늦게 읽음
 * 1. 파이프 (스트림)      : 바이트가 이어 붙어서 한 번에 12바이트로 받음
 * 2. 데이터그램 (메시지)   : socketpair(AF_UNIX, SOCK_DGRAM). 보낸 단위 그대로 3번에 받음
 * 3. 공유 메모리          : shm_open + mmap. 부모 · 자식이 같은 int를 20만 번씩 더함
 *                          잠금 없이는 값이 사라지고, 이름 있는 세마포어로 잠그면 정확
 */
#include <fcntl.h>
#include <semaphore.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#define LOOPS 200000

static const char *chunks[] = {"HEL", "LO\nWO", "RLD\n"};

struct board {
    volatile long count;
};

static void add(struct board *b, sem_t *lock)
{
    for (int i = 0; i < LOOPS; i++) {
        if (lock)
            sem_wait(lock);
        b->count++;                     /* 읽기 · 더하기 · 쓰기 (10장) */
        if (lock)
            sem_post(lock);
    }
}

static long shared_count(int use_lock)
{
    /* 이름 있는 공유 메모리: 관계없는 프로세스도 같은 이름으로 열 수 있음 */
    shm_unlink("/hg-board");
    int fd = shm_open("/hg-board", O_CREAT | O_RDWR, 0600);
    ftruncate(fd, sizeof(struct board));
    struct board *b = mmap(NULL, sizeof *b, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    shm_unlink("/hg-board");            /* 이름은 지워도, 이미 연결한 프로세스는 계속 씀 */
    b->count = 0;

    sem_t *lock = NULL;
    if (use_lock) {
        sem_unlink("/hg-board-lock");
        lock = sem_open("/hg-board-lock", O_CREAT | O_EXCL, 0600, 1);
        sem_unlink("/hg-board-lock");
    }

    pid_t pid = fork();
    if (pid == 0) {
        add(b, lock);
        _exit(0);
    }
    add(b, lock);
    waitpid(pid, NULL, 0);
    long r = b->count;
    munmap(b, sizeof *b);
    if (lock)
        sem_close(lock);
    return r;
}

int main(void)
{
    char buf[64];

    /* 1. 파이프 */
    int p[2];
    pipe(p);
    if (fork() == 0) {
        for (int i = 0; i < 3; i++)
            write(p[1], chunks[i], strlen(chunks[i]));
        _exit(0);
    }
    close(p[1]);
    usleep(10000);                      /* 받는 쪽이 늦게 옴 */
    wait(NULL);
    ssize_t n = read(p[0], buf, sizeof buf);
    close(p[0]);
    int lines = 0;
    for (ssize_t i = 0; i < n; i++)
        if (buf[i] == '\n')
            lines++;
    printf("파이프 (스트림)       : 3번에 나눠 쓴 3 · 5 · 4바이트를 한 번에 %zd바이트로 받음 → '\\n'으로 다시 나눠 %d줄\n",
           n, lines);

    /* 2. 데이터그램 */
    int s[2];
    socketpair(AF_UNIX, SOCK_DGRAM, 0, s);
    if (fork() == 0) {
        for (int i = 0; i < 3; i++)
            send(s[1], chunks[i], strlen(chunks[i]), 0);
        _exit(0);
    }
    usleep(10000);
    wait(NULL);
    char sizes[32] = "";
    int count = 0;
    while ((n = recv(s[0], buf, sizeof buf, MSG_DONTWAIT)) > 0) {
        snprintf(sizes + strlen(sizes), sizeof sizes - strlen(sizes), "%s%zd", count ? " · " : "", n);
        count++;
    }
    close(s[0]);
    close(s[1]);
    printf("데이터그램 (메시지)    : 3번에 나눠 보낸 것을 %d번에 받음, 크기 %s바이트 그대로\n", count, sizes);

    /* 3. 공유 메모리 */
    long a = shared_count(0);
    long b = shared_count(1);
    printf("공유 메모리 잠금 없이  : 두 프로세스가 %d번씩 더함, 결과 %ld (기대값 %d)\n", LOOPS, a, 2 * LOOPS);
    printf("공유 메모리 + 세마포어 : 결과 %ld\n", b);

    return lines == 2 && count == 3 && strcmp(sizes, "3 · 5 · 4") == 0 && b == 2 * LOOPS ? 0 : 1;
}
