FROM debian:bookworm
RUN apt-get update && apt-get install -y gcc-aarch64-linux-gnu gcc libc6-dev make file && rm -rf /var/lib/apt/lists/*
WORKDIR /build
ENV CROSS_PREFIX=aarch64-linux-gnu-
