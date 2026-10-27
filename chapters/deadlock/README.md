# 데드락

태스크 둘이 서로 상대가 쥔 자원을 기다리며 영원히 멈추는 상황

우유 · 시럽 뮤텍스 두 개. 직원 A는 우유 → 시럽, 직원 B는 시럽 → 우유 순서로 잡고 음료를 만듦.

| 방법 | Linux (스레드 2 × 1만 잔) | FreeRTOS (태스크 2 × 1000잔) |
|---|---|---|
| 순서 반대, 무한 대기 | 데드락 (1초 지켜보고 판정) | 데드락 (500ms 지켜보고 판정, 우유 주인 A · 시럽 주인 B) |
| 순서 반대 + 놓고 다시 | `trylock` 실패 시 쥔 것을 놓고 다시. 완료 | 100ms 시간 제한, 실패 시 쥔 것을 놓고 다시. 완료 |
| 순서 고정 (항상 우유 먼저) | 완료 | 완료 |
| 같은 뮤텍스 두 번 | 오류 검사 뮤텍스가 `EDEADLK` 돌려줌 | |

## 실행

```bash
cmake --preset posix && cmake --build --preset posix
./build/posix/chapters/deadlock/deadlock-linux
./build/posix/chapters/deadlock/deadlock-freertos
```

## 포인트

- 데드락 네 조건: 상호 배제, 점유 대기, 비선점, 순환 대기. 하나만 깨도 안 생김
- 가장 흔한 해결은 잠금 순서 고정. 모든 코드가 "우유 먼저"를 지키면 순환 대기가 생기지 않음
- 시간 제한 · `trylock`은 데드락을 알아채고 빠져나오는 방법. 놓고 다시 할 때 둘이 같은 박자로 계속 부딪치지 않게 쉬는 시간을 다르게 둠
- 첫 잔의 쉬는 줄(`usleep` · `vTaskDelay`)을 빼면: Linux는 1만 잔 동안 결국 엇갈려서 10번 중 10번 데드락(macOS), FreeRTOS는 5번 중 2번만 데드락. 테스트에서 안 보이다가 현장에서 터지는 이유
- 멈춘 태스크를 지워도 쥐고 있던 뮤텍스는 풀리지 않음. FreeRTOS 예제는 단계마다 뮤텍스를 새로 만듦
- `xSemaphoreGetMutexHolder()`로 누가 쥐고 있는지 볼 수 있음 (`INCLUDE_xSemaphoreGetMutexHolder 1`)

## 지원 플랫폼

| posix | qemu-mps2 | stm32f4 | arduino-uno |
|---|---|---|---|
| ✓ | ✓ | 예정 | 예정 |
