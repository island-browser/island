# syntax=docker/dockerfile:1.7
# Island hybrid build images.
#
# Base image is pinned by immutable manifest digest in docker/images.lock.json.
# The manifest digest below is the multi-arch index for ubuntu:24.04 (tag pushed
# 2026-08-04); Docker resolves the correct architecture-specific image at pull
# time. See docs/build-execution-policy.md for the full container constraints.

ARG UBUNTU_BASE_DIGEST=sha256:561618e2c15bf2397621dd04f96926663a3b5616c189cf7e38db7e82f5c538ea

FROM ubuntu@${UBUNTU_BASE_DIGEST} AS base

ENV DEBIAN_FRONTEND=noninteractive \
    LANG=C.UTF-8 \
    LC_ALL=C.UTF-8

# Minimal common layer: only what every stage needs.
# Pinning apt versions is fragile across image rebuilds; the base image digest
# is the reproducibility anchor, not per-package versions.
# hadolint ignore=DL3008
RUN apt-get update \
    && apt-get install --no-install-recommends --yes \
        ca-certificates \
        python3 \
    && rm -rf /var/lib/apt/lists/*

# ---------------------------------------------------------------------------
# portable-tests: run dependency Python tests, design drift, icon pipeline,
# and package fixture tests. No compiler, no CEF, no network at runtime.
# ---------------------------------------------------------------------------
FROM base AS portable-tests

# hadolint ignore=DL3008
RUN apt-get update \
    && apt-get install --no-install-recommends --yes \
        build-essential \
        file \
    && rm -rf /var/lib/apt/lists/*

# Non-root user for the test runtime. UID 1000 is stable and does not collide
# with the host caller UID mapping performed by the runner.
RUN groupadd --system --gid 1000 island \
    && useradd --system --uid 1000 --gid island --home-dir /home/island --create-home island

# The workdir backs named volumes, which inherit this directory's ownership on
# first use. 0777 lets an arbitrary --user=<uid>:<gid> caller-UID mapping
# (Linux hosts) initialize and write the volume; nothing on the host is exposed.
RUN mkdir -p /work \
    && chown island:island /work \
    && chmod 0777 /work

WORKDIR /work

# The host wrapper mounts /src read-only and runs commands as the island user.
# Source is never copied into the image; the context stays minimal.
USER island

# ---------------------------------------------------------------------------
# linux-build: build and test the Linux product on a matching native
# architecture. Prerequisites mirror the existing native-linux CI job.
# ---------------------------------------------------------------------------
FROM base AS linux-build

# hadolint ignore=DL3008
RUN apt-get update \
    && apt-get install --no-install-recommends --yes \
        build-essential \
        cmake \
        ninja-build \
        pkg-config \
        git \
        fontconfig \
        libfontconfig1-dev \
        libgtk-3-dev \
        libnss3-dev \
        libasound2-dev \
        xvfb \
    && rm -rf /var/lib/apt/lists/*

RUN groupadd --system --gid 1000 island \
    && useradd --system --uid 1000 --gid island --home-dir /home/island --create-home island

# Same named-volume ownership handling as the portable-tests stage.
RUN mkdir -p /work \
    && chown island:island /work \
    && chmod 0777 /work

WORKDIR /work

USER island

# ---------------------------------------------------------------------------
# dev: optional interactive development shell. Not used by CI.
# ---------------------------------------------------------------------------
FROM linux-build AS dev

# hadolint ignore=DL3008
RUN apt-get update \
    && apt-get install --no-install-recommends --yes \
        curl \
        less \
        vim-tiny \
    && rm -rf /var/lib/apt/lists/*

USER island
