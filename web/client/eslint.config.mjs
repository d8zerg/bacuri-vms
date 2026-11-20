import { defineConfig } from "eslint/config";
import ts from "typescript-eslint";
import hooks from "eslint-plugin-react-hooks";
export default defineConfig(
  {
    files: ["**/*.{ts,tsx}"],
    extends: [ts.configs.recommendedTypeChecked],
    languageOptions: { parserOptions: { projectService: true } },
    plugins: { "react-hooks": hooks },
    rules: {
      "react-hooks/rules-of-hooks": "error",
      "react-hooks/exhaustive-deps": "error",
    },
  },
  {
    files: ["src/ui-kit/**/*.{ts,tsx}"],
    rules: {
      "no-restricted-imports": [
        "error",
        {
          patterns: [
            {
              regex:
                "(^|/)(workflows|adapters|session|stories|tests)(/|$)|^(@storybook/|storybook$|react-router)",
              message:
                "UI components receive data and actions through their props.",
            },
          ],
        },
      ],
      "no-restricted-globals": [
        "error",
        "fetch",
        "WebSocket",
        "RTCPeerConnection",
        "MediaSource",
      ],
    },
  },
);
