import { useState } from "react";
import type { Meta, StoryObj } from "@storybook/react-vite";
import { ArchiveTimeline } from "../src/ui-kit/ArchiveTimeline";
import type {
  TimelineData,
  TimelineEntry,
} from "../src/ui-kit/ArchiveTimeline";

const start = Date.UTC(2026, 8, 18, 14, 20);
const at = (seconds: number) => start + seconds * 1000;
const entries: TimelineEntry[] = [
  { startMs: at(0), endMs: at(115), kind: "recorded" },
  {
    startMs: at(115),
    endMs: at(160),
    kind: "gap",
    reason: "Нет связи с камерой",
  },
  { startMs: at(160), endMs: at(218), kind: "recorded" },
  { startMs: at(218), endMs: at(260), kind: "estimated", uncertaintyMs: 8000 },
  { startMs: at(260), endMs: at(320), kind: "pending" },
  {
    startMs: at(320),
    endMs: at(340),
    kind: "unavailable",
    reason: "Том недоступен",
  },
];
const ready = {
  status: "ready",
  window: { startMs: start, endMs: at(360) },
  entries,
} satisfies TimelineData;
interface ExampleProps {
  initialData: TimelineData;
  initialPosition: number;
}

function Example({ initialData, initialPosition }: ExampleProps) {
  const [data, setData] = useState(initialData);
  const [position, setPosition] = useState(initialPosition);
  return (
    <ArchiveTimeline
      data={data}
      positionMs={position}
      onPositionChange={setPosition}
      onRefresh={() => {
        if (data.status === "ready") {
          setData({
            ...data,
            entries: data.entries.map((entry) =>
              entry.kind === "pending" ? { ...entry, kind: "recorded" } : entry,
            ),
          });
        } else {
          setData(ready);
        }
      }}
    />
  );
}

const meta = {
  title: "Архив/Шкала архива",
  id: "archive-timeline",
  component: Example,
  args: { initialData: ready, initialPosition: at(82) },
} satisfies Meta<typeof Example>;
export default meta;
type Story = StoryObj<typeof meta>;

export const Recorded: Story = { name: "Запись" };
export const Gap: Story = {
  name: "Подтверждённый пропуск",
  args: { initialPosition: at(132) },
};
export const Estimated: Story = {
  name: "Оценочное время",
  args: { initialPosition: at(236) },
};
export const Pending: Story = {
  name: "Каталогизация",
  args: { initialPosition: at(301) },
};
export const Unavailable: Story = {
  name: "Том недоступен",
  args: { initialPosition: at(330) },
};
export const Unknown: Story = {
  name: "Покрытие неизвестно",
  args: { initialPosition: at(350) },
};
export const Loading: Story = {
  name: "Загрузка",
  args: { initialData: { status: "loading" } },
};
export const Empty: Story = {
  name: "Пустой результат",
  args: { initialData: { status: "empty" } },
};
export const Recovery: Story = {
  name: "Ошибка и восстановление",
  args: {
    initialData: { status: "error", message: "Связь с площадкой потеряна." },
  },
};
export const Midnight: Story = {
  name: "Переход через полночь",
  args: {
    initialPosition: start,
    initialData: {
      status: "ready",
      window: { startMs: start, endMs: start + 86400000 },
      entries: [
        { kind: "estimated", startMs: start - 1000, endMs: start + 86400000 },
      ],
    },
  },
};
