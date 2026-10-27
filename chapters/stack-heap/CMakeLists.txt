# 스택과 힙
if(PLATFORM STREQUAL "posix")
    find_package(Threads REQUIRED)
    add_executable(stack-heap-linux linux/main.c)
    target_link_libraries(stack-heap-linux PRIVATE Threads::Threads)
endif()

# heap_4 통계 · 정적 할당을 쓰는 보드(QEMU) 전용
if(PLATFORM STREQUAL "qemu-mps2")
    add_executable(stack-heap-freertos freertos/main.c)
    target_link_libraries(stack-heap-freertos PRIVATE board)
endif()
