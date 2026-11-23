FROM mcr.microsoft.com/playwright:v1.64.0-noble@sha256:06a9939e57531807f8d5fd76ce44b53165ffb7d7501d87ab10e285c20b1e971f
RUN apt-get update && apt-get install -y --no-install-recommends pulseaudio && rm -rf /var/lib/apt/lists/*
WORKDIR /workspace/web/client
ENV STORYBOOK_DISABLE_TELEMETRY=1 HOME=/tmp CI=1
COPY package.json package-lock.json ./
RUN npm ci --ignore-scripts --no-audit --no-fund \
    && install -d -m 1777 node_modules/.cache node_modules/.vite-temp
COPY . .
