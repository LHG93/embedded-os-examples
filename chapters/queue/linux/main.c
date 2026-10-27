/*
 * 큐 — Linux(pthread)
 *
 * pthread에는 큐가 없어서 고리 버퍼 + 뮤텍스 + 조건 변수 둘(빈칸 생김 · 내용 생김)로 직접 만듦 (q_send · q_recv)
 * OSTEP 30장의 생산자 · 소비자 문제와 같은 구조
 *
 * 1. 순서 · 꽉 차면 기다림  : 캐셔가 주문 8개를 한꺼번에 넣고, 바리스타는 5ms마다 하나씩 뺌
 * 2. 값 복사 vs 포인터      : 보낸 뒤 원래 메모를 고치면? 값으로 보낸 건 그대로, 포인터로 보낸 건 바뀜
 * 3. 기다리지 않고 넣기      : 꽉 찼을 때 바로 실패
 * 4. 커널 메시지 큐 (mq_open): 프로세스끼리도 쓰는 POSIX 메시지 큐. Linux 전용 (macOS엔 없음)
 */
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifdef __linux__
#include <fcntl.h>
#include <mqueue.h>
#endif

#define LEN    3
#define ORDERS 8

struct order {
    int no;
    char drink[24];
};

struct queue {
    pthread_mutex_t m;
    pthread_cond_t not_full, not_empty;
    size_t size, head, count;
    unsigned char *buf;
};

static void q_init(struct queue *q, size_t size)
{
    pthread_mutex_init(&q->m, NULL);
    pthread_cond_init(&q->not_full, NULL);
    pthread_cond_init(&q->not_empty, NULL);
    q->size = size;
    q->head = q->count = 0;
    q->buf = malloc(size * LEN);
}

/* block이 0이면 꽉 찼을 때 바로 실패(-1). 내용은 큐 안으로 복사 */
static int q_send(struct queue *q, const void *item, int block)
{
    pthread_mutex_lock(&q->m);
    while (q->count == LEN) {
        if (!block) {
            pthread_mutex_unlock(&q->m);
            return -1;
        }
        pthread_cond_wait(&q->not_full, &q->m);
    }
    memcpy(q->buf + ((q->head + q->count) % LEN) * q->size, item, q->size);
    q->count++;
    pthread_cond_signal(&q->not_empty);
    pthread_mutex_unlock(&q->m);
    return 0;
}

static void q_recv(struct queue *q, void *item)
{
    pthread_mutex_lock(&q->m);
    while (q->count == 0)
        pthread_cond_wait(&q->not_empty, &q->m);
    memcpy(item, q->buf + q->head * q->size, q->size);
    q->head = (q->head + 1) % LEN;
    q->count--;
    pthread_cond_signal(&q->not_full);
    pthread_mutex_unlock(&q->m);
}

static int q_full(struct queue *q)
{
    pthread_mutex_lock(&q->m);
    int full = q->count == LEN;
    pthread_mutex_unlock(&q->m);
    return full;
}

static struct queue q;
static int blocked;
static char got[64];

static void *cashier(void *arg)
{
    (void)arg;
    for (int i = 1; i <= ORDERS; i++) {
        struct order o = {i, "라테"};
        if (q_full(&q))
            blocked++;
        q_send(&q, &o, 1);
    }
    return NULL;
}

static void *barista(void *arg)
{
    (void)arg;
    struct order o;
    for (int i = 0; i < ORDERS; i++) {
        q_recv(&q, &o);
        snprintf(got + strlen(got), sizeof got - strlen(got), "%s%d", i ? " " : "", o.no);
        usleep(5000);                   /* 음료 만드는 시간 */
    }
    return NULL;
}

int main(void)
{
    /* 1. 순서 · 꽉 차면 기다림 */
    pthread_t c, b;
    q_init(&q, sizeof(struct order));
    pthread_create(&b, NULL, barista, NULL);
    pthread_create(&c, NULL, cashier, NULL);
    pthread_join(c, NULL);
    pthread_join(b, NULL);
    printf("순서 · 꽉 차면 기다림 : 받은 순서 %s, 캐셔가 꽂이가 차서 기다린 횟수 %d번\n", got, blocked);

    /* 2. 값 복사 vs 포인터 */
    struct order memo = {9, "라테"}, by_value;
    q_init(&q, sizeof(struct order));
    q_send(&q, &memo, 0);
    strcpy(memo.drink, "아메리카노");
    q_recv(&q, &by_value);

    struct order memo2 = {10, "라테"}, *p = &memo2, *by_pointer;
    q_init(&q, sizeof(struct order *));
    q_send(&q, &p, 0);                  /* 주소만 복사됨 */
    strcpy(memo2.drink, "아메리카노");
    q_recv(&q, &by_pointer);
    printf("값 복사              : 보낸 뒤 메모를 아메리카노로 고쳐도 받은 주문은 %s\n", by_value.drink);
    printf("포인터로 보내면       : 받은 주문이 %s (보낸 뒤 고친 내용이 보임)\n", by_pointer->drink);

    /* 3. 기다리지 않고 넣기 */
    q_init(&q, sizeof(struct order));
    int ok = 0, dropped = 0;
    for (int i = 1; i <= 5; i++) {
        struct order o = {i, "라테"};
        if (q_send(&q, &o, 0) == 0)
            ok++;
        else
            dropped++;
    }
    printf("기다리지 않고 넣기    : 5개 중 %d개 들어감, %d개 버림\n", ok, dropped);

    int pass = strcmp(got, "1 2 3 4 5 6 7 8") == 0 && blocked > 0 &&
               strcmp(by_value.drink, "라테") == 0 && strcmp(by_pointer->drink, "아메리카노") == 0 &&
               ok == LEN && dropped == 5 - LEN;

    /* 4. 커널 메시지 큐 */
#ifdef __linux__
    struct mq_attr attr = {.mq_maxmsg = LEN, .mq_msgsize = sizeof(struct order)};
    mq_unlink("/hg-orders");
    mqd_t mq = mq_open("/hg-orders", O_CREAT | O_RDWR | O_NONBLOCK, 0600, &attr);
    if (mq == (mqd_t)-1) {
        perror("mq_open");
        return 1;
    }
    int sent = 0, full = 0;
    for (int i = 1; i <= 5; i++) {
        struct order o = {i, "라테"};
        if (mq_send(mq, (const char *)&o, sizeof o, 0) == 0)
            sent++;
        else if (errno == EAGAIN)
            full++;                     /* O_NONBLOCK이라 꽉 차면 EAGAIN */
    }
    char order_list[32] = "";
    struct order o;
    while (mq_receive(mq, (char *)&o, sizeof o, NULL) >= 0)
        snprintf(order_list + strlen(order_list), sizeof order_list - strlen(order_list), "%s%d",
                 order_list[0] ? " " : "", o.no);
    mq_close(mq);
    mq_unlink("/hg-orders");
    printf("커널 메시지 큐        : 5개 중 %d개 들어감, %d개는 EAGAIN, 꺼낸 순서 %s\n", sent, full, order_list);
    pass = pass && sent == LEN && full == 5 - LEN && strcmp(order_list, "1 2 3") == 0;
#else
    printf("커널 메시지 큐        : Linux 전용 (mq_open). 도커 컨테이너 안에서 실행\n");
#endif
    return pass ? 0 : 1;
}
