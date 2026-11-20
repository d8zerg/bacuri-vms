FROM debian:trixie-slim@sha256:a72b4433de1cf7d6bae0c4cee74d0547f58601a643b3375f070c51600d63b6b2 AS toolchain
ARG DEBIAN_SNAPSHOT=20261119T030000Z
RUN sed -i \
        -e "s|http://deb.debian.org/debian-security$|http://snapshot.debian.org/archive/debian-security/${DEBIAN_SNAPSHOT}/|" \
        -e "s|http://deb.debian.org/debian$|http://snapshot.debian.org/archive/debian/${DEBIAN_SNAPSHOT}/|" \
        -e '/^Signed-By:/a Check-Valid-Until: no' \
        /etc/apt/sources.list.d/debian.sources \
    && apt-get -o Acquire::Retries=3 -o Acquire::http::Timeout=30 -o Acquire::Languages=none update \
    && apt-get -o Acquire::Retries=3 -o Acquire::http::Timeout=30 install -y --no-install-recommends \
        ca-certificates \
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
        libgrpc++-dev \
        libpq-dev \
        libsodium-dev \
        libssl-dev \
        libsqlite3-dev \
        protobuf-compiler-grpc \
        libgstrtspserver-1.0-dev \
        libprotobuf-dev \
        ninja-build \
        openssl \
        pkg-config \
        postgresql-client-17 \
        protobuf-compiler \
        python3-jsonschema \
        python3-openapi-spec-validator \
        python3-protobuf \
        python3-yaml \
        sqlite3 \
    && rm -rf /var/lib/apt/lists/* \
    && install -d -m 1777 /var/lib/bacuri-vms/archive
RUN apt-get -o Acquire::Retries=3 -o Acquire::http::Timeout=30 -o Acquire::Languages=none update \
    && apt-get -o Acquire::Retries=3 -o Acquire::http::Timeout=30 install -y --no-install-recommends \
        gstreamer1.0-nice libgstreamer-plugins-bad1.0-dev \
    && rm -rf /var/lib/apt/lists/*
ADD --checksum=sha256:94d34280e3915535bd73ac9e6896b11a5d0dea31b6c04f8c1c1acf9bf61013a1 \
    https://github.com/conan-io/conan/releases/download/2.34.0/conan-2.34.0-amd64.deb /tmp/conan.deb
RUN dpkg -i /tmp/conan.deb && rm /tmp/conan.deb && conan --version
ENV XDG_CACHE_HOME=/tmp/bacuri-vms-cache
WORKDIR /workspace

FROM node:22-bookworm-slim@sha256:e9f565d32859f0cbfb52125eafe8be39390f24dac302dc3cdf566e8ba3ab21ee AS api-types
WORKDIR /opt/bacuri-api-tools
COPY tools/api-client/package.json tools/api-client/package-lock.json ./
RUN npm ci --ignore-scripts --no-audit --no-fund
ENV PATH=/opt/bacuri-api-tools/node_modules/.bin:$PATH
WORKDIR /workspace

FROM toolchain AS dev
COPY conanfile.py conan.lock /opt/bacuri/
COPY conan/profiles/ /opt/bacuri/profiles/
RUN export CONAN_HOME=/opt/bacuri/conan-cache && \
    for profile in gcc-debug clang-debug gcc-release; do \
        generators="/opt/bacuri/deps/$profile"; \
        mkdir -p "$generators" && \
        conan install /opt/bacuri/conanfile.py \
            -pr:b /opt/bacuri/profiles/gcc-release -pr:h "/opt/bacuri/profiles/$profile" \
            --lockfile=/opt/bacuri/conan.lock --build=missing -of "$generators" \
            --format=json --out-file="$generators/graph.json" || exit 1; \
    done && conan cache clean '*' --source --build --download --temp
RUN (dpkg-query -W && sha256sum /opt/bacuri/deps/*/graph.json /opt/bacuri/profiles/*) \
        > /tmp/bacuri-build-inputs && \
    sha256sum /tmp/bacuri-build-inputs > /opt/bacuri/deps/toolchain.sha256 && \
    rm /tmp/bacuri-build-inputs

FROM dev AS quality
RUN apt-get -o Acquire::Retries=3 update \
    && apt-get -o Acquire::Retries=3 install -y --no-install-recommends clang-format-19 gcovr git \
    && rm -rf /var/lib/apt/lists/*
ADD --checksum=sha256:551f6fc83ea457d62a0d98237cbad105af8d557003051f41f3e7ca7b3f2470eb \
    https://github.com/gitleaks/gitleaks/releases/download/v8.30.1/gitleaks_8.30.1_linux_x64.tar.gz /tmp/gitleaks.tgz
RUN tar -xzf /tmp/gitleaks.tgz -C /usr/local/bin gitleaks && rm /tmp/gitleaks.tgz

FROM dev AS rtsp-camera
COPY CMakeLists.txt ./
COPY src ./src
COPY storage ./storage
COPY contracts/v1/control.proto ./contracts/v1/
COPY tests/fixtures ./tests/fixtures
RUN --network=none cmake -S . -B /tmp/fixture-build -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_TOOLCHAIN_FILE=/opt/bacuri/deps/gcc-release/conan_toolchain.cmake \
        -DBUILD_TESTING=OFF \
        -DBACURI_VMS_BUILD_RTSP_FIXTURE=ON \
    && cmake --build /tmp/fixture-build --target bacuri-vms-test-rtsp-camera \
    && install /tmp/fixture-build/tests/fixtures/bacuri-vms-test-rtsp-camera /usr/local/bin/ \
    && rm -rf /tmp/fixture-build
USER 65534:65534
EXPOSE 8554/tcp
CMD ["bacuri-vms-test-rtsp-camera"]
