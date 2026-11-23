import { useState } from "react";
import type { Meta, StoryObj } from "@storybook/react-vite";
import { FilterPanel, matchesQuery } from "../src/ui-kit/FilterPanel";

const meta = {
  title: "Поля и формы/Поиск и фильтры",
  component: FilterPanel,
} satisfies Meta<typeof FilterPanel>;
export default meta;
function Example() {
  const [query, setQuery] = useState(""),
    [state, setState] = useState("");
  return (
    <FilterPanel
      label="Фильтры устройств"
      queryLabel="Название устройства"
      query={query}
      onQuery={setQuery}
      filtered={!!state}
      onReset={() => {
        setQuery("");
        setState("");
      }}
    >
      <label>
        Состояние
        <select value={state} onChange={(e) => setState(e.target.value)}>
          <option value="">Все состояния</option>
          <option value="applied">Применено</option>
          <option value="pending">Ожидает рекордера</option>
        </select>
      </label>
    </FilterPanel>
  );
}
export const Devices: StoryObj = {
  name: "Поиск со списком состояний",
  render: () => <Example />,
};

function JournalExample() {
  const [draft, edit] = useState("");
  const [applied, apply] = useState("");
  const actions = [
    "Принятие в работу",
    "Результат проверки",
    "Закрытие тревоги",
  ];
  return (
    <>
      <FilterPanel
        label="Поиск журнала"
        queryLabel="Действие"
        query={draft}
        onQuery={edit}
        filtered={!!applied}
        onApply={() => apply(draft.trim())}
        onReset={() => {
          edit("");
          apply("");
        }}
      />
      <ul>
        {actions
          .filter((action) => matchesQuery(applied, action))
          .map((action) => (
            <li key={action}>{action}</li>
          ))}
      </ul>
    </>
  );
}
export const Journal: StoryObj = {
  name: "Поиск по Enter или кнопке",
  render: () => <JournalExample />,
};
