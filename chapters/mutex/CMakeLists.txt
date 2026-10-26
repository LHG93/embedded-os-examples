# 뮤텍스
if(PLATFORM STREQUAL "posix")
    find_package(Threads REQUIRED)
    add_executable(mutex-linux linux/main.c)
    target_link_libraries(mutex-linux PRIVATE Threads::Threads)
endif()

add_executable(mutex-freertos freertos/main.c)
target_link_libraries(mutex-freertos PRIVATE board)
