import { useState } from "react";
import type { Meta, StoryObj } from "@storybook/react-vite";
import { WorkspaceRouter } from "../src/app/Workspace";
import { fixture, utc } from "./workspaceFixture";
export function Example({
  initial,
  condition,
}: {
  initial: string;
  condition: string;
}) {
  const [services] = useState(() => fixture(condition));
  return <WorkspaceRouter services={services} initial={initial} />;
}
const meta = {
  title: "Рабочее место/Экраны",
  id: "workspace",
  component: Example,
  parameters: { layout: "fullscreen" },
  args: { initial: "/monitor?point=dock&at=" + utc(180), condition: "normal" },
} satisfies Meta<typeof Example>;
export default meta;
type Story = StoryObj<typeof meta>;
export const Monitor: Story = { name: "Мониторинг" };
export const Installer: Story = {
  name: "Подключение камеры",
  args: { initial: "/cameras", condition: "slow" },
};
export const EmptyFeed: Story = {
  name: "Пустая лента",
  args: { condition: "empty" },
};
export const Offline: Story = {
  name: "Ошибка связи с камерой",
  args: { initial: "/cameras", condition: "offline" },
};
