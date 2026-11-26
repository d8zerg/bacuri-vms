import { createElement } from "react";
import { useEffect } from "storybook/preview-api";
import { LanguageProvider, selectLanguage } from "../src/i18n";

let toolbarLanguage;

export default {
  parameters: { layout: "padded" },
  globalTypes: {
    theme: {
      toolbar: {
        title: "Theme",
        icon: "paintbrush",
        items: [
          { value: "system", title: "System" },
          { value: "light", title: "Light" },
          { value: "dark", title: "Dark" },
        ],
      },
    },
    language: {
      toolbar: {
        title: "Language",
        icon: "globe",
        items: [
          { value: "ru", title: "Русский" },
          { value: "en", title: "English" },
        ],
      },
    },
  },
  initialGlobals: { theme: "system", language: "ru" },
  loaders: [
    async ({ globals }) => {
      const language = globals.language === "en" ? "en" : "ru";
      if (language !== toolbarLanguage) {
        toolbarLanguage = language;
        await selectLanguage(language);
      }
      return {};
    },
  ],
  decorators: [
    (story, { globals }) => {
      const theme = globals.theme;
      useEffect(() => {
        const root = document.documentElement;
        if (theme === "light" || theme === "dark") root.dataset.theme = theme;
        else delete root.dataset.theme;
      }, [theme]);
      return createElement(LanguageProvider, null, story());
    },
  ],
};
