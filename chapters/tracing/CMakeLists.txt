# 트레이싱
if(PLATFORM STREQUAL "posix")
    find_package(Threads REQUIRED)
    add_executable(tracing-linux linux/main.c)
    target_link_libraries(tracing-linux PRIVATE Threads::Threads)
endif()

# 진짜 타이머 인터럽트와 25MHz 시계로 기록해서 보드(QEMU) 전용
if(PLATFORM STREQUAL "qemu-mps2")
    add_executable(tracing-freertos freertos/main.c)
    target_link_libraries(tracing-freertos PRIVATE board)
endif()
