# 컨텍스트 스위칭과 틱
if(PLATFORM STREQUAL "posix")
    find_package(Threads REQUIRED)
    add_executable(context-switch-linux linux/main.c)
    target_link_libraries(context-switch-linux PRIVATE Threads::Threads)
endif()

# Cortex-M3 레지스터 · 25MHz 시계를 직접 봐서 보드(QEMU) 전용
if(PLATFORM STREQUAL "qemu-mps2")
    add_executable(context-switch-freertos freertos/main.c)
    target_link_libraries(context-switch-freertos PRIVATE board)
endif()
