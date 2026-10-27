# 큐
if(PLATFORM STREQUAL "posix")
    find_package(Threads REQUIRED)
    add_executable(queue-linux linux/main.c)
    target_link_libraries(queue-linux PRIVATE Threads::Threads)
endif()

add_executable(queue-freertos freertos/main.c)
target_link_libraries(queue-freertos PRIVATE board)
