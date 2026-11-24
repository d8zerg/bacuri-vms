declare module "*/archive/player.js" {
  export class ArchivePlayer {
    constructor(
      video: HTMLVideoElement,
      session: string,
      manifest: import("../../../../build/contracts/api").components["schemas"]["PlaybackManifest"],
      onStatus: (message: string) => void,
    );
    readonly origin: number;
    seek(utc: string): Promise<boolean>;
    stop(): void;
  }
}
