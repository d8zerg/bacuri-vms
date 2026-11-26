// The states of a form that saves a change and may ask to confirm a destructive action first.
import { useReducer } from "react";

export type Submission =
  | { kind: "editing" }
  | { kind: "confirming" }
  | { kind: "saving" }
  | { kind: "failed"; message: string };

export type SubmissionEvent =
  | { type: "confirm" }
  | { type: "save" }
  | { type: "saved" }
  | { type: "fail"; message: string };

export function submission(
  state: Submission,
  event: SubmissionEvent,
): Submission {
  switch (event.type) {
    case "confirm":
      return state.kind === "saving" ? state : { kind: "confirming" };
    case "save":
      return state.kind === "saving" ? state : { kind: "saving" };
    case "saved":
      return { kind: "editing" };
    case "fail":
      return { kind: "failed", message: event.message };
  }
}

export function useSubmission() {
  const [state, dispatch] = useReducer(submission, { kind: "editing" });
  return {
    state,
    busy: state.kind === "saving",
    confirming: state.kind === "confirming",
    error: state.kind === "failed" ? state.message : "",
    // Runs the save once at a time and records its outcome.
    async run(action: () => Promise<void>, failure: string) {
      if (state.kind === "saving") return;
      dispatch({ type: "save" });
      try {
        await action();
        dispatch({ type: "saved" });
      } catch {
        dispatch({ type: "fail", message: failure });
      }
    },
    confirm: () => dispatch({ type: "confirm" }),
  };
}
