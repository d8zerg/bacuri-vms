// Definitions shared by the archive search and the cases of the investigations section.
import type {
  CaseDraft,
  Point,
  Services,
  VideoSearch,
} from "../../contracts/workspace";
import { locale } from "../../i18n";

export type Props = {
  site: string;
  services: Services;
  points: Point[];
  user: string;
};
export type Material = CaseDraft["materials"][number];
export type Region = NonNullable<VideoSearch["region"]>;
export const iso = (ms: number) => new Date(ms).toISOString();
// Time of day with milliseconds in the interface language.
export const clock = (value: string) =>
  new Date(value).toLocaleTimeString(locale(), {
    hour12: false,
    hour: "2-digit",
    minute: "2-digit",
    second: "2-digit",
    fractionalSecondDigits: 3,
  });
export const entire: Region = { left: 0, top: 0, right: 1, bottom: 1 };
