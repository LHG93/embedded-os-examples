# 플랫폼 추가

장 예제의 FreeRTOS 코드는 `include/board.h` 함수만 사용. 새 보드는 장 코드 수정 없이 세 가지만 만듦.

1. **`platforms/<이름>/` 폴더**
   - `CMakeLists.txt`: `FREERTOS_PORT`, `FREERTOS_HEAP` 지정, `freertos_config`(FreeRTOSConfig.h 위치)와 `board` 라이브러리 생성. `posix/CMakeLists.txt` 참고
   - `FreeRTOSConfig.h`: 보드 클럭 · 틱 · 인터럽트 우선순위에 맞춘 설정
   - `board.c`: `board.h` 함수 구현 (로그는 UART, LED는 GPIO)
   - 보드용 시작 코드, 링커 스크립트, 벤더 드라이버
2. **`CMakePresets.json` 프리셋 하나**: 크로스 컴파일 보드는 툴체인 파일(`CMAKE_TOOLCHAIN_FILE`)도 지정
3. **장 README "지원 플랫폼" 표에 한 칸**

보드가 있어야 의미 있는 장(인터럽트 우선순위, 워치독 등)은 장 README에 "보드 전용" 표시. `posix`엔 시그널 · 스레드로 흉내 낸 버전을 둠.

## 예정

| 이름 | 보드 | FreeRTOS 포트 |
|---|---|---|
| `qemu-mps2` | QEMU `mps2-an385` (Cortex-M3) | `GCC_ARM_CM3` |
| `stm32f4` | STM32F4 계열 (예: STM32F4DISCOVERY, Nucleo-F446RE) | `GCC_ARM_CM4F` |
| `arduino-uno` | 아두이노 우노 R3 (ATmega328P) | AVR 포트 (RAM 2KB라 태스크 수 적게) |
