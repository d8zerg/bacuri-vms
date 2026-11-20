import type { Meta, StoryObj } from "@storybook/react-vite";
import { AlertList } from "../src/ui-kit/AlertList";
const meta = {
  title: "UI Kit/Лента тревог",
  component: AlertList,
  args: {
    feed: { status: "online", items: [] },
    points: [],
    selected: null,
    listRef: null,
    onOpen: () => {},
  },
} satisfies Meta<typeof AlertList>;
export default meta;
type Story = StoryObj<typeof meta>;
export const Empty: Story = { name: "Пустая лента" };
export const Reconnecting: Story = {
  name: "Восстановление связи",
  args: { feed: { status: "reconnecting", items: [] } },
};
export const Denied: Story = {
  name: "Доступ закрыт",
  args: { feed: { status: "denied", items: [] } },
};
