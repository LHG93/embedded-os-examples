# ISR에서 태스크로 넘기기

ISR은 짧게 "일이 생겼다"만 알리고, 실제 일은 태스크가 함 (지연 처리, deferred interrupt processing)

## FreeRTOS on Cortex-M3 (qemu-mps2 전용)

타이머 인터럽트 약 10ms마다 100번. ISR이 알린 순간부터 처리 태스크가 시작할 때까지 걸린 시간

| 방법 | 평균 | 최대 |
|---|---|---|
| `xSemaphoreGiveFromISR(&woken)` + `portYIELD_FROM_ISR(woken)` | 약 10us | 300 ~ 450us |
| 세마포어, 전환 요청 없이 (`NULL`) | 약 220us | 약 1ms (다음 틱까지) |
| `vTaskNotifyGiveFromISR` + 바로 전환 | 약 9us | 약 300us |
| `xQueueSendFromISR` + 바로 전환 | 약 12us | 약 400us. 100개 모두 순서대로 받음 |

최대값이 튀는 건 QEMU가 PC 위에서 돌기 때문. 실제 보드는 훨씬 고름

```bash
cmake --preset qemu-mps2 && cmake --build --preset qemu-mps2 --target isr-to-task-freertos
./scripts/run_qemu.sh build/qemu-mps2/chapters/isr-to-task/isr-to-task-freertos.elf
```

## Linux (사용자 공간)

시그널 처리기는 쓸 수 있는 함수가 적어서 알리기만 하고, 일은 스레드가 함. 알리는 방법은 self-pipe(`write()`는 처리기에서 써도 됨)

| 예제 | 결과 |
|---|---|
| 10ms마다 `SIGALRM` 100번, 처리기 → 작업 스레드 | 평균 40 ~ 70us, 최대 250 ~ 460us |

```bash
./build/posix/chapters/isr-to-task/isr-to-task-linux
```

## 포인트

- `FromISR` 함수는 기다리지 않음. 대신 "더 높은 태스크를 깨웠는지"를 `woken`으로 알려 줌
- `portYIELD_FROM_ISR(woken)`을 빼면 깨운 태스크가 다음 틱까지 기다림 (평균 10us → 220us)
- 태스크 알림은 세마포어 · 큐 객체 없이 태스크에 직접 알려서 가장 가벼움. 받는 태스크가 하나일 때만
- 처리가 알림보다 느리면 이벤트를 잃음. 처리 태스크에 `vTaskDelay(15ms)`를 넣으면 세마포어 · 알림은 100번 중 69번만, 큐는 꽉 차서 일부를 버림
- Linux 커널 드라이버는 같은 일을 스레드 IRQ(`request_threaded_irq`) · 워크큐로 함

## 지원 플랫폼

| posix | qemu-mps2 | stm32f4 | arduino-uno |
|---|---|---|---|
| Linux 시그널 예제 | ✓ | 예정 | 예정 |
