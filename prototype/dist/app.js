const byId = (id) => document.getElementById(id);
const asset = "assets/yard.webm";
const liveAsset = "assets/yard-live.webm";
const camera = "Погрузочные ворота 02";
const fixtureUrls = {
  main: "rtsp://127.0.0.1:8554/main",
  sub: "rtsp://127.0.0.1:8554/sub",
};

const alerts = [
  {
    time: 82,
    name: "Движение у ворот",
    priority: "Высокая",
    tone: "danger",
    detail: "Датчик зафиксировал движение в зоне погрузки. Доступен фрагмент записи.",
  },
  {
    time: 132,
    name: "Потеря видеопотока",
    priority: "Средняя",
    tone: "warning",
    detail: "Камера потеряла связь. Проверьте интервал на шкале архива.",
  },
  {
    time: 236,
    name: "Смещение часов камеры",
    priority: "Средняя",
    tone: "warning",
    detail: "Запись доступна. Время камеры определено с погрешностью ±8 с.",
  },
  {
    time: 301,
    name: "Поток восстановлен",
    priority: "Информация",
    tone: "neutral",
    detail: "Камера снова передаёт видео. Последняя запись добавляется в каталог.",
  },
];

const archiveIntervals = [
  { end: 115, state: "recorded" },
  { end: 160, state: "gap" },
  { end: 218, state: "recorded" },
  { end: 260, state: "estimated" },
  { end: 360, state: "pending" },
];

const notices = {
  offline: {
    label: "Связь потеряна",
    tone: "danger",
    detail: "Подключитесь к площадке для продолжения просмотра.",
    action: "Повторить подключение",
  },
  gap: {
    label: "В этом интервале нет записи",
    tone: "danger",
    detail: "Камера потеряла связь с площадкой между 14:21:55 и 14:22:40.",
  },
  pending: {
    label: "Запись добавляется в каталог",
    tone: "warning",
    detail: "Сегмент сохранён. Проверьте доступность через несколько секунд.",
    action: "Проверить снова",
  },
  media: {
    label: "Видео недоступно",
    tone: "danger",
    detail: "Не удалось загрузить тестовый фрагмент.",
    action: "Повторить загрузку",
  },
};

const monitor = byId("monitor-screen");
const installer = byId("install-screen");
const monitorVideo = byId("monitor-video");
const installVideo = byId("install-video");
const cursor = byId("timeline-cursor");
const condition = byId("network-condition");
const mediaRetry = byId("media-retry");
const playToggle = byId("play-toggle");
const alertList = byId("alert-list");

let online = true;
let mode = "live";
let selectedAlert = 0;
let pendingResolved = false;
let mediaFailed = false;
let mediaBlocked = false;
let clipStart = 0;
let resumeAfterLoss = true;

function formatClock(seconds) {
  const minutes = 20 + Math.floor(seconds / 60);
  const remainder = String(seconds % 60).padStart(2, "0");
  return `14:${minutes}:${remainder}`;
}

function indicator(id, text, tone) {
  const element = byId(id);
  element.textContent = text;
  element.dataset.tone = tone;
}

function selectedInterval() {
  return archiveIntervals.find(({ end }) => Number(cursor.value) < end);
}

function archiveState() {
  const { state } = selectedInterval();
  return state === "pending" && pendingResolved ? "recorded" : state;
}

function monitorNotice() {
  if (!online) return notices.offline;
  if (mode === "archive" && notices[archiveState()]) return notices[archiveState()];
  return mediaFailed ? notices.media : null;
}

function updateClock() {
  const seconds = mode === "live" ? 360 + Math.floor(monitorVideo.currentTime) : Number(cursor.value);
  byId("video-clock").textContent = formatClock(seconds);
  cursor.setAttribute("aria-valuetext", `${formatClock(Number(cursor.value))}, UTC+3`);
}

function updatePlayControl() {
  const playing = !monitorVideo.paused && !monitorVideo.ended;
  const label = playing ? "Пауза" : "Воспроизвести";
  byId("play-label").textContent = label;
  playToggle.setAttribute("aria-label", playing ? "Приостановить видео" : "Воспроизвести видео");
  playToggle.dataset.playing = String(playing);
  playToggle.disabled = mediaBlocked;
}

function syncMonitor() {
  const notice = monitorNotice();
  mediaBlocked = Boolean(notice);
  if (mediaBlocked) monitorVideo.pause();

  byId("show-live").setAttribute("aria-pressed", String(mode === "live"));
  byId("show-archive").setAttribute("aria-pressed", String(mode === "archive"));
  byId("camera-profile").textContent = mode === "live"
    ? "Дополнительный поток · 640 × 360"
    : "Основной поток · 1280 × 720";
  byId("back-to-alert").hidden = mode === "live";
  byId("media-message").hidden = !notice;

  if (notice) {
    byId("media-message-title").textContent = notice.label;
    byId("media-message-detail").textContent = notice.detail;
    mediaRetry.hidden = !notice.action;
    mediaRetry.textContent = notice.action ?? "";
    indicator("view-tag", !online ? "Нет связи" : "Архив", notice.tone);
    indicator("play-state", notice.label, notice.tone);
  } else if (mode === "archive" && archiveState() === "estimated") {
    indicator("view-tag", "Архив", "warning");
    indicator("play-state", "Время оценочное ±8 с", "warning");
  } else {
    indicator("view-tag", mode === "live" ? "Прямой эфир" : "Архив", "success");
    indicator("play-state", mode === "live" ? "Поток доступен" : "Запись подтверждена", "success");
  }

  updatePlayControl();
  updateClock();
}

function playMonitor() {
  if (mediaBlocked || monitor.hidden) return;
  monitorVideo.play().catch((error) => {
    if (error.name !== "AbortError" && !mediaBlocked && !monitor.hidden) {
      indicator("play-state", "Нажмите «Воспроизвести»", "warning");
    }
  });
}

function setMonitorAsset(source) {
  if (monitorVideo.getAttribute("src") === source) return;
  mediaFailed = false;
  monitorVideo.src = source;
}

function seekArchive(seconds) {
  mode = "archive";
  clipStart = seconds;
  cursor.value = seconds;
  monitorVideo.loop = false;
  setMonitorAsset(asset);
  if (monitorVideo.readyState > 0) monitorVideo.currentTime = 0;
  syncMonitor();
  playMonitor();
}

function showLive() {
  mode = "live";
  cursor.value = cursor.max;
  monitorVideo.loop = true;
  setMonitorAsset(liveAsset);
  syncMonitor();
  playMonitor();
}

function switchScreen(showInstaller) {
  monitor.hidden = showInstaller;
  installer.hidden = !showInstaller;

  const current = byId(showInstaller ? "show-install" : "show-monitor");
  const other = byId(showInstaller ? "show-monitor" : "show-install");
  current.setAttribute("aria-current", "page");
  other.removeAttribute("aria-current");

  if (showInstaller) {
    monitorVideo.pause();
  } else {
    installVideo.pause();
    if (mode === "live") playMonitor();
  }
  byId(showInstaller ? "install-heading" : "monitor-heading").focus();
}

const alertCards = alerts.map((alert, index) => {
  const listItem = byId("alert-template").content.firstElementChild.cloneNode(true);
  const card = listItem.querySelector("button");
  const priority = card.querySelector(".alert-priority");
  priority.textContent = alert.priority;
  priority.dataset.tone = alert.tone;
  card.querySelector(".alert-clock").textContent = formatClock(alert.time);
  card.querySelector(".alert-name").textContent = alert.name;
  card.querySelector(".alert-camera").textContent = camera;
  card.addEventListener("click", () => selectAlert(index));
  alertList.append(listItem);
  return card;
});

function selectAlert(index) {
  const alert = alerts[index];
  selectedAlert = index;
  alertCards.forEach((card, cardIndex) => card.setAttribute("aria-pressed", String(cardIndex === index)));
  byId("alert-time").textContent = `${formatClock(alert.time)} · UTC+3`;
  byId("alert-title").textContent = alert.name;
  byId("alert-detail").textContent = alert.detail;

  if (mode === "archive") seekArchive(alert.time);
}

function updateNetwork(nextOnline) {
  if (online && !nextOnline) {
    resumeAfterLoss = !monitorVideo.paused && !monitorVideo.ended;
  }
  online = nextOnline;
  condition.value = online ? "online" : "offline";
  indicator(
    "network-label",
    online ? "Локальная сеть доступна" : "Связь с площадкой потеряна",
    online ? "success" : "danger",
  );

  if (!online && ["checking", "connected", "recording"].includes(installPhase)) {
    cameraUnavailable();
  } else {
    renderInstaller();
  }

  syncMonitor();
  if (online && resumeAfterLoss) playMonitor();
}

byId("show-monitor").addEventListener("click", () => switchScreen(false));
byId("show-install").addEventListener("click", () => switchScreen(true));
condition.addEventListener("change", () => updateNetwork(condition.value === "online"));
byId("show-live").addEventListener("click", showLive);
byId("show-archive").addEventListener("click", () => seekArchive(alerts[selectedAlert].time));
byId("open-alert").addEventListener("click", () => {
  seekArchive(alerts[selectedAlert].time);
  byId("camera-heading").focus();
});
byId("back-to-alert").addEventListener("click", () => {
  showLive();
  alertCards[selectedAlert].focus();
});
cursor.addEventListener("input", () => seekArchive(Number(cursor.value)));
playToggle.addEventListener("click", () => {
  if (monitorVideo.paused) playMonitor();
  else monitorVideo.pause();
});

mediaRetry.addEventListener("click", () => {
  if (!online) {
    updateNetwork(true);
  } else if (mode === "archive" && archiveState() === "pending") {
    pendingResolved = true;
    byId("pending-segment").className = "recorded";
    seekArchive(Number(cursor.value));
  } else {
    mediaFailed = false;
    monitorVideo.load();
    syncMonitor();
    playMonitor();
  }
  byId("camera-heading").focus();
});

for (const event of ["play", "pause", "ended"]) {
  monitorVideo.addEventListener(event, updatePlayControl);
}
monitorVideo.addEventListener("playing", syncMonitor);
monitorVideo.addEventListener("error", () => {
  mediaFailed = true;
  syncMonitor();
});
monitorVideo.addEventListener("timeupdate", () => {
  if (mediaBlocked) return;
  if (mode === "archive") {
    const { end } = archiveIntervals.find((interval) => clipStart < interval.end);
    cursor.value = Math.min(clipStart + Math.floor(monitorVideo.currentTime), end - 1);
  }
  updateClock();
});

const cameraName = byId("camera-name");
const mainUrl = byId("main-url");
const subUrl = byId("sub-url");
const formMessage = byId("form-message");
const checkCamera = byId("check-camera");
const recordButton = byId("record-button");
const viewRecording = byId("view-recording");
const verifyRecording = byId("verify-recording");

const connectedPhases = ["connected", "recording", "recorded", "opening", "playing", "verified", "media-error"];
const recordedPhases = ["recorded", "opening", "playing", "verified", "media-error"];
const installMessages = {
  checking: ["Проверяем подключение", "Ожидаем ответ от тестовой камеры."],
  unreachable: ["Камера недоступна", "Восстановите тестовую связь и повторите проверку."],
  connected: ["Запись ещё не создана", "Оба потока доступны. Создайте тестовую запись."],
  recording: ["Идёт запись", "Ожидаем завершения тестового фрагмента."],
  recorded: ["Запись готова", "Откройте фрагмент и проверьте воспроизведение."],
  opening: ["Открываем запись", "Загружаем тестовый фрагмент."],
  "media-error": ["Запись не открылась", "Повторите воспроизведение тестового фрагмента."],
};

let installPhase = "idle";
let operationTimer;

function showFormMessage(text, invalidInput) {
  formMessage.textContent = text;
  formMessage.hidden = !text;
  for (const input of [cameraName, mainUrl, subUrl]) {
    if (input === invalidInput) {
      input.setAttribute("aria-invalid", "true");
      input.setAttribute("aria-describedby", "form-message");
    } else {
      input.removeAttribute("aria-invalid");
      input.removeAttribute("aria-describedby");
    }
  }
  invalidInput?.focus();
}

function renderInstaller() {
  const connected = connectedPhases.includes(installPhase);
  const recorded = recordedPhases.includes(installPhase);
  const played = ["playing", "verified"].includes(installPhase);
  const verified = installPhase === "verified";
  const unreachable = installPhase === "unreachable" || (!online && connected);
  const [title, detail] = installMessages[installPhase] ?? ["Запись ещё не создана", "Проверьте подключение обоих потоков."];

  byId("preview-camera").textContent = cameraName.value.trim() || camera;
  byId("install-message").hidden = played;
  byId("install-message-title").textContent = title;
  byId("install-message-detail").textContent = detail;
  checkCamera.setAttribute("aria-disabled", String(installPhase === "checking" || installPhase === "recording"));
  checkCamera.textContent = installPhase === "unreachable" ? "Повторить проверку" : "Проверить подключение";
  recordButton.hidden = recorded;
  recordButton.disabled = !["connected", "recording"].includes(installPhase) || !online;
  recordButton.setAttribute("aria-disabled", String(installPhase === "recording"));
  viewRecording.hidden = !recorded;
  viewRecording.disabled = !recorded;
  viewRecording.classList.toggle("primary", !played);
  viewRecording.setAttribute("aria-disabled", String(installPhase === "opening"));
  verifyRecording.hidden = installPhase !== "playing";
  verifyRecording.disabled = installPhase !== "playing";

  const untested = "Ожидает проверки";
  const connectionText = unreachable ? "Нет связи" : connected ? "Камера доступна" : installPhase === "checking" ? "Идёт проверка" : untested;
  const connectionTone = unreachable ? "danger" : connected ? "success" : "neutral";
  indicator("camera-connection", connectionText, connectionTone);
  indicator("main-state", connected ? "H.264 · 1280 × 720" : untested, connected ? "success" : "neutral");
  indicator("sub-state", connected ? "H.264 · 640 × 360" : untested, connected ? "success" : "neutral");
  indicator("record-state", recorded ? "Запись сохранена" : installPhase === "recording" ? "Идёт запись" : untested, recorded ? "success" : "neutral");
  indicator("playback-state", verified ? "Подтверждено" : played ? "Кадр воспроизводится" : untested, played ? "success" : "neutral");
  indicator("install-feedback", verified ? "Воспроизведение тестовой записи подтверждено." : "", verified ? "success" : "neutral");
}

function setInstallPhase(phase) {
  installPhase = phase;
  renderInstaller();
}

function resetInstallation() {
  clearTimeout(operationTimer);
  installVideo.pause();
  if (installVideo.hasAttribute("src")) {
    installVideo.removeAttribute("src");
    installVideo.load();
  }
  showFormMessage("");
  setInstallPhase("idle");
}

function cameraUnavailable() {
  clearTimeout(operationTimer);
  showFormMessage("Нет ответа от тестовой камеры. Восстановите тестовую связь и повторите проверку.");
  setInstallPhase("unreachable");
}

function validateFixture(input, path) {
  let url;
  try {
    url = new URL(input.value.trim());
  } catch {
    return "Введите RTSP-адрес потока.";
  }
  if (url.protocol !== "rtsp:") return "Адрес потока должен начинаться с rtsp://.";
  if (url.username || url.password) return "Удалите учётные данные из тестового адреса.";
  const fixtureHost = ["127.0.0.1", "localhost", "rtsp-camera"].includes(url.hostname);
  if (!fixtureHost || url.pathname !== path || !url.port || url.search || url.hash) {
    return `Введите адрес тестового потока ${path} или подставьте тестовые адреса.`;
  }
  return "";
}

for (const input of [cameraName, mainUrl, subUrl]) {
  input.addEventListener("input", resetInstallation);
}

byId("fill-fixture").addEventListener("click", () => {
  mainUrl.value = fixtureUrls.main;
  subUrl.value = fixtureUrls.sub;
  resetInstallation();
  checkCamera.focus();
});

byId("camera-form").addEventListener("submit", (event) => {
  event.preventDefault();
  if (["checking", "recording"].includes(installPhase)) return;
  resetInstallation();

  if (!cameraName.value.trim()) {
    showFormMessage("Введите название камеры.", cameraName);
    return;
  }

  for (const [input, path] of [[mainUrl, "/main"], [subUrl, "/sub"]]) {
    const error = validateFixture(input, path);
    if (error) {
      showFormMessage(error, input);
      return;
    }
  }

  setInstallPhase("checking");
  operationTimer = setTimeout(() => {
    if (!online) {
      cameraUnavailable();
      return;
    }
    const advanceFocus = document.activeElement === checkCamera;
    setInstallPhase("connected");
    if (advanceFocus) recordButton.focus();
  }, 600);
});

recordButton.addEventListener("click", () => {
  if (installPhase !== "connected") return;
  setInstallPhase("recording");
  operationTimer = setTimeout(() => {
    const advanceFocus = document.activeElement === recordButton;
    setInstallPhase("recorded");
    if (advanceFocus) viewRecording.focus();
  }, 850);
});

viewRecording.addEventListener("click", () => {
  if (installPhase === "opening") return;
  const wasVerified = installPhase === "verified";
  if (!wasVerified) setInstallPhase("opening");
  if (installVideo.getAttribute("src") !== asset || installVideo.error) installVideo.src = asset;
  if (installVideo.readyState > 0) installVideo.currentTime = 0;
  installVideo.play().catch(() => {
    if (["opening", "playing", "verified"].includes(installPhase)) setInstallPhase("media-error");
  });
});

installVideo.addEventListener("playing", () => {
  if (installPhase !== "opening") return;
  const advanceFocus = document.activeElement === viewRecording;
  setInstallPhase("playing");
  if (advanceFocus) verifyRecording.focus();
});
verifyRecording.addEventListener("click", () => {
  setInstallPhase("verified");
  byId("install-feedback").focus();
});

byId("alert-count").textContent = alerts.length;
selectAlert(0);
syncMonitor();
renderInstaller();
