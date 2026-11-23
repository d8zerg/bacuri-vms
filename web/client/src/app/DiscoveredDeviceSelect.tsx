import type { DiscoveredCamera } from "../contracts/workspace";
import {
  FilterPanel,
  matchesQuery,
  needsListSearch,
} from "../ui-kit/FilterPanel";

export function matchingDevices(devices: DiscoveredCamera[], query: string) {
  return devices.filter((d) =>
    matchesQuery(query, d.device_service, d.endpoint_id),
  );
}

export function DiscoveredDeviceSelect({
  devices,
  query,
  setQuery,
  value,
  onSelect,
  multiple = false,
}: {
  devices: DiscoveredCamera[];
  query: string;
  setQuery: (query: string) => void;
  value: string;
  onSelect: (value: string) => void;
  multiple?: boolean;
}) {
  const shown = matchingDevices(devices, query);
  function search(next: string) {
    setQuery(next);
    if (
      value &&
      !matchingDevices(devices, next).some((d) => d.selection === value)
    )
      onSelect("");
  }
  const picker = (
    <label className="bk-field">
      {multiple ? "Устройство" : "Обнаруженная камера"}
      <select
        value={value}
        disabled={!shown.length}
        onChange={(e) => onSelect(e.target.value)}
      >
        {(multiple || !value) && (
          <option value="">
            {multiple
              ? query
                ? "Все найденные по фильтру"
                : "Все обнаруженные устройства"
              : "Выберите камеру"}
          </option>
        )}
        {shown.map((d) => (
          <option key={d.endpoint_id} value={d.selection}>
            {d.device_service}
          </option>
        ))}
      </select>
    </label>
  );
  return needsListSearch(devices.length, query) ? (
    <>
      <FilterPanel
        label="Фильтры обнаруженных устройств"
        queryLabel="Адрес или идентификатор устройства"
        query={query}
        onQuery={search}
        onReset={() => search("")}
      >
        {picker}
      </FilterPanel>
      {!shown.length && <p role="status">Устройства по запросу не найдены.</p>}
    </>
  ) : (
    picker
  );
}
