import { dateTimeFormat } from "../i18n";

const utcOptions: Intl.DateTimeFormatOptions = {
  timeZone: "UTC",
  day: "numeric",
  month: "short",
  year: "numeric",
  hour: "2-digit",
  minute: "2-digit",
  second: "2-digit",
  hourCycle: "h23",
};

export function formatUtc(utc: string) {
  const fraction = utc.match(/\.(\d+)Z$/)?.[1]?.replace(/0+$/, "");
  return (
    dateTimeFormat(utcOptions).format(new Date(utc)) +
    (fraction ? "," + fraction : "") +
    " UTC"
  );
}

export function milliseconds(utc: string) {
  const fraction = utc.match(/\.\d{3}(\d+)Z$/)?.[1];
  return Date.parse(utc) + (fraction ? Number("0." + fraction) : 0);
}
