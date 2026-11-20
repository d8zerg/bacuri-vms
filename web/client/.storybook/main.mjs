const config = {
  framework: {
    name: "@storybook/react-vite",
    options: { builder: { viteConfigPath: false } },
  },
  stories: ["../stories/**/*.stories.tsx"],
  core: { disableTelemetry: true, disableWhatsNewNotifications: true },
  async viteFinal(config) {
    return {
      ...config,
      cacheDir: "/tmp/bacuri-storybook",
      build: { ...config.build, sourcemap: true },
    };
  },
};
export default config;
