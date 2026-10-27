/*
 * 스택 오버플로와 메모리 보호 — FreeRTOS on Cortex-M3 (QEMU mps2-an385 전용)
 *
 * 1. FreeRTOS 검사      : configCHECK_FOR_STACK_OVERFLOW 2. 스택 끝 16바이트에 채워 둔 무늬가 지워졌는지
 *                        문맥 전환 때마다 확인. 넘친 뒤에야 알아챔 (넘친 곳은 여분 공간이라 피해 없음)
 * 2. MPU 읽기 전용       : 설정값 배열을 읽기 전용 영역으로. 쓰는 순간 MemManage 폴트 → 그 쓰기만 건너뛰고 계속
 * 3. MPU 스택 가드       : 태스크 스택 바로 아래 256바이트를 접근 금지 영역으로. 넘치는 바로 그 명령에서 폴트
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "board.h"
#include "mps2.h"

enum { PART_HOOK, PART_RO, PART_GUARD };
static volatile int part;

/* ---------- 1. FreeRTOS 검사 ---------- */
static volatile int hook_hits;
static char hook_task[16];

/* 문맥 전환 중(PendSV)에 불림. 여기서는 기록만 */
void vApplicationStackOverflowHook(TaskHandle_t t, char *name)
{
    (void)t;
    hook_hits++;
    strncpy(hook_task, name, sizeof hook_task - 1);
}

/* 스택 128워드(512바이트) 앞에 여분 1KB를 붙여 둠. 넘쳐도 여분만 덮음 */
static struct {
    uint8_t spare[1024];
    StackType_t stack[128];
} small;
static StaticTask_t small_tcb;

static int __attribute__((noinline)) deep(int n)
{
    volatile uint8_t buf[64];           /* 한 단계마다 64바이트 넘게 씀 */
    buf[0] = (uint8_t)n;
    return n ? deep(n - 1) + buf[0] : 0;
}

static volatile int small_done;

static void small_task(void *arg)
{
    (void)arg;
    deep(8);                            /* 8단계 × 약 80바이트: 512바이트 스택을 넘음 */
    vTaskDelay(1);                      /* 문맥 전환 → 여기서 검사됨 */
    small_done = 1;
    vTaskDelete(NULL);
}

/* ---------- 2 · 3. MPU ---------- */
static volatile uint32_t settings[8] __attribute__((aligned(32))) = { 7, 7, 7, 7, 7, 7, 7, 7 };
static volatile uint32_t fault_addr, fault_cfsr;
static volatile int ro_faults;

/* 가드 256바이트 바로 위에 스택 768바이트. FreeRTOS는 스택만 알고 가드는 모름
 * 가드는 함수 하나가 한 번에 내려가는 크기(스택 프레임)보다 커야 함. 작으면 건너뜀 */
#define GUARD_LOG2 8
static struct {
    StackType_t guard[(1u << GUARD_LOG2) / sizeof(StackType_t)];
    StackType_t stack[192];
} guarded __attribute__((aligned(1u << GUARD_LOG2)));
static StaticTask_t guarded_tcb;

static void mpu_region(int n, uint32_t base, uint32_t log2size, uint32_t attr)
{
    MPU_RNR = (uint32_t)n;
    MPU_RBAR = base;
    MPU_RASR = attr | MPU_SIZE(log2size) | 1u;
}

/* MemManage 폴트. 쌓인 레지스터가 가드 안일 수도 있어서, MPU부터 끄고 봄 */
__attribute__((used)) void memmanage_c(uint32_t *frame)
{
    MPU_CTRL = 0;
    fault_cfsr = SCB_CFSR;
    fault_addr = SCB_MMFAR;
    if (part == PART_RO) {
        uint16_t op = *(uint16_t *)frame[6];
        frame[6] += (op & 0xF800u) >= 0xE800u ? 4u : 2u;    /* 막힌 명령(16 또는 32비트)만 건너뜀 */
        ro_faults++;
        SCB_CFSR = 0xFFu;               /* 메모리 관리 폴트 표시 지움 */
        MPU_CTRL = 5u;                  /* 다시 켜고 하던 곳으로 */
        return;
    }
    char buf[160];
    snprintf(buf, sizeof buf, "MPU 스택 가드         : 태스크 %s가 가드에 닿는 순간 폴트 (주소 0x%08lx, 가드 0x%08lx ~ +%u, CFSR 0x%02lx)",
             pcTaskGetName(NULL), (unsigned long)fault_addr, (unsigned long)(uintptr_t)guarded.guard,
             (unsigned)sizeof guarded.guard, (unsigned long)(fault_cfsr & 0xFFu));
    board_raw_log(buf);
    board_finish(fault_addr >= (uintptr_t)guarded.guard && fault_addr < (uintptr_t)guarded.guard + sizeof guarded.guard ? 0 : 1);
}

__attribute__((naked)) void MemManage_Handler(void)
{
    __asm volatile("tst lr, #4      \n"
                   "ite eq          \n"
                   "mrseq r0, msp   \n"
                   "mrsne r0, psp   \n"
                   "b memmanage_c   \n");
}

static void guarded_task(void *arg)
{
    (void)arg;
    deep(40);                           /* 1KB 스택을 넘을 만큼 깊이 */
    board_raw_log("가드를 넘지 않음");
    board_finish(1);
}

static void runner(void *arg)
{
    (void)arg;

    /* 1. FreeRTOS 검사 */
    part = PART_HOOK;
    xTaskCreateStatic(small_task, "small", 128, NULL, tskIDLE_PRIORITY + 2, small.stack, &small_tcb);
    vTaskDelay(pdMS_TO_TICKS(20));
    size_t touched = 0;
    for (size_t i = 0; i < sizeof small.spare; i++)
        if (small.spare[i])
            touched = sizeof small.spare - i;           /* 여분에서 덮인 깊이 */
    board_log("FreeRTOS 스택 검사   : 태스크 \"%s\" 넘침 감지 %d번 (스택 아래로 %u바이트 넘쳤음, 넘친 뒤에 알아챔)",
              hook_task, hook_hits, (unsigned)touched);

    /* 2 · 3. MPU 켜기 */
    SCB_SHCSR |= 1u << 16;                              /* MemManage 폴트 켜기 (끄면 HardFault로 감) */
    mpu_region(0, (uint32_t)(uintptr_t)settings, 5, MPU_AP_RO | MPU_XN);           /* 32바이트 읽기 전용 */
    mpu_region(1, (uint32_t)(uintptr_t)guarded.guard, GUARD_LOG2, MPU_AP_NONE | MPU_XN);  /* 스택 바로 아래 가드 */
    MPU_CTRL = 5u;                                      /* 켜기 + 나머지는 기본 지도 */
    __asm volatile("dsb\n isb");

    /* 2. 읽기 전용 */
    part = PART_RO;
    uint32_t before = settings[0];
    settings[0] = 99;                                   /* 막혀야 함 */
    board_log("MPU 읽기 전용         : settings[0] = 99 쓰기 → MemManage 폴트 %d번 (주소 0x%08lx), 값은 그대로 %lu",
              ro_faults, (unsigned long)fault_addr, (unsigned long)settings[0]);
    if (ro_faults != 1 || settings[0] != before)
        board_finish(1);

    /* 3. 스택 가드 */
    part = PART_GUARD;
    xTaskCreateStatic(guarded_task, "guarded", 192, NULL, tskIDLE_PRIORITY + 2, guarded.stack, &guarded_tcb);
    vTaskDelay(pdMS_TO_TICKS(100));
    board_log("폴트가 나지 않음");
    board_finish(1);
    vTaskDelete(NULL);
}

int main(void)
{
    board_init();
    xTaskCreate(runner, "runner", configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY + 1, NULL);
    vTaskStartScheduler();
    return board_exit_code();
}
