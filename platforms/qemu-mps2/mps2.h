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
#define NVIC_ISPR        REG(0xE000E200)        /* 인터럽트 걸어 두기 (소프트웨어로 발생) */
#define NVIC_IPR(n)      (*(volatile uint8_t *)(0xE000E400 + (n)))  /* 우선순위, 작을수록 급함 */

/* 시스템 */
#define SCB_CFSR         REG(0xE000ED28)        /* 폴트 원인 */
#define SCB_MMFAR        REG(0xE000ED34)        /* 메모리 관리 폴트 주소 */
#define SCB_SHCSR        REG(0xE000ED24)        /* 폴트 핸들러 켜기 */

#define CPU_HZ           25000000u

static inline void nvic_enable(int irq, uint8_t prio)
{
    NVIC_IPR(irq) = prio;
    NVIC_ISER = 1u << irq;
}

#endif
