# syntax=docker/dockerfile:1
#
# TwinLine in a container:
#   docker run --rm -v "$PWD":/work ghcr.io/yusuffeness/twinline analyze notes.txt
#   docker run --rm -it -v "$PWD":/work ghcr.io/yusuffeness/twinline notes.txt

FROM debian:bookworm-slim AS build
RUN apt-get update \
 && apt-get install -y --no-install-recommends gcc libc6-dev cmake make \
 && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY CMakeLists.txt ./
COPY cmake ./cmake
COPY include ./include
COPY src ./src
COPY man ./man
COPY README.md CHANGELOG.md LICENSE ./
COPY docs/CONFIGURATION.md ./docs/
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr \
          -DTWINLINE_BUILD_TESTS=OFF -DTWINLINE_BUILD_BENCHMARKS=OFF \
          -DTWINLINE_WARNINGS_AS_ERRORS=OFF \
 && cmake --build build --parallel \
 && DESTDIR=/out cmake --install build --strip

FROM debian:bookworm-slim
LABEL org.opencontainers.image.title="TwinLine" \
      org.opencontainers.image.description="A concurrent terminal editor that finds the lines that look alike" \
      org.opencontainers.image.source="https://github.com/yusuffeness/TwinLine" \
      org.opencontainers.image.licenses="MIT"
COPY --from=build /out/usr /usr
RUN useradd --create-home --uid 1000 twinline
USER twinline
WORKDIR /work
ENV TERM=xterm-256color
ENTRYPOINT ["twinline"]
