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

export const Plan: Story = {
  name: "Поэтажный план",
  args: { initial: "/maps?plan=floor&point=dock" },
};
export const EmptyMaps: Story = {
  name: "Нет карт и планов",
  args: { initial: "/maps", condition: "empty" },
};
export const MapsUnavailable: Story = {
  name: "Карты недоступны",
  args: { initial: "/maps", condition: "map-error" },
};

export const MapManagement: Story = {
  name: "Установка и обновление карт",
  args: { initial: "/maps?plan=floor", condition: "map-management" },
};

export const PtzPlan: Story = {
  name: "Калибровка PTZ на плане",
  args: { initial: "/maps?plan=floor&point=dock", condition: "ptz-plan" },
};
