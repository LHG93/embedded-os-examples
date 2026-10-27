/*
 * 종합 프로젝트 — Linux(pthread)
 *
 * FreeRTOS 판과 같은 파이프라인을 스레드로 만듦
 *   sensor     : 1ms마다 깨어나 센서 값을 큐로 (타이머 인터럽트 대신 절대 시각까지 잠듦)   9 · 20장
 *   process    : 10개씩 평균 · 최소 · 최대 → 프레임, 기준을 넘으면 경보
 *   tx         : 프레임을 패킷으로 "전송"
 *   alarm      : 경보를 조건 변수로 기다림
 *   supervisor : 100ms마다 process · tx의 "살아 있음" 비트를 보고 킥
 *   watchdog   : 300ms 동안 킥이 없으면 종료 (소프트웨어 워치독. 장비에선 /dev/watchdog)    21장
 * 큐는 16장처럼 고리 버퍼 + 뮤텍스 + 조건 변수. 센서는 기다리지 않고 넣어서 꽉 차면 버림
 * 같은 센서 값 · 같은 프레임이라 패킷 합계가 FreeRTOS 판과 같음
 */
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define SAMPLES         1000
#define QLEN            16
#define FRAME           10
#define THRESHOLD       3200
#define SLOW_PROCESS_US 0
#define DEADLINE_US     10000

enum { ALIVE_PROC = 1 << 0, ALIVE_TX = 1 << 1 };

struct sample {
    uint16_t val, seq;
    int last;                           /* 끝 표시 */
    uint64_t stamp;                     /* 센서가 읽은 시각 (ns) */
};

struct packet {
    int last, len;
    uint64_t stamp;
    char text[44];
};

/* ---------- 큐 (16장) ---------- */
struct queue {
    pthread_mutex_t m;
    pthread_cond_t not_full, not_empty;
    size_t size, head, count, len;
    unsigned char *buf;
};

static void q_init(struct queue *q, size_t size, size_t len)
{
    pthread_mutex_init(&q->m, NULL);
    pthread_cond_init(&q->not_full, NULL);
    pthread_cond_init(&q->not_empty, NULL);
    q->size = size;
    q->len = len;
    q->head = q->count = 0;
    q->buf = malloc(size * len);
}

/* block이 0이면 꽉 찼을 때 바로 실패(-1) */
static int q_send(struct queue *q, const void *item, int block)
{
    pthread_mutex_lock(&q->m);
    while (q->count == q->len) {
        if (!block) {
            pthread_mutex_unlock(&q->m);
            return -1;
        }
        pthread_cond_wait(&q->not_full, &q->m);
    }
    memcpy(q->buf + ((q->head + q->count) % q->len) * q->size, item, q->size);
    q->count++;
    pthread_cond_signal(&q->not_empty);
    pthread_mutex_unlock(&q->m);
    return 0;
}

/* 꺼내기 전 대기 개수를 돌려줌 */
static size_t q_recv(struct queue *q, void *item)
{
    pthread_mutex_lock(&q->m);
    while (q->count == 0)
        pthread_cond_wait(&q->not_empty, &q->m);
    size_t waiting = q->count;
    memcpy(item, q->buf + q->head * q->size, q->size);
    q->head = (q->head + 1) % q->len;
    q->count--;
    pthread_cond_signal(&q->not_full);
    pthread_mutex_unlock(&q->m);
    return waiting;
}

/* ---------- 시간 ---------- */
static uint64_t now_ns(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000u + (uint64_t)t.tv_nsec;
}

static void sleep_ns(uint64_t ns)
{
    struct timespec d = { (time_t)(ns / 1000000000u), (long)(ns % 1000000000u) };
    nanosleep(&d, NULL);
}

static void busy_us(uint64_t us)
{
    uint64_t s = now_ns();
    while (now_ns() - s < us * 1000u) { }
}

static struct queue sample_q, pkt_q;
static uint64_t t_start;
static atomic_uint alive;
static atomic_int stop;
static _Atomic uint64_t last_kick;
static uint32_t lcg = 12345, drops, late_max;

static pthread_mutex_t alarm_m = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t alarm_c = PTHREAD_COND_INITIALIZER;
static int alarm_pending, alarm_quit;
static uint32_t alarm_frame;

static pthread_mutex_t stats_lock = PTHREAD_MUTEX_INITIALIZER;
static struct {                         /* stats_lock으로 보호 */
    uint32_t samples, gaps, frames, max_wait;
    uint32_t packets, bytes, sum, lat_max, lat_sum;
    uint32_t alarms, alarm_at[8];
    uint32_t kicks, misses;
    char first[3][44];
} st;

/* ---------- 센서: 1ms마다 ---------- */
static uint16_t read_sensor(uint32_t seq)
{
    uint32_t v = seq % 400;
    int32_t tri = v < 200 ? (int32_t)v * 20 : (int32_t)(400 - v) * 20;
    lcg = lcg * 1103515245u + 12345u;
    int32_t noise = (int32_t)((lcg >> 16) % 101) - 50;
    int32_t x = tri + noise;
    return (uint16_t)(x < 0 ? 0 : x);
}

static void *sensor(void *arg)
{
    (void)arg;
    uint64_t next = t_start;
    for (uint32_t i = 0; i < SAMPLES; i++) {
        next += 1000000u;                               /* 다음 깨어날 절대 시각 */
        uint64_t now = now_ns();
        if (next > now)
            sleep_ns(next - now);
        now = now_ns();
        uint32_t late = (uint32_t)((now - next) / 1000u);
        if (late > late_max)
            late_max = late;
        struct sample s = { read_sensor(i), (uint16_t)i, 0, now };
        if (q_send(&sample_q, &s, 0) < 0)
            drops++;                                    /* 꽉 참: 기다리지 않고 버림 */
    }
    return NULL;
}

/* ---------- 처리 ---------- */
static void *process(void *arg)
{
    (void)arg;
    struct sample s;
    uint32_t n = 0, sum = 0, mn = 0xFFFF, mx = 0, frame = 0;
    int above = 0, expect = -1;
    for (;;) {
        size_t waiting = q_recv(&sample_q, &s);
        if (s.last)
            break;
        busy_us(SLOW_PROCESS_US);
        int gap = expect >= 0 && s.seq != (uint16_t)expect;
        expect = (uint16_t)(s.seq + 1);
        sum += s.val;
        mn = s.val < mn ? s.val : mn;
        mx = s.val > mx ? s.val : mx;
        pthread_mutex_lock(&stats_lock);
        st.samples++;
        st.gaps += gap;
        if (waiting > st.max_wait)
            st.max_wait = (uint32_t)waiting;
        pthread_mutex_unlock(&stats_lock);
        if (++n < FRAME)
            continue;

        uint32_t avg = sum / FRAME;
        if (avg > THRESHOLD && !above) {                /* 넘어서는 순간만 */
            pthread_mutex_lock(&alarm_m);
            alarm_frame = frame;
            alarm_pending++;
            pthread_cond_signal(&alarm_c);
            pthread_mutex_unlock(&alarm_m);
        }
        above = avg > THRESHOLD;

        struct packet p = { 0, 0, s.stamp, "" };
        p.len = snprintf(p.text, sizeof p.text, "#%03lu avg=%lu min=%lu max=%lu", (unsigned long)frame,
                         (unsigned long)avg, (unsigned long)mn, (unsigned long)mx);
        q_send(&pkt_q, &p, 1);
        pthread_mutex_lock(&stats_lock);
        st.frames++;
        pthread_mutex_unlock(&stats_lock);
        atomic_fetch_or(&alive, ALIVE_PROC);
        frame++;
        n = sum = mx = 0;
        mn = 0xFFFF;
    }
    struct packet end = { 1, 0, 0, "" };
    q_send(&pkt_q, &end, 1);
    return NULL;
}

/* ---------- 전송 ---------- */
static void *tx(void *arg)
{
    (void)arg;
    struct packet p;
    for (;;) {
        q_recv(&pkt_q, &p);
        if (p.last)
            break;
        uint32_t lat = (uint32_t)((now_ns() - p.stamp) / 1000u);
        uint32_t sum = 0;
        for (int i = 0; i < p.len; i++)
            sum += (unsigned char)p.text[i];
        pthread_mutex_lock(&stats_lock);
        if (st.packets < 3)
            strcpy(st.first[st.packets], p.text);
        st.packets++;
        st.bytes += (uint32_t)p.len;
        st.sum = (st.sum + sum) & 0xFFFFu;
        st.lat_sum += lat;
        if (lat > st.lat_max)
            st.lat_max = lat;
        pthread_mutex_unlock(&stats_lock);
        atomic_fetch_or(&alive, ALIVE_TX);
    }
    return NULL;
}

/* ---------- 경보 ---------- */
static void *alarm_thread(void *arg)
{
    (void)arg;
    pthread_mutex_lock(&alarm_m);
    for (;;) {
        while (!alarm_pending && !alarm_quit)
            pthread_cond_wait(&alarm_c, &alarm_m);
        if (!alarm_pending)
            break;
        alarm_pending--;
        pthread_mutex_lock(&stats_lock);
        if (st.alarms < 8)
            st.alarm_at[st.alarms] = alarm_frame;
        st.alarms++;
        pthread_mutex_unlock(&stats_lock);
    }
    pthread_mutex_unlock(&alarm_m);
    return NULL;
}

/* ---------- 감시 · 워치독 ---------- */
static void *supervisor(void *arg)
{
    (void)arg;
    uint64_t next = t_start;
    for (;;) {
        next += 100000000u;
        uint64_t now = now_ns();
        if (next > now)
            sleep_ns(next - now);
        if (atomic_load(&stop))
            break;
        unsigned b = atomic_exchange(&alive, 0);
        pthread_mutex_lock(&stats_lock);
        if (b == (ALIVE_PROC | ALIVE_TX)) {
            atomic_store(&last_kick, now_ns());
            st.kicks++;
        } else {
            st.misses++;
        }
        pthread_mutex_unlock(&stats_lock);
    }
    return NULL;
}

static void *watchdog(void *arg)
{
    (void)arg;
    while (!atomic_load(&stop)) {
        sleep_ns(10000000u);
        if (now_ns() - atomic_load(&last_kick) > 300000000u) {
            printf("[워치독] 300ms 동안 킥이 없어 시간 초과 → 종료. 빠짐 %lu개\n", (unsigned long)drops);
            exit(3);
        }
    }
    return NULL;
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    q_init(&sample_q, sizeof(struct sample), QLEN);
    q_init(&pkt_q, sizeof(struct packet), 8);
    t_start = now_ns();
    atomic_store(&last_kick, t_start);

    pthread_t th[6];
    pthread_create(&th[0], NULL, alarm_thread, NULL);
    pthread_create(&th[1], NULL, process, NULL);
    pthread_create(&th[2], NULL, tx, NULL);
    pthread_create(&th[3], NULL, supervisor, NULL);
    pthread_create(&th[4], NULL, watchdog, NULL);
    pthread_create(&th[5], NULL, sensor, NULL);

    pthread_join(th[5], NULL);                          /* 센서가 1000개를 다 보낼 때까지 */
    struct sample end = { 0, 0, 1, 0 };
    q_send(&sample_q, &end, 1);                         /* 남은 샘플 뒤에 끝 표시 */
    pthread_join(th[1], NULL);
    pthread_join(th[2], NULL);
    pthread_mutex_lock(&alarm_m);
    alarm_quit = 1;
    pthread_cond_signal(&alarm_c);
    pthread_mutex_unlock(&alarm_m);
    pthread_join(th[0], NULL);
    atomic_store(&stop, 1);
    pthread_join(th[3], NULL);
    pthread_join(th[4], NULL);

    printf("종합 프로젝트: 센서 1kHz → 처리 → 전송, 샘플 %d개\n", SAMPLES);
    printf("1. 센서 · 큐 : 샘플 %d개 보냄 · %lu개 받음, 빠짐 %lu개, 순서 끊김 %lu번, 큐 최대 대기 %lu개 / %d\n",
           SAMPLES, (unsigned long)st.samples, (unsigned long)drops, (unsigned long)st.gaps,
           (unsigned long)st.max_wait, QLEN);
    char al[64] = "";
    for (uint32_t i = 0, k = 0; i < st.alarms && i < 8; i++)
        k += (uint32_t)snprintf(al + k, sizeof al - k, "%s#%03lu", i ? " · " : "", (unsigned long)st.alarm_at[i]);
    printf("2. 처리      : 프레임 %lu개 (10샘플 평균 · 최소 · 최대), 경보 %lu번 (평균 > %d 넘어설 때, %s)\n",
           (unsigned long)st.frames, (unsigned long)st.alarms, THRESHOLD, al);
    printf("3. 전송      : 패킷 %lu개, %lu바이트, 합계 0x%04lx, 처음 3개는\n", (unsigned long)st.packets,
           (unsigned long)st.bytes, (unsigned long)st.sum);
    for (int i = 0; i < 3 && i < (int)st.packets; i++)
        printf("     %s\n", st.first[i]);
    printf("4. 지연      : 마지막 샘플 → 전송 평균 %luus · 최대 %luus (마감 %dus)\n",
           (unsigned long)(st.packets ? st.lat_sum / st.packets : 0), (unsigned long)st.lat_max, DEADLINE_US);
    printf("5. 워치독    : 100ms마다 감시, 킥 %lu번 · 못 함 %lu번 (process · tx가 둘 다 살아 있을 때만)\n",
           (unsigned long)st.kicks, (unsigned long)st.misses);
    printf("6. 센서 주기 : 1ms 예정보다 늦게 깨어난 최대 %luus (타이머 인터럽트가 아니라 잠들었다 깨는 방식)\n",
           (unsigned long)late_max);

    char why[96] = "";
    size_t w = 0;
#define FAIL(bad, msg) \
    if (bad) w += (size_t)snprintf(why + w, sizeof why - w, "%s%s", w ? " · " : "", msg)
    FAIL(st.samples != SAMPLES || drops || st.gaps, "샘플 빠짐");
    FAIL(st.frames != SAMPLES / FRAME || st.packets != st.frames || st.alarms != 3, "프레임 · 경보 수 다름");
    FAIL(st.lat_max >= DEADLINE_US, "마감 넘김");
    FAIL(st.kicks < 9 || st.misses, "워치독 킥 못 함");
    int ok = w == 0;
    printf("결과        : %s%s%s\n", ok ? "통과" : "실패 (", why, ok ? "" : ")");
    return ok ? 0 : 1;
}
