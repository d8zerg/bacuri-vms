type Device = { model?: string; serial?: string; endpoint_id?: string };
type ChannelIdentity = { device: Device; key: string; token: string };

export function groupChannels<T>(
  items: T[],
  identity: (item: T) => ChannelIdentity,
) {
  const groups = new Map<string, { key: string; device: Device; items: T[] }>();
  for (const item of items) {
    const { key, device } = identity(item);
    const group = groups.get(key);
    if (group) group.items.push(item);
    else groups.set(key, { key, device, items: [item] });
  }
  const all = [...groups.values()];
  for (const group of all) {
    group.items.sort((a, b) =>
      identity(a).token.localeCompare(identity(b).token, "ru", {
        numeric: true,
      }),
    );
  }
  return all.map((group) => {
    const { model, serial, endpoint_id } = group.device;
    const similar = all.filter((other) => other.device.model === model);
    const distinguish =
      similar.length > 1
        ? serial &&
          !similar.some(
            (other) => other !== group && other.device.serial === serial,
          )
          ? serial
          : endpoint_id || group.key
        : "";
    return {
      ...group,
      title: [model || "Камера", distinguish].filter(Boolean).join(" · "),
    };
  });
}

export function channelName(name: string, model = "", token = "") {
  if (name !== [model, token].filter(Boolean).join(" · ")) return name;
  const number = /^channel(\d+)$/i.exec(token);
  return number ? "Канал " + number[1] : token || "Основной канал";
}
