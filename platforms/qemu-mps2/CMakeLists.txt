# QEMU mps2-an385 플랫폼: Cortex-M3 보드를 QEMU로 흉내. 보드 없이 인터럽트 · 타이머 · 워치독 · MPU 실습
# 실행: ./scripts/run_qemu.sh build/qemu-mps2/chapters/<장>/<예제>.elf

set(FREERTOS_PORT GCC_ARM_CM3 CACHE STRING "FreeRTOS 포트" FORCE)
# heap_4 = 반납한 블록을 합쳐 주는 일반용 힙 (22장)
set(FREERTOS_HEAP "4" CACHE STRING "FreeRTOS 힙 구현" FORCE)

add_library(freertos_config INTERFACE)
target_include_directories(freertos_config SYSTEM INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

add_library(board STATIC board.c startup.c)
# 시스템 호출은 libc보다 먼저 보이게 실행 파일마다 직접 넣음
target_sources(board INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/syscalls.c)
target_include_directories(board PUBLIC ${PROJECT_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(board PUBLIC freertos_config)
# 시작 코드의 벡터 테이블이 지워지지 않게, 링커 스크립트는 모든 실행 파일에
target_link_options(board PUBLIC -T${CMAKE_CURRENT_SOURCE_DIR}/mps2.ld -Wl,--undefined=vector_table)
