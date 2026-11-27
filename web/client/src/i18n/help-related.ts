// Tasks that can precede, follow or resolve the situation described by each guide.
export const relatedTopics: Record<string, string[]> = {
  "start-monitoring": ["camera-on-map", "review-alarm", "archive-playback"],
  "camera-on-map": ["start-monitoring", "map-placement", "map-regions"],
  "ptz-control": ["ptz-busy", "camera-on-map", "camera-audio"],
  "review-alarm": [
    "archive-playback",
    "investigate-export",
    "activity-journal",
  ],
  "investigate-export": [
    "compare-recordings",
    "export-fragment",
    "verify-export",
  ],
  "connect-camera": [
    "recording-missing",
    "recover-camera-archive",
    "apply-camera-batch",
  ],
  "access-privacy": ["users-and-groups", "privacy-masks", "sign-in"],
  "video-rules": ["recording-policy", "review-alarm", "archive-playback"],
  "recording-policy": [
    "manual-recording",
    "recording-missing",
    "site-diagnostics",
  ],
  "install-site": ["sign-in", "connect-camera", "update-restore"],
  "update-restore": ["site-diagnostics", "recording-missing", "install-site"],
  "recording-missing": [
    "recording-policy",
    "connect-camera",
    "recover-camera-archive",
  ],
  "ptz-busy": ["ptz-control", "access-privacy", "connection-lost"],
  "sign-in": ["users-and-groups", "connection-lost", "start-monitoring"],
  "connection-lost": ["site-diagnostics", "sign-in", "recover-camera-archive"],
  "camera-audio": ["start-monitoring", "access-privacy", "connect-camera"],
  "map-placement": ["camera-on-map", "map-regions", "ptz-control"],
  "map-regions": ["map-placement", "camera-on-map", "connection-lost"],
  "apply-camera-batch": [
    "connect-camera",
    "changes-not-saved",
    "recording-missing",
  ],
  "changes-not-saved": [
    "access-privacy",
    "connection-lost",
    "activity-journal",
  ],
  "archive-playback": [
    "compare-recordings",
    "export-fragment",
    "recording-missing",
  ],
  "compare-recordings": [
    "archive-playback",
    "investigate-export",
    "verify-export",
  ],
  "export-fragment": [
    "archive-playback",
    "investigate-export",
    "verify-export",
  ],
  "verify-export": ["export-fragment", "investigate-export", "privacy-masks"],
  "recover-camera-archive": [
    "recording-missing",
    "connect-camera",
    "archive-playback",
  ],
  "users-and-groups": ["access-privacy", "privacy-masks", "activity-journal"],
  "privacy-masks": ["access-privacy", "verify-export", "users-and-groups"],
  "site-diagnostics": [
    "connection-lost",
    "recording-missing",
    "update-restore",
  ],
  "manual-recording": [
    "recording-policy",
    "archive-playback",
    "recording-missing",
  ],
  "activity-journal": [
    "review-alarm",
    "users-and-groups",
    "investigate-export",
  ],
};
