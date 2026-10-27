# 데드락
if(PLATFORM STREQUAL "posix")
    find_package(Threads REQUIRED)
    add_executable(deadlock-linux linux/main.c)
    target_link_libraries(deadlock-linux PRIVATE Threads::Threads)
endif()

add_executable(deadlock-freertos freertos/main.c)
target_link_libraries(deadlock-freertos PRIVATE board)
