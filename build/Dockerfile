FROM debian:bookworm
RUN apt-get update && apt-get install -y --no-install-recommends \
      gcc-arm-linux-gnueabihf \
      binutils-arm-linux-gnueabihf \
      make python3 perl \
 && rm -rf /var/lib/apt/lists/*
