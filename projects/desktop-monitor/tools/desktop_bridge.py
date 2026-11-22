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
from typing import Any, Callable, Iterable, Optional
from urllib import error as urllib_error
from urllib import request as urllib_request

import psutil

try:
    import serial
    from serial import SerialException
    from serial.tools import list_ports
except ImportError:  # pragma: no cover - optional when running --http-only
    serial = None  # type: ignore[assignment]
    list_ports = None  # type: ignore[assignment]

    class SerialException(OSError):
        """Fallback serial error when pyserial is not installed."""


DEFAULT_PORT = "auto"
DEFAULT_INTERVAL = 1.0
DEFAULT_LHM_DATA_URL = "http://127.0.0.1:8085/data.json"
DEFAULT_HTTP_HOST = "127.0.0.1"
DEFAULT_HTTP_PORT = 8765
ESPRESSIF_USB_VID = 0x303A
ESPRESSIF_JTAG_PID = 0x1001
BYTES_PER_GIB = 1024 ** 3
# Firmware desktop_state.c rejects lines with strlen(line) >= 256 (DESKTOP_LINE_LEN)
# and demo_desktop.c drops lines >= 255 bytes before '\n'. Keep encoded frames
# including '\n' at most 255 bytes so strlen(line) is always <= 255 (< 256).
MAX_PCSTAT_LINE_BYTES = 255
MAX_RATE_MB_S = 6553.5


class StatusStreamHub:
    """Fan out the latest PCSTAT records to local browser clients."""

    def __init__(self) -> None:
        self._clients: list[queue.Queue[str]] = []
        self._lock = threading.Lock()
        self._serial_port: Optional[str] = None
        self._serial_connected = False
        self._port_mode = "auto"

    def set_serial_status(
        self,
        *,
        port: Optional[str],
        connected: bool,
        mode: str,
    ) -> None:
        with self._lock:
            self._serial_port = port
            self._serial_connected = connected
            self._port_mode = mode

    def serial_status(self) -> dict[str, Any]:
        with self._lock:
            return {
                "port_mode": self._port_mode,
                "serial_port": self._serial_port,
                "serial_connected": self._serial_connected,
            }

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
        self.send_header("Access-Control-Allow-Methods", "GET, OPTIONS")
        self.send_header("Access-Control-Allow-Private-Network", "true")
        self.send_header("Cache-Control", "no-cache, no-transform")

    def do_OPTIONS(self) -> None:  # noqa: N802 - stdlib handler API
        self.send_response(204)
        self._write_cors()
        self.send_header("Content-Length", "0")
        self.end_headers()

    def do_GET(self) -> None:  # noqa: N802 - stdlib handler API
        if self.path == "/health":
            health_body = {
                "ok": True,
                "protocol": "PCSTAT",
                "clients": self.hub.client_count(),
                **self.hub.serial_status(),
            }
            payload = json.dumps(health_body).encode("ascii")
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

    handler_cls = type("BoundBridgeRequestHandler", (BridgeRequestHandler,), {"hub": hub})
    server = ThreadingHTTPServer((host, port), handler_cls)
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


def default_system_disk_path() -> str:
    """Return the system root path for psutil.disk_usage()."""

    if os.name == "nt":
        system_drive = os.environ.get("SystemDrive", "C:").rstrip("\\/")
        return f"{system_drive}\\"
    return "/"


def bytes_to_gb(byte_count: float) -> float:
    """Convert a byte count to GiB (reported as GB by Windows/macOS UIs)."""

    return max(0.0, float(byte_count)) / BYTES_PER_GIB


def is_auto_port(port_name: Optional[str]) -> bool:
    """Return True when the user asked for automatic Espressif port detection."""

    return bool(port_name and port_name.strip().lower() == "auto")


def _port_sort_key(port_info: Any) -> tuple[int, int, str]:
    pid = getattr(port_info, "pid", None)
    description = " ".join(
        str(getattr(port_info, attr, "") or "")
        for attr in ("description", "product", "interface", "manufacturer")
    ).lower()
    is_jtag_pid = 0 if pid == ESPRESSIF_JTAG_PID else 1
    is_jtag_desc = 0 if any(tok in description for tok in ("jtag", "serial", "espressif")) else 1
    device = str(getattr(port_info, "device", "") or "")
    return (is_jtag_pid, is_jtag_desc, device)


def list_espressif_ports(
    port_lister: Optional[Callable[[], Iterable[Any]]] = None,
) -> list[Any]:
    """Return connected serial ports whose USB VID matches Espressif (0x303A)."""

    lister = port_lister
    if lister is None:
        if list_ports is None:
            return []
        lister = list_ports.comports
    try:
        ports = list(lister())
    except OSError:
        return []
    matched = [
        port for port in ports
        if getattr(port, "vid", None) == ESPRESSIF_USB_VID and getattr(port, "device", None)
    ]
    matched.sort(key=_port_sort_key)
    return matched


def detect_espressif_port(
    port_lister: Optional[Callable[[], Iterable[Any]]] = None,
) -> Optional[str]:
    """Return the preferred Espressif USB Serial/JTAG port device, if present."""

    matched = list_espressif_ports(port_lister=port_lister)
    if not matched:
        return None
    return str(matched[0].device)


def resolve_serial_port(
    requested_port: str,
    port_lister: Optional[Callable[[], Iterable[Any]]] = None,
) -> Optional[str]:
    """Resolve '--port auto' via USB VID 0x303A, or pass explicit ports through."""

    if is_auto_port(requested_port):
        return detect_espressif_port(port_lister=port_lister)
    cleaned = requested_port.strip()
    return cleaned or None


def open_serial(port_name: str) -> Any:
    """Open USB Serial/JTAG without toggling DTR/RTS into a reset."""

    if serial is None:
        raise SerialException("pyserial is not installed")

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


def close_serial_safely(connection: Any) -> None:
    """Close a serial connection without raising secondary cleanup errors."""

    if connection is None:
        return
    try:
        if getattr(connection, "is_open", True):
            connection.close()
    except (SerialException, OSError):
        pass


def _valid_capacity_pair(used_gb: Optional[float], total_gb: Optional[float]) -> Optional[tuple[float, float]]:
    if used_gb is None or total_gb is None:
        return None
    try:
        used = float(used_gb)
        total = float(total_gb)
    except (TypeError, ValueError):
        return None
    if not (math.isfinite(used) and math.isfinite(total)):
        return None
    if total <= 0.0 or used < 0.0:
        return None
    return (min(used, total), total)


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
    mem_used_gb: Optional[float] = None,
    mem_total_gb: Optional[float] = None,
    disk_used_gb: Optional[float] = None,
    disk_total_gb: Optional[float] = None,
    time_text: Optional[str] = None,
    uptime_text: Optional[str] = None,
    host_name: Optional[str] = None,
) -> bytes:
    """Build one newline-delimited PCSTAT record for the device and simulator.

    Optional capacity fields (mem_used_gb, mem_total_gb, disk_used_gb,
    disk_total_gb) are appended as key=value tokens. The ESP32-C3 firmware
    parser (desktop_state.c) ignores unknown keys while requiring the total
    line length to remain strictly below DESKTOP_LINE_LEN (256 bytes).
    """

    elapsed = max(elapsed, 0.001)
    rx_rate = max(0, network_after.bytes_recv - network_before.bytes_recv)
    tx_rate = max(0, network_after.bytes_sent - network_before.bytes_sent)
    rx_mb = min(MAX_RATE_MB_S, rx_rate / elapsed / (1024 * 1024))
    tx_mb = min(MAX_RATE_MB_S, tx_rate / elapsed / (1024 * 1024))
    core_fields = [
        f"PCSTAT|cpu={round(max(0, min(100, cpu)))}",
        f"mem={round(max(0, min(100, memory)))}",
        f"disk={round(max(0, min(100, disk)))}",
        f"rx={rx_mb:.1f}",
        f"tx={tx_mb:.1f}",
    ]
    if cpu_mhz is not None and cpu_mhz > 0:
        core_fields.append(f"cpu_mhz={min(65535, cpu_mhz)}")
    if gpu:
        if "gpu" in gpu:
            core_fields.append(f"gpu={round(max(0, min(100, gpu['gpu'])))}")
        if "gpu_mhz" in gpu:
            core_fields.append(f"gpu_mhz={min(65535, max(0, gpu['gpu_mhz']))}")
        if "gpu_c" in gpu:
            core_fields.append(f"gpu_c={gpu['gpu_c']}")
    if cpu_temp_c is not None:
        core_fields.append(f"cpu_c={cpu_temp_c}")
    if refresh_hz is not None and refresh_hz > 0:
        core_fields.append(f"hz={min(65535, refresh_hz)}")

    mem_pair = _valid_capacity_pair(mem_used_gb, mem_total_gb)
    disk_pair = _valid_capacity_pair(disk_used_gb, disk_total_gb)
    mem_extra = (
        [f"mem_used_gb={mem_pair[0]:.1f}", f"mem_total_gb={mem_pair[1]:.1f}"]
        if mem_pair is not None
        else []
    )
    disk_extra = (
        [f"disk_used_gb={disk_pair[0]:.1f}", f"disk_total_gb={disk_pair[1]:.1f}"]
        if disk_pair is not None
        else []
    )

    resolved_time = time_text if time_text is not None else time.strftime("%H:%M")
    resolved_uptime = (
        uptime_text
        if uptime_text is not None
        else format_uptime(time.time() - psutil.boot_time())
    )
    resolved_host = (
        host_name.replace("|", "_").replace("=", "_").encode("ascii", "replace").decode("ascii")[:20] or "Windows"
        if host_name is not None
        else safe_host_name()
    )

    def encode_record(extras: list[str], host: str) -> bytes:
        all_fields = [
            *core_fields,
            *extras,
            f"time={resolved_time}",
            f"uptime={resolved_uptime}",
            f"host={host}",
        ]
        return ("|".join(all_fields) + "\n").encode("ascii", "replace")

    extras = [*mem_extra, *disk_extra]
    encoded = encode_record(extras, resolved_host)
    if len(encoded) > MAX_PCSTAT_LINE_BYTES and len(resolved_host) > 4:
        overflow = len(encoded) - MAX_PCSTAT_LINE_BYTES
        trimmed_host = resolved_host[: max(4, len(resolved_host) - overflow)]
        encoded = encode_record(extras, trimmed_host)
    if len(encoded) > MAX_PCSTAT_LINE_BYTES and disk_extra:
        encoded = encode_record(mem_extra, resolved_host)
    if len(encoded) > MAX_PCSTAT_LINE_BYTES and mem_extra:
        encoded = encode_record([], resolved_host)
    return encoded


def parse_args(argv: Optional[list[str]] = None) -> argparse.Namespace:
    """Parse bridge command-line options."""

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--port",
        default=DEFAULT_PORT,
        help="USB Serial/JTAG port, or 'auto' to detect Espressif VID 0x303A (default: auto)",
    )
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
        "--http-only",
        action="store_true",
        help="stream metrics to the local browser simulator without opening a serial port",
    )
    parser.add_argument(
        "--no-http",
        action="store_true",
        help="disable the local browser stream",
    )
    parser.add_argument("--once", action="store_true", help="send one record and exit")
    args = parser.parse_args(argv)
    if args.http_only and args.no_http:
        parser.error("--http-only cannot be combined with --no-http")
    return args


def main(argv: Optional[list[str]] = None) -> int:
    """Collect and stream metrics until interrupted."""

    args = parse_args(argv)
    if not args.http_only and serial is None:
        print(
            "pyserial is not installed. Run 'python -m pip install -r tools/requirements.txt' "
            "or pass --http-only to stream only to the browser simulator.",
            flush=True,
        )
        return 1

    interval = max(0.2, args.interval)
    auto_mode = not args.http_only and is_auto_port(args.port)
    port_mode = "http-only" if args.http_only else ("auto" if auto_mode else "manual")
    psutil.cpu_percent(interval=None)
    previous_network = psutil.net_io_counters()
    previous_time = time.monotonic()
    sensor_cache = SensorCache()
    cpu_frequency = CpuFrequencySampler()
    connection: Optional[Any] = None
    active_port: Optional[str] = None
    ever_connected = False
    last_wait_key: Optional[str] = None
    stream_hub = StatusStreamHub()
    stream_hub.set_serial_status(port=None, connected=False, mode=port_mode)
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

    if args.http_only:
        print("Serial output disabled (--http-only); streaming to local HTTP clients only.", flush=True)

    try:
        while True:
            if not args.http_only and connection is None:
                target_port = resolve_serial_port(args.port)
                if target_port is None:
                    wait_key = "auto:none"
                    if last_wait_key != wait_key:
                        print(
                            f"Waiting for Espressif USB Serial/JTAG device (VID 0x{ESPRESSIF_USB_VID:04X})...",
                            flush=True,
                        )
                        last_wait_key = wait_key
                    stream_hub.set_serial_status(port=None, connected=False, mode=port_mode)
                    if args.once:
                        return 1
                else:
                    try:
                        connection = open_serial(target_port)
                        active_port = target_port
                        last_wait_key = None
                        verb = "Reconnected to" if ever_connected else "Connected to"
                        ever_connected = True
                        suffix = f" (auto-detected VID 0x{ESPRESSIF_USB_VID:04X})" if auto_mode else ""
                        print(f"{verb} {active_port}{suffix}", flush=True)
                        stream_hub.set_serial_status(port=active_port, connected=True, mode=port_mode)
                    except (SerialException, OSError) as error:
                        wait_key = f"{target_port}:{error}"
                        if last_wait_key != wait_key:
                            print(f"Waiting for {target_port}: {error}", flush=True)
                            last_wait_key = wait_key
                        stream_hub.set_serial_status(port=target_port, connected=False, mode=port_mode)
                        if args.once:
                            return 1

            time.sleep(interval)
            now = time.monotonic()
            current_network = psutil.net_io_counters()
            virtual_memory = psutil.virtual_memory()
            disk_usage = psutil.disk_usage(default_system_disk_path())
            sensor_cache.refresh(now)
            payload = build_status_line(
                network_before=previous_network,
                network_after=current_network,
                elapsed=now - previous_time,
                cpu=psutil.cpu_percent(interval=None),
                memory=virtual_memory.percent,
                disk=disk_usage.percent,
                cpu_mhz=cpu_frequency.sample(),
                gpu=read_nvidia_metrics(),
                cpu_temp_c=sensor_cache.cpu_temperature,
                refresh_hz=read_display_refresh_hz(),
                mem_used_gb=bytes_to_gb(virtual_memory.used),
                mem_total_gb=bytes_to_gb(virtual_memory.total),
                disk_used_gb=bytes_to_gb(disk_usage.used),
                disk_total_gb=bytes_to_gb(disk_usage.total),
            )
            previous_network = current_network
            previous_time = now

            # The browser gets the same snapshot as the device. This avoids
            # opening the device's COM port a second time from Web Serial.
            stream_hub.publish(payload)
            if args.http_only:
                print(payload.decode("ascii").rstrip(), flush=True)
            elif connection is not None:
                try:
                    connection.write(payload)
                    print(payload.decode("ascii").rstrip(), flush=True)
                except (SerialException, OSError) as error:
                    action = "rescanning Espressif ports" if auto_mode else f"retrying {active_port}"
                    print(
                        f"Serial connection lost on {active_port}: {error} ({action})",
                        flush=True,
                    )
                    close_serial_safely(connection)
                    connection = None
                    active_port = None
                    last_wait_key = None
                    stream_hub.set_serial_status(port=None, connected=False, mode=port_mode)
                    if args.once:
                        return 1

            if args.once:
                return 0
    except KeyboardInterrupt:
        print("Stopping desktop bridge", flush=True)
        return 0
    finally:
        close_serial_safely(connection)
        cpu_frequency.close()
        if stream_server is not None:
            stream_server.shutdown()
            stream_server.server_close()


if __name__ == "__main__":
    raise SystemExit(main())
