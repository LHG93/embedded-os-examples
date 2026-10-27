# 스케줄링
if(PLATFORM STREQUAL "posix")
    find_package(Threads REQUIRED)
    add_executable(scheduling-linux linux/main.c)
    target_link_libraries(scheduling-linux PRIVATE Threads::Threads)
endif()

add_executable(scheduling-freertos freertos/main.c)
target_link_libraries(scheduling-freertos PRIVATE board)
