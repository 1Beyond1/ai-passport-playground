"""Stream Windows desktop metrics to the AI Passport Desktop Monitor."""

from __future__ import annotations

import argparse
import ctypes
import json
import math
import os
import platform
import queue
import re
import shutil
import subprocess
import threading
import time
from functools import lru_cache
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from typing import Optional
from urllib import error as urllib_error
from urllib import request as urllib_request

import psutil
import serial
from serial import SerialException


DEFAULT_PORT = "COM3"
DEFAULT_INTERVAL = 1.0
DEFAULT_LHM_DATA_URL = "http://127.0.0.1:8085/data.json"
DEFAULT_HTTP_HOST = "127.0.0.1"
DEFAULT_HTTP_PORT = 8765


class StatusStreamHub:
    """Fan out the latest PCSTAT records to local browser clients."""

    def __init__(self) -> None:
        self._clients: list[queue.Queue[str]] = []
        self._lock = threading.Lock()

    def subscribe(self) -> queue.Queue[str]:
        client: queue.Queue[str] = queue.Queue(maxsize=4)
        with self._lock:
            self._clients.append(client)
        return client

    def unsubscribe(self, client: queue.Queue[str]) -> None:
        with self._lock:
            if client in self._clients:
                self._clients.remove(client)

    def publish(self, payload: bytes) -> None:
        line = payload.decode("ascii", "replace").strip()
        if not line:
            return
        with self._lock:
            clients = tuple(self._clients)
        for client in clients:
            try:
                client.put_nowait(line)
            except queue.Full:
                try:
                    client.get_nowait()
                except queue.Empty:
                    pass
                try:
                    client.put_nowait(line)
                except queue.Full:
                    pass

    def client_count(self) -> int:
        with self._lock:
            return len(self._clients)


class BridgeRequestHandler(BaseHTTPRequestHandler):
    """Serve a localhost-only SSE stream for the browser simulator."""

    hub: StatusStreamHub
    server_version = "AIPASSPORT-Bridge/1.0"

    def _write_cors(self) -> None:
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Cache-Control", "no-cache, no-transform")

    def do_GET(self) -> None:  # noqa: N802 - stdlib handler API
        if self.path == "/health":
            payload = json.dumps(
                {"ok": True, "protocol": "PCSTAT", "clients": self.hub.client_count()}
            ).encode("ascii")
            self.send_response(200)
            self._write_cors()
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Content-Length", str(len(payload)))
            self.end_headers()
            self.wfile.write(payload)
            return

        if self.path != "/stream":
            self.send_error(404, "Use /health or /stream")
            return

        client = self.hub.subscribe()
        self.send_response(200)
        self._write_cors()
        self.send_header("Connection", "keep-alive")
        self.send_header("Content-Type", "text/event-stream; charset=utf-8")
        self.end_headers()
        try:
            self.wfile.write(b": connected\n\n")
            self.wfile.flush()
            while True:
                try:
                    line = client.get(timeout=15.0)
                    payload = f"data: {line}\n\n".encode("ascii", "replace")
                except queue.Empty:
                    payload = b": keepalive\n\n"
                self.wfile.write(payload)
                self.wfile.flush()
        except (BrokenPipeError, ConnectionResetError, OSError):
            pass
        finally:
            self.hub.unsubscribe(client)

    def log_message(self, format: str, *args: object) -> None:
        # The bridge's metric log is more useful than one line per browser poll.
        return


def start_status_server(host: str, port: int, hub: StatusStreamHub) -> ThreadingHTTPServer:
    """Start the browser stream without blocking the metric/serial loop."""

    BridgeRequestHandler.hub = hub
    server = ThreadingHTTPServer((host, port), BridgeRequestHandler)
    server.daemon_threads = True
    thread = threading.Thread(
        target=server.serve_forever,
        name="desktop-bridge-http",
        daemon=True,
    )
    thread.start()
    return server


@lru_cache(maxsize=1)
def nvidia_smi_path() -> Optional[str]:
    """Find NVIDIA's query utility once, without requiring a shell."""

    return shutil.which("nvidia-smi")


def parse_sensor_number(value: str) -> Optional[int]:
    """Convert a hardware-monitor value such as N/A into a safe integer."""

    try:
        number = float(value.strip())
    except (TypeError, ValueError):
        return None
    if not math.isfinite(number):
        return None
    return round(number)


def _parse_lhm_value(value: object) -> Optional[float]:
    """Parse a numeric LibreHardwareMonitor REST value."""

    if isinstance(value, (int, float)) and not isinstance(value, bool):
        number = float(value)
    else:
        match = re.search(r"[-+]?\d+(?:\.\d+)?", str(value or ""))
        if not match:
            return None
        try:
            number = float(match.group(0))
        except ValueError:
            return None
    return number if math.isfinite(number) else None


def _iter_lhm_nodes(node: object):
    """Yield every node in LibreHardwareMonitor's nested data.json tree."""

    if isinstance(node, dict):
        yield node
        for child in node.get("Children", []):
            yield from _iter_lhm_nodes(child)
    elif isinstance(node, list):
        for child in node:
            yield from _iter_lhm_nodes(child)


def read_lhm_cpu_temperature_rest() -> Optional[int]:
    """Read a CPU package/core temperature from LHM's loopback REST endpoint."""

    request = urllib_request.Request(
        DEFAULT_LHM_DATA_URL,
        headers={"Accept": "application/json", "User-Agent": "AIPASSPORT desktop bridge"},
    )
    try:
        with urllib_request.urlopen(request, timeout=0.6) as response:
            payload = json.loads(response.read(2_000_000).decode("utf-8"))
    except (OSError, TimeoutError, UnicodeError, ValueError, urllib_error.URLError):
        return None

    candidates: list[tuple[int, float]] = []
    for node in _iter_lhm_nodes(payload):
        sensor_type = str(node.get("Type", "")).lower()
        name = " ".join(
            str(node.get(key, ""))
            for key in ("Text", "Name", "SensorName", "Identifier")
        ).lower()
        if sensor_type and sensor_type != "temperature" and "temperature" not in name:
            continue
        if any(token in name for token in ("gpu", "graphics", "vram", "hot spot")):
            continue
        if not any(token in name for token in ("cpu", "processor", "package", "tctl", "tdie", "ccd", "core")):
            continue
        value = _parse_lhm_value(node.get("RawValue", node.get("Value")))
        if value is None or not -40 <= value <= 150:
            continue
        priority = 1
        if any(token in name for token in ("cpu", "processor")):
            priority += 4
        if any(token in name for token in ("package", "tctl", "tdie", "ccd")):
            priority += 2
        candidates.append((priority, value))

    if not candidates:
        return None
    best_priority = max(priority for priority, _ in candidates)
    return round(max(value for priority, value in candidates if priority == best_priority))


def read_nvidia_metrics() -> dict[str, int]:
    """Read the first NVIDIA GPU's live load, graphics clock and temperature."""

    executable = nvidia_smi_path()
    if not executable:
        return {}
    command = [
        executable,
        "--query-gpu=utilization.gpu,clocks.gr,temperature.gpu",
        "--format=csv,noheader,nounits",
    ]
    try:
        completed = subprocess.run(
            command,
            capture_output=True,
            text=True,
            timeout=0.8,
            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
            check=False,
        )
    except (OSError, subprocess.SubprocessError):
        return {}
    if completed.returncode != 0:
        return {}
    row = next((line.strip() for line in completed.stdout.splitlines() if line.strip()), "")
    values = [parse_sensor_number(part) for part in row.split(",", 2)]
    if len(values) != 3:
        return {}
    metrics: dict[str, int] = {}
    if values[0] is not None and 0 <= values[0] <= 100:
        metrics["gpu"] = values[0]
    if values[1] is not None and values[1] >= 0:
        metrics["gpu_mhz"] = values[1]
    if values[2] is not None and -40 <= values[2] <= 150:
        metrics["gpu_c"] = values[2]
    return metrics


def read_cpu_frequency_mhz() -> Optional[int]:
    """Read the current aggregate CPU clock in MHz when the OS exposes it."""

    try:
        frequency = psutil.cpu_freq()
    except (AttributeError, OSError):
        return None
    if frequency is None or frequency.current <= 0:
        return None
    return round(frequency.current)


if os.name == "nt":
    class _PdhCounterValue(ctypes.Structure):
        _fields_ = [("status", ctypes.c_ulong), ("double_value", ctypes.c_double)]


class CpuFrequencySampler:
    """Sample effective CPU clock instead of Windows' nominal clock value."""

    def __init__(self) -> None:
        self._pdh = None
        self._query = ctypes.c_void_p()
        self._base_counter = ctypes.c_void_p()
        self._performance_counter = ctypes.c_void_p()
        self._ready = False
        self._open()

    def _open(self) -> None:
        if os.name != "nt":
            return
        try:
            pdh = ctypes.windll.pdh
            pdh.PdhOpenQueryW.argtypes = [ctypes.c_wchar_p, ctypes.c_void_p, ctypes.POINTER(ctypes.c_void_p)]
            pdh.PdhOpenQueryW.restype = ctypes.c_ulong
            pdh.PdhAddEnglishCounterW.argtypes = [ctypes.c_void_p, ctypes.c_wchar_p, ctypes.c_void_p, ctypes.POINTER(ctypes.c_void_p)]
            pdh.PdhAddEnglishCounterW.restype = ctypes.c_ulong
            pdh.PdhCollectQueryData.argtypes = [ctypes.c_void_p]
            pdh.PdhCollectQueryData.restype = ctypes.c_ulong
            pdh.PdhGetFormattedCounterValue.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.POINTER(ctypes.c_ulong), ctypes.POINTER(_PdhCounterValue)]
            pdh.PdhGetFormattedCounterValue.restype = ctypes.c_ulong
            pdh.PdhCloseQuery.argtypes = [ctypes.c_void_p]
            pdh.PdhCloseQuery.restype = ctypes.c_ulong
        except AttributeError:
            return

        self._pdh = pdh
        if pdh.PdhOpenQueryW(None, 0, ctypes.byref(self._query)) != 0:
            self._pdh = None
            return
        base_path = r"\Processor Information(_Total)\Processor Frequency"
        performance_path = r"\Processor Information(_Total)\% Processor Performance"
        if pdh.PdhAddEnglishCounterW(
            self._query, base_path, 0, ctypes.byref(self._base_counter)
        ) != 0 or pdh.PdhAddEnglishCounterW(
            self._query, performance_path, 0, ctypes.byref(self._performance_counter)
        ) != 0:
            self.close()
            return
        # Seed the two-sample counters. The next call, after the bridge's
        # normal one-second delay, produces an effective clock value.
        if pdh.PdhCollectQueryData(self._query) != 0:
            self.close()
            return
        self._ready = True

    def sample(self) -> Optional[int]:
        """Return base frequency × processor performance when available."""

        if not self._ready or self._pdh is None:
            return read_cpu_frequency_mhz()
        if self._pdh.PdhCollectQueryData(self._query) != 0:
            return read_cpu_frequency_mhz()

        def formatted(counter: ctypes.c_void_p) -> Optional[float]:
            value = _PdhCounterValue()
            status = self._pdh.PdhGetFormattedCounterValue(
                counter, 0x00000200, None, ctypes.byref(value)
            )
            if status != 0 or value.status != 0 or not math.isfinite(value.double_value):
                return None
            return value.double_value

        base = formatted(self._base_counter)
        performance = formatted(self._performance_counter)
        if base is not None and performance is not None and base > 0 and performance > 0:
            return round(base * performance / 100.0)
        return round(base) if base is not None and base > 0 else read_cpu_frequency_mhz()

    def close(self) -> None:
        if self._pdh is not None and self._query:
            self._pdh.PdhCloseQuery(self._query)
        self._pdh = None
        self._ready = False


def read_cpu_temperature() -> Optional[int]:
    """Read a CPU temperature from psutil or LibreHardwareMonitor if available."""

    sensors = getattr(psutil, "sensors_temperatures", None)
    if sensors is not None:
        try:
            groups = sensors()
        except (AttributeError, OSError):
            groups = {}
        candidates = []
        for entries in groups.values():
            for entry in entries:
                name = str(getattr(entry, "label", "") or getattr(entry, "name", ""))
                if any(token in name.lower() for token in ("cpu", "package", "tctl", "tdie")):
                    current = getattr(entry, "current", None)
                    if current is not None:
                        candidates.append(float(current))
        if candidates:
            return round(max(candidates))

    # Windows does not expose CPU package temperature through psutil. Prefer
    # LibreHardwareMonitor's loopback REST endpoint when it is enabled; this
    # avoids relying on the WMI exporter, which is unavailable in some LHM
    # builds. The query is cached by SensorCache below.
    temperature = read_lhm_cpu_temperature_rest()
    if temperature is not None:
        return temperature

    # Keep the read-only WMI namespace as a fallback for older LHM builds.
    if os.name != "nt":
        return None
    powershell = shutil.which("powershell") or shutil.which("pwsh")
    if not powershell:
        return None
    query = (
        "$s = Get-CimInstance -Namespace root\\LibreHardwareMonitor "
        "-ClassName Sensor -ErrorAction SilentlyContinue | "
        "Where-Object { $_.SensorType -eq 'Temperature' -and "
        "$_.Name -match 'CPU|Tctl|Tdie|Package' }; "
        "if ($s) { ($s | Sort-Object Value -Descending | "
        "Select-Object -First 1 -ExpandProperty Value) }"
    )
    try:
        completed = subprocess.run(
            [powershell, "-NoProfile", "-NonInteractive", "-Command", query],
            capture_output=True,
            text=True,
            timeout=1.5,
            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
            check=False,
        )
    except (OSError, subprocess.SubprocessError):
        return None
    if completed.returncode != 0:
        return None
    value = next((line.strip() for line in completed.stdout.splitlines() if line.strip()), "")
    temperature = parse_sensor_number(value)
    return temperature if temperature is not None and -40 <= temperature <= 150 else None


class SensorCache:
    """Keep slower optional sensor discovery off the one-second sample path."""

    def __init__(self) -> None:
        self.cpu_temperature: Optional[int] = None
        self.last_refresh = 0.0

    def refresh(self, now: float) -> None:
        if now - self.last_refresh < 5.0:
            return
        self.cpu_temperature = read_cpu_temperature()
        self.last_refresh = now


if os.name == "nt":
    class _DisplayPosition(ctypes.Structure):
        _fields_ = [
            ("x", ctypes.c_long),
            ("y", ctypes.c_long),
            ("orientation", ctypes.c_ulong),
            ("fixed_output", ctypes.c_ulong),
        ]


    class _DisplaySettings(ctypes.Structure):
        _fields_ = [
            ("color", ctypes.c_short),
            ("duplex", ctypes.c_short),
            ("y_resolution", ctypes.c_short),
            ("tt_option", ctypes.c_short),
            ("collate", ctypes.c_short),
            # Windows WCHAR is two bytes even when Python's c_wchar is wider.
            ("form_name", ctypes.c_ushort * 32),
            ("log_pixels", ctypes.c_ushort),
            ("bits_per_pel", ctypes.c_ulong),
            ("pels_width", ctypes.c_ulong),
            ("pels_height", ctypes.c_ulong),
            ("display_flags", ctypes.c_ulong),
            ("display_frequency", ctypes.c_ulong),
        ]


    class _DevMode(ctypes.Structure):
        _fields_ = [
            ("device_name", ctypes.c_ushort * 32),
            ("spec_version", ctypes.c_ushort),
            ("driver_version", ctypes.c_ushort),
            ("size", ctypes.c_ushort),
            ("driver_extra", ctypes.c_ushort),
            ("fields", ctypes.c_ulong),
            # EnumDisplaySettingsW fills both the position and mode-settings
            # portions in sequence on Windows, despite the SDK declaration's
            # anonymous union. Keep this ABI layout for the API call.
            ("position", _DisplayPosition),
            ("settings", _DisplaySettings),
            ("icm_method", ctypes.c_ulong),
            ("icm_intent", ctypes.c_ulong),
            ("media_type", ctypes.c_ulong),
            ("dither_type", ctypes.c_ulong),
            ("reserved1", ctypes.c_ulong),
            ("reserved2", ctypes.c_ulong),
            ("panning_width", ctypes.c_ulong),
            ("panning_height", ctypes.c_ulong),
        ]


def read_display_refresh_hz() -> Optional[int]:
    """Read the current primary-display refresh rate from Windows."""

    if os.name != "nt":
        return None
    mode = _DevMode()
    mode.size = ctypes.sizeof(_DevMode)
    try:
        ok = ctypes.windll.user32.EnumDisplaySettingsW(None, -1, ctypes.byref(mode))
    except (AttributeError, OSError):
        return None
    frequency = int(mode.settings.display_frequency)
    return frequency if ok and 0 < frequency <= 1000 else None


def format_uptime(seconds: float) -> str:
    """Format host uptime in the compact form used by the device UI."""

    total_minutes = max(0, int(seconds // 60))
    days, remainder = divmod(total_minutes, 24 * 60)
    hours, minutes = divmod(remainder, 60)
    if days:
        return f"{days}d{hours}h"
    if hours:
        return f"{hours}h{minutes}m"
    return f"{minutes}m"


def safe_host_name() -> str:
    """Return an ASCII-safe host label that fits the device buffer."""

    host = platform.node() or "Windows"
    host = host.replace("|", "_").replace("=", "_")
    return host.encode("ascii", "replace").decode("ascii")[:20] or "Windows"


def open_serial(port_name: str) -> serial.Serial:
    """Open USB Serial/JTAG without toggling DTR/RTS into a reset."""

    connection = serial.Serial(
        port=None,
        baudrate=115200,
        timeout=0.2,
        write_timeout=1.0,
        dsrdtr=False,
        rtscts=False,
    )
    connection.port = port_name
    connection.dtr = False
    connection.rts = False
    connection.open()
    return connection


def build_status_line(
    *,
    network_before: object,
    network_after: object,
    elapsed: float,
    cpu: float,
    memory: float,
    disk: float,
    cpu_mhz: Optional[int] = None,
    gpu: Optional[dict[str, int]] = None,
    cpu_temp_c: Optional[int] = None,
    refresh_hz: Optional[int] = None,
) -> bytes:
    """Build one newline-delimited PCSTAT record for the device."""

    elapsed = max(elapsed, 0.001)
    rx_rate = max(0, network_after.bytes_recv - network_before.bytes_recv)
    tx_rate = max(0, network_after.bytes_sent - network_before.bytes_sent)
    rx_mb = rx_rate / elapsed / (1024 * 1024)
    tx_mb = tx_rate / elapsed / (1024 * 1024)
    fields = [
        f"PCSTAT|cpu={round(max(0, min(100, cpu)))}",
        f"mem={round(max(0, min(100, memory)))}",
        f"disk={round(max(0, min(100, disk)))}",
        f"rx={rx_mb:.1f}",
        f"tx={tx_mb:.1f}",
    ]
    if cpu_mhz is not None and cpu_mhz > 0:
        fields.append(f"cpu_mhz={cpu_mhz}")
    if gpu:
        if "gpu" in gpu:
            fields.append(f"gpu={round(max(0, min(100, gpu['gpu'])))}")
        if "gpu_mhz" in gpu:
            fields.append(f"gpu_mhz={max(0, gpu['gpu_mhz'])}")
        if "gpu_c" in gpu:
            fields.append(f"gpu_c={gpu['gpu_c']}")
    if cpu_temp_c is not None:
        fields.append(f"cpu_c={cpu_temp_c}")
    if refresh_hz is not None and refresh_hz > 0:
        fields.append(f"hz={refresh_hz}")
    fields.extend([
        f"time={time.strftime('%H:%M')}",
        f"uptime={format_uptime(time.time() - psutil.boot_time())}",
        f"host={safe_host_name()}",
    ])
    line = "|".join(fields) + "\n"
    return line.encode("ascii", "replace")


def parse_args() -> argparse.Namespace:
    """Parse bridge command-line options."""

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default=DEFAULT_PORT, help="USB Serial/JTAG port")
    parser.add_argument(
        "--interval",
        type=float,
        default=DEFAULT_INTERVAL,
        help="seconds between status records (default: 1)",
    )
    parser.add_argument(
        "--http-host",
        default=DEFAULT_HTTP_HOST,
        help="local host for the simulator SSE stream (default: 127.0.0.1)",
    )
    parser.add_argument(
        "--http-port",
        type=int,
        default=DEFAULT_HTTP_PORT,
        help="local port for /health and /stream (default: 8765)",
    )
    parser.add_argument(
        "--no-http",
        action="store_true",
        help="disable the local browser stream",
    )
    parser.add_argument("--once", action="store_true", help="send one record and exit")
    return parser.parse_args()


def main() -> int:
    """Collect and stream metrics until interrupted."""

    args = parse_args()
    interval = max(0.2, args.interval)
    psutil.cpu_percent(interval=None)
    previous_network = psutil.net_io_counters()
    previous_time = time.monotonic()
    sensor_cache = SensorCache()
    cpu_frequency = CpuFrequencySampler()
    connection: Optional[serial.Serial] = None
    stream_hub = StatusStreamHub()
    stream_server: Optional[ThreadingHTTPServer] = None

    if not args.no_http:
        try:
            stream_server = start_status_server(args.http_host, args.http_port, stream_hub)
            print(
                f"Browser stream: http://{args.http_host}:{args.http_port}/stream",
                flush=True,
            )
        except OSError as error:
            print(f"Browser stream unavailable: {error}", flush=True)

    try:
        while True:
            if connection is None:
                try:
                    connection = open_serial(args.port)
                    print(f"Connected to {args.port}", flush=True)
                except (SerialException, OSError) as error:
                    print(f"Waiting for {args.port}: {error}", flush=True)
                    if args.once:
                        return 1

            time.sleep(interval)
            now = time.monotonic()
            current_network = psutil.net_io_counters()
            system_drive = os.environ.get("SystemDrive", "C:")
            current_disk = psutil.disk_usage(f"{system_drive}\\").percent
            sensor_cache.refresh(now)
            payload = build_status_line(
                network_before=previous_network,
                network_after=current_network,
                elapsed=now - previous_time,
                cpu=psutil.cpu_percent(interval=None),
                memory=psutil.virtual_memory().percent,
                disk=current_disk,
                cpu_mhz=cpu_frequency.sample(),
                gpu=read_nvidia_metrics(),
                cpu_temp_c=sensor_cache.cpu_temperature,
                refresh_hz=read_display_refresh_hz(),
            )
            previous_network = current_network
            previous_time = now

            # The browser gets the same snapshot as the device. This avoids
            # opening the device's COM port a second time from Web Serial.
            stream_hub.publish(payload)
            if connection is not None:
                try:
                    connection.write(payload)
                    print(payload.decode("ascii").rstrip(), flush=True)
                except (SerialException, OSError) as error:
                    print(f"Serial connection lost: {error}", flush=True)
                    connection.close()
                    connection = None
                    if args.once:
                        return 1

            if args.once:
                return 0
    except KeyboardInterrupt:
        print("Stopping desktop bridge", flush=True)
        return 0
    finally:
        if connection is not None and connection.is_open:
            connection.close()
        cpu_frequency.close()
        if stream_server is not None:
            stream_server.shutdown()
            stream_server.server_close()


if __name__ == "__main__":
    raise SystemExit(main())
