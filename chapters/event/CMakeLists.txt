# 이벤트
if(PLATFORM STREQUAL "posix")
    find_package(Threads REQUIRED)
    add_executable(event-linux linux/main.c)
    target_link_libraries(event-linux PRIVATE Threads::Threads)
endif()

add_executable(event-freertos freertos/main.c)
target_link_libraries(event-freertos PRIVATE board)
