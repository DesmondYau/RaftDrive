FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

# ---- Base build tools ----
RUN apt-get update && apt-get install -y \
    cmake build-essential git curl wget pkg-config \
    libssl-dev zlib1g-dev libasio-dev \
    && rm -rf /var/lib/apt/lists/*

# ---- gRPC v1.62.0 (same version as Lab0_gRPC) ----
# Builds and installs protobuf + gRPC + grpc_cpp_plugin to /usr/local
WORKDIR /deps
RUN git clone --recurse-submodules -b v1.62.0 --depth 1 --shallow-submodules https://github.com/grpc/grpc
RUN mkdir -p /deps/grpc/build && \
    cd /deps/grpc/build && \
    cmake -DgRPC_INSTALL=ON \
          -DgRPC_BUILD_TESTS=OFF \
          -DCMAKE_BUILD_TYPE=Release \
          .. && \
    make -j$(nproc) install && \
    ldconfig

# ---- GoogleTest v1.14.0 (same as Lab0_gRPC) ----
RUN git clone --depth 1 --branch v1.14.0 https://github.com/google/googletest.git
RUN mkdir -p /deps/googletest/build && \
    cd /deps/googletest/build && \
    cmake .. -DCMAKE_INSTALL_PREFIX=/usr/local && \
    make -j$(nproc) && \
    make install && \
    ldconfig

# ---- AWS SDK C++ (S3 only — skips all other services for speed) ----
RUN apt-get update && apt-get install -y libcurl4-openssl-dev uuid-dev \
    && rm -rf /var/lib/apt/lists/*
RUN git clone --depth 1 --branch 1.11.370 \
    https://github.com/aws/aws-sdk-cpp.git --recurse-submodules
RUN mkdir -p /deps/aws-sdk-cpp/build && \
    cd /deps/aws-sdk-cpp/build && \
    cmake .. \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_ONLY="s3" \
        -DENABLE_TESTING=OFF \
        -DAUTORUN_UNIT_TESTS=OFF \
        -DCMAKE_INSTALL_PREFIX=/usr/local && \
    make -j$(nproc) && \
    make install && \
    ldconfig

# ---- Crow (header-only web framework) ----
RUN git clone --depth 1 https://github.com/CrowCpp/Crow.git /deps/crow && \
    mkdir -p /deps/crow/build && \
    cd /deps/crow/build && \
    cmake .. \
        -DCROW_BUILD_EXAMPLES=OFF \
        -DCROW_BUILD_TESTS=OFF \
        -DCMAKE_INSTALL_PREFIX=/usr/local && \
    make install

# ---- Node.js 20 (for the React frontend) ----
RUN curl -fsSL https://deb.nodesource.com/setup_20.x | bash - && \
    apt-get install -y nodejs && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /workspace/raftdrive
