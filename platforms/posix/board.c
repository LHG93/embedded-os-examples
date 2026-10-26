/*
 * board.c — POSIX 플랫폼 board.h 구현
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "FreeRTOS.h"
#include "task.h"
#include "board.h"

static int exit_code;

void board_init(void)
{
    /* 출력이 바로 보이도록 버퍼 끔 */
    setvbuf(stdout, NULL, _IONBF, 0);
}

void board_log(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    putchar('\n');
}

void board_led(bool on)
{
    board_log("[LED] %s", on ? "on" : "off");
}

void board_finish(int code)
{
    exit_code = code;
    vTaskEndScheduler();  /* main()의 vTaskStartScheduler() 다음 줄로 복귀 */
}

int board_exit_code(void)
{
    return exit_code;
}

void vAssertCalled(const char *file, unsigned long line)
{
    fprintf(stderr, "configASSERT 실패: %s:%lu\n", file, line);
    abort();
}
