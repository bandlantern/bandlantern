// Minimal typing for the one Node API the tests use (no @types/node dependency).
declare module "node:fs" {
  export function readFileSync(path: URL): Uint8Array;
}
