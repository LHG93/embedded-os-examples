# 태스크 · 스레드 · 프로세스

일하는 단위. 프로세스는 메모리를 따로, 스레드는 같은 프로세스 안에서 함께 씀. RTOS 태스크는 주소 공간이 하나뿐이라 스레드에 가까움

## FreeRTOS (posix · qemu-mps2 둘 다)

```text
태스크 상태 (eTaskGetState):
  runner  : 실행 (Running)
  ready   : 준비 (Ready)  ← 우선순위가 낮아 줄만 섬
  sleeper : 차단 (Blocked)  ← vTaskDelay로 잠
  waiter  : 차단 (Blocked)  ← 세마포어를 기다림
  writer  : 일시 정지 (Suspended)  ← vTaskSuspend로 멈춤
태스크 목록 (vTaskListTasks, 상태 X 실행 · R 준비 · B 차단 · S 일시 정지):
runner          X  2  837  1
...
메모리 공유 : writer가 shared = 42로 바꿈 → runner가 본 값 42 (주소 0x2000005c로 같음: 예)
```

## Linux

```text
프로세스 (fork)    : 자식이 global = 100으로 바꿈 → 부모가 본 값 0, 주소 0xaaaad03b0018 (같은 주소, 다른 메모리)
스레드 (pthread)   : 스레드가 global = 100으로 바꿈 → 메인이 본 값 100
ID                 : PID 203 하나에 메인 TID 203, 스레드 TID 206
상태 (/proc)       : 잠든 자식 S, 끝났지만 아직 거두지 않은 자식 Z (좀비)
```

ID · 상태 줄은 Linux 전용(`gettid`, `/proc`)

## 실행

```bash
cmake --preset posix && cmake --build --preset posix
./build/posix/chapters/task/task-freertos
./build/posix/chapters/task/task-linux
```

## 포인트

- fork한 자식은 부모 메모리의 복사본을 받음. 주소(가상 주소)는 같아도 실제 메모리는 따로라 서로 안 보임
- 스레드 · RTOS 태스크는 같은 메모리를 봄. 그래서 10장부터의 동기화가 필요
- FreeRTOS 태스크 상태: 실행 · 준비 · 차단 · 일시 정지. vTaskListTasks로 한눈에 (`configUSE_TRACE_FACILITY`, `configUSE_STATS_FORMATTING_FUNCTIONS`)
- `ready` 태스크를 우선순위 3으로 올려 무한 루프를 돌리면 runner(2)가 영원히 못 돎 (7장 굶주림)
- 끝난 자식을 `waitpid`로 거두지 않으면 좀비(Z)로 남음

## 지원 플랫폼

| posix | qemu-mps2 | stm32f4 | arduino-uno |
|---|---|---|---|
| ✓ | ✓ | 예정 | 예정 |
