/*
 * FreeRTOSConfig.h — POSIX 플랫폼 설정
 *
 * 값별 의미는 강의 "FreeRTOS 내부" 장에서 다룸.
 * 여기선 예제 실행에 필요한 최소한만 켬.
 */
#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

/* 스케줄링 */
#define configUSE_PREEMPTION                    1   /* 우선순위 높은 태스크가 오면 바로 선점 */
#define configUSE_TIME_SLICING                  1   /* 같은 우선순위끼리 틱마다 번갈아 실행 */
#define configTICK_RATE_HZ                      1000 /* 틱 1번 = 1ms */
#define configMAX_PRIORITIES                    7
#define configTICK_TYPE_WIDTH_IN_BITS           TICK_TYPE_WIDTH_64_BITS
#define configIDLE_SHOULD_YIELD                 1

/* 태스크 스택. POSIX 포트는 실제 스택을 pthread가 따로 잡아서 작게 둬도 됨 */
#define configMINIMAL_STACK_SIZE                ( ( unsigned short ) 1024 )
#define configMAX_TASK_NAME_LEN                 16

/* 메모리 */
#define configSUPPORT_DYNAMIC_ALLOCATION        1
#define configSUPPORT_STATIC_ALLOCATION         0
#define configTOTAL_HEAP_SIZE                   ( ( size_t ) ( 64 * 1024 ) ) /* heap_3에선 미사용 */

/* 동기화 · 통신 */
#define configUSE_MUTEXES                       1
#define configUSE_RECURSIVE_MUTEXES             1
#define configUSE_COUNTING_SEMAPHORES           1
#define configUSE_TASK_NOTIFICATIONS            1
#define configQUEUE_REGISTRY_SIZE               0

/* 소프트웨어 타이머 */
#define configUSE_TIMERS                        1
#define configTIMER_TASK_PRIORITY               ( configMAX_PRIORITIES - 1 )
#define configTIMER_QUEUE_LENGTH                10
#define configTIMER_TASK_STACK_DEPTH            configMINIMAL_STACK_SIZE

/* 훅 함수 · 검사 */
#define configUSE_IDLE_HOOK                     0
#define configUSE_TICK_HOOK                     0
#define configUSE_MALLOC_FAILED_HOOK            0
#define configCHECK_FOR_STACK_OVERFLOW          0   /* POSIX에선 의미 없음. 보드 장에서 켬 */

/* 사용할 API */
#define INCLUDE_vTaskDelete                     1
#define INCLUDE_vTaskDelay                      1
#define INCLUDE_vTaskSuspend                    1
#define INCLUDE_vTaskPrioritySet                1
#define INCLUDE_uxTaskPriorityGet               1
#define INCLUDE_xTaskGetSchedulerState          1
#define INCLUDE_xTaskGetCurrentTaskHandle       1

/* configASSERT 실패 시 파일 · 줄 번호 출력 후 정지 */
void vAssertCalled( const char * file, unsigned long line );
#define configASSERT( x )    if( ( x ) == 0 ) vAssertCalled( __FILE__, __LINE__ )

#endif
