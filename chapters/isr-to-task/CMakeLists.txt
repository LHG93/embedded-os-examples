# ISR에서 태스크로 넘기기
if(PLATFORM STREQUAL "posix")
    find_package(Threads REQUIRED)
    add_executable(isr-to-task-linux linux/main.c)
    target_link_libraries(isr-to-task-linux PRIVATE Threads::Threads)
endif()

# 진짜 인터럽트가 필요해서 보드(QEMU) 전용
if(PLATFORM STREQUAL "qemu-mps2")
    add_executable(isr-to-task-freertos freertos/main.c)
    target_link_libraries(isr-to-task-freertos PRIVATE board)
endif()
