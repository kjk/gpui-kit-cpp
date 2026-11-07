// Slim user32/gdi32 helpers for driving native windows from bun tests.
// Inspired by SumatraPDF tests/winapi.ts: post messages (SendInput is dropped
// on this machine), PrintWindow captures work for background windows.

import { dlopen, FFIType, JSCallback, ptr } from "bun:ffi";

const user32 = dlopen("user32.dll", {
  EnumWindows: { args: [FFIType.function, FFIType.i64], returns: FFIType.bool },
  GetClassNameW: { args: [FFIType.ptr, FFIType.ptr, FFIType.i32], returns: FFIType.i32 },
  GetWindowThreadProcessId: { args: [FFIType.ptr, FFIType.ptr], returns: FFIType.u32 },
  PostMessageW: { args: [FFIType.ptr, FFIType.u32, FFIType.i64, FFIType.i64], returns: FFIType.bool },
  SendMessageW: { args: [FFIType.ptr, FFIType.u32, FFIType.i64, FFIType.i64], returns: FFIType.i64 },
  MoveWindow: {
    args: [FFIType.ptr, FFIType.i32, FFIType.i32, FFIType.i32, FFIType.i32, FFIType.bool],
    returns: FFIType.bool,
  },
  GetClientRect: { args: [FFIType.ptr, FFIType.ptr], returns: FFIType.bool },
  GetWindowRect: { args: [FFIType.ptr, FFIType.ptr], returns: FFIType.bool },
  GetWindowTextW: { args: [FFIType.ptr, FFIType.ptr, FFIType.i32], returns: FFIType.i32 },
  IsWindowVisible: { args: [FFIType.ptr], returns: FFIType.bool },
  ShowWindow: { args: [FFIType.ptr, FFIType.i32], returns: FFIType.bool },
  SetForegroundWindow: { args: [FFIType.ptr], returns: FFIType.bool },
  GetForegroundWindow: { args: [], returns: FFIType.ptr },
  GetCursorPos: { args: [FFIType.ptr], returns: FFIType.bool },
  GetDC: { args: [FFIType.ptr], returns: FFIType.u64 },
  GetWindowDC: { args: [FFIType.ptr], returns: FFIType.u64 },
  ReleaseDC: { args: [FFIType.ptr, FFIType.u64], returns: FFIType.i32 },
  PrintWindow: { args: [FFIType.ptr, FFIType.u64, FFIType.u32], returns: FFIType.bool },
  GetSystemMetrics: { args: [FFIType.i32], returns: FFIType.i32 },
  SystemParametersInfoW: { args: [FFIType.u32, FFIType.u32, FFIType.ptr, FFIType.u32], returns: FFIType.bool },
  SetProcessDpiAwarenessContext: { args: [FFIType.i64], returns: FFIType.bool },
  SetCursorPos: { args: [FFIType.i32, FFIType.i32], returns: FFIType.bool },
  ClientToScreen: { args: [FFIType.ptr, FFIType.ptr], returns: FFIType.bool },
  SetWindowPos: {
    args: [FFIType.ptr, FFIType.i64, FFIType.i32, FFIType.i32, FFIType.i32, FFIType.i32, FFIType.u32],
    returns: FFIType.bool,
  },
  RedrawWindow: { args: [FFIType.ptr, FFIType.ptr, FFIType.u64, FFIType.u32], returns: FFIType.bool },
});

const gdi32 = dlopen("gdi32.dll", {
  CreateCompatibleDC: { args: [FFIType.u64], returns: FFIType.u64 },
  CreateCompatibleBitmap: { args: [FFIType.u64, FFIType.i32, FFIType.i32], returns: FFIType.u64 },
  SelectObject: { args: [FFIType.u64, FFIType.u64], returns: FFIType.u64 },
  BitBlt: {
    args: [
      FFIType.u64,
      FFIType.i32,
      FFIType.i32,
      FFIType.i32,
      FFIType.i32,
      FFIType.u64,
      FFIType.i32,
      FFIType.i32,
      FFIType.u32,
    ],
    returns: FFIType.bool,
  },
  DeleteObject: { args: [FFIType.u64], returns: FFIType.bool },
  DeleteDC: { args: [FFIType.u64], returns: FFIType.bool },
});

const gdiplus = dlopen("gdiplus.dll", {
  GdiplusStartup: { args: [FFIType.ptr, FFIType.ptr, FFIType.ptr], returns: FFIType.u32 },
  GdipCreateBitmapFromHBITMAP: { args: [FFIType.u64, FFIType.u64, FFIType.ptr], returns: FFIType.u32 },
  GdipSaveImageToFile: { args: [FFIType.u64, FFIType.ptr, FFIType.ptr, FFIType.ptr], returns: FFIType.u32 },
  GdipDisposeImage: { args: [FFIType.u64], returns: FFIType.u32 },
});

export const SW_RESTORE = 9;
const HWND_TOP = 0;
const SWP_NOMOVE = 0x0002;
const SWP_NOSIZE = 0x0001;
const SWP_NOACTIVATE = 0x0010;
const RDW_INVALIDATE = 0x0001;
const RDW_UPDATENOW = 0x0100;
const RDW_ALLCHILDREN = 0x0080;

// Put the window in front without activating it, and make it repaint now.
// SetForegroundWindow is refused on a session nothing is holding, but the
// z-order and the update region are not: a window behind another photographs
// as whatever DWM last held for it, which is sometimes blank.
export function bringToTopAndRedraw(hwnd: number): void {
  user32.symbols.SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
  user32.symbols.RedrawWindow(hwnd, null, 0n, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
}

// Keep a window above every non-topmost one, so a capture of the visible
// desktop surface (captureWindowSurfaceToPng) photographs it rather than
// whatever else the user has open over it. -1 is HWND_TOPMOST.
export function setTopmost(hwnd: number): void {
  user32.symbols.SetWindowPos(hwnd, -1, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

// The heavier hammer for the same problem: move the window a pixel and back,
// which is a resize as far as the app and DWM are concerned and so redraws
// and recomposites everything. An invalidate alone does not always get a
// GPU-composited window off a blank surface.
export function nudgeWindow(hwnd: number): void {
  const r = getWindowRect(hwnd);
  const w = r.right - r.left;
  const h = r.bottom - r.top;
  moveWindow(hwnd, r.left, r.top, w, h - 1);
  moveWindow(hwnd, r.left, r.top, w, h);
}
export const WM_CLOSE = 0x0010;
export const WM_MOUSEMOVE = 0x0200;
export const WM_LBUTTONDOWN = 0x0201;
export const WM_LBUTTONUP = 0x0202;
export const WM_RBUTTONDOWN = 0x0204;
export const WM_RBUTTONUP = 0x0205;
export const MK_LBUTTON = 0x0001;
export const MK_RBUTTON = 0x0002;
export const PW_RENDERFULLCONTENT = 0x00000002;

export type Rect = { left: number; top: number; right: number; bottom: number };

export function sleep(ms: number): Promise<void> {
  return Bun.sleep(ms);
}

export function packCoords(x: number, y: number): number {
  return ((y & 0xffff) << 16) | (x & 0xffff);
}

export function wideZ(s: string): Uint16Array {
  const buf = new Uint16Array(s.length + 1);
  for (let i = 0; i < s.length; i++) {
    buf[i] = s.charCodeAt(i);
  }
  buf[s.length] = 0;
  return buf;
}

export function setProcessDpiAware(): boolean {
  return user32.symbols.SetProcessDpiAwarenessContext(-4n as unknown as number);
}

export function getSystemMetrics(index: number): number {
  return user32.symbols.GetSystemMetrics(index);
}

export function getWorkArea(): Rect {
  const buf = new Int32Array(4);
  if (!user32.symbols.SystemParametersInfoW(0x0030, 0, ptr(buf), 0)) {
    return { left: 0, top: 0, right: getSystemMetrics(0), bottom: getSystemMetrics(1) };
  }
  return { left: buf[0]!, top: buf[1]!, right: buf[2]!, bottom: buf[3]! };
}

export function enumWindows(visit: (hwnd: number) => boolean): void {
  const cb = new JSCallback((hwnd: number) => visit(hwnd), {
    args: [FFIType.ptr, FFIType.i64],
    returns: FFIType.bool,
  });
  try {
    user32.symbols.EnumWindows(cb, 0n);
  } finally {
    cb.close();
  }
}

export function getClassName(hwnd: number): string {
  const buf = new Uint16Array(256);
  const n = user32.symbols.GetClassNameW(hwnd, ptr(buf), 256);
  let s = "";
  for (let i = 0; i < n; i++) {
    s += String.fromCharCode(buf[i]!);
  }
  return s;
}

export function getWindowPid(hwnd: number): number {
  const out = new Uint32Array(1);
  user32.symbols.GetWindowThreadProcessId(hwnd, ptr(out));
  return out[0]!;
}

export function getWindowText(hwnd: number): string {
  const buf = new Uint16Array(512);
  const n = user32.symbols.GetWindowTextW(hwnd, ptr(buf), 512);
  let s = "";
  for (let i = 0; i < n; i++) {
    s += String.fromCharCode(buf[i]!);
  }
  return s;
}

export function isWindowVisible(hwnd: number): boolean {
  return user32.symbols.IsWindowVisible(hwnd);
}

export function getWindowRect(hwnd: number): Rect {
  const buf = new Int32Array(4);
  user32.symbols.GetWindowRect(hwnd, ptr(buf));
  return { left: buf[0]!, top: buf[1]!, right: buf[2]!, bottom: buf[3]! };
}

export function getClientRect(hwnd: number): Rect {
  const buf = new Int32Array(4);
  user32.symbols.GetClientRect(hwnd, ptr(buf));
  return { left: buf[0]!, top: buf[1]!, right: buf[2]!, bottom: buf[3]! };
}

export function moveWindow(hwnd: number, x: number, y: number, w: number, h: number): boolean {
  return user32.symbols.MoveWindow(hwnd, x, y, w, h, true);
}

export function showWindow(hwnd: number, cmd: number): boolean {
  return user32.symbols.ShowWindow(hwnd, cmd);
}

export type WorkAreaHalf = "left" | "right";

// The left or right half of the primary work area (taskbar excluded), 80% of
// its height, top-aligned. Anything that wants a window this size asks here,
// so a window moved into the rect and a window opened at it are the same rect.
export function workAreaHalfRect(side: WorkAreaHalf): { x: number; y: number; w: number; h: number } {
  const wa = getWorkArea();
  const mid = wa.left + Math.floor((wa.right - wa.left) / 2);
  return {
    x: side === "left" ? wa.left : mid,
    y: wa.top,
    w: side === "left" ? mid - wa.left : wa.right - mid,
    h: Math.floor((wa.bottom - wa.top) * 0.8),
  };
}

// Place on the left or right half of the primary work area.
export function placeOnWorkAreaHalf(hwnd: number, side: WorkAreaHalf): boolean {
  if (!hwnd) {
    return false;
  }
  showWindow(hwnd, SW_RESTORE);
  const r = workAreaHalfRect(side);
  return moveWindow(hwnd, r.x, r.y, r.w, r.h);
}

export function setForegroundWindow(hwnd: number): boolean {
  return user32.symbols.SetForegroundWindow(hwnd);
}

export function getForegroundWindow(): number {
  return Number(user32.symbols.GetForegroundWindow() ?? 0);
}

// SetForegroundWindow is a request, not an order: Windows refuses it while
// another process owns the foreground, and it says so only through the return
// value. A window captured while inactive gets the inactive caption shade,
// which reads as a diff in every screenshot comparison, so keep asking.
export async function waitForForeground(hwnd: number, timeoutMs = 3000): Promise<boolean> {
  const deadline = Date.now() + timeoutMs;
  // A locked or disconnected session has no foreground window at all and will
  // not grow one while the shot runs, so waiting out the full timeout there
  // just adds seconds to every capture. A window that belongs to someone else
  // may still be given up, so that one is worth waiting for.
  const emptyDeadline = Date.now() + 400;
  for (;;) {
    setForegroundWindow(hwnd);
    const fg = getForegroundWindow();
    if (fg === hwnd) {
      return true;
    }
    const now = Date.now();
    if (now >= deadline || (fg === 0 && now >= emptyDeadline)) {
      return false;
    }
    await sleep(50);
  }
}

export function setCursorPos(x: number, y: number): boolean {
  return user32.symbols.SetCursorPos(x, y);
}

export function getCursorPos(): { x: number; y: number } {
  const buf = new Int32Array(2);
  user32.symbols.GetCursorPos(ptr(buf));
  return { x: buf[0]!, y: buf[1]! };
}

// Put the pointer somewhere the window is not, at a work area corner. Windows
// delivers a wheel notch to whatever sits under the cursor, so a stray scroll
// while a shot is up lands in the app and captures a scrolled pane instead of
// the top of the page; a pointer resting on a control is an unasked-for hover
// state in the same way.
// Returns true when the pointer had to move, i.e. when the window needs a
// moment to repaint without it.
export function parkCursorOutside(hwnd: number): boolean {
  const r = getWindowRect(hwnd);
  const at = getCursorPos();
  if (at.x < r.left || at.x > r.right || at.y < r.top || at.y > r.bottom) {
    return false;
  }
  const wa = getWorkArea();
  const corners = [
    { x: wa.left + 1, y: wa.bottom - 1 },
    { x: wa.right - 1, y: wa.bottom - 1 },
    { x: wa.left + 1, y: wa.top + 1 },
    { x: wa.right - 1, y: wa.top + 1 },
  ];
  const away = corners.find((p) => p.x < r.left || p.x > r.right || p.y < r.top || p.y > r.bottom);
  setCursorPos(away?.x ?? wa.right - 1, away?.y ?? wa.bottom - 1);
  return true;
}

export function clientToScreen(hwnd: number, x: number, y: number): { x: number; y: number } {
  const buf = new Int32Array([x, y]);
  user32.symbols.ClientToScreen(hwnd, ptr(buf));
  return { x: buf[0]!, y: buf[1]! };
}

export function sendMessage(hwnd: number, msg: number, wParam: number, lParam: number): bigint {
  return user32.symbols.SendMessageW(hwnd, msg, BigInt(wParam), BigInt(lParam));
}

export function postMessage(hwnd: number, msg: number, wParam: number, lParam: number): boolean {
  return user32.symbols.PostMessageW(hwnd, msg, BigInt(wParam), BigInt(lParam));
}

export function findVisiblePidWindow(pid: number): number {
  let found = 0;
  enumWindows((hwnd) => {
    if (getWindowPid(hwnd) !== pid || !isWindowVisible(hwnd)) {
      return true;
    }
    const r = getWindowRect(hwnd);
    if (r.right - r.left < 80 || r.bottom - r.top < 80) {
      return true;
    }
    found = hwnd;
    return false;
  });
  return found;
}

export async function waitForPidWindow(pid: number, timeoutMs = 20000): Promise<number> {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    const h = findVisiblePidWindow(pid);
    if (h) {
      return h;
    }
    await sleep(150);
  }
  return 0;
}

function isSizedWindow(hwnd: number): boolean {
  if (!isWindowVisible(hwnd)) {
    return false;
  }
  const r = getWindowRect(hwnd);
  return r.right - r.left >= 80 && r.bottom - r.top >= 80;
}

export function findVisibleClassWindows(className: string): number[] {
  const found: number[] = [];
  enumWindows((hwnd) => {
    if (!isSizedWindow(hwnd) || getClassName(hwnd) !== className) {
      return true;
    }
    found.push(hwnd);
    return true;
  });
  return found;
}

// Wait for a visible window of `className` that is not in `ignore` (hwnds
// that already existed before launch). Needed when the app is a child of
// WinDbg and does not share the spawned PID.
export async function waitForNewClassWindow(
  className: string,
  ignore: Set<number>,
  timeoutMs = 60000,
): Promise<number> {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    for (const h of findVisibleClassWindows(className)) {
      if (!ignore.has(h)) {
        return h;
      }
    }
    await sleep(200);
  }
  return 0;
}

export async function clickClient(hwnd: number, x: number, y: number, settleMs = 200): Promise<void> {
  const lp = packCoords(x, y);
  sendMessage(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, lp);
  sendMessage(hwnd, WM_LBUTTONUP, 0, lp);
  if (settleMs) {
    await sleep(settleMs);
  }
}

// The secondary button, for a context menu or a popover that opens on it.
export async function rightClickClient(hwnd: number, x: number, y: number, settleMs = 200): Promise<void> {
  const lp = packCoords(x, y);
  sendMessage(hwnd, WM_RBUTTONDOWN, MK_RBUTTON, lp);
  sendMessage(hwnd, WM_RBUTTONUP, 0, lp);
  if (settleMs) {
    await sleep(settleMs);
  }
}

// Leave the pointer over the window. Answers false when the pointer could not
// actually be placed there, which is not a detail a caller can ignore: the
// window keeps a TrackMouseEvent(TME_LEAVE) up, so Windows answers the
// synthetic move with WM_MOUSELEAVE at once and the hover state is gone again
// before the frame that would have shown it. A locked desktop and a CI agent
// with no interactive session both refuse SetCursorPos.
export async function hoverClient(hwnd: number, x: number, y: number, settleMs = 200): Promise<boolean> {
  const lp = packCoords(x, y);
  const scr = clientToScreen(hwnd, x, y);
  setCursorPos(scr.x, scr.y);
  const at = getCursorPos();
  const placed = Math.abs(at.x - scr.x) <= 1 && Math.abs(at.y - scr.y) <= 1;
  sendMessage(hwnd, WM_MOUSEMOVE, 0, lp);
  if (settleMs) {
    await sleep(settleMs);
  }
  return placed;
}

const PNG_ENCODER_CLSID = new Uint8Array([
  0x06, 0xf4, 0x7c, 0x55, 0x04, 0x1a, 0xd3, 0x11, 0x9a, 0x73, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e,
]);

let gdiplusStarted = false;
function ensureGdiplus(): void {
  if (gdiplusStarted) {
    return;
  }
  const input = new Uint8Array(24);
  new DataView(input.buffer).setUint32(0, 1, true);
  const token = new BigUint64Array(1);
  if (gdiplus.symbols.GdiplusStartup(ptr(token), ptr(input), 0) === 0) {
    gdiplusStarted = true;
  }
}

export function captureWindowToPng(hwnd: number, outPath: string): boolean {
  ensureGdiplus();
  const r = getWindowRect(hwnd);
  const w = r.right - r.left;
  const h = r.bottom - r.top;
  if (w <= 0 || h <= 0) {
    return false;
  }
  const winDC = user32.symbols.GetWindowDC(hwnd);
  const memDC = gdi32.symbols.CreateCompatibleDC(winDC);
  const bmp = gdi32.symbols.CreateCompatibleBitmap(winDC, w, h);
  const oldObj = gdi32.symbols.SelectObject(memDC, bmp);
  user32.symbols.PrintWindow(hwnd, memDC, PW_RENDERFULLCONTENT);
  gdi32.symbols.SelectObject(memDC, oldObj);

  const gpBmp = new BigUint64Array(1);
  gdiplus.symbols.GdipCreateBitmapFromHBITMAP(bmp, 0n, ptr(gpBmp));
  const status = gdiplus.symbols.GdipSaveImageToFile(gpBmp[0]!, ptr(wideZ(outPath)), ptr(PNG_ENCODER_CLSID), 0);
  gdiplus.symbols.GdipDisposeImage(gpBmp[0]!);

  gdi32.symbols.DeleteObject(bmp);
  gdi32.symbols.DeleteDC(memDC);
  user32.symbols.ReleaseDC(hwnd, winDC);
  return status === 0;
}

// Capture the pixels already in the visible client area without PrintWindow.
// PrintWindow asks GPUI to draw again, which hides exactly the stale-present
// failure an interaction/damage benchmark needs to preserve in its evidence.
// The caller must keep the window visible and unobscured.
export function captureWindowSurfaceToPng(hwnd: number, outPath: string): boolean {
  ensureGdiplus();
  const r = getClientRect(hwnd);
  const origin = clientToScreen(hwnd, 0, 0);
  const w = r.right - r.left;
  const h = r.bottom - r.top;
  if (w <= 0 || h <= 0) {
    return false;
  }
  // A flip-model swap chain is not readable through GetWindowDC. Read the
  // compositor's already-visible desktop surface at the window coordinates.
  const screenDC = user32.symbols.GetDC(null);
  const memDC = gdi32.symbols.CreateCompatibleDC(screenDC);
  const bmp = gdi32.symbols.CreateCompatibleBitmap(screenDC, w, h);
  const oldObj = gdi32.symbols.SelectObject(memDC, bmp);
  const copied = gdi32.symbols.BitBlt(
    memDC,
    0,
    0,
    w,
    h,
    screenDC,
    origin.x,
    origin.y,
    0x40cc0020 /* CAPTUREBLT | SRCCOPY */,
  );
  gdi32.symbols.SelectObject(memDC, oldObj);

  const gpBmp = new BigUint64Array(1);
  gdiplus.symbols.GdipCreateBitmapFromHBITMAP(bmp, 0n, ptr(gpBmp));
  const status = copied
    ? gdiplus.symbols.GdipSaveImageToFile(gpBmp[0]!, ptr(wideZ(outPath)), ptr(PNG_ENCODER_CLSID), 0)
    : 1;
  gdiplus.symbols.GdipDisposeImage(gpBmp[0]!);

  gdi32.symbols.DeleteObject(bmp);
  gdi32.symbols.DeleteDC(memDC);
  user32.symbols.ReleaseDC(null, screenDC);
  return status === 0;
}

export async function killAndWait(proc: Bun.Subprocess | undefined | null, timeoutMs = 5000): Promise<void> {
  if (!proc) {
    return;
  }
  try {
    proc.kill();
  } catch {
    /* already gone */
  }
  // proc.exitCode never leaves null for a process we killed -- Bun reports the
  // signal instead -- so polling it waited out the whole timeout on every
  // single spawn. The promise settles as soon as the process is reaped.
  let timer: ReturnType<typeof setTimeout> | undefined;
  await Promise.race([
    proc.exited,
    new Promise<void>((resolve) => {
      timer = setTimeout(resolve, timeoutMs);
    }),
  ]);
  if (timer !== undefined) {
    clearTimeout(timer);
  }
}
