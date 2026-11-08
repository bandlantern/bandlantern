// WebGL2 waterfall. Thin GL layer; logic lives in waterfallMath.ts.
//
// Texture choice: R32F, sampled with NEAREST (or LINEAR across bins when the browser exposes
// OES_texture_float_linear). Creating, uploading (texSubImage2D) and sampling R32F textures is
// core WebGL2; EXT_color_buffer_float is only needed to RENDER into float textures, which we do
// not. So the R8 + CPU dB->byte fallback is not needed and not implemented; the dB->colour
// mapping happens on the GPU through a 256-entry palette texture.
// Refs: WebGL2 spec 3.7.6 (texImage/texSubImage), OpenGL ES 3.0 spec table 3.12 (R32F),
// GLSL ES 3.00 spec (gl_VertexID, texelFetch, int %).
//
// Mixed rows (ADR-017): every ring row is resampled (max-hold) into TEXTURE_WIDTH texels that
// span the shown part of that row, and a second texture holds that row's lower and upper
// frequency edge (rowLayout.ts). The shader finds each pixel's frequency in the view and looks
// it up in each row's own range, so rows made for different views share the screen; outside a
// row's range the empty colour (palette bottom) shows.

import { COLORMAPS } from "./colormaps";
import { RowLayouts } from "./rowLayout";
import { resampleMax } from "./spectrumMath";
import type { RowGeometry } from "./rowGeometry";
import { advanceHead, backingSize, buildPalette, type ColorStops } from "./waterfallMath";

const RING_HEIGHT = 2048; // rows kept; <= MAX_TEXTURE_SIZE on every WebGL2 device (min 2048)
const EMPTY_DBFS = -200; // frames.md: dB floor
const TEXTURE_WIDTH = 4096; // texels per row (rows of any bin_count are resampled to it)
const MAX_TAPS = 8; // texels max-held per pixel when a pixel covers several

const VERTEX_SHADER = `#version 300 es
out vec2 v_uv; // (0,0) top-left
void main() {
  vec2 p = vec2((gl_VertexID == 1) ? 3.0 : -1.0, (gl_VertexID == 2) ? 3.0 : -1.0);
  gl_Position = vec4(p, 0.0, 1.0);
  v_uv = vec2(p.x * 0.5 + 0.5, 0.5 - p.y * 0.5);
}`;

const FRAGMENT_SHADER = `#version 300 es
precision highp float;
precision highp int;
precision highp sampler2D;
uniform sampler2D u_rows;
uniform sampler2D u_layout; // per ring row: (lower, upper) edge, Hz from the reference
uniform sampler2D u_palette;
uniform int u_head;     // ring row the next incoming row goes to
uniform int u_height;   // ring rows
uniform int u_visible;  // rows shown
uniform float u_rows_on_screen; // canvas height / device pixels per row
uniform float u_view_lo;   // view start, Hz from the reference
uniform float u_view_span; // Hz
uniform float u_width_px;  // device pixels across
uniform float u_texels;    // texels per row
uniform float u_min_db;
uniform float u_max_db;
in vec2 v_uv;
out vec4 o_color;
void main() {
  int age = min(int(v_uv.y * u_rows_on_screen), u_visible - 1);
  int row = (u_head - 1 - age + u_height) % u_height; // same as ringRowForAge()
  vec2 edges = texelFetch(u_layout, ivec2(0, row), 0).rg;
  float row_span = edges.g - edges.r;
  float pixel_hz = u_view_span / u_width_px;
  float freq = u_view_lo + v_uv.x * u_view_span;
  float db = u_min_db; // the empty colour
  if (row_span > 0.0) {
    // Several texels under one pixel: max-hold a few of them so a narrow carrier survives.
    int taps = clamp(int(ceil(pixel_hz / (row_span / u_texels))), 1, ${MAX_TAPS});
    float row_y = (float(row) + 0.5) / float(u_height);
    bool seen = false;
    float peak = u_min_db;
    for (int k = 0; k < taps; k++) {
      float f = freq + pixel_hz * ((float(k) + 0.5) / float(taps) - 0.5); // same as rowTextureX()
      float x = (f - edges.r) / row_span;
      if (x < 0.0 || x > 1.0) continue;
      float v = texture(u_rows, vec2(x, row_y)).r;
      peak = seen ? max(peak, v) : v;
      seen = true;
    }
    if (seen) db = peak;
  }
  float t = clamp((db - u_min_db) / (u_max_db - u_min_db), 0.0, 1.0);
  o_color = texelFetch(u_palette, ivec2(int(t * 255.0 + 0.5), 0), 0); // same as dbToPaletteIndex()
}`;

function compile(gl: WebGL2RenderingContext, type: number, source: string): WebGLShader {
  const shader = gl.createShader(type);
  if (!shader) {
    throw new Error("createShader failed");
  }
  gl.shaderSource(shader, source);
  gl.compileShader(shader);
  if (!gl.getShaderParameter(shader, gl.COMPILE_STATUS)) {
    throw new Error("shader compile: " + gl.getShaderInfoLog(shader));
  }
  return shader;
}

export class Waterfall {
  private gl: WebGL2RenderingContext;
  private program: WebGLProgram;
  private rows_tex: WebGLTexture;
  private layout_tex: WebGLTexture;
  private palette_tex: WebGLTexture;
  private vao: WebGLVertexArrayObject;
  private u_head: WebGLUniformLocation;
  private u_height: WebGLUniformLocation;
  private u_visible: WebGLUniformLocation;
  private u_rows_on_screen: WebGLUniformLocation;
  private px_per_row = 1; // device pixels per row: the time zoom
  private u_view_lo: WebGLUniformLocation;
  private u_view_span: WebGLUniformLocation;
  private u_width_px: WebGLUniformLocation;
  private u_min_db: WebGLUniformLocation;
  private u_max_db: WebGLUniformLocation;
  private linear_ok: boolean;

  private texture_width: number;
  private allocated = false;
  private layouts = new RowLayouts(RING_HEIGHT);
  private scratch: Float32Array;
  private view_lo_hz = 0;
  private view_hi_hz = 1;
  private head = 0;
  private min_db = -110;
  private max_db = -20;
  /** True when new data or a resize/range change needs a draw. */
  dirty = true;

  constructor(private canvas: HTMLCanvasElement) {
    const gl = canvas.getContext("webgl2", { antialias: false, alpha: false });
    if (!gl) {
      throw new Error("WebGL2 not available");
    }
    this.gl = gl;
    this.linear_ok = gl.getExtension("OES_texture_float_linear") !== null;
    this.texture_width = Math.min(TEXTURE_WIDTH, gl.getParameter(gl.MAX_TEXTURE_SIZE));
    this.scratch = new Float32Array(this.texture_width);

    const program = gl.createProgram();
    if (!program) {
      throw new Error("createProgram failed");
    }
    gl.attachShader(program, compile(gl, gl.VERTEX_SHADER, VERTEX_SHADER));
    gl.attachShader(program, compile(gl, gl.FRAGMENT_SHADER, FRAGMENT_SHADER));
    gl.linkProgram(program);
    if (!gl.getProgramParameter(program, gl.LINK_STATUS)) {
      throw new Error("program link: " + gl.getProgramInfoLog(program));
    }
    this.program = program;
    this.u_head = this.uniform("u_head");
    this.u_height = this.uniform("u_height");
    this.u_visible = this.uniform("u_visible");
    this.u_rows_on_screen = this.uniform("u_rows_on_screen");
    this.u_view_lo = this.uniform("u_view_lo");
    this.u_view_span = this.uniform("u_view_span");
    this.u_width_px = this.uniform("u_width_px");
    this.u_min_db = this.uniform("u_min_db");
    this.u_max_db = this.uniform("u_max_db");
    gl.useProgram(program);
    gl.uniform1i(this.uniform("u_rows"), 0);
    gl.uniform1i(this.uniform("u_palette"), 1);
    gl.uniform1i(this.uniform("u_layout"), 2);
    gl.uniform1f(this.uniform("u_texels"), this.texture_width);

    this.vao = gl.createVertexArray();
    this.rows_tex = gl.createTexture();
    this.layout_tex = gl.createTexture();
    this.palette_tex = gl.createTexture();

    gl.bindTexture(gl.TEXTURE_2D, this.palette_tex);
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA8, 256, 1, 0, gl.RGBA, gl.UNSIGNED_BYTE, buildPalette(COLORMAPS[0].stops));
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
  }

  setColormap(stops: ColorStops): void {
    const gl = this.gl;
    gl.bindTexture(gl.TEXTURE_2D, this.palette_tex);
    gl.texSubImage2D(gl.TEXTURE_2D, 0, 0, 0, 256, 1, gl.RGBA, gl.UNSIGNED_BYTE, buildPalette(stops));
    this.dirty = true;
  }

  /** Time zoom: each row drawn this many device pixels tall (1 = one row per pixel). */
  setPxPerRow(px_per_row: number): void {
    this.px_per_row = Math.max(1, px_per_row);
    this.dirty = true;
  }

  setRangeDb(min_db: number, max_db: number): void {
    this.min_db = min_db;
    this.max_db = max_db;
    this.dirty = true;
  }

  /** Call when the CSS size or devicePixelRatio changes. */
  resize(css_w: number, css_h: number, dpr: number): void {
    const size = backingSize(css_w, css_h, dpr);
    if (this.canvas.width !== size.w || this.canvas.height !== size.h) {
      this.canvas.width = size.w;
      this.canvas.height = size.h;
      // Resizing clears the canvas, and the browser can paint before the next animation frame,
      // which flashes black while dragging a splitter. Redraw now instead of waiting.
      this.render();
    }
  }

  /** The frequency range the waterfall shows, Hz. Moving far from the reference re-bases the layouts. */
  setView(lo_hz: number, hi_hz: number): void {
    this.view_lo_hz = lo_hz;
    this.view_hi_hz = hi_hz;
    if (this.layouts.needsRebase((lo_hz + hi_hz) / 2)) {
      this.layouts.rebase((lo_hz + hi_hz) / 2);
      this.uploadLayouts();
    }
    this.dirty = true;
  }

  /** Upload one row into the ring: resampled across the part of it that geom says to show. */
  pushRow(bins_dbfs: Float32Array, geom: RowGeometry): void {
    const gl = this.gl;
    if (!this.allocated) {
      this.allocate();
    }
    resampleMax(bins_dbfs, geom, geom.lo_hz, geom.hi_hz, this.scratch);
    for (let i = 0; i < this.scratch.length; i++) {
      if (Number.isNaN(this.scratch[i])) this.scratch[i] = EMPTY_DBFS;
    }
    gl.bindTexture(gl.TEXTURE_2D, this.rows_tex);
    gl.texSubImage2D(gl.TEXTURE_2D, 0, 0, this.head, this.texture_width, 1, gl.RED, gl.FLOAT, this.scratch);
    this.layouts.set(this.head, geom.lo_hz, geom.hi_hz);
    gl.bindTexture(gl.TEXTURE_2D, this.layout_tex);
    gl.texSubImage2D(gl.TEXTURE_2D, 0, 0, this.head, 1, 1, gl.RG, gl.FLOAT, this.layouts.offsets, this.head * 2);
    this.head = advanceHead(this.head, RING_HEIGHT);
    this.dirty = true;
  }

  render(): void {
    const gl = this.gl;
    this.dirty = false;
    gl.viewport(0, 0, this.canvas.width, this.canvas.height);
    if (!this.allocated) {
      gl.clearColor(0, 0, 0.06, 1);
      gl.clear(gl.COLOR_BUFFER_BIT);
      return;
    }
    gl.useProgram(this.program);
    gl.bindVertexArray(this.vao);
    gl.activeTexture(gl.TEXTURE0);
    gl.bindTexture(gl.TEXTURE_2D, this.rows_tex);
    gl.activeTexture(gl.TEXTURE1);
    gl.bindTexture(gl.TEXTURE_2D, this.palette_tex);
    gl.activeTexture(gl.TEXTURE2);
    gl.bindTexture(gl.TEXTURE_2D, this.layout_tex);
    gl.uniform1i(this.u_head, this.head);
    gl.uniform1i(this.u_height, RING_HEIGHT);
    const rows_on_screen = this.canvas.height / this.px_per_row;
    gl.uniform1i(this.u_visible, Math.min(RING_HEIGHT, Math.ceil(rows_on_screen)));
    gl.uniform1f(this.u_rows_on_screen, rows_on_screen);
    gl.uniform1f(this.u_view_lo, this.view_lo_hz - this.layouts.ref_hz);
    gl.uniform1f(this.u_view_span, this.view_hi_hz - this.view_lo_hz);
    gl.uniform1f(this.u_width_px, this.canvas.width);
    gl.uniform1f(this.u_min_db, this.min_db);
    gl.uniform1f(this.u_max_db, this.max_db);
    gl.drawArrays(gl.TRIANGLES, 0, 3);
  }

  private uniform(name: string): WebGLUniformLocation {
    const location = this.gl.getUniformLocation(this.program, name);
    if (!location) {
      throw new Error("missing uniform " + name);
    }
    return location;
  }

  private uploadLayouts(): void {
    const gl = this.gl;
    gl.bindTexture(gl.TEXTURE_2D, this.layout_tex);
    gl.texSubImage2D(gl.TEXTURE_2D, 0, 0, 0, 1, RING_HEIGHT, gl.RG, gl.FLOAT, this.layouts.offsets);
  }

  private allocate(): void {
    const gl = this.gl;
    // Rows never written have an empty layout range, so the texture needs no initial data.
    gl.bindTexture(gl.TEXTURE_2D, this.rows_tex);
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.R32F, this.texture_width, RING_HEIGHT, 0, gl.RED, gl.FLOAT, null);
    const filter = this.linear_ok ? gl.LINEAR : gl.NEAREST;
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, filter);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, filter);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
    gl.bindTexture(gl.TEXTURE_2D, this.layout_tex);
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.RG32F, 1, RING_HEIGHT, 0, gl.RG, gl.FLOAT, null);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
    this.allocated = true;
    this.head = 0;
    this.uploadLayouts();
  }
}
