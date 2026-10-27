# FreeRTOS 내부 (설정, 포팅 레이어)

FreeRTOS는 CPU와 상관없는 커널(tasks.c · queue.c …)과, CPU마다 다른 포팅 레이어(port.c · portmacro.h)로 나뉨. 설정은 FreeRTOSConfig.h 한 곳. Cortex-M3 포트가 실제로 무엇을 맡는지 레지스터로 확인

## FreeRTOS (qemu-mps2 전용)

```text
1. 설정표 (FreeRTOSConfig.h)
   configCPU_CLOCK_HZ 25000000 · configTICK_RATE_HZ 1000 · configMAX_PRIORITIES 7 · configTOTAL_HEAP_SIZE 262144
   configUSE_PREEMPTION 1 · configUSE_TIME_SLICING 1 · configUSE_TICKLESS_IDLE 0
2. 포팅 레이어가 맡은 예외 (벡터 표)
   SVCall  (11) → 0x00002581 vPortSVCHandler      같음: 예  첫 태스크 시작
   PendSV  (14) → 0x000027b1 xPortPendSVHandler   같음: 예  문맥 전환 (8장)
   SysTick (15) → 0x000027f9 xPortSysTickHandler  같음: 예  틱
   SysTick 다시 세는 값 24999 = 25000000 / 1000 - 1, 켜짐: 예
   우선순위 (작을수록 급함): PendSV 0xff · SysTick 0xff · SVCall 0x00
   임계 구역 안 BASEPRI 0x40 (밖 0x00) → 0x40 이상 인터럽트는 막히고, 그보다 급한 것은 들어옴
3. 태스크 하나의 값 (heap_4)
   스택 256워드 = 1024바이트, TCB(StaticTask_t) 92바이트 → 힙에서 1136바이트 줄어듦 (블록 머리 · 정렬 포함)
4. 모두 잠든 1000틱 동안 (configUSE_TICKLESS_IDLE 0)
   틱 1000 증가, 틱 인터럽트 1000번
```

주소는 빌드마다 다를 수 있음

## 실행

```bash
cmake --preset qemu-mps2 && cmake --build --preset qemu-mps2
./scripts/run_qemu.sh build/qemu-mps2/chapters/freertos-internals/freertos-internals-freertos.elf
```

## 바꿔 보기

| 바꿀 곳 | 결과 |
|---|---|
| `platforms/qemu-mps2/FreeRTOSConfig.h`에 `#define configUSE_TICKLESS_IDLE 1` | 4번이 "틱 1000 증가, 틱 인터럽트 2번". 할 일이 없으면 틱을 멈추고 깨어날 때 한꺼번에 셈. SysTick은 24비트라 25MHz에서 한 번에 약 0.67초까지만 멈춤 |

## 포인트

- 포트가 맡는 예외 셋: SVC(첫 태스크 시작), PendSV(문맥 전환), SysTick(틱). 새 칩으로 옮길 때 이 셋과 `portmacro.h`(자료형 · 임계 구역 · 양보)를 맞춤
- PendSV · SysTick은 가장 낮은 우선순위(`configKERNEL_INTERRUPT_PRIORITY`). 다른 인터럽트가 다 끝난 뒤 전환
- 임계 구역은 인터럽트를 다 끄지 않고 `BASEPRI`로 `configMAX_SYSCALL_INTERRUPT_PRIORITY`(0x40) 이상만 막음. 그보다 급한 인터럽트는 늦지 않는 대신 FreeRTOS API를 못 씀(18장)
- 태스크 하나 = 스택 + TCB(Cortex-M3에서 92바이트) + 힙 블록 머리. 태스크 수를 정할 때 계산
- `configUSE_TICK_HOOK`을 켜 둠(board.c의 빈 `vApplicationTickHook`). 이 장 예제가 덮어써서 틱 인터럽트 수를 셈

## 지원 플랫폼

| posix | qemu-mps2 | stm32f4 | arduino-uno |
|---|---|---|---|
| – | ✓ | 예정 | 예정 |
