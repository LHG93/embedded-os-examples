/*
 * board.c — QEMU mps2-an385 플랫폼 board.h 구현
 * 로그는 UART0, 끝낼 때는 세미호스팅으로 QEMU에 종료 코드를 넘김
 */
#include <stdarg.h>
#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"
#include "board.h"
#include "mps2.h"

#define UART0_DATA    REG(0x40004000)
#define UART0_STATE   REG(0x40004004)   /* bit0 = 보내기 버퍼 꽉 참 */
#define UART0_CTRL    REG(0x40004008)   /* bit0 = 보내기 켜기 */
#define UART0_BAUDDIV REG(0x40004010)

static void uart_putc(char c)
{
    while (UART0_STATE & 1u) { }
    UART0_DATA = (uint32_t)(unsigned char)c;
}

static void uart_puts(const char *s)
{
    while (*s)
        uart_putc(*s++);
}

/* ARM 세미호스팅 SYS_EXIT_EXTENDED: QEMU가 이 값으로 종료 (-semihosting-config enable=on) */
static void semihost_exit(int code)
{
    static uint32_t block[2];
    block[0] = 0x20026;                 /* ADP_Stopped_ApplicationExit */
    block[1] = (uint32_t)code;
    register uint32_t r0 __asm("r0") = 0x20;
    register uint32_t r1 __asm("r1") = (uint32_t)block;
    __asm volatile("bkpt 0xAB" : : "r"(r0), "r"(r1) : "memory");
    for (;;) { }
}

void board_raw_log(const char *s)
{
    uart_puts(s);
    uart_putc('\n');
}

void board_init(void)
{
    UART0_BAUDDIV = 16;
    UART0_CTRL = 1u;
}

void board_log(const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    taskENTER_CRITICAL();               /* 여러 태스크가 찍어도 줄이 섞이지 않게 */
    uart_puts(buf);
    uart_putc('\n');
    taskEXIT_CRITICAL();
}

void board_led(bool on)
{
    board_log("[LED] %s", on ? "on" : "off");
}

void board_finish(int code)
{
    semihost_exit(code);
}

int board_exit_code(void)
{
    return 1;                           /* 여기까지 오면 스케줄러가 못 뜬 것 */
}

/* 폴트: 원인과 위치를 찍고 종료 코드 3 */
void board_fault(const char *what, const uint32_t *frame)
{
    char buf[160];
    if (frame)
        snprintf(buf, sizeof buf, "[%s] PC=0x%08lx LR=0x%08lx CFSR=0x%08lx MMFAR=0x%08lx", what,
                 (unsigned long)frame[6], (unsigned long)frame[5],
                 (unsigned long)SCB_CFSR, (unsigned long)SCB_MMFAR);
    else
        snprintf(buf, sizeof buf, "[%s]", what);
    uart_puts(buf);
    uart_putc('\n');
    semihost_exit(3);
}

void vAssertCalled(const char *file, unsigned long line)
{
    char buf[160];
    taskDISABLE_INTERRUPTS();
    snprintf(buf, sizeof buf, "configASSERT 실패: %s:%lu", file, line);
    uart_puts(buf);
    uart_putc('\n');
    semihost_exit(2);
}

/* 스택 넘침 · 힙 부족 (22 · 23장) */
__attribute__((weak)) void vApplicationStackOverflowHook(TaskHandle_t task, char *name)
{
    (void)task;
    char buf[80];
    snprintf(buf, sizeof buf, "[스택 넘침] 태스크 %s", name);
    uart_puts(buf);
    uart_putc('\n');
    semihost_exit(4);
}

/* 태스크 전환마다 불림 (FreeRTOSConfig.h의 traceTASK_SWITCHED_IN). 기본은 아무것도 안 함 (27장이 덮어씀) */
__attribute__((weak)) void board_trace_switch_in(void)
{
}

/* 틱마다 불림. 기본은 아무것도 안 함 (25장 예제가 덮어써서 틱 인터럽트 수를 셈) */
__attribute__((weak)) void vApplicationTickHook(void)
{
}

__attribute__((weak)) void vApplicationMallocFailedHook(void)
{
    uart_puts("[힙 부족] pvPortMalloc 실패\n");
    semihost_exit(5);
}
