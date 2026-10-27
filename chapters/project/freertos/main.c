/*
 * 종합 프로젝트 — FreeRTOS on Cortex-M3 (QEMU mps2-an385 전용)
 *
 * 센서 수집 → 처리 → 전송 파이프라인을 1초 돌리고 결과를 확인
 *   TIMER0 ISR (1kHz)  : 센서 값(삼각파 + 잡음)을 읽어 큐로                     18 · 19 · 16장
 *   process (3)        : 10개씩 평균 · 최소 · 최대 → 프레임, 기준을 넘으면 경보   15장
 *   tx (2)             : 메시지 버퍼로 받은 프레임을 패킷으로 "전송"              17장
 *   alarm (4)          : 경보 비트를 기다렸다가 시각 기록                        15장
 *   supervisor (1)     : 100ms마다 process · tx의 "살아 있음" 비트를 보고 워치독 킥  21장
 *   stats              : 뮤텍스로 보호하는 공유 통계                             11장
 * 샘플 1000개(1초)로 끝나서 지연 · 스택 여유 말고는 매번 같은 결과. Linux 판과 패킷 합계가 같음
 * SLOW_PROCESS_US를 키우면 처리가 샘플보다 느려짐 (바꿔 보기)
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "event_groups.h"
#include "message_buffer.h"
#include "board.h"
#include "mps2.h"

#define SAMPLES         1000            /* 1kHz로 1초 */
#define QLEN            16
#define FRAME           10              /* 샘플 10개 = 프레임 하나 (10ms) */
#define THRESHOLD       3200            /* 프레임 평균이 이것을 넘어서면 경보 */
#define SLOW_PROCESS_US 0               /* 샘플 하나마다 더 걸리는 처리 시간 */
#define DEADLINE_US     10000           /* 샘플 → 전송 마감 = 프레임 주기 */
#define US(c)           ((c) / (CPU_HZ / 1000000u))

enum { ALARM = 1 << 0, ALIVE_PROC = 1 << 1, ALIVE_TX = 1 << 2 };

typedef struct {
    uint16_t val, seq;
    uint32_t stamp;                     /* ISR에서 읽은 시각 (25MHz) */
} sample_t;

static QueueHandle_t sample_q;
static MessageBufferHandle_t pkt_mb;
static EventGroupHandle_t ev;
static SemaphoreHandle_t stats_lock;
static TaskHandle_t h_proc, h_tx, h_alarm, h_sup;
static volatile uint32_t isr_seq, drops, alarm_frame;
static uint32_t lcg = 12345;

static struct stats {                   /* stats_lock으로 보호 */
    uint32_t samples, gaps, frames, max_wait;
    uint32_t packets, bytes, sum, lat_max, lat_sum;
    uint32_t alarms, alarm_at[8];
    uint32_t kicks, misses;
    char first[3][40];
} st;

/* ---------- 워치독 (21장) ---------- */
static void wdog_start(uint32_t ms)
{
    WDOG_LOCK = WDOG_UNLOCK_KEY;
    WDOG_LOAD = ms * (CPU_HZ / 1000u);
    WDOG_CTRL = (1u << 0) | (1u << 1);
    WDOG_LOCK = 0;
}

static void wdog_kick(void)
{
    WDOG_LOCK = WDOG_UNLOCK_KEY;
    WDOG_INTCLR = 1u;
    WDOG_LOCK = 0;
}

static void wdog_stop(void)
{
    WDOG_LOCK = WDOG_UNLOCK_KEY;
    WDOG_CTRL = 0;
    WDOG_LOCK = 0;
}

void NMI_Handler(void)                   /* 킥을 놓쳐 워치독이 먼저 울림 */
{
    char buf[120];                      /* 인터럽트 안이라 board_log(임계 구역) 대신 바로 UART로 */
    snprintf(buf, sizeof buf, "[워치독] 300ms 동안 킥이 없어 시간 초과 → 리셋. 그때까지 샘플 %lu개 중 빠짐 %lu개",
             (unsigned long)isr_seq, (unsigned long)drops);
    board_raw_log(buf);
    board_finish(3);
}

/* ---------- 센서: 1kHz 타이머 인터럽트 ---------- */
static uint16_t read_sensor(uint32_t seq)
{
    uint32_t v = seq % 400;                         /* 0.4초 주기 삼각파 0 ~ 4000 */
    int32_t tri = v < 200 ? (int32_t)v * 20 : (int32_t)(400 - v) * 20;
    lcg = lcg * 1103515245u + 12345u;
    int32_t noise = (int32_t)((lcg >> 16) % 101) - 50;  /* ±50 */
    int32_t x = tri + noise;
    return (uint16_t)(x < 0 ? 0 : x);
}

void TIMER0_Handler(void)
{
    TIMER_INTCLR(TIMER0_BASE) = 1u;
    sample_t s = { read_sensor(isr_seq), (uint16_t)isr_seq, clock_now() };
    if (++isr_seq == SAMPLES)
        TIMER_CTRL(TIMER0_BASE) = 0;                /* 1000개 채우면 센서 멈춤 */
    BaseType_t woken = pdFALSE;
    if (xQueueSendFromISR(sample_q, &s, &woken) != pdPASS)
        drops++;                                    /* 꽉 참: 기다릴 수 없으니 버림 */
    portYIELD_FROM_ISR(woken);
}

static void busy_us(uint32_t us)
{
    uint32_t s = clock_now();
    while (clock_now() - s < us * 25u) { }
}

/* ---------- 처리 ---------- */
static void process(void *arg)
{
    (void)arg;
    sample_t s;
    uint32_t n = 0, sum = 0, mn = 0xFFFF, mx = 0, frame = 0, last_stamp = 0;
    int above = 0, expect = -1;
    for (;;) {
        xQueueReceive(sample_q, &s, portMAX_DELAY);
        UBaseType_t waiting = uxQueueMessagesWaiting(sample_q) + 1;
        busy_us(SLOW_PROCESS_US);
        int gap = expect >= 0 && s.seq != (uint16_t)expect;
        expect = (uint16_t)(s.seq + 1);
        sum += s.val;
        mn = s.val < mn ? s.val : mn;
        mx = s.val > mx ? s.val : mx;
        last_stamp = s.stamp;
        xSemaphoreTake(stats_lock, portMAX_DELAY);
        st.samples++;
        st.gaps += gap;
        if (waiting > st.max_wait)
            st.max_wait = waiting;
        xSemaphoreGive(stats_lock);
        if (++n < FRAME)
            continue;

        uint32_t avg = sum / FRAME;
        if (avg > THRESHOLD && !above) {            /* 넘어서는 순간만 */
            alarm_frame = frame;
            xEventGroupSetBits(ev, ALARM);
        }
        above = avg > THRESHOLD;

        /* 패킷 = [마지막 샘플 시각 4바이트][글자] (길이가 제각각이라 메시지 버퍼) */
        uint8_t pkt[48];
        memcpy(pkt, &last_stamp, 4);
        int len = snprintf((char *)pkt + 4, sizeof pkt - 4, "#%03lu avg=%lu min=%lu max=%lu",
                           (unsigned long)frame, (unsigned long)avg, (unsigned long)mn, (unsigned long)mx);
        xMessageBufferSend(pkt_mb, pkt, 4 + (size_t)len, portMAX_DELAY);
        xSemaphoreTake(stats_lock, portMAX_DELAY);
        st.frames++;
        xSemaphoreGive(stats_lock);
        xEventGroupSetBits(ev, ALIVE_PROC);
        frame++;
        n = sum = mx = 0;
        mn = 0xFFFF;
    }
}

/* ---------- 전송 ---------- */
static void tx(void *arg)
{
    (void)arg;
    uint8_t pkt[48];
    for (;;) {
        size_t n = xMessageBufferReceive(pkt_mb, pkt, sizeof pkt - 1, portMAX_DELAY);
        uint32_t stamp;
        memcpy(&stamp, pkt, 4);
        uint32_t lat = US(clock_now() - stamp);
        pkt[n] = '\0';
        uint32_t sum = 0;
        for (size_t i = 4; i < n; i++)
            sum += pkt[i];
        xSemaphoreTake(stats_lock, portMAX_DELAY);
        if (st.packets < 3)
            strncpy(st.first[st.packets], (char *)pkt + 4, sizeof st.first[0] - 1);
        st.packets++;
        st.bytes += (uint32_t)(n - 4);
        st.sum = (st.sum + sum) & 0xFFFFu;
        st.lat_sum += lat;
        if (lat > st.lat_max)
            st.lat_max = lat;
        xSemaphoreGive(stats_lock);
        xEventGroupSetBits(ev, ALIVE_TX);
    }
}

/* ---------- 경보 ---------- */
static void alarm(void *arg)
{
    (void)arg;
    for (;;) {
        xEventGroupWaitBits(ev, ALARM, pdTRUE, pdFALSE, portMAX_DELAY);
        xSemaphoreTake(stats_lock, portMAX_DELAY);
        if (st.alarms < 8)
            st.alarm_at[st.alarms] = alarm_frame;
        st.alarms++;
        xSemaphoreGive(stats_lock);
    }
}

/* ---------- 감시: 둘 다 살아 있을 때만 킥 ---------- */
static void supervisor(void *arg)
{
    (void)arg;
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        xTaskDelayUntil(&last, pdMS_TO_TICKS(100));
        EventBits_t b = xEventGroupClearBits(ev, ALIVE_PROC | ALIVE_TX);   /* 지운 값을 돌려줌 */
        xSemaphoreTake(stats_lock, portMAX_DELAY);
        if ((b & (ALIVE_PROC | ALIVE_TX)) == (ALIVE_PROC | ALIVE_TX)) {
            wdog_kick();
            st.kicks++;
        } else {
            st.misses++;
        }
        xSemaphoreGive(stats_lock);
    }
}

static void runner(void *arg)
{
    (void)arg;
    clock_start();
    sample_q = xQueueCreate(QLEN, sizeof(sample_t));
    pkt_mb = xMessageBufferCreate(256);
    ev = xEventGroupCreate();
    stats_lock = xSemaphoreCreateMutex();
    xTaskCreate(alarm, "alarm", configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY + 4, &h_alarm);
    xTaskCreate(process, "process", configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY + 3, &h_proc);
    xTaskCreate(tx, "tx", configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY + 2, &h_tx);
    xTaskCreate(supervisor, "supervisor", configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY + 1, &h_sup);

    wdog_start(300);
    timer_start(TIMER0_BASE, TIMER0_IRQn, 1000, 0xC0);     /* 1kHz, FreeRTOS API를 써도 되는 우선순위 */
    vTaskDelay(pdMS_TO_TICKS(SAMPLES + 30));                /* 남은 샘플 · 패킷이 다 흐를 때까지 */
    nvic_disable(TIMER0_IRQn);
    wdog_stop();

    xSemaphoreTake(stats_lock, portMAX_DELAY);
    struct stats snap = st;
    xSemaphoreGive(stats_lock);                             /* 복사만 하고 바로 놓음. 출력은 잠금 밖에서 */
    uint32_t sent = isr_seq;
    board_log("종합 프로젝트: 센서 1kHz → 처리 → 전송, 샘플 %d개", SAMPLES);
    board_log("1. 센서 · 큐 : 샘플 %lu개 보냄 · %lu개 받음, 빠짐 %lu개, 순서 끊김 %lu번, 큐 최대 대기 %lu개 / %d",
              (unsigned long)sent, (unsigned long)snap.samples, (unsigned long)drops, (unsigned long)snap.gaps,
              (unsigned long)snap.max_wait, QLEN);
    char al[64] = "";
    for (uint32_t i = 0, k = 0; i < snap.alarms && i < 8; i++)
        k += (uint32_t)snprintf(al + k, sizeof al - k, "%s#%03lu", i ? " · " : "", (unsigned long)snap.alarm_at[i]);
    board_log("2. 처리      : 프레임 %lu개 (10샘플 평균 · 최소 · 최대), 경보 %lu번 (평균 > %d 넘어설 때, %s)",
              (unsigned long)snap.frames, (unsigned long)snap.alarms, THRESHOLD, al);
    board_log("3. 전송      : 패킷 %lu개, %lu바이트, 합계 0x%04lx, 처음 3개는", (unsigned long)snap.packets,
              (unsigned long)snap.bytes, (unsigned long)snap.sum);
    for (int i = 0; i < 3 && i < (int)snap.packets; i++)
        board_log("     %s", snap.first[i]);
    board_log("4. 지연      : 마지막 샘플 → 전송 평균 %luus · 최대 %luus (마감 %dus)",
              (unsigned long)(snap.packets ? snap.lat_sum / snap.packets : 0), (unsigned long)snap.lat_max, DEADLINE_US);
    board_log("5. 워치독    : 100ms마다 감시, 킥 %lu번 · 못 함 %lu번 (process · tx가 둘 다 살아 있을 때만)",
              (unsigned long)snap.kicks, (unsigned long)snap.misses);
    board_log("6. 스택 여유 : process %luB · tx %luB · alarm %luB · supervisor %luB (각 %d워드 중)",
              (unsigned long)uxTaskGetStackHighWaterMark(h_proc) * 4, (unsigned long)uxTaskGetStackHighWaterMark(h_tx) * 4,
              (unsigned long)uxTaskGetStackHighWaterMark(h_alarm) * 4, (unsigned long)uxTaskGetStackHighWaterMark(h_sup) * 4,
              configMINIMAL_STACK_SIZE);

    char why[96] = "";
    size_t w = 0;
#define FAIL(bad, msg) \
    if (bad) w += (size_t)snprintf(why + w, sizeof why - w, "%s%s", w ? " · " : "", msg)
    FAIL(sent != SAMPLES || snap.samples != sent || drops || snap.gaps, "샘플 빠짐");
    FAIL(snap.frames != SAMPLES / FRAME || snap.packets != snap.frames || snap.alarms != 3, "프레임 · 경보 수 다름");
    FAIL(snap.lat_max >= DEADLINE_US, "마감 넘김");
    FAIL(snap.kicks < 9 || snap.misses, "워치독 킥 못 함");
    int ok = w == 0;
    board_log("결과        : %s%s%s", ok ? "통과" : "실패 (", why, ok ? "" : ")");
    board_finish(ok ? 0 : 1);
    vTaskDelete(NULL);
}

int main(void)
{
    board_init();
    xTaskCreate(runner, "runner", configMINIMAL_STACK_SIZE * 2, NULL, tskIDLE_PRIORITY + 5, NULL);
    vTaskStartScheduler();
    return board_exit_code();
}
