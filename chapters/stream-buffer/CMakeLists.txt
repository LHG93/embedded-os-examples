# 스트림 · 메시지 버퍼, 공유 메모리
if(PLATFORM STREQUAL "posix")
    find_package(Threads REQUIRED)
    add_executable(stream-buffer-linux linux/main.c)
    target_link_libraries(stream-buffer-linux PRIVATE Threads::Threads)
endif()

add_executable(stream-buffer-freertos freertos/main.c)
target_link_libraries(stream-buffer-freertos PRIVATE board)
