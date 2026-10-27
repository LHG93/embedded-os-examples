# 큐

태스크끼리 데이터를 순서대로 주고받는 통로. 넣을 때 내용이 복사되고, 꽉 차면 넣는 쪽이, 비면 빼는 쪽이 기다림

| 예제 | 내용 | 결과 |
|---|---|---|
| 순서 · 꽉 차면 기다림 | 길이 3 큐에 주문 8개를 한꺼번에 넣고, 5ms마다 하나씩 뺌 | 받은 순서 1~8 그대로, 넣는 쪽이 4~5번 기다림 |
| 값 복사 vs 포인터 | 보낸 뒤 원래 메모를 "아메리카노"로 고침 | 값으로 보낸 건 "라테" 그대로, 포인터는 "아메리카노" |
| 기다리지 않고 넣기 | 대기 시간 0으로 5개 넣기 | 3개 들어가고 2개 실패 |
| 최신 값 하나 (FreeRTOS) | 길이 1 큐에 `xQueueOverwrite` · `xQueuePeek` | 마지막 값만 남고, 읽어도 안 빠짐 |
| 커널 메시지 큐 (Linux) | `mq_open` 길이 3, `O_NONBLOCK` | 3개 들어가고 2개 `EAGAIN`, 꺼낸 순서 1 2 3 |

- FreeRTOS: `xQueueCreate()` · `xQueueSend()` · `xQueueReceive()`
- Linux: 스레드끼리는 고리 버퍼 + 뮤텍스 + 조건 변수로 직접 만듦 (`q_send` · `q_recv`). 프로세스끼리는 `mq_open` (Linux 전용, macOS엔 없음)

## 실행

```bash
cmake --preset posix && cmake --build --preset posix
./build/posix/chapters/queue/queue-linux
./build/posix/chapters/queue/queue-freertos
```

## 포인트

- 큐는 값을 복사함. 큰 데이터는 포인터를 보내되, 받는 쪽이 다 쓸 때까지 보낸 쪽이 그 메모리를 건드리지 않게 약속해야 함
- 대기 시간 0으로 넣으면 꽉 찼을 때 데이터를 버림. 캐셔 예제를 0으로 바꾸면 주문 1~4만 도착하고, 8개를 기다리는 바리스타는 영원히 기다림
- 빼는 쪽에서 "빈칸 생김" 신호(`not_full`)를 빼먹으면, 넣는 쪽이 꽉 찬 큐 앞에서 영원히 기다림
- 센서 최신 값처럼 "마지막 하나만 중요"하면 길이 1 큐 + `xQueueOverwrite()` (메일박스)

## 지원 플랫폼

| posix | qemu-mps2 | stm32f4 | arduino-uno |
|---|---|---|---|
| ✓ | 예정 | 예정 | 예정 |
