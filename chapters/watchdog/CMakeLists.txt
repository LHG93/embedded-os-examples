# 워치독
if(PLATFORM STREQUAL "posix")
    find_package(Threads REQUIRED)
    add_executable(watchdog-linux linux/main.c)
    target_link_libraries(watchdog-linux PRIVATE Threads::Threads)
endif()

# 하드웨어 워치독이 필요해서 보드(QEMU) 전용
if(PLATFORM STREQUAL "qemu-mps2")
    add_executable(watchdog-freertos freertos/main.c)
    target_link_libraries(watchdog-freertos PRIVATE board)
endif()
