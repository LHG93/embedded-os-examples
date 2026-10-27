# FreeRTOS 내부 (설정, 포팅 레이어)
# Cortex-M3 포트의 예외 · 레지스터를 직접 봐서 보드(QEMU) 전용
if(PLATFORM STREQUAL "qemu-mps2")
    add_executable(freertos-internals-freertos freertos/main.c)
    target_link_libraries(freertos-internals-freertos PRIVATE board)
endif()
