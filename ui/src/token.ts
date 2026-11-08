// Token bootstrap. Contract: docs/api/README.md "Bootstrap in a plain browser" steps 3-4.
// Pure functions: location/storage/history are passed in so this runs without a DOM.

export const TOKEN_STORAGE_KEY = "bandlantern.token";
export const MISSING_TOKEN_MESSAGE = "Open the link from the engine";

export interface StorageLike {
  getItem(key: string): string | null;
  setItem(key: string, value: string): void;
}

export interface HistoryLike {
  replaceState(data: unknown, unused: string, url?: string | URL | null): void;
}

export type TokenState = { kind: "ok"; token: string } | { kind: "missing"; message: string };

export function tokenFromHash(hash: string): string | null {
  const params = new URLSearchParams(hash.startsWith("#") ? hash.slice(1) : hash);
  const token = params.get("token");
  return token ? token : null;
}

/** Call before anything else runs. `path_and_search` is location.pathname + location.search. */
export function bootstrapToken(
  hash: string,
  path_and_search: string,
  storage: StorageLike,
  history: HistoryLike,
): TokenState {
  const fromHash = tokenFromHash(hash);
  if (fromHash) {
    storage.setItem(TOKEN_STORAGE_KEY, fromHash);
    history.replaceState(null, "", path_and_search);
    return { kind: "ok", token: fromHash };
  }
  const stored = storage.getItem(TOKEN_STORAGE_KEY);
  if (stored) {
    return { kind: "ok", token: stored };
  }
  return { kind: "missing", message: MISSING_TOKEN_MESSAGE };
}
