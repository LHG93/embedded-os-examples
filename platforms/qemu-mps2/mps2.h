/*
 * mps2.h — mps2-an385 주변장치 주소 (QEMU · ARM AN385 문서 기준)
 * 장 예제에서 인터럽트 · 타이머 · 워치독을 직접 다룰 때 씀
 */
#ifndef MPS2_H
#define MPS2_H

#include <stdint.h>

#define REG(addr) (*(volatile uint32_t *)(addr))

/* CMSDK APB 타이머 0 · 1: 25MHz로 내려 세다 0이 되면 인터럽트 */
#define TIMER0_BASE      0x40000000u
#define TIMER1_BASE      0x40001000u
#define TIMER_CTRL(b)    REG((b) + 0x00)    /* bit0 켜기, bit3 인터럽트 켜기 */
#define TIMER_VALUE(b)   REG((b) + 0x04)
#define TIMER_RELOAD(b)  REG((b) + 0x08)
#define TIMER_INTCLR(b)  REG((b) + 0x0C)    /* 1을 써서 인터럽트 지움 */
#define TIMER0_IRQn      8
#define TIMER1_IRQn      9

/* CMSDK 듀얼 타이머 1: 여기서는 32비트 자유 실행 시계로 씀 (25MHz) */
#define DUAL1_BASE       0x40002000u
#define DUAL1_LOAD       REG(DUAL1_BASE + 0x00)
#define DUAL1_VALUE      REG(DUAL1_BASE + 0x04)
#define DUAL1_CTRL       REG(DUAL1_BASE + 0x08)  /* bit7 켜기, bit1 32비트 */

/* CMSDK APB 워치독: 0이 되면 인터럽트(NMI), 한 번 더 0이 되면 리셋 */
#define WDOG_BASE        0x40008000u
#define WDOG_LOAD        REG(WDOG_BASE + 0x000)
#define WDOG_VALUE       REG(WDOG_BASE + 0x004)
#define WDOG_CTRL        REG(WDOG_BASE + 0x008) /* bit0 인터럽트 켜기, bit1 리셋 켜기 */
#define WDOG_INTCLR      REG(WDOG_BASE + 0x00C) /* 아무 값이나 쓰면 지우고 다시 LOAD부터 셈 = 킥 */
#define WDOG_LOCK        REG(WDOG_BASE + 0xC00) /* 0x1ACCE551을 써야 다른 레지스터를 고칠 수 있음 */
#define WDOG_UNLOCK_KEY  0x1ACCE551u

/* NVIC */
#define NVIC_ISER        REG(0xE000E100)        /* 인터럽트 켜기 (IRQ 0~31) */
#define NVIC_ICER        REG(0xE000E180)        /* 인터럽트 끄기 */
#define NVIC_ISPR        REG(0xE000E200)        /* 인터럽트 걸어 두기 (소프트웨어로 발생) */
#define NVIC_ICPR        REG(0xE000E280)        /* 걸려 있는 인터럽트 지우기 */
#define NVIC_IPR(n)      (*(volatile uint8_t *)(0xE000E400 + (n)))  /* 우선순위, 작을수록 급함 */

/* 시스템 */
#define SCB_CFSR         REG(0xE000ED28)        /* 폴트 원인 */
#define SCB_MMFAR        REG(0xE000ED34)        /* 메모리 관리 폴트 주소 */
#define SCB_SHCSR        REG(0xE000ED24)        /* 폴트 핸들러 켜기 */

/* MPU (ARMv7-M PMSAv7, 영역 8개) */
#define MPU_CTRL         REG(0xE000ED94)        /* bit0 켜기, bit2 PRIVDEFENA(나머지 주소는 기본 지도) */
#define MPU_RNR          REG(0xE000ED98)        /* 고칠 영역 번호 */
#define MPU_RBAR         REG(0xE000ED9C)        /* 시작 주소 (크기만큼 정렬) */
#define MPU_RASR         REG(0xE000EDA0)        /* 크기 · 권한 · 켜기 */
#define MPU_AP_NONE      (0u << 24)             /* 접근 금지 */
#define MPU_AP_RO        (6u << 24)             /* 읽기만 */
#define MPU_XN           (1u << 28)             /* 실행 금지 */
#define MPU_SIZE(log2)   (((log2) - 1u) << 1)   /* 영역 크기 = 2^log2 바이트 */

#define CPU_HZ           25000000u

/* 커널을 거치지 않고 UART에 바로 찍기. NMI · 폴트처럼 커널을 쓸 수 없는 곳에서 씀 (board.c) */
void board_raw_log(const char *s);

static inline void nvic_enable(int irq, uint8_t prio)
{
    NVIC_IPR(irq) = prio;
    NVIC_ICPR = 1u << irq;
    NVIC_ISER = 1u << irq;
}

static inline void nvic_disable(int irq)
{
    NVIC_ICER = 1u << irq;
    NVIC_ICPR = 1u << irq;
}

/* 25MHz로 올라가는 시계 (듀얼 타이머 1을 자유 실행으로). 1us = 25 */
static inline void clock_start(void)
{
    DUAL1_LOAD = 0xFFFFFFFFu;
    DUAL1_CTRL = (1u << 7) | (1u << 1);
}

static inline uint32_t clock_now(void)
{
    return ~DUAL1_VALUE;
}

/* CMSDK 타이머를 period_us마다 인터럽트가 나게 켬 */
static inline void timer_start(uint32_t base, int irq, uint32_t period_us, uint8_t prio)
{
    TIMER_CTRL(base) = 0;
    TIMER_RELOAD(base) = period_us * (CPU_HZ / 1000000u) - 1u;
    TIMER_VALUE(base) = TIMER_RELOAD(base);
    TIMER_INTCLR(base) = 1u;
    nvic_enable(irq, prio);
    TIMER_CTRL(base) = (1u << 0) | (1u << 3);
}

static inline void timer_stop(uint32_t base, int irq)
{
    TIMER_CTRL(base) = 0;
    TIMER_INTCLR(base) = 1u;
    nvic_disable(irq);
}

#endif
