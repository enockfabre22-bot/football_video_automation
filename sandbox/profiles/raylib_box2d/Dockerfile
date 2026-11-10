# Image de rendu isolée pour les animations Raylib + Box2D (manuelles ou générées par l'IA).
# Construction : bash scripts/build_sandbox.sh
FROM ubuntu:24.04

ARG RAYLIB_VERSION=5.5
ARG BOX2D_VERSION=v3.1.0

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential cmake git ca-certificates pkg-config \
        xvfb xauth \
        libgl1-mesa-dev libgl1-mesa-dri libglu1-mesa \
        libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev \
        ffmpeg \
    && rm -rf /var/lib/apt/lists/*

# raylib (versions figées : le contrat donné à l'IA en dépend)
RUN git clone --depth 1 --branch ${RAYLIB_VERSION} https://github.com/raysan5/raylib.git /opt/src/raylib \
    && cmake -S /opt/src/raylib -B /opt/src/raylib/build \
        -DBUILD_EXAMPLES=OFF -DPLATFORM=Desktop -DCMAKE_BUILD_TYPE=Release \
    && cmake --build /opt/src/raylib/build -j"$(nproc)" \
    && cmake --install /opt/src/raylib/build \
    && rm -rf /opt/src/raylib

# Box2D v3 (API C)
# Box2D compile avec -Werror codé en dur : GCC 13 (Ubuntu 24.04) y lève un faux positif
# "maybe-uninitialized" dans distance.c. On désactive seulement le "avertissement = erreur".
RUN git clone --depth 1 --branch ${BOX2D_VERSION} https://github.com/erincatto/box2d.git /opt/src/box2d \
    && sed -i -e 's/ -Werror//g' -e 's/COMPILE_WARNING_AS_ERROR ON/COMPILE_WARNING_AS_ERROR OFF/' /opt/src/box2d/src/CMakeLists.txt \
    && cmake -S /opt/src/box2d -B /opt/src/box2d/build -DCMAKE_BUILD_TYPE=Release \
        -DBOX2D_SAMPLES=OFF -DBOX2D_BENCHMARKS=OFF -DBOX2D_UNIT_TESTS=OFF -DBOX2D_DOCS=OFF \
    && cmake --build /opt/src/box2d/build -j"$(nproc)" \
    && find /opt/src/box2d/build -name 'libbox2d*.a' -exec cp {} /usr/local/lib/ \; \
    && cp -r /opt/src/box2d/include/box2d /usr/local/include/ \
    && rm -rf /opt/src/box2d

# Rendu OpenGL logiciel (Mesa llvmpipe) ; caches dans /tmp (le reste du système est en lecture seule)
ENV LIBGL_ALWAYS_SOFTWARE=1 \
    HOME=/tmp \
    XDG_CACHE_HOME=/tmp/.cache \
    MESA_SHADER_CACHE_DISABLE=true

# Le conteneur est lancé avec l'UID/GID de l'utilisateur du VPS (--user), jamais en root
WORKDIR /work
