/*
 * FreeRTOS 내부 — Cortex-M3 (QEMU mps2-an385 전용)
 *
 * 1. 설정표      : FreeRTOSConfig.h 값이 커널에 그대로 들어감
 * 2. 포팅 레이어 : 포트(port.c)가 맡은 예외 셋(SVC · PendSV · SysTick)과 그 설정
 *                  SysTick 다시 세는 값, 우선순위, 임계 구역 안 BASEPRI
 * 3. 태스크 값   : 태스크 하나를 만들면 힙에서 스택 + TCB만큼 빠짐
 * 4. 틱리스      : 모두 잠든 1000틱 동안 틱 인터럽트가 몇 번 오나 (configUSE_TICKLESS_IDLE)
 */
#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"
#include "board.h"
#include "mps2.h"

#define SYST_CSR   REG(0xE000E010)
#define SYST_RVR   REG(0xE000E014)      /* SysTick이 0까지 센 뒤 다시 시작하는 값 */
#define SCB_SHPR2  REG(0xE000ED1C)      /* SVCall 우선순위 [31:24] */
#define SCB_SHPR3  REG(0xE000ED20)      /* PendSV [23:16] · SysTick [31:24] */

#ifndef configUSE_TICKLESS_IDLE
#define configUSE_TICKLESS_IDLE 0
#endif

typedef void (*vector)(void);
extern const vector vector_table[];
extern void vPortSVCHandler(void);
extern void xPortPendSVHandler(void);
extern void xPortSysTickHandler(void);

static volatile uint32_t tick_irqs;

void vApplicationTickHook(void)          /* board.c의 빈 함수를 덮어씀. 틱 인터럽트마다 불림 */
{
    tick_irqs++;
}

static uint32_t basepri(void)
{
    uint32_t v;
    __asm volatile ("mrs %0, basepri" : "=r"(v));
    return v;
}

static void sleeper(void *arg)
{
    (void)arg;
    for (;;)
        vTaskDelay(portMAX_DELAY);
}

static void vec(const char *name, int n, vector want, const char *want_name, const char *role)
{
    board_log("   %-7s (%2d) → 0x%08lx %-20s 같음: %s  %s", name, n, (unsigned long)(uintptr_t)vector_table[n],
              want_name, vector_table[n] == want ? "예" : "아니오", role);
}

static void runner(void *arg)
{
    (void)arg;
    int ok = 1;

    /* 1. 설정표 */
    board_log("1. 설정표 (FreeRTOSConfig.h)");
    board_log("   configCPU_CLOCK_HZ %lu · configTICK_RATE_HZ %lu · configMAX_PRIORITIES %d · configTOTAL_HEAP_SIZE %u",
              (unsigned long)configCPU_CLOCK_HZ, (unsigned long)configTICK_RATE_HZ, configMAX_PRIORITIES,
              (unsigned)configTOTAL_HEAP_SIZE);
    board_log("   configUSE_PREEMPTION %d · configUSE_TIME_SLICING %d · configUSE_TICKLESS_IDLE %d",
              configUSE_PREEMPTION, configUSE_TIME_SLICING, configUSE_TICKLESS_IDLE);

    /* 2. 포팅 레이어 */
    board_log("2. 포팅 레이어가 맡은 예외 (벡터 표)");
    vec("SVCall", 11, vPortSVCHandler, "vPortSVCHandler", "첫 태스크 시작");
    vec("PendSV", 14, xPortPendSVHandler, "xPortPendSVHandler", "문맥 전환 (8장)");
    vec("SysTick", 15, xPortSysTickHandler, "xPortSysTickHandler", "틱");
    ok &= vector_table[11] == vPortSVCHandler && vector_table[14] == xPortPendSVHandler &&
          vector_table[15] == xPortSysTickHandler;

    uint32_t rvr = SYST_RVR;
    board_log("   SysTick 다시 세는 값 %lu = %lu / %lu - 1, 켜짐: %s", (unsigned long)rvr,
              (unsigned long)configCPU_CLOCK_HZ, (unsigned long)configTICK_RATE_HZ, (SYST_CSR & 1u) ? "예" : "아니오");
    ok &= rvr == configCPU_CLOCK_HZ / configTICK_RATE_HZ - 1u;

    uint32_t pendsv = (SCB_SHPR3 >> 16) & 0xFFu, systick = (SCB_SHPR3 >> 24) & 0xFFu, svc = (SCB_SHPR2 >> 24) & 0xFFu;
    taskENTER_CRITICAL();
    uint32_t in_crit = basepri();
    taskEXIT_CRITICAL();
    board_log("   우선순위 (작을수록 급함): PendSV 0x%02lx · SysTick 0x%02lx · SVCall 0x%02lx",
              (unsigned long)pendsv, (unsigned long)systick, (unsigned long)svc);
    board_log("   임계 구역 안 BASEPRI 0x%02lx (밖 0x%02lx) → 0x%02lx 이상 인터럽트는 막히고, 그보다 급한 것은 들어옴",
              (unsigned long)in_crit, (unsigned long)basepri(), (unsigned long)in_crit);
    ok &= pendsv == (configKERNEL_INTERRUPT_PRIORITY & 0xFFu) && systick == (configKERNEL_INTERRUPT_PRIORITY & 0xFFu) &&
          in_crit == configMAX_SYSCALL_INTERRUPT_PRIORITY && basepri() == 0;

    /* 3. 태스크 하나의 값 */
    size_t before = xPortGetFreeHeapSize();
    TaskHandle_t t;
    xTaskCreate(sleeper, "sleeper", 256, NULL, tskIDLE_PRIORITY + 1, &t);
    size_t used = before - xPortGetFreeHeapSize();
    board_log("3. 태스크 하나의 값 (heap_4)");
    board_log("   스택 256워드 = 1024바이트, TCB(StaticTask_t) %u바이트 → 힙에서 %u바이트 줄어듦 (블록 머리 · 정렬 포함)",
              (unsigned)sizeof(StaticTask_t), (unsigned)used);
    ok &= used >= 1024 + sizeof(StaticTask_t) && used < 1024 + sizeof(StaticTask_t) + 64;

    /* 4. 틱리스 */
    vTaskDelay(2);
    uint32_t i0 = tick_irqs;
    TickType_t k0 = xTaskGetTickCount();
    vTaskDelay(1000);                     /* 이 동안 runner · sleeper 모두 잠듦 → idle만 */
    uint32_t irqs = tick_irqs - i0;
    TickType_t ticks = xTaskGetTickCount() - k0;
    board_log("4. 모두 잠든 1000틱 동안 (configUSE_TICKLESS_IDLE %d)", configUSE_TICKLESS_IDLE);
    board_log("   틱 %lu 증가, 틱 인터럽트 %lu번", (unsigned long)ticks, (unsigned long)irqs);
#if configUSE_TICKLESS_IDLE
    ok &= ticks >= 1000 && irqs < 50;    /* 틱을 멈추고 한꺼번에 몰아서 셈 */
#else
    ok &= ticks >= 1000 && irqs >= 995;
#endif

    vTaskDelete(t);
    board_finish(ok ? 0 : 1);
    vTaskDelete(NULL);
}

int main(void)
{
    board_init();
    xTaskCreate(runner, "runner", configMINIMAL_STACK_SIZE * 2, NULL, tskIDLE_PRIORITY + 2, NULL);
    vTaskStartScheduler();
    return board_exit_code();
}
