/*
 * 워치독 — Linux (사용자 공간: 감시 스레드 + 감독 프로세스)
 *
 * 하드웨어 워치독(/dev/watchdog)은 커널 드라이버가 다루고, 보통 systemd가 킥함. 컨테이너에서는 못 씀
 * 여기서는 같은 구조를 프로세스 안에서 만듦
 * - 작업 스레드 A · B: 50ms마다 "살아 있음" 시각을 남김
 * - 감시 스레드: 10ms마다 확인. 누가 150ms 넘게 소식이 없으면 프로세스를 끝냄(종료 코드 2)
 * - 감독(부모) 프로세스: 자식이 비정상으로 끝나면 다시 띄움 (systemd의 Restart=on-failure)
 * 1번째 실행: 100ms 뒤 B가 멈춤 → 감시가 감지 → 종료 코드 2 → 감독이 다시 시작
 * 2번째 실행: 500ms 정상 → 종료 코드 0
 */
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define BEAT_MS  50
#define LIMIT_MS 150

static atomic_long beat[2];
static int hang_b;

static long now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

static void *worker(void *arg)
{
    int id = (int)(long)arg;
    long start = now_ms();
    for (;;) {
        if (id == 1 && hang_b && now_ms() - start > 100)
            for (;;)
                pause();                /* B가 멈춤 (영원히 기다림) */
        atomic_store(&beat[id], now_ms());
        usleep(BEAT_MS * 1000);
    }
    return NULL;
}

static int child(int hang)
{
    pthread_t t[2];
    hang_b = hang;
    for (long i = 0; i < 2; i++) {
        atomic_store(&beat[i], now_ms());
        pthread_create(&t[i], NULL, worker, (void *)i);
    }
    long start = now_ms();
    while (now_ms() - start < 500) {     /* 감시: 10ms마다 확인 */
        usleep(10000);
        for (int i = 0; i < 2; i++) {
            long silent = now_ms() - atomic_load(&beat[i]);
            if (silent > LIMIT_MS) {
                printf("  감시 스레드: %c가 %ldms 동안 소식 없음 → 프로세스 종료(2)\n", 'A' + i, silent);
                fflush(stdout);
                _exit(2);
            }
        }
    }
    printf("  500ms 정상\n");
    fflush(stdout);
    _exit(0);
}

int main(void)
{
    int restarts = 0, status = 0;
    for (int run = 1; run <= 3; run++) {
        printf("감독: %d번째 실행\n", run);
        fflush(stdout);
        pid_t pid = fork();
        if (pid == 0)
            child(run == 1);            /* 첫 실행만 B가 멈춤 */
        waitpid(pid, &status, 0);
        if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
            break;
        restarts++;
        printf("감독: 종료 코드 %d → 다시 시작\n", WEXITSTATUS(status));
    }
    printf("결과: 다시 시작 %d번, 마지막 종료 코드 %d\n", restarts, WEXITSTATUS(status));
    return restarts == 1 && WEXITSTATUS(status) == 0 ? 0 : 1;
}
