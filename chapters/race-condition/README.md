# 레이스 컨디션과 임계 구역

두 실행 흐름이 같은 변수를 "읽기 → 고치기 → 쓰기" 하다가 서로의 결과를 덮어써서 값이 틀어지는 문제

| 예제 | 잠금 없이 | 고친 방법 | 실행 결과 (예) |
|---|---|---|---|
| `linux/main.c` | 스레드 2개 × 100만 번 `counter++` | C11 원자 연산 `atomic_fetch_add` | 잠금 없이 약 100만 (기대 200만) → 원자 연산 200만 |
| `freertos/main.c` | 태스크 2개 × 1000번, 읽기와 쓰기 사이에 `taskYIELD()` | 임계 구역 `taskENTER_CRITICAL()` | 잠금 없이 1000 (기대 2000) → 임계 구역 2000 |

## 실행

```bash
cmake --preset posix && cmake --build --preset posix
./build/posix/chapters/race-condition/race-condition-linux
./build/posix/chapters/race-condition/race-condition-freertos
```

## 포인트

- `volatile`로는 레이스를 못 막음. 매번 메모리에서 읽고 쓸 뿐, 세 단계를 한 번에 하지는 않음
- 코어가 하나인 MCU에서도 레이스 발생. 읽기와 쓰기 사이에 문맥 전환(틱 인터럽트, 더 높은 우선순위 태스크)이 끼면 됨. FreeRTOS 예제는 그 틈을 `taskYIELD()`로 일부러 만들어 매번 같은 결과가 나옴
- Linux 예제는 CPU 1개 환경(코어 1개로 제한한 컨테이너 등)에선 잠금 없이도 우연히 맞을 수 있음
- 임계 구역 안에선 인터럽트와 문맥 전환이 막힘. 짧게 유지하고, 그 안에서 기다리는 함수(`vTaskDelay`, `taskYIELD` 등) 호출 금지

## 지원 플랫폼

| posix | qemu-mps2 | stm32f4 | arduino-uno |
|---|---|---|---|
| ✓ | ✓ | 예정 | 예정 |
