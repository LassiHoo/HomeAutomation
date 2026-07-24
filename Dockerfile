# syntax=docker/dockerfile:1
#
# Multi-stage build for the home automation hub, targeting linux/arm64
# (Raspberry Pi) via `docker buildx build --platform linux/arm64`. Buildx
# runs this whole file under QEMU emulation for the target arch, so no
# cross-compiler juggling is needed here — every RUN step below executes
# as if it were native arm64.
#
# NOTE on libgpiod versions: Ubuntu 22.04's apt repo only ships libgpiod
# 1.6.3 (the old sysfs-successor API). This project is written against the
# modern libgpiod v2 C++ API (gpiod::chip / gpiod::line_request), which is
# not packaged for jammy. The builder stage below builds libgpiod v2 from
# source and the runtime stage copies over just the resulting shared
# libraries — see README.md "Design decisions" for the full rationale.

FROM ubuntu:22.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
      build-essential \
      cmake \
      git \
      ca-certificates \
      pkg-config \
      autoconf \
      autoconf-archive \
      automake \
      libtool \
      libtool-bin \
      libsqlite3-dev \
    && rm -rf /var/lib/apt/lists/*

# Build libgpiod v2 (see NOTE above) and install it under /usr/local.
RUN git clone --branch v2.2.4 --depth 1 https://github.com/brgl/libgpiod.git /tmp/libgpiod \
    && cd /tmp/libgpiod \
    && ./autogen.sh --enable-bindings-cxx --prefix=/usr/local \
    && make -j"$(nproc)" \
    && make install \
    && ldconfig \
    && rm -rf /tmp/libgpiod

WORKDIR /src
COPY CMakeLists.txt ./
COPY src ./src
COPY include ./include

# FetchContent (spdlog/nlohmann-json/cpp-httplib) needs network access, which
# is available at build time.
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build -j"$(nproc)"

# --- Runtime stage -----------------------------------------------------

FROM ubuntu:22.04 AS runtime

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
      libsqlite3-0 \
    && rm -rf /var/lib/apt/lists/*

# Custom-built libgpiod v2 runtime libraries (not available via apt on jammy).
COPY --from=builder /usr/local/lib/libgpiod.so.3* /usr/local/lib/
COPY --from=builder /usr/local/lib/libgpiodcxx.so.2* /usr/local/lib/
RUN ldconfig

WORKDIR /app
COPY --from=builder /src/build/hub ./hub
COPY config ./config

RUN mkdir -p /app/data /app/logs

# Runs as root: /dev/gpiochip0 is typically root:root or root:gpio 660 on
# Raspberry Pi OS, and the container has no reliable way to join the host's
# gpio group. This is standard practice for GPIO-passthrough containers.
EXPOSE 8080
ENV HUB_HOST=0.0.0.0 \
    HUB_PORT=8080 \
    HUB_DATA_DIR=/app/data \
    HUB_LOG_DIR=/app/logs

ENTRYPOINT ["./hub", "config/devices.json"]
