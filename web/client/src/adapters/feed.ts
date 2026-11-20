import type { AlertItem, Feed } from "../contracts/workspace";
import { api, data } from "./api";
type Frame =
  | { type: "upsert"; cursor: string; alert: AlertItem }
  | { type: "remove"; cursor: string; alert_id: string }
  | { type: "resync_required" };
export function subscribe(siteId: string, publish: (feed: Feed) => void) {
  const ctl = new AbortController();
  let items: AlertItem[] = [],
    cursor: string | null = null,
    socket: WebSocket | undefined,
    timer: ReturnType<typeof setTimeout>;
  function reset() {
    cursor = null;
    items = [];
  }
  const emit = (status: Feed["status"]) => publish({ items, status });
  function reconnect() {
    timer = setTimeout(() => void connect(), 500);
  }
  async function connect() {
    try {
      if (!cursor) {
        const snap = data(
          await api.GET("/sites/{siteId}/alerts", {
            params: { path: { siteId } },
            signal: ctl.signal,
          }),
        );
        if (ctl.signal.aborted) return;
        items = snap.items.slice().reverse();
        cursor = snap.cursor;
      }
      const url = new URL(
        "/api/v1/sites/" + encodeURIComponent(siteId) + "/alerts/stream",
        location.href,
      );
      url.protocol = "wss:";
      const ws = new WebSocket(url, "bacuri.vms.alerts.v1");
      socket = ws;
      let opened = false;
      ws.onopen = () => {
        if (ctl.signal.aborted) return;
        opened = true;
        ws.send(JSON.stringify({ type: "subscribe", after: cursor }));
        emit("online");
      };
      ws.onmessage = (event: MessageEvent<string>) => {
        if (ctl.signal.aborted || !cursor) return;
        try {
          const msg = JSON.parse(event.data) as Frame;
          if (msg.type === "resync_required") {
            reset();
            emit("reconnecting");
            ws.close();
            return;
          }
          if (msg.type === "remove")
            items = items.filter((x) => x.id !== msg.alert_id);
          else if (msg.type === "upsert")
            items = [
              msg.alert,
              ...items.filter((x) => x.id !== msg.alert.id),
            ].slice(0, 100);
          else throw Error("Unexpected alert frame");
          cursor = msg.cursor;
          emit("online");
        } catch {
          reset();
          emit("reconnecting");
          ws.close();
        }
      };
      ws.onclose = ({ code }) => {
        if (ctl.signal.aborted) return;
        if (!opened || [1008, 1011, 1013].includes(code)) reset();
        emit(code === 1008 ? "denied" : "reconnecting");
        reconnect();
      };
    } catch (e) {
      if (ctl.signal.aborted) return;
      reset();
      emit(
        e instanceof Error && ["401", "403", "404"].includes(e.message)
          ? "denied"
          : "reconnecting",
      );
      reconnect();
    }
  }
  void connect();
  return () => {
    ctl.abort();
    clearTimeout(timer);
    socket?.close();
  };
}
