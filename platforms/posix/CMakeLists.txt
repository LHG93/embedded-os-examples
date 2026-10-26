# POSIX 플랫폼: FreeRTOS POSIX 포트로 리눅스 · 도커 · macOS에서 FreeRTOS 실행
# 태스크 하나하나가 pthread, 한 번에 하나만 실행 (싱글코어 흉내)

set(FREERTOS_PORT GCC_POSIX CACHE STRING "FreeRTOS 포트" FORCE)
# heap_3 = 표준 malloc/free 래퍼. POSIX에서 가장 단순
set(FREERTOS_HEAP "3" CACHE STRING "FreeRTOS 힙 구현" FORCE)

add_library(freertos_config INTERFACE)
target_include_directories(freertos_config SYSTEM INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

find_package(Threads REQUIRED)

add_library(board STATIC board.c)
target_include_directories(board PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(board PUBLIC freertos_config Threads::Threads)
