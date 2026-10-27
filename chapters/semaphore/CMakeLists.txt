# 세마포어
if(PLATFORM STREQUAL "posix")
    find_package(Threads REQUIRED)
    add_executable(semaphore-linux linux/main.c)
    target_link_libraries(semaphore-linux PRIVATE Threads::Threads)
endif()

add_executable(semaphore-freertos freertos/main.c)
target_link_libraries(semaphore-freertos PRIVATE board)
