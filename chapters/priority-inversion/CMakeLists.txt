# 우선순위 역전
if(PLATFORM STREQUAL "posix")
    find_package(Threads REQUIRED)
    add_executable(priority-inversion-linux linux/main.c)
    target_link_libraries(priority-inversion-linux PRIVATE Threads::Threads)
endif()

add_executable(priority-inversion-freertos freertos/main.c)
target_link_libraries(priority-inversion-freertos PRIVATE board)
