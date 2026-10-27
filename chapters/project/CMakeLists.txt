# 종합 프로젝트: 센서 수집 → 처리 → 전송
if(PLATFORM STREQUAL "posix")
    find_package(Threads REQUIRED)
    add_executable(project-linux linux/main.c)
    target_link_libraries(project-linux PRIVATE Threads::Threads)
endif()

# 진짜 타이머 인터럽트 · 워치독을 써서 보드(QEMU) 전용
if(PLATFORM STREQUAL "qemu-mps2")
    add_executable(project-freertos freertos/main.c)
    target_link_libraries(project-freertos PRIVATE board)
endif()
