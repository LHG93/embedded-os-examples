# 레이스 컨디션
# Linux(pthread) 예제는 PC에서 직접 도는 프로그램이라 posix일 때만 빌드
if(PLATFORM STREQUAL "posix")
    find_package(Threads REQUIRED)
    add_executable(race-condition-linux linux/main.c)
    target_link_libraries(race-condition-linux PRIVATE Threads::Threads)
endif()

add_executable(race-condition-freertos freertos/main.c)
target_link_libraries(race-condition-freertos PRIVATE board)
