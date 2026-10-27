# 인터럽트 기초
if(PLATFORM STREQUAL "posix")
    add_executable(interrupt-linux linux/main.c)
endif()

# 진짜 인터럽트가 필요해서 보드(QEMU) 전용
if(PLATFORM STREQUAL "qemu-mps2")
    add_executable(interrupt-freertos freertos/main.c)
    target_link_libraries(interrupt-freertos PRIVATE board)
endif()
