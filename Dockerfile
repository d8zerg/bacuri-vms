FROM debian:trixie-slim@sha256:a72b4433de1cf7d6bae0c4cee74d0547f58601a643b3375f070c51600d63b6b2 AS dev
ARG DEBIAN_SNAPSHOT=20261119T030000Z
RUN sed -i \
        -e "s|http://deb.debian.org/debian-security$|http://snapshot.debian.org/archive/debian-security/${DEBIAN_SNAPSHOT}/|" \
        -e "s|http://deb.debian.org/debian$|http://snapshot.debian.org/archive/debian/${DEBIAN_SNAPSHOT}/|" \
        -e '/^Signed-By:/a Check-Valid-Until: no' \
        /etc/apt/sources.list.d/debian.sources \
    && apt-get -o Acquire::Retries=3 -o Acquire::http::Timeout=30 -o Acquire::Languages=none update \
    && apt-get -o Acquire::Retries=3 -o Acquire::http::Timeout=30 install -y --no-install-recommends \
        clang-19 \
        clang-tidy-19 \
        cmake \
        g++-14 \
        gstreamer1.0-libav \
        gstreamer1.0-plugins-good \
        gstreamer1.0-plugins-ugly \
        gstreamer1.0-tools \
        gstreamer1.0-x \
        fonts-dejavu-core \
        libclang-rt-19-dev \
        libgstrtspserver-1.0-dev \
        ninja-build \
        pkg-config \
        postgresql-client-17 \
        python3-jsonschema \
        sqlite3 \
    && rm -rf /var/lib/apt/lists/* \
    && install -d -m 1777 /var/lib/bacuri-vms/archive
ENV XDG_CACHE_HOME=/tmp/bacuri-vms-cache
WORKDIR /workspace

FROM dev AS rtsp-camera
COPY CMakeLists.txt ./
COPY src ./src
COPY tests/fixtures ./tests/fixtures
RUN cmake -S . -B /tmp/fixture-build -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_CXX_COMPILER=g++-14 \
        -DBUILD_TESTING=OFF \
        -DBACURI_VMS_BUILD_RTSP_FIXTURE=ON \
    && cmake --build /tmp/fixture-build --target bacuri-vms-test-rtsp-camera \
    && install /tmp/fixture-build/tests/fixtures/bacuri-vms-test-rtsp-camera /usr/local/bin/ \
    && rm -rf /tmp/fixture-build
USER 65534:65534
EXPOSE 8554/tcp
CMD ["bacuri-vms-test-rtsp-camera"]
