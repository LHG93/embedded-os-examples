# 타이머와 알람
if(PLATFORM STREQUAL "posix")
    add_executable(timer-linux linux/main.c)
endif()

add_executable(timer-freertos freertos/main.c)
target_link_libraries(timer-freertos PRIVATE board)
