#!/usr/bin/env python3
"""
Zelda ALttP Autonomous Debug Harness
=====================================
Connects to zelda.exe Oracle debug server, runs deterministic save-state tests,
captures WRAM timeseries + write traces, takes screenshots, and writes a
timestamped evidence packet to debug_runs/<timestamp>/.

Usage:
  python debug_harness.py [--rebuild] [--no-launch] [--slot N] [--phase A|B|C|D|all]

  --rebuild    Do a full clean rebuild before launching (msbuild /t:Rebuild)
  --no-launch  Skip launching zelda.exe (assume it is already running)
  --slot N     Force use of save slot N for movement tests (default: auto-pick)
  --phase X    Run only one phase (A/B/C/D/all)  default: all
"""

import argparse
import json
import os
import socket
import subprocess
import sys
import time
import textwrap
from datetime import datetime
from pathlib import Path

# ── Project constants ────────────────────────────────────────────────────────
PROJECT_ROOT = Path(r"F:\Projects\snesrecomp\LegendofZeldaAlttpRecomp")
ZELDA_EXE    = PROJECT_ROOT / "build" / "bin-x64-Oracle" / "zelda.exe"
ZELDA_SLN    = PROJECT_ROOT / "zelda.sln"
TCP_HOST     = "127.0.0.1"
TCP_PORT     = 4378
CONNECT_RETRIES = 30
CONNECT_DELAY   = 1.0   # seconds between TCP connect retries

# WRAM addresses (16-bit WRAM offsets; debug server takes hex)
W_POS_Y       = 0x0020  # Link Y word (lo/hi)
W_POS_X       = 0x0022  # Link X word (lo/hi)
W_SPEED_Y     = 0x0027  # Link speed/subpixel
W_SPEED_X     = 0x0028
W_MOVE_MAG    = 0x0030  # movement magnitude
W_MOVE_FLAG   = 0x006D  # movement-occurred flag
W_FACING      = 0x006E  # facing direction
W_ANIM_IDX   = 0x0072  # animation index
W_JOY_CUR    = 0x00F0  # current joypad (word)
W_JOY_P1     = 0x00F2  # P1 joypad current (word)
W_JOY_PREV   = 0x00F4  # previous joypad (word)
W_TILE_BUF   = 0x09B8  # tile detection buffer (8 bytes)

TRACE_ADDRS = [
    W_POS_Y, W_POS_X, W_MOVE_MAG, W_MOVE_FLAG,
    W_FACING, W_ANIM_IDX, W_JOY_P1, W_TILE_BUF,
]

# Button names accepted by set_controller
DIRECTIONS = ["up", "down", "left", "right"]

# ── TCP client ───────────────────────────────────────────────────────────────
class DebugClient:
    def __init__(self, host=TCP_HOST, port=TCP_PORT):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.connect((host, port))
        self.sock.settimeout(15.0)
        self._buf = b""
        # The server sends a banner {"connected":true,"frame":N} immediately on
        # accept — drain it now so all subsequent cmd() pairs are in sync.
        try:
            banner = self._readline(timeout=5.0)
            # Verify it looks like the banner, not a response to a command
            parsed = json.loads(banner) if banner else {}
            if not parsed.get("connected"):
                # Unexpected — push it back into our buffer for the next read
                self._buf = (banner + "\n").encode() + self._buf
        except Exception:
            pass  # no banner or parse error — proceed anyway

    def close(self):
        try: self.sock.close()
        except Exception: pass

    def _readline(self, timeout=15.0):
        self.sock.settimeout(timeout)
        while b"\n" not in self._buf:
            chunk = self.sock.recv(65536)
            if not chunk:
                raise ConnectionError("TCP disconnected")
            self._buf += chunk
        line, self._buf = self._buf.split(b"\n", 1)
        return line.decode("utf-8", errors="replace").strip()

    def raw(self, command: str, timeout=15.0) -> str:
        self.sock.sendall((command + "\n").encode())
        return self._readline(timeout)

    def cmd(self, command: str, timeout=15.0) -> dict:
        resp = self.raw(command, timeout)
        try:
            return json.loads(resp)
        except json.JSONDecodeError:
            return {"raw": resp, "ok": False}

    # ── helpers ──────────────────────────────────────────────────────────────
    def ping(self) -> dict:
        return self.cmd("ping")

    def frame(self) -> int:
        r = self.cmd("frame")
        return r.get("frame", -1)

    def pause(self) -> dict:
        return self.cmd("pause")

    def cont(self) -> dict:
        return self.cmd("continue")

    def step(self, n: int, timeout=60.0) -> dict:
        return self.cmd(f"step {n}", timeout=timeout)

    def loadstate(self, slot: int) -> dict:
        return self.cmd(f"loadstate {slot}")

    def screenshot(self, path: str) -> dict:
        # Always pass absolute path; forward slashes for fopen on Windows
        p = str(Path(path).resolve()).replace("\\", "/")
        return self.cmd(f"screenshot {p}", timeout=10.0)

    def set_controller(self, buttons: str) -> dict:
        return self.cmd(f"set_controller {buttons}")

    def clear_controller(self) -> dict:
        return self.cmd("clear_controller")

    def wram_timeseries(self, addr: int, length: int = 2,
                        from_frame: int = None, to_frame: int = None,
                        limit: int = 512) -> dict:
        args = f"{addr:x} {length}"
        if from_frame is not None:
            args += f" {from_frame}"
            if to_frame is not None:
                args += f" {to_frame}"
                args += f" {limit}"
        return self.cmd(f"wram_timeseries {args}", timeout=15.0)

    def wram_writes_at(self, addr: int, from_frame: int = 0,
                       to_frame: int = 9999999, limit: int = 128) -> dict:
        return self.cmd(f"wram_writes_at {addr:x} {from_frame} {to_frame} {limit}",
                        timeout=15.0)

    def read_ram(self, addr: int, length: int = 1) -> dict:
        return self.cmd(f"read_ram {addr:x} {length}")

    def call_stack(self) -> dict:
        return self.cmd("call_stack", timeout=5.0)

    def history_status(self) -> dict:
        return self.cmd("history", timeout=5.0)


# ── Harness ──────────────────────────────────────────────────────────────────
class Harness:
    def __init__(self, run_dir: Path, rebuild: bool):
        self.run_dir = run_dir
        self.run_dir.mkdir(parents=True, exist_ok=True)
        self._log_f = open(run_dir / "tcp_log.txt", "w", encoding="utf-8")
        self.summary: list[str] = []
        self.proc = None
        self.c: DebugClient = None
        self.rebuild = rebuild
        self._frame_base = 0   # frame number at last loadstate+settle

    # ── logging ───────────────────────────────────────────────────────────────
    def log(self, msg: str):
        ts = datetime.now().strftime("%H:%M:%S.%f")[:-3]
        line = f"[{ts}] {msg}"
        print(line)
        self._log_f.write(line + "\n")
        self._log_f.flush()

    def note(self, msg: str):
        self.summary.append(msg)
        self.log(f"NOTE: {msg}")

    # ── build ─────────────────────────────────────────────────────────────────
    def do_build(self) -> bool:
        self.log("=== PHASE: Full clean rebuild (msbuild /t:Rebuild) ===")
        build_log = self.run_dir / "build.log"
        try:
            result = subprocess.run(
                ["msbuild", str(ZELDA_SLN),
                 "/p:Configuration=Oracle", "/p:Platform=x64",
                 "/m", "/t:Rebuild", "/v:minimal"],
                cwd=str(PROJECT_ROOT),
                capture_output=True,
                text=True,
                timeout=600,
            )
        except FileNotFoundError:
            self.log("ERROR: msbuild not found on PATH. Run from VS Developer Command Prompt.")
            return False
        except subprocess.TimeoutExpired:
            self.log("ERROR: Build timed out after 600s")
            return False

        build_log.write_text(result.stdout + "\n--- STDERR ---\n" + result.stderr,
                             encoding="utf-8")
        self.log(f"Build exit code: {result.returncode}")
        if result.returncode != 0:
            # Show last 50 lines of build output for diagnosis
            lines = (result.stdout + result.stderr).splitlines()[-50:]
            for l in lines:
                self.log(f"  BUILD| {l}")
            self.note(f"BUILD FAILED rc={result.returncode} — see build.log")
            return False
        self.note("Build succeeded (Oracle|x64 /t:Rebuild)")
        # Record exe timestamp
        try:
            mtime = ZELDA_EXE.stat().st_mtime
            self.note(f"zelda.exe mtime: {datetime.fromtimestamp(mtime).isoformat()}")
        except Exception:
            pass
        return True

    # ── launch ────────────────────────────────────────────────────────────────
    def kill_existing(self):
        """Kill any leftover zelda.exe processes."""
        try:
            result = subprocess.run(
                ["taskkill", "/F", "/IM", "zelda.exe"],
                capture_output=True, text=True
            )
            if "SUCCESS" in result.stdout:
                self.log("Killed existing zelda.exe")
                time.sleep(1.0)
        except Exception:
            pass

    def launch(self) -> bool:
        self.kill_existing()
        if not ZELDA_EXE.exists():
            self.log(f"ERROR: {ZELDA_EXE} not found")
            return False
        self.log(f"Launching {ZELDA_EXE}")
        self.proc = subprocess.Popen(
            [str(ZELDA_EXE)],
            cwd=str(ZELDA_EXE.parent),
            creationflags=subprocess.CREATE_NEW_PROCESS_GROUP,
        )
        self.log(f"Process PID={self.proc.pid}")
        return True

    # ── connect ───────────────────────────────────────────────────────────────
    def connect(self) -> bool:
        self.log(f"Connecting to TCP {TCP_HOST}:{TCP_PORT} ...")
        for i in range(CONNECT_RETRIES):
            try:
                self.c = DebugClient()
                r = self.c.ping()
                self.log(f"Connected. ping={r}")
                self.note(f"TCP connected at attempt {i+1}; frame={r.get('frame')}")
                return True
            except (ConnectionRefusedError, OSError) as e:
                self.log(f"  Attempt {i+1}/{CONNECT_RETRIES}: {e}")
                time.sleep(CONNECT_DELAY)
        self.note("BLOCKED: TCP connection failed after all retries")
        return False

    # ── TCP wrappers with logging ─────────────────────────────────────────────
    def _cmd(self, command: str, timeout=15.0) -> dict:
        self.log(f">>> {command}")
        r = self.c.cmd(command, timeout)
        self.log(f"<<< {json.dumps(r)[:400]}")
        return r

    def _step(self, n: int, timeout=60.0) -> dict:
        r = self._cmd(f"step {n}", timeout)
        return r

    def _screenshot(self, name: str) -> dict:
        path = self.run_dir / name
        path.parent.mkdir(parents=True, exist_ok=True)
        p = str(path.resolve()).replace("\\", "/")
        self.log(f">>> screenshot {p}")
        r = self.c.screenshot(str(path))
        self.log(f"<<< {json.dumps(r)[:200]}")
        ok = "error" not in r
        self.log(f"  Screenshot {'saved' if ok else 'FAILED'}: {path}")
        return r

    def _loadstate_and_settle(self, slot: int, settle: int = 10) -> int:
        """Load state, step settle frames, return frame number after settle."""
        r = self._cmd(f"loadstate {slot}")
        self.log(f"  loadstate={r}; stepping {settle} frames to settle...")
        sr = self._step(settle, timeout=60.0)
        fr = self._cmd("frame")
        f = fr.get("frame", -1)
        self.log(f"  Settled at frame {f}")
        self._frame_base = f
        return f

    def _timeseries_batch(self, addrs: list[int], from_f: int, to_f: int,
                          prefix: str = "") -> dict:
        """Query wram_timeseries for each address and save JSON."""
        results = {}
        for addr in addrs:
            r = self._cmd(
                f"wram_timeseries {addr:x} 2 {from_f} {to_f} 512",
                timeout=15.0)
            results[f"0x{addr:04x}"] = r
        out_path = self.run_dir / f"{prefix}_timeseries.json"
        out_path.parent.mkdir(parents=True, exist_ok=True)
        out_path.write_text(json.dumps(results, indent=2), encoding="utf-8")
        self.log(f"  Timeseries saved: {out_path}")
        return results

    def _write_trace_batch(self, addrs: list[int], from_f: int, to_f: int,
                           prefix: str = "") -> dict:
        """Query wram_writes_at for each address and save JSON."""
        results = {}
        for addr in addrs:
            r = self._cmd(
                f"wram_writes_at {addr:x} {from_f} {to_f} 128",
                timeout=15.0)
            results[f"0x{addr:04x}"] = r
        out_path = self.run_dir / f"{prefix}_writes.json"
        out_path.parent.mkdir(parents=True, exist_ok=True)
        out_path.write_text(json.dumps(results, indent=2), encoding="utf-8")
        self.log(f"  Write traces saved: {out_path}")
        return results

    # ── Phase A: Basic launch verification ────────────────────────────────────
    def phase_a(self) -> bool:
        self.log("\n======== PHASE A: Basic launch verification ========")

        # Ping
        r = self._cmd("ping")
        if not r.get("ok"):
            self.note("PHASE A FAIL: ping returned not-ok")
            return False
        self.note(f"PHASE A: ping ok, frame={r.get('frame')}")

        # Frame
        r = self._cmd("frame")
        self.note(f"PHASE A: current frame={r.get('frame')}, func={r.get('func','?')}")

        # History status
        r = self._cmd("history")
        self.note(f"PHASE A: history={json.dumps(r)[:200]}")

        # Call stack (quick test)
        r = self._cmd("call_stack", timeout=5.0)
        self.log(f"  call_stack={json.dumps(r)[:300]}")

        # Controller state
        r = self._cmd("get_controller")
        self.note(f"PHASE A: controller baseline={r}")

        return True

    # ── Phase B: Save state verification ──────────────────────────────────────
    def phase_b(self) -> int:
        """Returns best slot number (-1 on failure)."""
        self.log("\n======== PHASE B: Save state verification ========")
        best_slot = -1
        slot_info = {}

        for slot in [0, 1, 2]:
            self.log(f"\n--- Slot {slot} ---")
            f = self._loadstate_and_settle(slot, settle=15)
            self._step(5)  # a few more frames so PPU is stable

            scr = self._screenshot(f"slot_{slot}_initial.bmp")
            ok = "error" not in scr

            # Read key WRAM bytes to check if Link is present
            pos_y = self._cmd(f"read_ram {W_POS_Y:x} 2")
            pos_x = self._cmd(f"read_ram {W_POS_X:x} 2")
            mode  = self._cmd(f"read_ram 10 1")   # game mode $7E:0010

            info = {
                "slot": slot, "frame": f, "screenshot_ok": ok,
                "pos_y": pos_y, "pos_x": pos_x, "game_mode": mode,
            }
            slot_info[slot] = info
            summary = (f"slot {slot}: frame={f} pos_y={pos_y} pos_x={pos_x} "
                       f"mode={mode} screenshot={'ok' if ok else 'FAIL'}")
            self.note(f"PHASE B: {summary}")

            # Any slot that is not mode 0/title is potentially usable
            if best_slot == -1:
                best_slot = slot   # default to slot 0

        # Save slot_info
        (self.run_dir / "slot_info.json").write_text(
            json.dumps(slot_info, indent=2), encoding="utf-8")

        if best_slot == -1:
            self.note("PHASE B FAIL: no usable slot found")
        else:
            self.note(f"PHASE B: using slot {best_slot} for movement tests")
        return best_slot

    # ── Phase C: Controller mapping verification ──────────────────────────────
    def phase_c(self, slot: int) -> dict:
        """Returns mapping table: direction -> observed WRAM changes."""
        self.log(f"\n======== PHASE C: Controller mapping (slot {slot}) ========")
        mapping = {}

        # Idle baseline
        self.log("--- C: idle baseline ---")
        f0 = self._loadstate_and_settle(slot, settle=10)
        idle_from = f0
        self._step(10)
        idle_to = self.c.frame()
        self._screenshot("ctrl_idle_baseline.bmp")
        idle_ts = self._timeseries_batch(
            [W_POS_Y, W_POS_X, W_JOY_P1, W_MOVE_MAG, W_MOVE_FLAG, W_FACING, W_ANIM_IDX],
            idle_from, idle_to, "ctrl_idle")
        self.note(f"PHASE C: idle baseline frames {idle_from}–{idle_to}")

        for btn in DIRECTIONS:
            self.log(f"\n--- C: testing '{btn}' ---")
            f0 = self._loadstate_and_settle(slot, settle=10)
            pre_frame = self.c.frame()

            # 5 idle frames before pressing
            self._step(5)
            self._screenshot(f"ctrl_{btn}_before.bmp")

            # Hold button for 30 frames
            self.c.set_controller(btn)
            self.log(f"  set_controller {btn}")
            self._step(30)
            self._screenshot(f"ctrl_{btn}_during.bmp")

            # Release and 5 more frames
            self.c.clear_controller()
            self._step(5)
            self._screenshot(f"ctrl_{btn}_after.bmp")
            post_frame = self.c.frame()

            # Timeseries for the whole window
            ts = self._timeseries_batch(
                [W_POS_Y, W_POS_X, W_JOY_P1, W_MOVE_MAG,
                 W_MOVE_FLAG, W_FACING, W_ANIM_IDX],
                pre_frame, post_frame,
                f"ctrl_{btn}")

            # Interpret position delta
            def extract_vals(ts_dict, addr):
                key = f"0x{addr:04x}"
                entries = ts_dict.get(key, {}).get("entries", [])
                return [(e["f"], e["hex"]) for e in entries]

            py_vals = extract_vals(ts, W_POS_Y)
            px_vals = extract_vals(ts, W_POS_X)
            joy_vals = extract_vals(ts, W_JOY_P1)
            mag_vals = extract_vals(ts, W_MOVE_MAG)
            flag_vals = extract_vals(ts, W_MOVE_FLAG)
            facing_vals = extract_vals(ts, W_FACING)

            row = {
                "button":       btn,
                "frames":       f"{pre_frame}–{post_frame}",
                "pos_y_changes": py_vals,
                "pos_x_changes": px_vals,
                "joy_p1_changes": joy_vals,
                "move_mag_changes": mag_vals,
                "move_flag_changes": flag_vals,
                "facing_changes": facing_vals,
            }
            mapping[btn] = row
            self.note(
                f"PHASE C: {btn:5s} | pos_y_changes={len(py_vals)} "
                f"pos_x_changes={len(px_vals)} joy_p1={len(joy_vals)} "
                f"mag={len(mag_vals)} flag={len(flag_vals)} facing={len(facing_vals)}")
            self.log(f"  joy_vals={joy_vals[:5]}")
            self.log(f"  mag_vals={mag_vals[:5]}")
            self.log(f"  flag_vals={flag_vals[:5]}")

        (self.run_dir / "ctrl_mapping.json").write_text(
            json.dumps(mapping, indent=2), encoding="utf-8")
        self.note(f"PHASE C: mapping saved to ctrl_mapping.json")
        return mapping

    # ── Phase D: Core movement failure trace ──────────────────────────────────
    def phase_d(self, slot: int, best_btn: str) -> dict:
        self.log(f"\n======== PHASE D: Movement failure trace (slot={slot}, btn={best_btn}) ========")

        f0 = self._loadstate_and_settle(slot, settle=10)
        start_frame = self.c.frame()

        # 5 idle frames
        self._step(5)
        f_after_idle = self.c.frame()
        self._screenshot("d_after_idle.bmp")

        # Hold movement for 20 frames, screenshot at 1, 5, 10, 20
        self.c.set_controller(best_btn)
        for milestone in [1, 4, 5, 10]:
            self._step(milestone)
            self._screenshot(f"d_move_{self.c.frame()}.bmp")

        f_during = self.c.frame()

        # Release, 5 more frames
        self.c.clear_controller()
        self._step(5)
        f_after_release = self.c.frame()
        self._screenshot("d_after_release.bmp")

        # Full timeseries over whole window
        ts = self._timeseries_batch(
            [W_POS_Y, W_POS_X, W_SPEED_Y, W_SPEED_X, W_MOVE_MAG,
             W_MOVE_FLAG, W_FACING, W_ANIM_IDX, W_JOY_P1, W_TILE_BUF],
            start_frame, f_after_release,
            "d_full")

        # Write traces
        write_addrs = [
            W_POS_Y, W_POS_X, W_MOVE_MAG, W_MOVE_FLAG,
            W_FACING, W_ANIM_IDX, W_JOY_P1,
            0x09BA,   # tile detection byte
        ]
        writes = self._write_trace_batch(write_addrs, start_frame, f_after_release, "d_full")

        # Check whether $0030/$006D ever changed
        def any_nonzero(ts_dict, addr):
            key = f"0x{addr:04x}"
            entries = ts_dict.get(key, {}).get("entries", [])
            for e in entries:
                if int(e["hex"], 16) != 0:
                    return True
            return False

        mag_nonzero  = any_nonzero(ts, W_MOVE_MAG)
        flag_nonzero = any_nonzero(ts, W_MOVE_FLAG)
        facing_changed = len(ts.get(f"0x{W_FACING:04x}", {}).get("entries", [])) > 1
        pos_y_changed  = len(ts.get(f"0x{W_POS_Y:04x}", {}).get("entries", [])) > 1
        pos_x_changed  = len(ts.get(f"0x{W_POS_X:04x}", {}).get("entries", [])) > 1

        self.note(f"PHASE D: frames {start_frame}–{f_after_release}")
        self.note(f"PHASE D: pos_y changed={pos_y_changed}  pos_x changed={pos_x_changed}")
        self.note(f"PHASE D: $0030 nonzero={mag_nonzero}   $006D nonzero={flag_nonzero}")
        self.note(f"PHASE D: $006E (facing) changed={facing_changed}")

        # Write $0030 writer summary
        mag_writes = writes.get(f"0x{W_MOVE_MAG:04x}", {}).get("matches", [])
        self.note(f"PHASE D: $0030 write count in window = {len(mag_writes)}")
        for w in mag_writes[:10]:
            self.note(f"  $0030 write: frame={w.get('f')} old={w.get('old')} val={w.get('val')} func={w.get('func')} parent={w.get('parent')}")

        # Write $006D writer summary
        flag_writes = writes.get(f"0x{W_MOVE_FLAG:04x}", {}).get("matches", [])
        self.note(f"PHASE D: $006D write count in window = {len(flag_writes)}")
        for w in flag_writes[:10]:
            self.note(f"  $006D write: frame={w.get('f')} old={w.get('old')} val={w.get('val')} func={w.get('func')} parent={w.get('parent')}")

        # Position writer summary
        py_writes = writes.get(f"0x{W_POS_Y:04x}", {}).get("matches", [])
        px_writes = writes.get(f"0x{W_POS_X:04x}", {}).get("matches", [])
        self.note(f"PHASE D: pos_y ($0020) write count = {len(py_writes)}")
        self.note(f"PHASE D: pos_x ($0022) write count = {len(px_writes)}")
        for w in (py_writes + px_writes)[:10]:
            self.note(f"  pos write: addr={w.get('adr')} frame={w.get('f')} val={w.get('val')} func={w.get('func')} parent={w.get('parent')}")

        result = {
            "frames": f"{start_frame}–{f_after_release}",
            "pos_y_changed": pos_y_changed,
            "pos_x_changed": pos_x_changed,
            "move_mag_nonzero": mag_nonzero,
            "move_flag_nonzero": flag_nonzero,
            "facing_changed": facing_changed,
            "mag_writes": mag_writes[:20],
            "flag_writes": flag_writes[:20],
            "pos_y_writes": py_writes[:20],
            "pos_x_writes": px_writes[:20],
        }
        (self.run_dir / "phase_d_result.json").write_text(
            json.dumps(result, indent=2), encoding="utf-8")
        return result

    # ── Phase E: Identify position writer and missing link in movement path ───
    def phase_e(self, slot: int, best_btn: str) -> dict:
        self.log(f"\n======== PHASE E: Find position writer and movement path gap ========")

        f0 = self._loadstate_and_settle(slot, settle=10)
        start_frame = self.c.frame()

        # Hold movement for 5 frames
        self.c.set_controller(best_btn)
        self._step(5)
        f_mid = self.c.frame()
        self.c.clear_controller()
        end_frame = f_mid

        # Tight write trace for just these 5 movement frames
        key_addrs = [W_POS_Y, W_POS_X, W_MOVE_MAG, W_MOVE_FLAG, W_FACING]
        writes = self._write_trace_batch(key_addrs, start_frame, end_frame, "e_tight")

        def first_write_func(writes_dict, addr):
            key = f"0x{addr:04x}"
            matches = writes_dict.get(key, {}).get("matches", [])
            if matches:
                m = matches[0]
                return {"func": m.get("func"), "parent": m.get("parent"),
                        "frame": m.get("f"), "val": m.get("val"), "bi": m.get("bi")}
            return None

        pos_y_writer  = first_write_func(writes, W_POS_Y)
        pos_x_writer  = first_write_func(writes, W_POS_X)
        mag_writer    = first_write_func(writes, W_MOVE_MAG)
        flag_writer   = first_write_func(writes, W_MOVE_FLAG)
        facing_writer = first_write_func(writes, W_FACING)

        self.note(f"PHASE E: pos_y writer = {pos_y_writer}")
        self.note(f"PHASE E: pos_x writer = {pos_x_writer}")
        self.note(f"PHASE E: $0030 (move mag) writer = {mag_writer}")
        self.note(f"PHASE E: $006D (move flag) writer = {flag_writer}")
        self.note(f"PHASE E: $006E (facing) writer = {facing_writer}")

        result = {
            "frames": f"{start_frame}–{end_frame}",
            "pos_y_writer": pos_y_writer,
            "pos_x_writer": pos_x_writer,
            "move_mag_writer": mag_writer,
            "move_flag_writer": flag_writer,
            "facing_writer": facing_writer,
        }
        (self.run_dir / "phase_e_result.json").write_text(
            json.dumps(result, indent=2), encoding="utf-8")
        return result

    # ── Phase F: Collision gate diagnosis ─────────────────────────────────────
    def phase_f(self, slot: int, best_btn: str) -> dict:
        self.log(f"\n======== PHASE F: Collision path gate investigation ========")
        """
        We can't call individual functions directly via TCP without break-style
        tooling. Instead we use wram_writes_at to see which functions write to
        the key movement state bytes during movement, and compare to idle.
        This tells us which functions ARE executing (write side effects) and
        which are absent (no write side effects = not executing or bailing early).
        """

        f0 = self._loadstate_and_settle(slot, settle=10)
        start_idle = self.c.frame()
        self._step(20)
        end_idle = self.c.frame()

        # Idle baseline writes
        idle_writes = self._write_trace_batch(
            [W_POS_Y, W_POS_X, W_MOVE_MAG, W_MOVE_FLAG,
             W_FACING, W_ANIM_IDX, W_TILE_BUF, W_JOY_P1],
            start_idle, end_idle, "f_idle")

        # Movement window
        f1 = self._loadstate_and_settle(slot, settle=10)
        start_move = self.c.frame()
        self.c.set_controller(best_btn)
        self._step(20)
        self.c.clear_controller()
        end_move = self.c.frame()

        move_writes = self._write_trace_batch(
            [W_POS_Y, W_POS_X, W_MOVE_MAG, W_MOVE_FLAG,
             W_FACING, W_ANIM_IDX, W_TILE_BUF, W_JOY_P1],
            start_move, end_move, "f_move")

        # Compare: what funcs appear in idle vs move for each key addr?
        def writers_set(writes_dict, addr):
            key = f"0x{addr:04x}"
            matches = writes_dict.get(key, {}).get("matches", [])
            return set(m.get("func", "?") for m in matches)

        comparison = {}
        for addr_name, addr in [
            ("$0020 pos_y", W_POS_Y), ("$0022 pos_x", W_POS_X),
            ("$0030 move_mag", W_MOVE_MAG), ("$006D move_flag", W_MOVE_FLAG),
            ("$006E facing", W_FACING), ("$0072 anim_idx", W_ANIM_IDX),
            ("$00F2 joy_p1", W_JOY_P1),
        ]:
            idle_set = writers_set(idle_writes, addr)
            move_set = writers_set(move_writes, addr)
            only_idle = idle_set - move_set
            only_move = move_set - idle_set
            comparison[addr_name] = {
                "idle_writers": list(idle_set),
                "move_writers": list(move_set),
                "only_in_idle": list(only_idle),
                "only_in_move": list(only_move),
            }
            self.note(f"PHASE F: {addr_name} | idle_writers={idle_set} | move_writers={move_set}")

        # Joy_P1 presence check: if $00F2 is never written during movement,
        # that is a sign joypad data is not being pushed into WRAM
        joy_idle  = writers_set(idle_writes,  W_JOY_P1)
        joy_move  = writers_set(move_writes, W_JOY_P1)
        self.note(f"PHASE F: $00F2 writers idle={joy_idle} move={joy_move}")

        # Summary: which key addresses have NO writes during movement?
        no_move_writes = []
        for addr_name, addr in [
            ("$0030 move_mag", W_MOVE_MAG), ("$006D move_flag", W_MOVE_FLAG),
            ("$006E facing", W_FACING), ("$0072 anim_idx", W_ANIM_IDX),
        ]:
            key = f"0x{addr:04x}"
            n = len(move_writes.get(key, {}).get("matches", []))
            if n == 0:
                no_move_writes.append(addr_name)
        self.note(f"PHASE F: addresses with ZERO writes during movement: {no_move_writes}")

        result = {
            "idle_frames": f"{start_idle}–{end_idle}",
            "move_frames": f"{start_move}–{end_move}",
            "comparison": comparison,
            "zero_move_writes": no_move_writes,
        }
        (self.run_dir / "phase_f_result.json").write_text(
            json.dumps(result, indent=2), encoding="utf-8")
        return result

    # ── Save final summary ────────────────────────────────────────────────────
    def save_summary(self, phase_results: dict):
        lines = [
            f"# Debug Run: {self.run_dir.name}",
            f"",
            f"Date: {datetime.now().isoformat()}",
            f"",
            "## Notes",
            "",
        ]
        for n in self.summary:
            lines.append(f"- {n}")

        lines += ["", "## Phase Results", ""]
        for ph, result in phase_results.items():
            lines.append(f"### {ph}")
            if isinstance(result, dict):
                for k, v in result.items():
                    lines.append(f"- **{k}**: {v}")
            else:
                lines.append(str(result))
            lines.append("")

        lines += [
            "## Hypothesis",
            "",
            "> (To be filled after evidence analysis)",
            "",
            "## Files Changed",
            "",
            "> None yet — evidence packet only",
        ]

        text = "\n".join(lines)
        (self.run_dir / "summary.md").write_text(text, encoding="utf-8")
        self.log(f"\nSummary written: {self.run_dir / 'summary.md'}")

    # ── Cleanup ───────────────────────────────────────────────────────────────
    def shutdown(self):
        if self.c:
            try: self.c.close()
            except Exception: pass
            self.c = None
        if self.proc:
            try:
                self.proc.kill()
                self.proc.wait(timeout=5)
            except Exception: pass
            self.proc = None
        self._log_f.close()


# ── Main ─────────────────────────────────────────────────────────────────────
def main():
    ap = argparse.ArgumentParser(description="Zelda ALttP debug harness")
    ap.add_argument("--rebuild",    action="store_true", help="Full clean rebuild")
    ap.add_argument("--no-launch",  action="store_true", help="Skip launching zelda.exe")
    ap.add_argument("--slot",       type=int, default=-1, help="Force save slot (0-2)")
    ap.add_argument("--phase",      default="all",
                    choices=["A", "B", "C", "D", "E", "F", "all"],
                    help="Which phase to run")
    args = ap.parse_args()

    ts = datetime.now().strftime("%Y%m%d_%H%M%S")
    run_dir = PROJECT_ROOT / "debug_runs" / ts
    h = Harness(run_dir, rebuild=args.rebuild)
    h.log(f"Run directory: {run_dir}")

    phase_results = {}
    best_slot = args.slot if args.slot >= 0 else -1

    try:
        # ── Build ──
        if args.rebuild:
            if not h.do_build():
                h.note("BLOCKED: build failed, aborting")
                h.save_summary(phase_results)
                return 1

        # ── Launch ──
        if not args.no_launch:
            if not h.launch():
                h.note("BLOCKED: could not launch zelda.exe")
                h.save_summary(phase_results)
                return 1
            time.sleep(3.0)   # give SDL/debug server time to spin up

        # ── Connect ──
        if not h.connect():
            h.note("BLOCKED: TCP connection failed")
            h.save_summary(phase_results)
            return 1

        # ── Phase A ──
        if args.phase in ("A", "all"):
            ok = h.phase_a()
            phase_results["A_basic_launch"] = {"ok": ok}
            if not ok:
                h.note("BLOCKED in Phase A")
                h.save_summary(phase_results)
                return 1

        # ── Phase B ──
        if args.phase in ("B", "all"):
            best_slot = h.phase_b()
            phase_results["B_save_states"] = {"best_slot": best_slot}
            if best_slot < 0:
                h.note("BLOCKED in Phase B: no usable save slot")
                h.save_summary(phase_results)
                return 1
        elif best_slot < 0:
            best_slot = 0
            h.log(f"Phase B skipped; defaulting to slot {best_slot}")

        # Determine best movement button from mapping (or use "right" as default)
        best_btn = "right"   # will be refined in Phase C

        # ── Phase C ──
        if args.phase in ("C", "all"):
            mapping = h.phase_c(best_slot)
            phase_results["C_controller_mapping"] = {k: v["frames"] for k, v in mapping.items()}
            # Pick direction where pos changed most
            best_changes = 0
            for btn, row in mapping.items():
                n = len(row["pos_y_changes"]) + len(row["pos_x_changes"])
                if n > best_changes:
                    best_changes = n
                    best_btn = btn
            h.note(f"PHASE C: best movement button = '{best_btn}' ({best_changes} pos changes)")

        # ── Phase D ──
        if args.phase in ("D", "all"):
            d_result = h.phase_d(best_slot, best_btn)
            phase_results["D_movement_trace"] = {
                "frames": d_result["frames"],
                "pos_y_changed": d_result["pos_y_changed"],
                "pos_x_changed": d_result["pos_x_changed"],
                "move_mag_nonzero": d_result["move_mag_nonzero"],
                "move_flag_nonzero": d_result["move_flag_nonzero"],
            }

        # ── Phase E ──
        if args.phase in ("E", "all"):
            e_result = h.phase_e(best_slot, best_btn)
            phase_results["E_position_writer"] = {
                "pos_y_writer_func": (e_result.get("pos_y_writer") or {}).get("func"),
                "move_mag_writer_func": (e_result.get("move_mag_writer") or {}).get("func"),
                "move_flag_writer_func": (e_result.get("move_flag_writer") or {}).get("func"),
            }

        # ── Phase F ──
        if args.phase in ("F", "all"):
            f_result = h.phase_f(best_slot, best_btn)
            phase_results["F_collision_gate"] = {
                "zero_move_writes": f_result["zero_move_writes"],
                "comparison_keys": list(f_result["comparison"].keys()),
            }

    except KeyboardInterrupt:
        h.log("Interrupted by user")
    except Exception as e:
        import traceback
        h.log(f"EXCEPTION: {e}")
        h.log(traceback.format_exc())
        h.note(f"Harness crashed: {e}")
    finally:
        h.save_summary(phase_results)
        h.shutdown()

    print(f"\n=== Run complete: {run_dir} ===")
    return 0


if __name__ == "__main__":
    sys.exit(main())
