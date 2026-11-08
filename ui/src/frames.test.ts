import { readFileSync } from "node:fs";
import { describe, expect, test } from "vitest";
import { encodeSpectrumFrame } from "./fakeStream";
import { parseFrame, type SpectrumRow } from "./frames";

const GOLDEN_PATH = new URL("../../testdata/api/spectrum_frame_v1.bin", import.meta.url);

function loadGolden(): ArrayBuffer {
  const bytes = readFileSync(GOLDEN_PATH);
  return bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.byteLength) as ArrayBuffer;
}

// frames.md "Golden vector"
const GOLDEN_ROW: SpectrumRow = {
  seq: 7,
  sample_index: 160000,
  center_freq_hz: 100100000,
  rate_sps: 2400000,
  bin_count: 8,
  bins_dbfs: new Float32Array([-200.0, -120.5, -90.25, -60.0, 0.0, -3.0103, -45.5, -110.75]),
};

function goodFrame(): Uint8Array {
  return new Uint8Array(loadGolden().slice(0));
}

function parseBytes(bytes: Uint8Array) {
  return parseFrame(bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.byteLength) as ArrayBuffer);
}

describe("parseFrame", () => {
  test("decodes the golden vector", () => {
    const result = parseFrame(loadGolden());
    expect(result.status).toBe("row");
    if (result.status !== "row") return;
    const row = result.row;
    expect(row.seq).toBe(7);
    expect(row.sample_index).toBe(160000);
    expect(row.center_freq_hz).toBe(100100000.0);
    expect(row.rate_sps).toBe(2400000.0);
    expect(row.bin_count).toBe(8);
    expect(Array.from(row.bins_dbfs)).toEqual(Array.from(GOLDEN_ROW.bins_dbfs));
    expect(row.bins_dbfs[5]).toBe(Math.fround(-3.0103));
  });

  test("rejects bad magic", () => {
    const b = goodFrame();
    b[0] = 0x58;
    expect(parseBytes(b).status).toBe("rejected");
  });

  test("rejects bad version", () => {
    const b = goodFrame();
    b[4] = 2;
    expect(parseBytes(b).status).toBe("rejected");
  });

  test("rejects header_len < 48", () => {
    const b = goodFrame();
    b[8] = 40;
    expect(parseBytes(b).status).toBe("rejected");
  });

  test("rejects length mismatch (truncated and extra byte)", () => {
    const b = goodFrame();
    expect(parseBytes(b.subarray(0, b.length - 1)).status).toBe("rejected");
    const longer = new Uint8Array(b.length + 1);
    longer.set(b);
    expect(parseBytes(longer).status).toBe("rejected");
  });

  test("rejects a frame shorter than the header", () => {
    expect(parseBytes(goodFrame().subarray(0, 20)).status).toBe("rejected");
  });

  test("ignores unknown kind", () => {
    const b = goodFrame();
    b[6] = 9;
    expect(parseBytes(b).status).toBe("ignored");
  });

  test("ignores unknown payload_format", () => {
    const b = goodFrame();
    b[10] = 7;
    expect(parseBytes(b).status).toBe("ignored");
  });

  test("header_len > 48 still finds the payload", () => {
    const golden = goodFrame();
    const extra = 16;
    const b = new Uint8Array(golden.length + extra);
    b.set(golden.subarray(0, 48), 0);
    b.fill(0xee, 48, 48 + extra); // appended header fields from a newer minor version
    b.set(golden.subarray(48), 48 + extra);
    b[8] = 48 + extra;
    const result = parseBytes(b);
    expect(result.status).toBe("row");
    if (result.status !== "row") return;
    expect(Array.from(result.row.bins_dbfs)).toEqual(Array.from(GOLDEN_ROW.bins_dbfs));
  });

  test("golden header fields are where frames.md says", () => {
    const v = new DataView(loadGolden());
    expect(v.getUint32(0, true)).toBe(0x00464c42); // "BLF\0"
    expect(v.getUint16(8, true)).toBe(48);
    expect(v.getUint32(12, true)).toBe(8);
  });
});

describe("encodeSpectrumFrame", () => {
  test("reproduces the golden bytes", () => {
    expect(Array.from(new Uint8Array(encodeSpectrumFrame(GOLDEN_ROW)))).toEqual(Array.from(goodFrame()));
  });

  test("round-trips through the parser", () => {
    const row: SpectrumRow = {
      seq: 123456789,
      sample_index: 2 ** 40,
      center_freq_hz: 433.92e6,
      rate_sps: 1.024e6,
      bin_count: 4,
      bins_dbfs: new Float32Array([-1.5, -200, 0, -99.25]),
    };
    const result = parseFrame(encodeSpectrumFrame(row));
    expect(result.status).toBe("row");
    if (result.status !== "row") return;
    expect(result.row).toEqual(row);
  });
});
