import type { components } from "../../../../build/contracts/api";
import type { CameraDraft, StreamCheck } from "../ui-kit/CameraConnectionForm";
export type { CameraDraft };
type Schema = components["schemas"];
export type Session = Schema["BrowserSession"];
export type PtzCommand = Schema["PtzCommand"];
export type Point = Schema["Point"];
export type AlertItem = Schema["Alert"];
export type Camera = Schema["Camera"];
export type DiscoveredCamera = Schema["DiscoveredCamera"];
export type CameraInspection = Schema["CameraInspection"];
export type BoundRecording = Schema["BoundRecording"];
export type Interval = Schema["UtcInterval"];
export type Entry = Schema["ArchiveEntry"];
export type AlertLink = Schema["AlertArchive"];
export type Target =
  | { mode: "live"; point: string }
  | {
      mode: "archive";
      point: string;
      interval: Interval;
      position: string;
      binding_revision?: string;
    };
export type Feed = {
  items: AlertItem[];
  status: "loading" | "online" | "reconnecting" | "denied";
};
export interface Services {
  session(signal: AbortSignal): Promise<Session>;
  login(username: string, password: string): Promise<Session>;
  logout(): Promise<void>;
  points(site: string, signal: AbortSignal): Promise<Point[]>;
  ptz(
    site: string,
    point: string,
    device: string,
    command: PtzCommand,
  ): Promise<void>;
  discover(site: string, signal: AbortSignal): Promise<DiscoveredCamera[]>;
  inspect(
    site: string,
    selection: string,
    username: string,
    password: string,
    signal: AbortSignal,
  ): Promise<CameraInspection>;
  refreshOnvif(
    site: string,
    camera: Camera,
    draft: CameraDraft,
    signal: AbortSignal,
  ): Promise<Camera>;
  cameras(site: string, signal: AbortSignal): Promise<Camera[]>;
  previousRecording(
    site: string,
    camera: string,
    signal: AbortSignal,
  ): Promise<BoundRecording>;
  register(
    site: string,
    draft: CameraDraft,
    signal: AbortSignal,
    form: object,
  ): Promise<Camera>;
  record(
    site: string,
    point: string,
    signal: AbortSignal,
    binding?: string,
  ): Promise<Interval>;
  checkProfiles(
    site: string,
    camera: Camera,
    signal: AbortSignal,
  ): Promise<{ main: StreamCheck; secondary: StreamCheck }>;
  archive(
    this: void,
    site: string,
    point: string,
    window: Interval,
    signal: AbortSignal,
  ): Promise<Entry[]>;
  resolve(site: string, alert: string, signal: AbortSignal): Promise<AlertLink>;
  subscribe(site: string, publish: (feed: Feed) => void): () => void;
  attach(
    video: HTMLVideoElement,
    site: string,
    target: Target,
    signal: AbortSignal,
    failure: (message: string) => void,
  ): Promise<void>;
}
