FROM debian:bookworm-slim

ARG DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    bash \
    build-essential \
    ca-certificates \
    cmake \
    git \
    glslang-tools \
    libgl1-mesa-dev \
    libvulkan-dev \
    libx11-dev \
    libxcursor-dev \
    libxi-dev \
    libxinerama-dev \
    libxrandr-dev \
    ninja-build \
    pkg-config \
    python3 \
    vulkan-tools \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace

CMD ["bash"]
