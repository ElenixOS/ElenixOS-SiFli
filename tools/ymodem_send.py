#!/usr/bin/env python3
"""
YMODEM file sender over serial port.

Usage:
    python ymodem_send.py [--port PORT] [--baud BAUD] [--file PATH]

Features:
    - System file picker dialog (macOS native via osascript)
    - File send history (stored in ~/.ymodem_send_history.json)
    - Interactive history selection
"""

import argparse
import json
import os
import subprocess
import sys
import time
from pathlib import Path

import serial


# ── YMODEM Constants ──────────────────────────────────────────────────────────
SOH = 0x01
STX = 0x02
EOT = 0x04
ACK = 0x06
NAK = 0x15
CA = 0x18
ASCII_C = 0x43

PACKET_128 = 128
PACKET_1024 = 1024

HISTORY_FILE = Path.home() / ".ymodem_send_history.json"
MAX_HISTORY = 50


# ── YMODEM Protocol ───────────────────────────────────────────────────────────
def crc16_xmodem(data: bytes) -> int:
    crc = 0
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = (crc << 1) ^ 0x1021
            else:
                crc <<= 1
    return crc & 0xFFFF


def build_packet(data: bytes, block_num: int, packet_size: int) -> bytes:
    header = bytearray(3)
    header[0] = SOH if packet_size == PACKET_128 else STX
    header[1] = block_num & 0xFF
    header[2] = 0xFF - header[1]

    payload = bytearray(packet_size)
    payload[:] = b'\x1A' * packet_size
    copy_len = min(len(data), packet_size)
    payload[:copy_len] = data[:copy_len]

    crc = crc16_xmodem(bytes(payload))
    footer = crc.to_bytes(2, 'big')

    return bytes(header) + bytes(payload) + footer


def build_header_packet(filename: str, filesize: int) -> bytes:
    payload = bytearray(PACKET_128)
    idx = 0
    for b in filename.encode():
        payload[idx] = b
        idx += 1
    payload[idx] = 0
    for i, b in enumerate(str(filesize).encode()):
        payload[idx + 1 + i] = b

    crc = crc16_xmodem(bytes(payload))
    header = bytes([SOH, 0x00, 0xFF])
    footer = crc.to_bytes(2, 'big')
    return header + bytes(payload) + footer


def split_file_to_packets(buffer: bytes, max_packet_size: int = PACKET_128):
    packets = []
    total = len(buffer)
    block = 1
    offset = 0
    while offset < total:
        remaining = total - offset
        psize = min(remaining, max_packet_size)
        if psize < PACKET_128:
            psize = PACKET_128
        chunk = buffer[offset:offset + psize]
        packets.append(build_packet(chunk, block, psize))
        block += 1
        offset += psize
    return packets


# Global receive buffer (preserves unconsumed data across wait_char calls)
_receive_buf = bytearray()

def _extract_from_buffer(valid: list) -> int | None:
    """Extract the first matching byte from the global buffer and remove prior data"""
    for i, b in enumerate(_receive_buf):
        if b in valid:
            found = _receive_buf[i]
            del _receive_buf[:i + 1]
            return found
    return None

def _fill_buffer(ser: serial.Serial, timeout: float, debug: bool):
    """Read serial data into the global buffer"""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if ser.in_waiting:
            data = ser.read(ser.in_waiting)
            if debug:
                print(f"  [RX] {data!r}", end="")
            _receive_buf.extend(data)
            return
        time.sleep(0.01)

def wait_char(ser: serial.Serial, valid: list, timeout: float = 15.0, debug: bool = False) -> int:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        result = _extract_from_buffer(valid)
        if result is not None:
            if debug:
                print()
            return result
        _fill_buffer(ser, 1.0, debug)
    if debug and _receive_buf:
        print()
    detail = f"  Received {len(_receive_buf)} bytes: {bytes(_receive_buf[:64])!r}" if _receive_buf else "  No data received"
    raise TimeoutError(f"Timeout waiting for data from receiver\n{detail}")


def ymodem_send(ser: serial.Serial, filename: str, filepath: str,
                progress_cb=None, pre_cmd: str | None = None,
                debug: bool = False):
    with open(filepath, 'rb') as f:
        file_buffer = f.read()

    packets = split_file_to_packets(file_buffer)
    total_bytes = len(file_buffer)

    def log(msg):
        print(f"  {msg}")

    base = os.path.basename(filename)

    # Clear buffers to ensure a clean state
    global _receive_buf
    _receive_buf = bytearray()
    ser.reset_input_buffer()

    # ── Send pre-command to put receiver into YMODEM mode, then wait for 'C' ──
    if pre_cmd:
        cmd_bytes = (pre_cmd if pre_cmd.endswith("\n") else pre_cmd + "\n").encode()
        log(f"Sending pre-command: {repr(cmd_bytes)}")
        ser.write(cmd_bytes)

    log("Waiting for 'C' ...")
    wait_char(ser, [ASCII_C], debug=debug)
    log("Sending file header ...")
    ser.write(build_header_packet(base, total_bytes))
    log("Waiting for ACK ...")
    wait_char(ser, [ACK], debug=debug)
    log("Waiting for 'C' ...")
    wait_char(ser, [ASCII_C], debug=debug)

    log("Starting data transfer ...")
    for i, pkt in enumerate(packets):
        retries = 3
        while retries > 0:
            ser.write(pkt)
            result = wait_char(ser, [ACK, NAK, CA], debug=debug)
            _receive_buf.clear()
            if result == ACK:
                break
            if result == CA:
                raise RuntimeError(f"Receiver cancelled (CAN) at block {i+1}")
            log(f"NAK for block {i+1}, retransmitting... ({retries})")
            retries -= 1
        if retries == 0:
            raise RuntimeError(f"Block {i+1} failed after 3 retries")
        sent = min((i + 1) * PACKET_128, total_bytes)
        if debug:
            print(f"  Block {i + 1}/{len(packets)}  {sent}/{total_bytes} bytes", end='\r')
        else:
            pct = sent * 100 // total_bytes
            print(f"  Progress: {pct}%", end='\r')
    print()
    log("Sending EOT ...")
    ser.write(bytes([EOT]))
    _receive_buf.clear()
    log("Waiting for NAK ...")
    try:
        result = wait_char(ser, [NAK, ACK], debug=debug, timeout=10.0)
        if result == ACK:
            log("Got ACK (receiver skipped NAK phase)")
    except TimeoutError:
        log("No NAK received, sending EOT anyway...")
    log("Sending EOT ...")
    ser.write(bytes([EOT]))
    _receive_buf.clear()
    log("Waiting for ACK ...")
    wait_char(ser, [ACK], debug=debug)
    # DON'T clear here — 'C' arrives alongside ACK (\x06\x43)
    log("Waiting for 'C' ...")
    wait_char(ser, [ASCII_C], debug=debug)
    _receive_buf.clear()
    log("Sending empty packet (end) ...")
    ser.write(build_header_packet('', 0))
    log("Waiting for ACK ...")
    wait_char(ser, [ACK], debug=debug)

    print("✓ Transfer complete!")

    return {
        "filePath": filepath,
        "totalBytes": total_bytes,
        "writtenBytes": total_bytes,
    }


# ── History ──────────────────────────────────────────────────────────────────────
def load_history():
    if HISTORY_FILE.exists():
        try:
            data = json.loads(HISTORY_FILE.read_text())
            return data if isinstance(data, list) else []
        except Exception:
            return []
    return []


def save_history(history: list):
    HISTORY_FILE.write_text(json.dumps(history, ensure_ascii=False, indent=2))


def add_to_history(filepath: str):
    history = load_history()
    abspath = os.path.abspath(filepath)
    if abspath in history:
        history.remove(abspath)
    history.insert(0, abspath)
    if len(history) > MAX_HISTORY:
        history = history[:MAX_HISTORY]
    save_history(history)


def pick_from_history(history: list) -> str | None:
    if not history:
        return None

    from collections import Counter
    base_counts = Counter(os.path.basename(fp) for fp in history)
    print("\nRecently sent files:")
    print("  " + "-" * 50)
    for i, fp in enumerate(history):
        basename = os.path.basename(fp)
        dirname = os.path.dirname(fp)
        label = basename if base_counts[basename] == 1 else f"{basename}  ← {dirname}"
        if len(label) > 60:
            label = label[:57] + "..."
        size = os.path.getsize(fp) if os.path.isfile(fp) else -1
        size_str = f"({size/1024:.0f}KB)" if size >= 0 else "(not found)"
        print(f"  [{i}] {label} {size_str}")
    print("  [-] Browse with file picker...")
    print()

    while True:
        choice = input("Choose [0-{}] or press Enter to skip: ".format(len(history) - 1)).strip()
        if choice == "":
            return None
        if choice == "-":
            return None
        try:
            idx = int(choice)
            if 0 <= idx < len(history):
                return history[idx]
        except ValueError:
            pass
        print("  Invalid choice, try again")


# ── File Picker (multi-platform) ──────────────────────────────────────────────
def _pick_file_macos() -> str | None:
    script = """
    set theFile to choose file with prompt "Select the file to send"
    set thePath to POSIX path of theFile
    return thePath
    """
    try:
        result = subprocess.run(
            ["osascript", "-e", script],
            capture_output=True, text=True, timeout=30
        )
        if result.returncode == 0:
            path = result.stdout.strip()
            return path if path else None
        return None
    except (subprocess.TimeoutExpired, FileNotFoundError):
        return None


def _pick_file_linux() -> str | None:
    for cmd in (["zenity", "--file-selection"], ["kdialog", "--getopenfilename", "."]):
        try:
            result = subprocess.run(cmd, capture_output=True, text=True, timeout=30)
            if result.returncode == 0:
                path = result.stdout.strip()
                return path if path else None
        except (subprocess.TimeoutExpired, FileNotFoundError):
            continue
    return None


def _pick_file_windows() -> str | None:
    script = """
    Add-Type -AssemblyName System.Windows.Forms
    $f = New-Object System.Windows.Forms.OpenFileDialog
    $f.ShowDialog() | Out-Null
    Write-Output $f.FileName
    """
    try:
        result = subprocess.run(
            ["powershell", "-NoProfile", "-Command", script],
            capture_output=True, text=True, timeout=30
        )
        if result.returncode == 0:
            path = result.stdout.strip()
            return path if path else None
        return None
    except (subprocess.TimeoutExpired, FileNotFoundError):
        return None


def _pick_file_tkinter() -> str | None:
    try:
        import tkinter as tk
        from tkinter import filedialog
        root = tk.Tk()
        root.withdraw()
        path = filedialog.askopenfilename(title="Select the file to send")
        root.destroy()
        return path if path else None
    except ImportError:
        return None


def pick_file_with_dialog() -> str | None:
    import platform
    system = platform.system()
    if system == "Darwin":
        path = _pick_file_macos()
        if path:
            return path
    elif system == "Linux":
        path = _pick_file_linux()
        if path:
            return path
    elif system == "Windows":
        path = _pick_file_windows()
        if path:
            return path

    return _pick_file_tkinter()


# ── Serial Port Helpers ───────────────────────────────────────────────────────
def list_serial_ports():
    import serial.tools.list_ports
    ports = serial.tools.list_ports.comports()
    return ports


def select_port_interactive() -> str:
    ports = list_serial_ports()
    if not ports:
        print("No serial ports found!")
        sys.exit(1)

    print("\nAvailable serial ports:")
    for i, p in enumerate(ports):
        print(f"  [{i}] {p.device}  -  {p.description}")
    print()

    while True:
        choice = input(f"Select port [0-{len(ports) - 1}]: ").strip()
        try:
            idx = int(choice)
            if 0 <= idx < len(ports):
                return ports[idx].device
        except ValueError:
            pass
        print("  Invalid choice, try again")


# ── Main ──────────────────────────────────────────────────────────────────────
def main():
    parser = argparse.ArgumentParser(description="YMODEM serial file sender tool")
    parser.add_argument("--port", "-p", help="Serial port device path")
    parser.add_argument("--baud", "-b", type=int, default=115200, help="Baud rate (default: 115200)")
    parser.add_argument("--file", "-f", help="File path (leave empty to pick from history or file dialog)")
    parser.add_argument("--cmd", default="yrcv",
                        help="Pre-command to put receiver into YMODEM mode (default: yrcv)")
    parser.add_argument("--no-cmd", action="store_true",
                        help="Do not send pre-command, assume receiver is already in YMODEM mode")
    parser.add_argument("--debug", "-d", action="store_true",
                        help="Show raw serial receive data")
    args = parser.parse_args()

    # ── 1) Select file ───────────────────────────────────────────────────────────
    filepath = None

    if args.file:
        filepath = os.path.abspath(args.file)
        if not os.path.isfile(filepath):
            print(f"File not found: {filepath}")
            sys.exit(1)
    else:
        history = load_history()
        if history:
            picked = pick_from_history(history)
            if picked:
                filepath = picked

        if not filepath:
            filepath = pick_file_with_dialog()
            if not filepath:
                print("No file selected, exiting")
                sys.exit(0)

    filepath = os.path.abspath(filepath)
    print(f"\n📄 Sending file: {filepath}")
    add_to_history(filepath)

    # ── 2) Select port ───────────────────────────────────────────────────────────
    port = args.port or select_port_interactive()
    baud = args.baud

    # ── 3) Connect serial port ──────────────────────────────────────────────────
    print(f"\n🔌 Connecting to port: {port} @ {baud}")
    try:
        ser = serial.Serial(port, baud, timeout=1)
    except serial.SerialException as e:
        print(f"Serial port connection failed: {e}")
        sys.exit(1)

    # ── 4) Test serial port connectivity ─────────────────────────────────────────
    if not args.no_cmd:
        ser.reset_input_buffer()
        test_cmd = b"sysinfo\n"
        print(f"\n📡 Sending test command: {test_cmd!r}")
        ser.write(test_cmd)
        deadline = time.monotonic() + 3.0
        buf = bytearray()
        while time.monotonic() < deadline:
            if ser.in_waiting:
                data = ser.read(ser.in_waiting)
                buf.extend(data)
            else:
                time.sleep(0.05)
        if buf:
            print(f"  Received {len(buf)} bytes: {bytes(buf[:128])!r}")
            print("  ✓ Serial communication OK")
        else:
            print("  ⚠ No reply received!")
            print("  Possible reasons:")
            print("    - Wrong serial port selected (try another if multiple USB ports are present)")
            print("    - Device not powered on or not ready")
            ans = input("  Continue sending file? [y/N] ").strip().lower()
            if ans != "y":
                ser.close()
                sys.exit(0)

    basename = os.path.basename(filepath)
    pre_cmd = None if args.no_cmd else args.cmd
    print(f"\nStarting YMODEM send: {basename}")
    if pre_cmd:
        print(f"  Pre-command: {pre_cmd}")
    try:
        ymodem_send(ser, basename, filepath, pre_cmd=pre_cmd, debug=args.debug)
    except TimeoutError as e:
        print(f"\n✗ Timeout: {e}")
        print("Make sure the receiver has entered YMODEM receive mode")
        sys.exit(1)
    except KeyboardInterrupt:
        print("\n\nCancelled")
    except Exception as e:
        print(f"\n✗ Send failed: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)
    finally:
        if ser.is_open:
            ser.close()


if __name__ == "__main__":
    main()
