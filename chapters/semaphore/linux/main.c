/*
 * 세마포어 — Linux(POSIX)
 *
 * 1. 주차장 (카운팅 세마포어): 자리 3개에 차 6대. 세마포어로 동시에 들어간 차를 3대로 제한
 * 2. 진동벨 (신호 보내기): 한 스레드가 신호를 5번 보내고, 다른 스레드가 받은 만큼 처리
 *
 * macOS는 이름 없는 세마포어(sem_init)를 지원하지 않아서 이름 있는 세마포어(sem_open) 사용.
 * 만들자마자 sem_unlink로 이름을 지워서, 프로그램이 끝나면 함께 사라짐
 */
#include <fcntl.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdatomic.h>
#include <stdio.h>
#include <unistd.h>

#define CARS  6
#define SLOTS 3
#define RINGS 5

static sem_t *open_sem(const char *name, unsigned value)
{
    sem_unlink(name);                       /* 지난 실행이 남긴 것 정리 */
    sem_t *s = sem_open(name, O_CREAT | O_EXCL, 0600, value);
    if (s == SEM_FAILED) { perror("sem_open"); return NULL; }
    sem_unlink(name);
    return s;
}

/* 1. 주차장 */
static sem_t *slots;
static atomic_int parked, max_parked;
static int use_sem;

static void *car(void *arg)
{
    (void)arg;
    if (use_sem)
        sem_wait(slots);                    /* 빈자리 하나 차지, 없으면 기다림 */

    int now = atomic_fetch_add(&parked, 1) + 1;
    int m = atomic_load(&max_parked);
    while (now > m && !atomic_compare_exchange_weak(&max_parked, &m, now)) { }
    usleep(20000);                          /* 20ms 주차 */
    atomic_fetch_sub(&parked, 1);

    if (use_sem)
        sem_post(slots);                    /* 자리 반납 */
    return NULL;
}

static int parking(int with_sem)
{
    pthread_t t[CARS];
    use_sem = with_sem;
    atomic_store(&parked, 0);
    atomic_store(&max_parked, 0);
    for (int i = 0; i < CARS; i++)
        pthread_create(&t[i], NULL, car, NULL);
    for (int i = 0; i < CARS; i++)
        pthread_join(t[i], NULL);
    return atomic_load(&max_parked);
}

/* 2. 진동벨 */
static sem_t *bell;

static void *barista(void *arg)
{
    (void)arg;
    for (int i = 0; i < RINGS; i++)
        sem_post(bell);                     /* 음료 완성: 벨 울림 */
    return NULL;
}

int main(void)
{
    slots = open_sem("/hg-os-slots", SLOTS);
    bell = open_sem("/hg-os-bell", 0);
    if (!slots || !bell)
        return 1;

    int a = parking(0);
    int b = parking(1);
    printf("세마포어 없이 : 동시에 주차한 차 최대 %d대 (자리 %d개)\n", a, SLOTS);
    printf("세마포어      : 동시에 주차한 차 최대 %d대 (자리 %d개)\n", b, SLOTS);

    pthread_t t;
    pthread_create(&t, NULL, barista, NULL);
    pthread_join(t, NULL);
    int got = 0;
    while (sem_trywait(bell) == 0)          /* 손님: 쌓인 신호를 하나씩 받음 */
        got++;
    printf("진동벨        : %d번 울림, 받은 신호 %d번 (POSIX 세마포어는 횟수를 셈)\n", RINGS, got);

    sem_close(slots);
    sem_close(bell);
    return (b <= SLOTS && got == RINGS) ? 0 : 1;
}
