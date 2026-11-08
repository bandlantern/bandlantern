import { expect, test } from "vitest";
import { MISSING_TOKEN_MESSAGE, TOKEN_STORAGE_KEY, bootstrapToken, type StorageLike } from "./token";

function fakeStorage(initial: Record<string, string> = {}): StorageLike & { data: Record<string, string> } {
  const data = { ...initial };
  return {
    data,
    getItem: (k) => (k in data ? data[k] : null),
    setItem: (k, v) => {
      data[k] = v;
    },
  };
}

function fakeHistory() {
  const calls: Array<{ url: unknown }> = [];
  return {
    calls,
    replaceState: (_data: unknown, _unused: string, url?: string | URL | null) => {
      calls.push({ url });
    },
  };
}

test("fragment token is stored and the fragment is cleared", () => {
  const storage = fakeStorage();
  const history = fakeHistory();
  const state = bootstrapToken("#token=EXAMPLE_TOKEN", "/?fake=1", storage, history);
  expect(state).toEqual({ kind: "ok", token: "EXAMPLE_TOKEN" });
  expect(storage.data[TOKEN_STORAGE_KEY]).toBe("EXAMPLE_TOKEN");
  expect(TOKEN_STORAGE_KEY).toBe("bandlantern.token");
  expect(history.calls).toEqual([{ url: "/?fake=1" }]); // no '#'
});

test("reload with empty fragment uses storage", () => {
  const storage = fakeStorage({ "bandlantern.token": "stored" });
  const history = fakeHistory();
  expect(bootstrapToken("", "/", storage, history)).toEqual({ kind: "ok", token: "stored" });
  expect(history.calls).toEqual([]);
});

test("fragment token replaces a stored one", () => {
  const storage = fakeStorage({ "bandlantern.token": "old" });
  bootstrapToken("#token=new", "/", storage, fakeHistory());
  expect(storage.data["bandlantern.token"]).toBe("new");
});

test("no token anywhere gives the error state", () => {
  const state = bootstrapToken("", "/", fakeStorage(), fakeHistory());
  expect(state).toEqual({ kind: "missing", message: MISSING_TOKEN_MESSAGE });
  expect(MISSING_TOKEN_MESSAGE).toBe("Open the link from the engine");
});

test("fragment without a token key is not a token", () => {
  expect(bootstrapToken("#other=1", "/", fakeStorage(), fakeHistory()).kind).toBe("missing");
});
