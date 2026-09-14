# Build stage
FROM ubuntu:22.04 as builder

LABEL maintainer="bniladridas"
LABEL description="Agentware Agent - Build Stage"
LABEL version="0.1"

# Set environment variables to avoid interactive prompts
ENV DEBIAN_FRONTEND=noninteractive \
    TZ=Etc/UTC \
    DEBCONF_NONINTERACTIVE_SEEN=true \
    BUILD_DEPS="build-essential cmake git" \
    RUNTIME_DEPS="libcurl4-openssl-dev libssl-dev zlib1g-dev"

# Install build dependencies
RUN apt-get update && \
    ln -fs /usr/share/zoneinfo/Etc/UTC /etc/localtime && \
    apt-get install -y --no-install-recommends tzdata $BUILD_DEPS $RUNTIME_DEPS \
    nlohmann-json3-dev \
    libgtest-dev \
    libgmock-dev \
    libssl-dev \
    libcurl4-openssl-dev \
    zlib1g-dev \
    libpqxx-dev \
    python3 \
    python3-pip \
    expect && \
    python3 -m pip install pytest && \
    dpkg-reconfigure --frontend noninteractive tzdata && \
    rm -rf /var/lib/apt/lists/*

# Configure git to skip SSL verification (temporary for build)
RUN git config --global http.sslVerify false

# Install additional build tools
RUN apt-get update && \
    apt-get install -y wget && \
    rm -rf /var/lib/apt/lists/*

# Download and install CPR from release tarball
WORKDIR /tmp
RUN wget https://github.com/libcpr/cpr/archive/refs/tags/1.10.5.tar.gz -O cpr-1.10.5.tar.gz && \
    tar -xzf cpr-1.10.5.tar.gz && \
    mkdir -p cpr-1.10.5/build && \
    cd cpr-1.10.5/build && \
    cmake .. \
        -DCPR_USE_SYSTEM_CURL=ON \
        -DCPR_ENABLE_SSL=ON \
        -DCPR_BUILD_TESTS=OFF \
        -DCPR_FORCE_USE_OPENSSL_FOR_SSL=ON \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/usr/local && \
    cmake --build . --parallel $(nproc) && \
    cmake --install . && \
    cd / && rm -rf /tmp/cpr-1.10.5 /tmp/cpr-1.10.5.tar.gz && \
    ldconfig

# Build the application
WORKDIR /app
COPY . .
RUN mkdir -p build && cd build && \
    cmake .. \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_PREFIX_PATH=/usr/local \
        -DCMAKE_MODULE_PATH=/usr/local/lib/cmake/cpr && \
    cmake --build . --parallel $(nproc) && \
    make install

# Runtime stage
FROM ubuntu:22.04

LABEL maintainer="bniladridas"
LABEL description="Agentware Agent - Runtime"
LABEL version="0.1"

# Install runtime dependencies
RUN apt-get update && \
    apt-get install -y --no-install-recommends \
    libcurl4 \
    libssl3 \
    zlib1g \
    libpqxx-6.4 \
    python3 \
    python3-pip && \
    rm -rf /var/lib/apt/lists/*

# Install Python test dependencies
RUN pip3 install --no-cache-dir pytest requests

# Copy built binary from builder
COPY --from=builder /usr/local/bin/agentware-agent /usr/local/bin/
COPY --from=builder /usr/local/lib/libcpr* /usr/local/lib/

# Create non-root user and set up environment
RUN useradd -m -s /bin/bash agentware && \
    mkdir -p /app/data && \
    chown -R agentware:agentware /app/data

USER agentware
WORKDIR /home/agentware

# Copy configuration template
COPY --chown=agentware:agentware .env.example .env

# Health check
HEALTHCHECK --interval=30s --timeout=10s --start-period=5s --retries=3 \
    CMD agentware-agent --version || exit 1

# Default command
CMD ["agentware-agent"]
