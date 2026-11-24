import type { components } from "../../../../build/contracts/api";
import type { CameraDraft, StreamCheck } from "../ui-kit/CameraConnectionForm";
export type { CameraDraft };
type Schema = components["schemas"];
export type Session = Schema["BrowserSession"];
export type AudioRequest = Schema["AudioRequest"];
export type CameraControls = Schema["CameraControls"];
export type PtzCommand = Schema["PtzCommand"];
export type Point = Schema["Point"];
export type AlertItem = Schema["Alert"];
export type Camera = Schema["Camera"];
export type DiscoveredCamera = Schema["DiscoveredCamera"];
export type CameraInspection = Schema["CameraInspection"];
export type EdgeArchive = Schema["EdgeArchive"];
export type BoundRecording = Schema["BoundRecording"];
export type Interval = Schema["UtcInterval"];
export type Entry = Schema["ArchiveEntry"];
export type AlertLink = Schema["AlertArchive"];
export type CaptureRule = Schema["RecordingRule"];
export type CapturePolicy = Schema["RecordingPolicy"];
export type CapturePolicies = Schema["RecordingPolicyList"];
export type CaptureStorage = Schema["RecordingStorage"];
export type ManualRecording = Schema["RecordingManualState"];
export type Layout = Schema["MonitorLayout"];
export type LayoutDraft = Schema["MonitorLayoutDraft"];
export type Quality = "auto" | "main" | "secondary";
export type Target =
  | {
      mode: "live";
      point: string;
      quality?: Quality;
      width?: number;
      requireSecondary?: boolean;
    }
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
export type PlanLink = Schema["PlanLink"];
export type PlanLinkPreview = Schema["PlanLinkPreview"];
export type PlanReference = Schema["PlanReference"];
export type FloorPlan = Schema["FloorPlan"];
export type MapScene = Schema["MapScene"];
export type MapManagement = Schema["MapManagement"];
export type MapSource = Schema["MapSourceState"];
export type MapMode = Schema["MapSelection"]["mode"];
export type Placement = Schema["PointPlacement"];
export type AlertWork = Schema["AlertWork"];
export type AlertCommand = Schema["AlertCommand"];
export type AuditEntry = Schema["AuditEntry"];
export type AuditFilter = Schema["AuditFilter"];
export type SearchTrack = Schema["SearchTrack"];
export type VideoRule = Schema["VideoRule"];
export type VideoSearch = Schema["ArchiveSearch"];
export type Investigation = Schema["Investigation"];
export type CaseDraft = Schema["InvestigationDraft"];
export type ExportOptions = {
  mode: "selection" | "original" | "derivative";
  reason: string;
  audio: boolean;
};
export interface Services {
  videoRule(
    site: string,
    point: string,
    signal: AbortSignal,
  ): Promise<Schema["RuleVersion"]>;
  saveRule(
    site: string,
    point: string,
    body: Schema["RuleChange"],
  ): Promise<Schema["RuleVersion"]>;
  simulateRule(
    site: string,
    point: string,
    body: Schema["RuleTrial"],
    signal: AbortSignal,
  ): Promise<Schema["TrialResult"]>;
  caseRecipients(
    site: string,
    materials: CaseDraft["materials"],
    signal: AbortSignal,
  ): Promise<{ id: string; name: string }[]>;
  exportCase(
    site: string,
    id: string,
    body: Schema["CaseExport"],
    signal: AbortSignal,
  ): Promise<Blob>;
  searchFrames(
    site: string,
    query: VideoSearch,
    signal: AbortSignal,
  ): Promise<SearchTrack[]>;
  cases(site: string, signal: AbortSignal): Promise<Investigation[]>;
  saveCase(
    site: string,
    id: string,
    draft: CaseDraft,
  ): Promise<Schema["SavedInvestigation"]>;

  capturePolicies(site: string, signal: AbortSignal): Promise<CapturePolicies>;
  captureGroups(
    site: string,
    signal: AbortSignal,
  ): Promise<{ id: string; name: string; points: string[] }[]>;
  saveCapture(
    site: string,
    id: string,
    body: Schema["RecordingPolicyDraft"],
  ): Promise<{ id: string; revision: string }>;
  deleteCapture(site: string, id: string, revision: string): Promise<void>;
  captureStorage(site: string, signal: AbortSignal): Promise<CaptureStorage>;
  setCaptureStorage(
    site: string,
    days: number,
    reserve: number,
    revision: string,
  ): Promise<CaptureStorage>;
  manualRecording(
    site: string,
    point: string,
    signal: AbortSignal,
  ): Promise<ManualRecording>;
  setManualRecording(
    site: string,
    point: string,
    active: boolean,
  ): Promise<ManualRecording>;
  layouts(site: string, signal: AbortSignal): Promise<Layout[]>;
  saveLayout(
    site: string,
    id: string,
    draft: LayoutDraft,
  ): Promise<{ id: string; revision: string }>;
  deleteLayout(site: string, id: string, revision: string): Promise<void>;
  exportFragment(
    site: string,
    point: string,
    interval: Interval,
    alert: string | null,
    signal: AbortSignal,
    options?: ExportOptions,
  ): Promise<Blob>;
  alertWork(
    site: string,
    alert: string,
    signal: AbortSignal,
  ): Promise<AlertWork>;
  alertCommand(
    site: string,
    alert: string,
    body: AlertCommand,
    key: string,
  ): Promise<Schema["AlertReceipt"]>;
  audit(
    site: string,
    body: AuditFilter,
    signal: AbortSignal,
  ): Promise<Schema["AuditPage"]>;
  delegate(
    site: string,
    body: Schema["IssueAlertDelegation"],
  ): Promise<Schema["IssuedAlertDelegation"]>;
  revokeDelegation(site: string, id: string): Promise<void>;
  previewPlan(site: string, body: PlanLink): Promise<PlanLinkPreview>;
  linkPlan(site: string, body: Schema["SavePlanLink"]): Promise<void>;
  maps(site: string, signal: AbortSignal): Promise<MapScene>;
  mapManagement(site: string, signal: AbortSignal): Promise<MapManagement>;
  mapSource(
    site: string,
    body: Schema["MapSourceRequest"],
  ): Promise<{ id: string }>;
  inspectMapSource(
    site: string,
    id: string,
    signal: AbortSignal,
  ): Promise<MapSource>;
  uploadMapChunk(
    site: string,
    id: string,
    offset: number,
    chunk: Blob,
    signal: AbortSignal,
  ): Promise<void>;
  pauseMapDownload(site: string, id: string, paused: boolean): Promise<void>;
  queueMapRegions(site: string, body: Schema["MapSelection"]): Promise<void>;
  calibrate(
    site: string,
    body: Schema["ViewCalibrationRequest"],
  ): Promise<void>;
  place(site: string, body: Schema["PointCoordinates"]): Promise<void>;
  addPlan(site: string, name: string, png: string): Promise<{ id: string }>;
  session(signal: AbortSignal): Promise<Session>;
  login(
    username: string,
    password: string,
    enrollment?: string,
  ): Promise<Session>;
  logout(): Promise<void>;
  points(site: string, signal: AbortSignal): Promise<Point[]>;
  audio(
    site: string,
    point: string,
    request: AudioRequest,
    signal?: AbortSignal,
  ): Promise<Schema["AudioResult"]>;
  cameraControls(
    site: string,
    point: string,
    signal: AbortSignal,
  ): Promise<CameraControls>;
  ptz(
    site: string,
    point: string,
    device: string,
    command: PtzCommand,
    target?: string,
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
  recoverGap(
    site: string,
    camera: string,
    recording: string,
    binding: string,
    interval: Interval,
    signal: AbortSignal,
  ): Promise<{
    blocks: number;
    duration_us: number;
    outcome: string;
    next: string | null;
  }>;
  edgeArchive(
    site: string,
    camera: string,
    signal: AbortSignal,
  ): Promise<EdgeArchive>;
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
    format?: (description: string) => void,
    renew?: () => void,
    archiveOrigin?: (
      utcSeconds: number,
      seek: (at: string) => Promise<number>,
    ) => void,
  ): Promise<void>;
}
