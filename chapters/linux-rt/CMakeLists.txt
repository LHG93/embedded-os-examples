# Linux 실시간 (PREEMPT_RT) — Linux 전용
if(PLATFORM STREQUAL "posix")
    find_package(Threads REQUIRED)
    add_executable(linux-rt-linux linux/main.c)
    target_link_libraries(linux-rt-linux PRIVATE Threads::Threads)
endif()
