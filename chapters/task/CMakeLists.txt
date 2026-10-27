# 태스크 · 스레드 · 프로세스
if(PLATFORM STREQUAL "posix")
    find_package(Threads REQUIRED)
    add_executable(task-linux linux/main.c)
    target_link_libraries(task-linux PRIVATE Threads::Threads)
endif()

add_executable(task-freertos freertos/main.c)
target_link_libraries(task-freertos PRIVATE board)
