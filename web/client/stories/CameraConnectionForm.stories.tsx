import { useRef } from "react";
import type { Meta, StoryObj } from "@storybook/react-vite";
import { CameraConnectionForm } from "../src/ui-kit/CameraConnectionForm";
import type { CameraDraft } from "../src/ui-kit/CameraConnectionForm";
import { useCameraConnectionCheck } from "../src/workflows/useCameraConnectionCheck";

const empty = { name: "", main_url: "", secondary_url: "" };
const camera = {
  name: "Погрузочные ворота 02",
  main_url: "rtsps://camera.test/main",
  secondary_url: "rtsps://camera.test/sub",
};
type ExampleProps = {
  initial: CameraDraft;
  scenario: "normal" | "disconnect" | "slow" | "error";
};

function Example({ initial, scenario }: ExampleProps) {
  const attempts = useRef(0);
  const form = useCameraConnectionCheck(initial, async (draft) => {
    const attempt = ++attempts.current;
    // A late reply can arrive even after the caller has cancelled its request.
    await new Promise((resolve) =>
      setTimeout(resolve, scenario === "slow" && attempt === 1 ? 3000 : 300),
    );
    if (scenario === "disconnect" && attempt === 1)
      return { status: "error", reason: "disconnected" };
    if (scenario === "error" && attempt === 1) throw new Error("check failed");
    return {
      status: "profiles",
      main: draft.main_url.endsWith("/offline") ? "unreachable" : "available",
      secondary: draft.secondary_url.endsWith("/incompatible")
        ? "incompatible"
        : "available",
    };
  });
  return <CameraConnectionForm {...form} />;
}

const meta = {
  title: "Камеры/Подключение камеры",
  id: "camera-connection",
  component: Example,
  args: { initial: camera, scenario: "normal" },
} satisfies Meta<typeof Example>;
export default meta;
type Story = StoryObj<typeof meta>;
export const Empty: Story = { name: "Новая камера", args: { initial: empty } };
export const Ready: Story = { name: "Адреса потоков" };
const submit: NonNullable<Story["play"]> = async ({ canvas, userEvent }) => {
  await userEvent.click(
    canvas.getByRole("button", { name: "Проверить подключение" }),
  );
};
export const Invalid: Story = {
  name: "Обязательные поля",
  args: { initial: empty },
  play: submit,
};
export const Checking: Story = {
  name: "Проверка с задержкой",
  args: { scenario: "slow" },
  play: submit,
};
export const Connected: Story = { name: "Оба потока доступны", play: submit };
export const Partial: Story = {
  name: "Несовместимый дополнительный поток",
  args: {
    initial: { ...camera, secondary_url: "rtsps://camera.test/incompatible" },
  },
  play: submit,
};
export const Disconnect: Story = {
  name: "Обрыв связи и повтор",
  args: { scenario: "disconnect" },
};
export const LateReply: Story = {
  name: "Изменение адреса во время проверки",
  args: { scenario: "slow" },
};
export const Failed: Story = {
  name: "Ошибка проверки",
  args: { scenario: "error" },
  play: submit,
};
