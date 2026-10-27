/*
 * 스트림 버퍼 · 메시지 버퍼 — FreeRTOS
 *
 * 보내는 쪽이 "HEL" · "LO\nWO" · "RLD\n"을 3번에 나눠 씀. 받는 쪽은 10ms 늦게 읽음
 * 1. 스트림 버퍼  : 바이트가 이어 붙어서 한 번에 12바이트로 받음. 줄은 '\n'을 찾아 다시 나눔
 * 2. 메시지 버퍼  : 쓴 단위 그대로 3번에 받음. 메시지마다 길이(size_t)가 함께 저장됨
 * 3. 트리거 레벨  : 1바이트씩 2ms마다 쓰고, 받는 쪽은 4바이트가 모였을 때 처음 깨어남
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "stream_buffer.h"
#include "message_buffer.h"
#include "board.h"

#define WRITER_PRI ( tskIDLE_PRIORITY + 2 )
#define RUNNER_PRI ( tskIDLE_PRIORITY + 1 )

static const char *chunks[] = { "HEL", "LO\nWO", "RLD\n" };
static StreamBufferHandle_t sb;
static MessageBufferHandle_t mb;
static volatile int done;

static void stream_writer(void *arg)
{
    (void)arg;
    for (int i = 0; i < 3; i++)
        xStreamBufferSend(sb, chunks[i], strlen(chunks[i]), portMAX_DELAY);
    done = 1;
    vTaskDelete(NULL);
}

static void message_writer(void *arg)
{
    (void)arg;
    for (int i = 0; i < 3; i++)
        xMessageBufferSend(mb, chunks[i], strlen(chunks[i]), portMAX_DELAY);
    done = 1;
    vTaskDelete(NULL);
}

static void byte_writer(void *arg)
{
    (void)arg;
    for (int i = 0; i < 8; i++) {
        vTaskDelay(pdMS_TO_TICKS(2));
        xStreamBufferSend(sb, &"ABCDEFGH"[i], 1, 0);
    }
    done = 1;
    vTaskDelete(NULL);
}

static void wait_done(void)
{
    while (!done)
        vTaskDelay(pdMS_TO_TICKS(1));
}

static void runner(void *arg)
{
    (void)arg;
    char buf[64];

    /* 1. 스트림 버퍼 */
    sb = xStreamBufferCreate(64, 1);
    done = 0;
    xTaskCreate(stream_writer, "writer", configMINIMAL_STACK_SIZE, NULL, WRITER_PRI, NULL);
    vTaskDelay(pdMS_TO_TICKS(10));              /* 받는 쪽이 늦게 옴 */
    wait_done();
    size_t n = xStreamBufferReceive(sb, buf, sizeof buf, 0);
    int lines = 0;
    for (size_t i = 0; i < n; i++)
        if (buf[i] == '\n')
            lines++;                            /* 경계는 내용 안의 '\n'으로 직접 찾음 */
    vStreamBufferDelete(sb);
    board_log("스트림 버퍼  : 3번에 나눠 쓴 3 · 5 · 4바이트를 한 번에 %u바이트로 받음 → '\\n'으로 다시 나눠 %d줄",
              (unsigned)n, lines);

    /* 2. 메시지 버퍼 */
    mb = xMessageBufferCreate(64);
    done = 0;
    xTaskCreate(message_writer, "writer", configMINIMAL_STACK_SIZE, NULL, WRITER_PRI, NULL);
    vTaskDelay(pdMS_TO_TICKS(10));
    wait_done();
    size_t used = 64 - xMessageBufferSpacesAvailable(mb);
    char sizes[32] = "";
    int count = 0;
    while ((n = xMessageBufferReceive(mb, buf, sizeof buf, 0)) > 0) {
        snprintf(sizes + strlen(sizes), sizeof sizes - strlen(sizes), "%s%u", count ? " · " : "", (unsigned)n);
        count++;
    }
    vMessageBufferDelete(mb);
    board_log("메시지 버퍼  : 3번에 나눠 쓴 것을 %d번에 받음, 크기 %s바이트 그대로 (차지한 공간 %u바이트 = 데이터 12 + 길이 %u바이트 × 3)",
              count, sizes, (unsigned)used, (unsigned)sizeof(size_t));

    /* 3. 트리거 레벨 */
    sb = xStreamBufferCreate(64, 4);           /* 4바이트가 모여야 받는 쪽을 깨움 */
    done = 0;
    TickType_t t0 = xTaskGetTickCount();
    xTaskCreate(byte_writer, "bytes", configMINIMAL_STACK_SIZE, NULL, WRITER_PRI, NULL);
    n = xStreamBufferReceive(sb, buf, sizeof buf, portMAX_DELAY);
    int woke_ms = (int)((xTaskGetTickCount() - t0) * portTICK_PERIOD_MS);
    wait_done();
    vStreamBufferDelete(sb);
    board_log("트리거 레벨 4 : 2ms마다 1바이트씩 쓰는 동안, 받는 쪽은 %dms에 처음 깨어나 %u바이트를 받음",
              woke_ms, (unsigned)n);

    board_finish(lines == 2 && count == 3 && strcmp(sizes, "3 · 5 · 4") == 0 &&
                 used == 12 + 3 * sizeof(size_t) && n == 4 && woke_ms == 8 ? 0 : 1);
    vTaskDelete(NULL);
}

int main(void)
{
    board_init();
    xTaskCreate(runner, "runner", configMINIMAL_STACK_SIZE * 2, NULL, RUNNER_PRI, NULL);
    vTaskStartScheduler();
    return board_exit_code();
}
