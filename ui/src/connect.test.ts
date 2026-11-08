import { expect, test } from "vitest";
import { engineUrl } from "./connect";

test("the page host by default", () => {
  expect(engineUrl("127.0.0.1:8780", null)).toBe("ws://127.0.0.1:8780/ws");
});

test("?engine= overrides with a host:port, and only that", () => {
  expect(engineUrl("localhost:5173", "127.0.0.1:8780")).toBe("ws://127.0.0.1:8780/ws");
  expect(engineUrl("localhost:5173", "evil.example/x?")).toBe("ws://localhost:5173/ws");
});
