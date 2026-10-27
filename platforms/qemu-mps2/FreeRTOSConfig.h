/*
 * FreeRTOSConfig.h — QEMU mps2-an385 (Cortex-M3, 25MHz) 설정
 * 값별 의미는 강의 "FreeRTOS 내부" 장에서 다룸
 */
#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

/* 클럭 · 스케줄링 */
#define configCPU_CLOCK_HZ                      ( 25000000UL )
#define configTICK_RATE_HZ                      1000 /* 틱 1번 = 1ms */
#define configUSE_PREEMPTION                    1
#define configUSE_TIME_SLICING                  1
#define configMAX_PRIORITIES                    7
#define configTICK_TYPE_WIDTH_IN_BITS           TICK_TYPE_WIDTH_32_BITS
#define configIDLE_SHOULD_YIELD                 1

/* 스택 (워드 단위, 1워드 = 4바이트) */
#define configMINIMAL_STACK_SIZE                ( ( unsigned short ) 512 )
#define configMAX_TASK_NAME_LEN                 16

/* 메모리: heap_4 */
#define configSUPPORT_DYNAMIC_ALLOCATION        1
#define configSUPPORT_STATIC_ALLOCATION         1   /* xTaskCreateStatic 등 (22장) */
#define configKERNEL_PROVIDED_STATIC_MEMORY     1   /* idle · 타이머 태스크 메모리는 커널이 준비 */
#define configTOTAL_HEAP_SIZE                   ( ( size_t ) ( 256 * 1024 ) )

/* 동기화 · 통신 */
#define configUSE_MUTEXES                       1
#define configUSE_RECURSIVE_MUTEXES             1
#define configUSE_COUNTING_SEMAPHORES           1
#define configUSE_TASK_NOTIFICATIONS            1
#define configQUEUE_REGISTRY_SIZE               0

/* 소프트웨어 타이머 (20장) */
#define configUSE_TIMERS                        1
#define configTIMER_TASK_PRIORITY               ( configMAX_PRIORITIES - 1 )
#define configTIMER_QUEUE_LENGTH                10
#define configTIMER_TASK_STACK_DEPTH            configMINIMAL_STACK_SIZE

/* 인터럽트 우선순위 (18장). Cortex-M은 숫자가 작을수록 급함
 * 커널(틱 · 문맥 전환)은 가장 낮게, FreeRTOS API를 부를 수 있는 ISR은 0x40 이상(0x40 ~ 0xFF)
 * 0x00 ~ 0x3F 인터럽트는 커널이 막지 않는 대신 FreeRTOS API를 부르면 안 됨 */
#define configKERNEL_INTERRUPT_PRIORITY         255
#define configMAX_SYSCALL_INTERRUPT_PRIORITY    0x40

/* 훅 · 검사 (22 · 23장) */
#define configUSE_IDLE_HOOK                     0
#define configUSE_TICK_HOOK                     0
#define configUSE_MALLOC_FAILED_HOOK            1
#define configCHECK_FOR_STACK_OVERFLOW          2

/* 사용할 API */
#define INCLUDE_vTaskDelete                     1
#define INCLUDE_vTaskDelay                      1
#define INCLUDE_xTaskDelayUntil                 1
#define INCLUDE_vTaskSuspend                    1
#define INCLUDE_vTaskPrioritySet                1
#define INCLUDE_uxTaskPriorityGet               1
#define INCLUDE_xTaskGetSchedulerState          1
#define INCLUDE_xTaskGetCurrentTaskHandle       1
#define INCLUDE_xSemaphoreGetMutexHolder        1
#define INCLUDE_uxTaskGetStackHighWaterMark     1
#define INCLUDE_xTimerPendFunctionCall          1

void vAssertCalled( const char * file, unsigned long line );
#define configASSERT( x )    if( ( x ) == 0 ) vAssertCalled( __FILE__, __LINE__ )

#endif
