# Build and test sotto-pw against a real PipeWire daemon, without touching the host.
FROM ubuntu:24.04
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    build-essential cmake pkg-config libpipewire-0.3-dev pipewire pipewire-bin wireplumber python3 dbus \
    && rm -rf /var/lib/apt/lists/*
