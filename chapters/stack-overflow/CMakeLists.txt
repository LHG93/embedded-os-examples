# 스택 오버플로와 메모리 보호
if(PLATFORM STREQUAL "posix")
    find_package(Threads REQUIRED)
    add_executable(stack-overflow-linux linux/main.c)
    target_link_libraries(stack-overflow-linux PRIVATE Threads::Threads)
endif()

# MPU가 필요해서 보드(QEMU) 전용
if(PLATFORM STREQUAL "qemu-mps2")
    add_executable(stack-overflow-freertos freertos/main.c)
    target_link_libraries(stack-overflow-freertos PRIVATE board)
endif()
