/*
 * 스택과 힙 — FreeRTOS on Cortex-M3 (QEMU mps2-an385, heap_4)
 *
 * 1. 스택 여유      : 똑같이 512워드(2KB) 스택이라도, 지역 배열 1KB를 쓰는 태스크는 여유가 그만큼 적음
 * 2. 힙 단편화      : 4KB 블록 10개를 받고 하나 걸러 반납. 빈 공간 합계는 20KB인데 가장 큰 덩어리는 4KB라
 *                    8KB 요청이 실패함. 나머지도 반납하면 heap_4가 이웃한 빈 블록을 합쳐 다시 큰 덩어리가 됨
 * 3. 정적 할당      : xTaskCreateStatic은 스택 · TCB를 미리 잡아 둔 배열에 만들어서 힙을 안 씀
 */
#include <stdint.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "board.h"

#define STACK_WORDS 512
#define BLOCK       4096
#define N_BLOCKS    10

static volatile int malloc_failed;

/* 힙이 모자랄 때 FreeRTOS가 부름. 여기서는 세기만 함 (보드 기본은 프로그램 종료) */
void vApplicationMallocFailedHook(void)
{
    malloc_failed++;
}

/* 1. 스택 여유 */
static volatile UBaseType_t mark_light, mark_heavy;

static void light(void *arg)
{
    (void)arg;
    volatile int counter = 0;
    for (int i = 0; i < 100; i++)
        counter++;
    mark_light = uxTaskGetStackHighWaterMark(NULL);   /* 지금까지 가장 적게 남았던 때 (워드) */
    vTaskDelete(NULL);
}

static void heavy(void *arg)
{
    (void)arg;
    volatile uint8_t buf[1024];                         /* 지역 배열은 스택에 잡힘 */
    memset((void *)buf, 0x5A, sizeof buf);
    mark_heavy = uxTaskGetStackHighWaterMark(NULL);
    vTaskDelete(NULL);
}

/* 3. 정적 할당용 메모리 */
static StackType_t static_stack[STACK_WORDS];
static StaticTask_t static_tcb;

static void idle_task(void *arg)
{
    (void)arg;
    vTaskDelete(NULL);
}

static void runner(void *arg)
{
    (void)arg;
    HeapStats_t hs;

    /* 1. 스택 여유 */
    xTaskCreate(light, "light", STACK_WORDS, NULL, tskIDLE_PRIORITY + 2, NULL);
    xTaskCreate(heavy, "heavy", STACK_WORDS, NULL, tskIDLE_PRIORITY + 2, NULL);
    vTaskDelay(pdMS_TO_TICKS(10));
    board_log("스택 여유 (2KB 중)  : 가벼운 태스크 %u바이트 남음, 지역 배열 1KB 쓰는 태스크 %u바이트 남음",
              (unsigned)(mark_light * sizeof(StackType_t)), (unsigned)(mark_heavy * sizeof(StackType_t)));

    /* 2. 힙 단편화 */
    void *blk[N_BLOCKS];
    for (int i = 0; i < N_BLOCKS; i++)
        blk[i] = pvPortMalloc(BLOCK);                   /* 이웃해서 차례로 잡힘 */
    vPortGetHeapStats(&hs);
    void *filler = pvPortMalloc(hs.xSizeOfLargestFreeBlockInBytes - 16);  /* 나머지 힙을 거의 다 씀 */
    for (int i = 0; i < N_BLOCKS; i += 2)
        vPortFree(blk[i]);                              /* 하나 걸러 반납 → 구멍 5개 */
    vPortGetHeapStats(&hs);
    size_t free1 = hs.xAvailableHeapSpaceInBytes, big1 = hs.xSizeOfLargestFreeBlockInBytes;
    void *want1 = pvPortMalloc(2 * BLOCK);
    board_log("하나 걸러 반납      : 빈 공간 합계 %u바이트, 가장 큰 덩어리 %u바이트 → 8KB 요청 %s",
              (unsigned)free1, (unsigned)big1, want1 ? "성공" : "실패");

    for (int i = 1; i < N_BLOCKS; i += 2)
        vPortFree(blk[i]);                              /* 나머지 반납 → 이웃끼리 합쳐짐 */
    vPortGetHeapStats(&hs);
    size_t big2 = hs.xSizeOfLargestFreeBlockInBytes;
    void *want2 = pvPortMalloc(2 * BLOCK);
    board_log("나머지도 반납       : 이웃한 빈 블록이 합쳐져 가장 큰 덩어리 %u바이트 → 8KB 요청 %s",
              (unsigned)big2, want2 ? "성공" : "실패");
    vPortFree(want2);
    vPortFree(want1);
    vPortFree(filler);

    /* 3. 정적 할당 */
    size_t before = xPortGetFreeHeapSize();
    xTaskCreate(idle_task, "dyn", STACK_WORDS, NULL, tskIDLE_PRIORITY + 1, NULL);
    size_t after_dyn = xPortGetFreeHeapSize();
    vTaskDelay(pdMS_TO_TICKS(5));                       /* 지운 태스크 메모리는 idle 태스크가 돌려줌 */
    size_t before_static = xPortGetFreeHeapSize();
    xTaskCreateStatic(idle_task, "static", STACK_WORDS, NULL, tskIDLE_PRIORITY + 1, static_stack, &static_tcb);
    size_t after_static = xPortGetFreeHeapSize();
    board_log("힙에서 줄어든 양    : xTaskCreate %u바이트 (스택 2KB + TCB), xTaskCreateStatic %u바이트",
              (unsigned)(before - after_dyn), (unsigned)(before_static - after_static));
    vTaskDelay(pdMS_TO_TICKS(5));

    vPortGetHeapStats(&hs);
    board_log("힙 기록            : 가장 적게 남았던 때 %u바이트, 받기 %u번 · 반납 %u번, 실패 %d번",
              (unsigned)hs.xMinimumEverFreeBytesRemaining, (unsigned)hs.xNumberOfSuccessfulAllocations,
              (unsigned)hs.xNumberOfSuccessfulFrees, malloc_failed);

    int ok = mark_heavy * sizeof(StackType_t) + 900 < mark_light * sizeof(StackType_t) &&
             free1 >= 5 * BLOCK && big1 < 2 * BLOCK && !want1 && big2 >= 2 * BLOCK && want2 &&
             before - after_dyn >= STACK_WORDS * sizeof(StackType_t) && before_static == after_static &&
             malloc_failed == 1;
    board_finish(ok ? 0 : 1);
    vTaskDelete(NULL);
}

int main(void)
{
    board_init();
    xTaskCreate(runner, "runner", configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY + 1, NULL);
    vTaskStartScheduler();
    return board_exit_code();
}
