# 스택 오버플로와 메모리 보호

스택이 넘치면 아무 말 없이 옆 메모리를 덮음. 넘친 뒤에 알아채는 방법(FreeRTOS 검사)과 넘치는 순간 막는 방법(MPU 가드)을 비교

## FreeRTOS on Cortex-M3 (qemu-mps2 전용)

```text
FreeRTOS 스택 검사   : 태스크 "small" 넘침 감지 2번 (스택 아래로 64바이트 넘쳤음, 넘친 뒤에 알아챔)
MPU 읽기 전용         : settings[0] = 99 쓰기 → MemManage 폴트 1번 (주소 0x20000000), 값은 그대로 7
MPU 스택 가드         : 태스크 guarded가 가드에 닿는 순간 폴트 (주소 0x200008cc, 가드 0x20000800 ~ +256, CFSR 0x92)
```

- FreeRTOS 검사(`configCHECK_FOR_STACK_OVERFLOW 2`): 스택 끝 16바이트 무늬가 지워졌는지 문맥 전환 때 확인. 넘친 뒤에 알아챔. 예제는 스택 앞에 여분 1KB를 둬서 피해가 없게 함
- MPU 읽기 전용: 폴트 처리기가 막힌 쓰기 명령만 건너뛰고 계속 실행
- MPU 스택 가드: 스택 바로 아래 256바이트를 접근 금지 영역으로. CFSR 0x92 = 접근 위반 + 예외 때 레지스터 쌓기도 막힘(MSTKERR)

```bash
cmake --preset qemu-mps2 && cmake --build --preset qemu-mps2 --target stack-overflow-freertos
./scripts/run_qemu.sh build/qemu-mps2/chapters/stack-overflow/stack-overflow-freertos.elf
```

## Linux

```text
가드 페이지          : 새 스레드 스택 아래 4096바이트 (pthread 기본)
256KB 스택 넘침      :
  깊이 238단계에서 SIGSEGV, 닿은 주소는 스택 시작에서 250KB 아래 (가드 페이지)
읽기 전용 페이지 쓰기 : 막힘(시그널), 값은 그대로 7
```

- 스레드 스택 아래 가드 페이지에 닿으면 SIGSEGV. 처리기는 `sigaltstack`으로 마련한 따로 스택에서 실행
- `mprotect(PROT_READ)` 페이지에 쓰면 시그널. MPU 대신 MMU가 막음
- macOS는 가드가 16KB, 시그널이 SIGBUS로 올 때도 있음

## 포인트

- 가드는 함수 하나가 한 번에 내려가는 크기보다 커야 함. 가드를 32바이트로 줄이면 한 단계에 약 80바이트씩 내려가는 재귀가 가드를 건너뛰고, 아래 메모리를 덮다가 엉뚱한 곳(설정값 영역 0x20000000)에서 멈춤
- `SCB_SHCSR`에서 MemManage 폴트를 켜지 않으면 MPU 위반이 HardFault로 감 (`[HardFault] ... CFSR=0x82`)
- FreeRTOS가 스택을 처음 채울 때(0xA5 무늬)와 검사할 때 스택 끝을 읽음. 가드를 스택 안에 두면 그때 폴트. 가드는 FreeRTOS에 넘긴 스택 바깥에
- arm64 Linux는 스레드 스택 최소가 128KB(`PTHREAD_STACK_MIN`). 64KB로 정하면 `pthread_attr_setstacksize`가 거절하고 기본 8MB로 만들어짐. 반환값을 꼭 확인

## 지원 플랫폼

| posix | qemu-mps2 | stm32f4 | arduino-uno |
|---|---|---|---|
| Linux 예제 | ✓ | 예정 | 예정 |
