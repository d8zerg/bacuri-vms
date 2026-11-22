FROM debian:trixie-slim@sha256:a72b4433de1cf7d6bae0c4cee74d0547f58601a643b3375f070c51600d63b6b2 AS debian-base
ARG DEBIAN_SNAPSHOT=20261119T030000Z
RUN sed -i \
        -e "s|http://deb.debian.org/debian-security$|http://snapshot.debian.org/archive/debian-security/${DEBIAN_SNAPSHOT}/|" \
        -e "s|http://deb.debian.org/debian$|http://snapshot.debian.org/archive/debian/${DEBIAN_SNAPSHOT}/|" \
        -e '/^Signed-By:/a Check-Valid-Until: no' \
        /etc/apt/sources.list.d/debian.sources

FROM debian-base AS maintenance-build
RUN sed -i 's/^Types: deb$/Types: deb deb-src/' /etc/apt/sources.list.d/debian.sources \
    && apt-get -o Acquire::Retries=3 update \
    && apt-get install -y --no-install-recommends build-essential dpkg-dev ca-certificates patch \
    && DEB_BUILD_PROFILES="nocheck nodoc" apt-get build-dep -y --no-install-recommends libxml2 libsoup3
WORKDIR /src
ARG XML_VERSION=2.12.7+dfsg+really2.9.14-2.1+deb13u3
ARG SOUP_VERSION=3.6.5-3
RUN apt-get source "libxml2=$XML_VERSION" "libsoup3=$SOUP_VERSION" \
    && mv libxml2-*/ libxml2 && mv libsoup3-*/ libsoup3
ADD --checksum=sha256:d2d821830eb60edbcfcbe30e2827420e9e439e98c85c812352969cdda764d60b \
    https://archive.ubuntu.com/ubuntu/pool/main/libx/libxml2/libxml2_2.9.14+dfsg-1.3ubuntu3.10.debian.tar.xz /tmp/xml-backports.tar.xz
ADD --checksum=sha256:c51f023b067bc1e25a06b8b34e138b8791efb1c68f19944914e17d66cf569121 \
    https://gitlab.gnome.org/GNOME/libsoup/-/commit/dbd0417ea3a569135e9a9344b7812076b7b178f1.patch /patches/CVE-2026-102560.patch
ADD --checksum=sha256:6a994eeceff5ecccdf51a5994dd4e1c8acf68643557919233477b1c18bedb87b \
    https://gitlab.gnome.org/GNOME/libsoup/-/commit/b91bbd7d7888c85b17a8b33173caa806dff51681.patch /patches/CVE-2026-2369.patch
COPY packaging/maintenance/build-debs /usr/local/bin/build-debs
RUN --network=none tar -xOf /tmp/xml-backports.tar.xz debian/patches/CVE-2026-6653.patch > /patches/CVE-2026-6653.patch \
    && sh /usr/local/bin/build-debs libxml2 "$XML_VERSION" CVE-2026-6653.patch \
    && sh /usr/local/bin/build-debs libsoup3 "$SOUP_VERSION" CVE-2026-2369.patch CVE-2026-102560.patch \
    && mkdir -p /out/xml /out/soup \
    && cp libxml2_*.deb /out/xml/ && cp libsoup-3.0-0_*.deb libsoup-3.0-common_*.deb /out/soup/
COPY packaging/maintenance/provenance.py /usr/local/bin/provenance.py
RUN --network=none mkdir -p /sdk/xml-dev && cp /src/libxml2-dev_*.deb /sdk/xml-dev/ \
    && python3 /usr/local/bin/provenance.py \
    && mkdir -p /out/source && cp /src/*.dsc /src/*.tar.* /src/*.buildinfo /out/source/ \
    && cp -r /patches /out/source/

FROM golang:1.27.4-trixie@sha256:f18c3d73a184fb0582828510722e723b63a93f7188d11dd223d9be6f6880e65d AS go-tools
ENV CGO_ENABLED=0 GOTOOLCHAIN=local
ADD --checksum=sha256:e90fb266d75837e75894c778bf594ab8e2787f12dce5a62651f21b893eaf9abb \
    https://codeload.github.com/gitleaks/gitleaks/tar.gz/refs/tags/v8.30.1 /tmp/gitleaks.tgz
ADD --checksum=sha256:207a2045e890d3ca3326027c5f585733085960b401a49dbc00e3bb628c054cf8 \
    https://codeload.github.com/tianon/gosu/tar.gz/refs/tags/1.20 /tmp/gosu.tgz
RUN mkdir -p /src/gitleaks /src/gosu /out \
    && tar -xf /tmp/gitleaks.tgz -C /src/gitleaks --strip-components=1 \
    && tar -xf /tmp/gosu.tgz -C /src/gosu --strip-components=1 \
    && cd /src/gitleaks && go mod download && go mod verify \
    && cd /src/gosu && go mod download && go mod verify
RUN --network=none cd /src/gitleaks && go build -p 2 -mod=readonly -trimpath -buildvcs=false -tags gore2regex \
        -ldflags '-s -w -X github.com/zricethezav/gitleaks/v8/version.Version=8.30.1' -o /out/gitleaks . \
    && cd /src/gosu && go build -p 2 -mod=readonly -trimpath -buildvcs=false -o /out/gosu .

RUN --network=none go version -m /out/gosu /out/gitleaks > /out/go-buildinfo.txt

FROM scratch AS maintained-packages
COPY --from=maintenance-build /out/ /
COPY --from=maintenance-build /sdk/ /
COPY --from=go-tools /out/ /tools/
COPY --from=go-tools /tmp/gosu.tgz /tmp/gitleaks.tgz /source/

FROM nginxinc/nginx-unprivileged:1.31-alpine@sha256:b5047b9b76845eac124358373c6852377c52e4b05834a47647e955dd91cb8f1e AS static-web
USER root
ARG BROTLI_VERSION=1.2.1-r0
RUN wget -q -O /tmp/brotli-libs.apk \
    "https://dl-cdn.alpinelinux.org/alpine/v3.24/main/$(apk --print-arch)/brotli-libs-${BROTLI_VERSION}.apk" \
    && apk add --no-cache --no-network /tmp/brotli-libs.apk && rm /tmp/brotli-libs.apk
USER 101
RUN --network=none nginx -t

FROM postgres:17.12-trixie@sha256:b60e90042aa6b8dbecd58e224b3e36114dbf8ad754465264c2476ad58a86e45c AS postgres
RUN --mount=from=maintenance-build,source=/out/xml,target=/debs dpkg --path-include='*' -i /debs/*.deb
COPY --from=maintenance-build /out/xml/provenance.json /usr/share/bacuri/maintenance/xml.json
COPY --from=go-tools /out/gosu /usr/local/bin/gosu

FROM debian-base AS toolchain
RUN apt-get -o Acquire::Retries=3 -o Acquire::http::Timeout=30 -o Acquire::Languages=none update \
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
COPY packaging/maintenance/webrtc-sdk /usr/local/bin/webrtc-sdk
RUN --mount=from=maintenance-build,source=/out,target=/debs \
    apt-get -o Acquire::Retries=3 update && apt-get install -y --no-install-recommends gstreamer1.0-nice \
    && sh /usr/local/bin/webrtc-sdk && dpkg --path-include='*' -i /debs/xml/*.deb /debs/soup/*.deb \
    && rm -rf /var/lib/apt/lists/*
COPY --from=maintenance-build /out/xml/provenance.json /usr/share/bacuri/maintenance/xml.json
COPY --from=maintenance-build /out/soup/provenance.json /usr/share/bacuri/maintenance/soup.json
ADD --checksum=sha256:94d34280e3915535bd73ac9e6896b11a5d0dea31b6c04f8c1c1acf9bf61013a1 \
    https://github.com/conan-io/conan/releases/download/2.34.0/conan-2.34.0-amd64.deb /tmp/conan.deb
RUN dpkg -i /tmp/conan.deb && rm /tmp/conan.deb && conan --version
ENV XDG_CACHE_HOME=/tmp/bacuri-vms-cache
WORKDIR /workspace

FROM node:24-trixie-slim@sha256:af02d57b57719b9657a45199a79264a9437940cd55b3c7b3fc99615654f124a0 AS api-types
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
# Use the headers and linker name from the same build as the maintained XML runtime.
RUN --mount=from=maintenance-build,source=/sdk/xml-dev,target=/packages \
    dpkg --path-include='*' -i /packages/*.deb
COPY --from=maintenance-build /sdk/xml-dev/provenance.json /usr/share/bacuri/maintenance/xml-dev.json
RUN apt-get -o Acquire::Retries=2 update && apt-get install -y --no-install-recommends libpng-dev libfido2-dev libarchive-dev libcurl4-openssl-dev \
    && rm -rf /var/lib/apt/lists/*
RUN (dpkg-query -W && sha256sum /opt/bacuri/deps/*/graph.json /opt/bacuri/profiles/*) \
        > /tmp/bacuri-build-inputs && \
    sha256sum /tmp/bacuri-build-inputs > /opt/bacuri/deps/toolchain.sha256 && \
    rm /tmp/bacuri-build-inputs

FROM dev AS quality
RUN apt-get -o Acquire::Retries=3 update \
    && apt-get -o Acquire::Retries=3 install -y --no-install-recommends clang-format-19 gcovr git \
    && rm -rf /var/lib/apt/lists/*
COPY --from=go-tools /out/gitleaks /usr/local/bin/gitleaks

FROM debian-base AS offline-dependencies
COPY packaging/debian/runtime-packages /tmp/runtime-packages
COPY --from=maintenance-build /out/xml/ /patched/xml/
COPY --from=maintenance-build /out/soup/ /patched/soup/
RUN mkdir -p /packages/partial && touch /tmp/empty-status && apt-get update && \
    apt-get --download-only --no-install-recommends -y -o Dir::State::status=/tmp/empty-status \
        -o Dir::Cache::archives=/packages install $(cat /tmp/runtime-packages) /patched/*/*.deb && \
    cp /patched/*/*.deb /packages/ && rm -rf /packages/partial /packages/lock

FROM dev AS offline-packager
RUN apt-get update && apt-get install -y --no-install-recommends gnupg apt-utils dpkg-dev && rm -rf /var/lib/apt/lists/*
COPY --from=offline-dependencies /packages /opt/bacuri/offline-packages

FROM debian-base AS install-guest
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends systemd-sysv linux-image-cloud-amd64 \
    initramfs-tools busybox iproute2 openssl gpgv e2fsprogs && rm -rf /var/lib/apt/lists/* && \
    rm -f /usr/sbin/policy-rc.d /etc/machine-id && touch /etc/machine-id && \
    systemctl enable systemd-networkd.service

FROM debian-base AS install-vm
RUN apt-get update && apt-get install -y --no-install-recommends qemu-system-x86 e2fsprogs python3 && rm -rf /var/lib/apt/lists/*
COPY --from=install-guest / /guest/
WORKDIR /workspace

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
