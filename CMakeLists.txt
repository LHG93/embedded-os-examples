cmake_minimum_required(VERSION 3.21)
project(embedded_os_examples C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# 빌드할 플랫폼. 지금은 posix(리눅스 · 도커 · macOS)만 있음
# 보드 추가: platforms/<이름>/ 폴더 + CMakePresets.json 프리셋 하나
set(PLATFORM "posix" CACHE STRING "빌드할 플랫폼 (platforms/ 아래 폴더 이름)")
if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/platforms/${PLATFORM}/CMakeLists.txt)
    message(FATAL_ERROR "platforms/${PLATFORM} 폴더 없음")
endif()

# 플랫폼이 freertos_config, board, FREERTOS_PORT, FREERTOS_HEAP 지정
add_subdirectory(platforms/${PLATFORM})
add_subdirectory(third_party/FreeRTOS-Kernel)
target_link_libraries(board PUBLIC freertos_kernel)

# 장마다 폴더 하나. 순서는 README.md 목차 기준
add_subdirectory(chapters/race-condition)
add_subdirectory(chapters/mutex)
add_subdirectory(chapters/semaphore)
