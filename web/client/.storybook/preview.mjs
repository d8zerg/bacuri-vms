import { useEffect } from "storybook/preview-api";

export default {
  parameters: { layout: "padded" },
  globalTypes: {
    theme: {
      toolbar: {
        title: "Тема",
        icon: "paintbrush",
        items: [
          { value: "system", title: "Системная" },
          { value: "light", title: "Светлая" },
          { value: "dark", title: "Тёмная" },
        ],
      },
    },
  },
  initialGlobals: { theme: "system" },
  decorators: [
    (story, { globals }) => {
      const theme = globals.theme;
      useEffect(() => {
        const root = document.documentElement;
        root.lang = "ru";
        if (theme === "light" || theme === "dark") root.dataset.theme = theme;
        else delete root.dataset.theme;
      }, [theme]);
      return story();
    },
  ],
};
