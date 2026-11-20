FROM selenium/standalone-firefox:4.53.0@sha256:f80274ecdbdc9c2ef67e89b70735aa9421d623bafb2aa1d3d6a2558137b3d2a2
USER root
ARG OPENH264_URL=https://ciscobinary.openh264.org/openh264-linux64-652bdb7719f30b52b08e506645a7322ff1b2cc6f.zip
ARG OPENH264_SHA512=f5246bf14d038adf4ce0c4360262ab722bc3de4220f047c3d542b4c564074b4877dc8659e3125c5171c749e7ce93f20cc63777eb5e1539e960670cbc5f30ac85
ENV MOZ_GMP_PATH=/opt/gmp-gmpopenh264/2.6.0
RUN curl --fail --silent --show-error --location --retry 3 "$OPENH264_URL" -o /tmp/openh264.zip \
    && echo "$OPENH264_SHA512  /tmp/openh264.zip" | sha512sum --check \
    && mkdir -p "$MOZ_GMP_PATH" && unzip -q /tmp/openh264.zip -d "$MOZ_GMP_PATH" \
    && chmod -R a+rX /opt/gmp-gmpopenh264 && rm /tmp/openh264.zip
USER seluser
