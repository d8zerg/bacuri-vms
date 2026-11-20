import type { Meta, StoryObj } from "@storybook/react-vite";
import { MediaPanel } from "../src/ui-kit/MediaPanel";
const meta = {
  title: "UI Kit/Видеоплеер",
  component: MediaPanel,
  args: {
    title: "Тестовая запись",
    mode: "archive",
    videoRef: null,
    state: { kind: "idle", message: "Запись ещё не создана" },
  },
} satisfies Meta<typeof MediaPanel>;
export default meta;
type Story = StoryObj<typeof meta>;
export const Empty: Story = { name: "Ожидание записи" };
export const Loading: Story = {
  name: "Подключение",
  args: { state: { kind: "loading", message: "Загрузка видео…" } },
};
export const Failed: Story = {
  name: "Прерывание просмотра",
  args: { state: { kind: "error", message: "Связь с камерой потеряна." } },
};
