// Where the UI's WebSocket goes. Contract: docs/api/README.md "Transport": `GET /ws` on the
// engine's port. Served by the engine, the page host is the engine, so `ws://<page host>/ws`.
// Under the Vite dev server (another port) the dev proxy forwards /ws; `?engine=127.0.0.1:8780`
// goes straight to the engine instead (ui/README.md).

const ENGINE_HOST = /^[A-Za-z0-9.-]+:\d{1,5}$/;

export function engineUrl(page_host: string, engine_param: string | null): string {
  const host = engine_param !== null && ENGINE_HOST.test(engine_param) ? engine_param : page_host;
  return `ws://${host}/ws`;
}
