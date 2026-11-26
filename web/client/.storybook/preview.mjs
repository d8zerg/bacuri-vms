import { createElement } from "react";
import { useEffect } from "storybook/preview-api";
import { LanguageProvider, selectLanguage } from "../src/i18n";

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
  decorators: [
    (story, { globals }) => {
      const theme = globals.theme;
      // Selected before rendering, so the first frame of a story already uses the chosen language.
      selectLanguage(globals.language === "en" ? "en" : "ru");
      useEffect(() => {
        const root = document.documentElement;
        if (theme === "light" || theme === "dark") root.dataset.theme = theme;
        else delete root.dataset.theme;
      }, [theme]);
      return createElement(LanguageProvider, null, story());
    },
  ],
};
