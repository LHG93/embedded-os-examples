# embedded-os-examples 개발 환경
# 기준: Ubuntu 26.04 LTS. 날짜 태그로 고정해서 언제 빌드해도 같은 기본 이미지
# amd64(일반 PC) · arm64(Apple Silicon Mac) 모두 지원
FROM ubuntu:resolute-20261006

ENV DEBIAN_FRONTEND=noninteractive \
    LANG=C.UTF-8 \
    LC_ALL=C.UTF-8

# 확인한 버전 (2026-10, Ubuntu 26.04 저장소 기준)
#   gcc 15.2.0 · cmake 4.2.3 · ninja 1.13.2 · make 4.4.1 · gdb 17.1 · git 2.53.0 · python3 3.14.4
#   gcc-arm-none-eabi 14.2.rel1 · newlib 4.6.0 · qemu-system-arm 10.2.1 · openocd 0.12.0
# 실제 설치된 버전은 컨테이너 안에서 scripts/versions.sh로 확인
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        cmake \
        ninja-build \
        gdb \
        git \
        python3 \
        ca-certificates \
        gcc-arm-none-eabi \
        libnewlib-arm-none-eabi \
        gdb-multiarch \
        qemu-system-arm \
        openocd \
    && rm -rf /var/lib/apt/lists/*

# Ubuntu 이미지 기본 일반 사용자(UID 1000)로 작업
# 마운트한 폴더에 root 소유 파일이 생기지 않게
USER ubuntu
WORKDIR /work
CMD ["bash"]
