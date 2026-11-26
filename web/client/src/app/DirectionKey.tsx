import { useEffect, useRef, useState } from "react";
import type { RefObject } from "react";
import { IconButton } from "../ui-kit/FormControls";
import type { ControlIcon } from "../ui-kit/FormControls";
import type { PtzCommand } from "../contracts/workspace";
import { ptzLabel } from "./cameraCommands";
import { pause } from "../workflows/pause";

// A tap requests one bounded step. A held key issues the next step only after acknowledgement.
export function DirectionKey({
  command,
  icon,
  canStop,
  send,
  disabled,
  activeKey,
}: {
  command: PtzCommand;
  icon: ControlIcon;
  canStop: boolean;
  send: (
    cmd: PtzCommand,
    target?: string,
    refresh?: boolean,
  ) => Promise<boolean>;
  activeKey: RefObject<AbortController | null>;
  disabled: boolean;
}) {
  const gesture = useRef<AbortController | null>(null);
  const suppressClick = useRef(false);
  const [held, setHeld] = useState(false);
  function release() {
    gesture.current?.abort();
    gesture.current = null;
  }
  useEffect(() => {
    const hidden = () => {
      if (document.hidden) release();
    };
    window.addEventListener("blur", release);
    document.addEventListener("visibilitychange", hidden);
    return () => {
      window.removeEventListener("blur", release);
      document.removeEventListener("visibilitychange", hidden);
      release();
    };
  }, []);
  async function hold(controller: AbortController) {
    let moved = false;
    try {
      await pause(350, controller.signal);
      suppressClick.current = true;
      setHeld(true);
      do {
        moved = true;
        if (!(await send(command, undefined, false))) break;
        await pause(650, controller.signal);
      } while (!controller.signal.aborted);
    } catch (error) {
      if (!controller.signal.aborted) throw error;
    } finally {
      if (activeKey.current === controller) activeKey.current = null;
      if (moved) {
        await send("stop");
        setHeld(false);
      }
    }
  }
  return (
    <IconButton
      icon={icon}
      label={ptzLabel(command)}
      data-command={command}
      data-held={held}
      aria-disabled={disabled && !held}
      onPointerDown={(e) => {
        if (
          !e.isPrimary ||
          e.button !== 0 ||
          disabled ||
          gesture.current ||
          activeKey.current
        )
          return;
        suppressClick.current = false;
        if (!canStop) return;
        const controller = new AbortController();
        activeKey.current = controller;
        gesture.current = controller;
        e.currentTarget.setPointerCapture(e.pointerId);
        void hold(controller);
      }}
      onPointerUp={release}
      onPointerCancel={() => {
        suppressClick.current = true;
        release();
      }}
      onLostPointerCapture={release}
      onClick={(e) => {
        if (e.detail === 0 || !suppressClick.current) void send(command);
        suppressClick.current = false;
      }}
    />
  );
}
