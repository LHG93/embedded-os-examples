# embedded-os-examples

[hglee93.com](https://hglee93.com) 강의 **"임베디드 운영체제 — Linux · FreeRTOS · AUTOSAR로 비교하며 배우기"** 예제 코드 (강의 페이지 준비 중)

- 개념 하나(레이스 컨디션, 뮤텍스, 세마포어, 큐, 인터럽트, 데드락 …)를 **Linux(pthread)** 와 **FreeRTOS** 코드로 나란히 비교
- 보드 없이 도커 하나로 전부 실행 가능
- AUTOSAR는 실제 스택(BSW · MCAL · RTE)에 상용 도구가 필요해서 강의 페이지에서 개념과 구조만 다룸

## 빠른 시작 (도커 추천)

```bash
git clone --recursive https://github.com/LHG93/embedded-os-examples.git
cd embedded-os-examples
docker compose run --rm dev      # 처음 한 번은 이미지 빌드로 몇 분 걸림
./scripts/run_all.sh             # 컨테이너 안에서 모든 예제 빌드 · 실행
```

`--recursive` 없이 받았으면 `git submodule update --init` 한 번 실행

예제 하나만 실행:

```bash
cmake --preset posix
cmake --build --preset posix --target race-condition-freertos
./build/posix/chapters/race-condition/race-condition-freertos
```

Cortex-M3 보드(QEMU)에서 실행:

```bash
cmake --preset qemu-mps2 && cmake --build --preset qemu-mps2
./scripts/run_qemu.sh build/qemu-mps2/chapters/queue/queue-freertos.elf
./scripts/run_all.sh qemu-mps2   # FreeRTOS 예제 전부
```

## 도커 없이

- 리눅스 · macOS + CMake 3.21 이상 + C 컴파일러(gcc 또는 clang)면 같은 명령으로 빌드됨
- WSL · VM 설정은 강의 "실습 환경" 장 참고

## 폴더 구조

```text
chapters/<주제>/        장마다 한 폴더
  linux/main.c           pthread 예제 (PC에서 바로 실행)
  freertos/main.c        FreeRTOS 예제 (board.h 함수만 사용 → 보드가 바뀌어도 같은 코드)
  README.md
platforms/<플랫폼>/      보드마다 한 번만: FreeRTOSConfig.h, board.c, 빌드 설정
include/board.h          장 예제와 보드 사이의 약속 (로그 출력, LED 등)
third_party/             FreeRTOS-Kernel (git submodule)
docker/Dockerfile        개발 환경 (버전 고정)
scripts/run_all.sh       모든 예제 빌드 · 실행 확인 (프리셋 posix · qemu-mps2)
scripts/run_qemu.sh      QEMU Cortex-M3에서 예제 하나 실행
cmake/                   크로스 컴파일 툴체인 파일
scripts/versions.sh      설치된 도구 버전 확인
```

## 플랫폼

| 플랫폼 | 상태 | 설명 |
|---|---|---|
| `posix` | 사용 가능 | 리눅스 · 도커 · macOS. PC에서 FreeRTOS 흉내 (태스크 = pthread, 싱글코어처럼 하나씩 실행) |
| `qemu-mps2` | 사용 가능 | QEMU `mps2-an385`로 Cortex-M3 보드 흉내. 진짜 NVIC · SysTick · 타이머 · 워치독 · MPU. 도커 이미지에 툴체인 · QEMU 포함 |
| `stm32f4` | 예정 | STM32F4 보드 |
| `arduino-uno` | 예정 | 아두이노 우노 (RAM 2KB) |

새 보드 추가 방법: [platforms/README.md](platforms/README.md)

## 장 목록

| 부 | 장 | 폴더 | 상태 |
|---|---|---|---|
| 1부 OS 기본기 | 태스크 · 스레드 · 프로세스 | [chapters/task](chapters/task) | 완료 |
| 2부 동기화 | 레이스 컨디션과 임계 구역 | [chapters/race-condition](chapters/race-condition) | 완료 |
| | 뮤텍스 | [chapters/mutex](chapters/mutex) | 완료 |
| | 세마포어 | [chapters/semaphore](chapters/semaphore) | 완료 |
| | 데드락 | [chapters/deadlock](chapters/deadlock) | 완료 |
| | 우선순위 역전 | [chapters/priority-inversion](chapters/priority-inversion) | 완료 |
| | 이벤트 | [chapters/event](chapters/event) | 완료 |
| 3부 통신 | 큐 | [chapters/queue](chapters/queue) | 완료 |
| | 스트림 · 메시지 버퍼, 공유 메모리 | [chapters/stream-buffer](chapters/stream-buffer) | 완료 |
| 4부 인터럽트와 시간 | 인터럽트 기초 | [chapters/interrupt](chapters/interrupt) | 완료 (FreeRTOS는 qemu-mps2) |
| | ISR에서 태스크로 넘기기 | [chapters/isr-to-task](chapters/isr-to-task) | 완료 (FreeRTOS는 qemu-mps2) |
| | 타이머와 알람 | [chapters/timer](chapters/timer) | 완료 |
| | 워치독 | [chapters/watchdog](chapters/watchdog) | 완료 (FreeRTOS는 qemu-mps2) |
| 5부 메모리와 안정성 | 스택과 힙 | [chapters/stack-heap](chapters/stack-heap) | 완료 (FreeRTOS는 qemu-mps2) |
| | 스택 오버플로와 메모리 보호 | [chapters/stack-overflow](chapters/stack-overflow) | 완료 (FreeRTOS는 qemu-mps2) |

폴더 이름엔 번호 안 붙임. 중간에 장이 추가돼도 링크 유지. 순서는 이 표 기준.

## 기준 버전 (2026-10 확인)

| 도구 | 버전 |
|---|---|
| Ubuntu (도커 기본 이미지) | 26.04 LTS (`ubuntu:resolute-20261006`) |
| gcc / cmake / ninja / make | 15.2.0 / 4.2.3 / 1.13.2 / 4.4.1 |
| gdb / git / python3 | 17.1 / 2.53.0 / 3.14.4 |
| gcc-arm-none-eabi / newlib | 14.2.rel1 / 4.6.0 |
| qemu-system-arm / openocd | 10.2.1 / 0.12.0 |
| FreeRTOS-Kernel | V11.3.2 |

내 환경 버전 확인: `./scripts/versions.sh`. 2026-10-27 이 이미지(arm64)에서 전체 예제 통과 확인

## 라이선스

[MIT](LICENSE). FreeRTOS-Kernel도 MIT.
