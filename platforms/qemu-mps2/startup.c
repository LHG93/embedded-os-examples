/*
 * startup.c — Cortex-M3 시작 코드와 벡터 테이블 (mps2-an385)
 * 리셋되면 .data 복사, .bss 지우기 후 main()
 * 인터럽트 핸들러는 약한 별칭이라, 장 예제에서 같은 이름으로 다시 만들면 그쪽이 쓰임
 */
#include <stdint.h>

extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss, _estack;
extern int main(void);
extern void board_fault(const char *what, const uint32_t *frame);

/* FreeRTOS 포트 핸들러 (벡터 테이블에 직접 넣어야 함) */
extern void vPortSVCHandler(void);
extern void xPortPendSVHandler(void);
extern void xPortSysTickHandler(void);

void Reset_Handler(void)
{
    uint32_t *src = &_sidata, *dst = &_sdata;
    while (dst < &_edata)
        *dst++ = *src++;
    for (dst = &_sbss; dst < &_ebss; dst++)
        *dst = 0;
    main();
    for (;;) { }
}

/* 폴트: 어디서 났는지(예외 때 쌓인 레지스터)를 넘겨 출력
 * 약한 심볼이라 장 예제가 같은 이름으로 다시 만들 수 있음 (23장 MPU) */
#define FAULT(name, id) \
    __attribute__((naked, weak)) void name(void) \
    { \
        __asm volatile("tst lr, #4      \n" \
                       "ite eq          \n" \
                       "mrseq r1, msp   \n" \
                       "mrsne r1, psp   \n" \
                       "movs r0, %0     \n" \
                       "b fault_entry   \n" :: "i"(id)); \
    }

FAULT(HardFault_Handler, 0)
FAULT(MemManage_Handler, 1)
FAULT(BusFault_Handler, 2)
FAULT(UsageFault_Handler, 3)

__attribute__((used)) void fault_entry(int id, const uint32_t *frame)
{
    static const char *const names[] = {"HardFault", "MemManage", "BusFault", "UsageFault"};
    board_fault(names[id], frame);
}

void Default_Handler(void)
{
    board_fault("처리기 없는 인터럽트", 0);
}

#define WEAK __attribute__((weak, alias("Default_Handler")))
void NMI_Handler(void) WEAK;
void DebugMon_Handler(void) WEAK;
void UART0RX_Handler(void) WEAK;
void UART0TX_Handler(void) WEAK;
void TIMER0_Handler(void) WEAK;
void TIMER1_Handler(void) WEAK;
void DUALTIMER_Handler(void) WEAK;

typedef void (*vector)(void);

__attribute__((section(".vectors"), used))
const vector vector_table[16 + 32] = {
    (vector)&_estack,
    Reset_Handler,
    NMI_Handler,                /* 워치독이 여기로 옴 */
    HardFault_Handler,
    MemManage_Handler,          /* MPU 위반 (23장) */
    BusFault_Handler,
    UsageFault_Handler,
    0, 0, 0, 0,
    vPortSVCHandler,
    DebugMon_Handler,
    0,
    xPortPendSVHandler,         /* 문맥 전환 */
    xPortSysTickHandler,        /* 틱 */
    [16 ... 16 + 31] = Default_Handler,
    [16 + 0] = UART0RX_Handler,
    [16 + 1] = UART0TX_Handler,
    [16 + 8] = TIMER0_Handler,
    [16 + 9] = TIMER1_Handler,
    [16 + 10] = DUALTIMER_Handler,
};
