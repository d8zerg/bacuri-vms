import createClient from "openapi-fetch";
import type { paths } from "../../../../build/contracts/api";
export const api = createClient<paths>({
  baseUrl: "/api/v1",
  cache: "no-store",
  credentials: "same-origin",
});
export class HttpFailure extends Error {
  constructor(
    readonly status: number,
    readonly code: string,
  ) {
    super(String(status));
  }
}
export function data<T>(result: {
  data?: T;
  error?: { code: string } | undefined;
  response: Response;
}): T {
  if (!result.response.ok || result.data === undefined)
    throw new HttpFailure(result.response.status, result.error?.code ?? "");
  return result.data;
}
