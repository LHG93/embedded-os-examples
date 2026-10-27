# 실시간이란
if(PLATFORM STREQUAL "posix")
    find_package(Threads REQUIRED)
    add_executable(realtime-linux linux/main.c)
    target_link_libraries(realtime-linux PRIVATE Threads::Threads)
endif()

add_executable(realtime-freertos freertos/main.c)
target_link_libraries(realtime-freertos PRIVATE board)
