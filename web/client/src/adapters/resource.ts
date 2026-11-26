// One way for screens to read server data: a cached value per key, cancellation when the key changes or the
// screen closes, retries of transient failures and optional polling.
import { useCallback, useEffect, useRef, useState } from "react";
import { HttpFailure } from "./api";
import { errorCode, retryOf } from "./errors";

export type Resource<T> = {
  // The last value read for the key, also while a newer read is running.
  value: T | undefined;
  // The failure of the last read, cleared by the next successful read.
  error: unknown;
  loading: boolean;
  // Reads the key again and keeps the shown value until the new one arrives.
  reload: () => void;
  // Shows a value the server returned for the key, such as the result of a saved change.
  set: (value: T) => void;
};

export type ResourceOptions = {
  // Reads the key again at this period in milliseconds while the screen is open.
  interval?: number;
  // A read that takes longer fails with a timeout.
  timeout?: number;
  // Number of repeated attempts after a transient failure.
  retries?: number;
};

// Values are cached per scope, such as the services of one signed-in session, so a fixture or a new sign-in
// never sees the values of another.
const caches = new WeakMap<object, Map<string, unknown>>();

function cache(scope: object) {
  let entries = caches.get(scope);
  if (!entries) {
    entries = new Map();
    caches.set(scope, entries);
  }
  return entries;
}

export function transient(error: unknown) {
  return error instanceof HttpFailure
    ? retryOf(error) === "later"
    : errorCode(error) === "connection_lost";
}

function delay(ms: number, signal: AbortSignal) {
  return new Promise<void>((resolve, reject) => {
    const timer = setTimeout(resolve, ms);
    signal.addEventListener(
      "abort",
      () => {
        clearTimeout(timer);
        reject(
          signal.reason instanceof Error ? signal.reason : new Error("aborted"),
        );
      },
      { once: true },
    );
  });
}

export async function attempt<T>(
  load: (signal: AbortSignal) => Promise<T>,
  signal: AbortSignal,
  { timeout, retries = 2 }: ResourceOptions = {},
): Promise<T> {
  for (let tried = 0; ; tried++) {
    const limited = timeout
      ? AbortSignal.any([signal, AbortSignal.timeout(timeout)])
      : signal;
    try {
      return await load(limited);
    } catch (error) {
      signal.throwIfAborted();
      if (tried >= retries || !transient(error)) throw error;
      await delay(400 * 2 ** tried, signal);
    }
  }
}

// `key` names the data together with its parameters; null reads nothing. `load` must read exactly that key.
export function useResource<T>(
  scope: object,
  key: string | null,
  load: (signal: AbortSignal) => Promise<T>,
  options: ResourceOptions = {},
): Resource<T> {
  const entries = cache(scope);
  const [state, setState] = useState<{
    key: string | null;
    value: T | undefined;
    error: unknown;
    loading: boolean;
  }>(() => ({
    key,
    value: key === null ? undefined : (entries.get(key) as T | undefined),
    error: undefined,
    loading: key !== null,
  }));
  const [version, setVersion] = useState(0);
  const loader = useRef(load);
  loader.current = load;
  const { interval, timeout, retries } = options;
  useEffect(() => {
    if (key === null) return;
    const controller = new AbortController();
    let running = false;
    async function read() {
      if (running || key === null) return;
      running = true;
      setState((old) =>
        old.key === key && old.loading
          ? old
          : {
              key,
              value:
                old.key === key
                  ? old.value
                  : (entries.get(key) as T | undefined),
              error: old.key === key ? old.error : undefined,
              loading: true,
            },
      );
      try {
        const value = await attempt(loader.current, controller.signal, {
          ...(timeout ? { timeout } : {}),
          ...(retries !== undefined ? { retries } : {}),
        });
        entries.set(key, value);
        setState((old) => ({
          key,
          // An unchanged answer keeps the shown object, so polling does not render the screen again.
          value:
            old.key === key &&
            JSON.stringify(old.value) === JSON.stringify(value)
              ? old.value
              : value,
          error: undefined,
          loading: false,
        }));
      } catch (error) {
        if (!controller.signal.aborted)
          setState((old) => ({
            key,
            value: old.key === key ? old.value : undefined,
            error,
            loading: false,
          }));
      } finally {
        running = false;
      }
    }
    void read();
    const timer = interval ? setInterval(() => void read(), interval) : 0;
    return () => {
      controller.abort();
      if (timer) clearInterval(timer);
    };
  }, [entries, key, version, interval, timeout, retries]);
  const reload = useCallback(() => setVersion((v) => v + 1), []);
  const set = useCallback(
    (value: T) => {
      if (key === null) return;
      entries.set(key, value);
      setState({ key, value, error: undefined, loading: false });
    },
    [entries, key],
  );
  const current = state.key === key;
  return {
    value: current
      ? state.value
      : key === null
        ? undefined
        : (entries.get(key) as T | undefined),
    error: current ? state.error : undefined,
    loading: key !== null && (!current || state.loading),
    reload,
    set,
  };
}
