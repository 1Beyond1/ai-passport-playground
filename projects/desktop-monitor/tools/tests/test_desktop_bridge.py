"""Unit tests for desktop_bridge.py (port auto-detection, reconnect, PCSTAT framing, HTTP SSE)."""

from __future__ import annotations

import io
import json
import math
import sys
import time
import unittest
from contextlib import redirect_stdout
from pathlib import Path
from types import SimpleNamespace
from unittest import mock
from urllib import error as urllib_error
from urllib import request as urllib_request

TOOLS_DIR = Path(__file__).resolve().parents[1]
if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))

import desktop_bridge  # noqa: E402


class PortDetectionTests(unittest.TestCase):
    def test_detect_espressif_port_matches_vid_and_prefers_jtag_pid(self) -> None:
        fake_ports = [
            SimpleNamespace(device="COM1", vid=0x0403, pid=0x6001, description="FTDI USB Serial"),
            SimpleNamespace(device="COM6", vid=0x303A, pid=0x4001, description="Espressif CDC"),
            SimpleNamespace(device="COM4", vid=0x303A, pid=0x1001, description="USB JTAG/serial debug unit"),
            SimpleNamespace(device="COM8", vid=None, pid=None, description="Standard Serial Port"),
        ]
        self.assertEqual(
            desktop_bridge.detect_espressif_port(port_lister=lambda: fake_ports),
            "COM4",
        )
        matched = desktop_bridge.list_espressif_ports(port_lister=lambda: fake_ports)
        self.assertEqual([p.device for p in matched], ["COM4", "COM6"])

    def test_detect_espressif_port_returns_none_when_absent(self) -> None:
        fake_ports = [
            SimpleNamespace(device="/dev/ttyUSB0", vid=0x1A86, pid=0x7523, description="CH340"),
        ]
        self.assertIsNone(desktop_bridge.detect_espressif_port(port_lister=lambda: fake_ports))

    def test_resolve_serial_port_supports_auto_and_explicit_ports(self) -> None:
        fake_ports = [
            SimpleNamespace(device="/dev/ttyACM0", vid=0x303A, pid=0x1001, description="USB JTAG/serial"),
        ]
        self.assertEqual(
            desktop_bridge.resolve_serial_port("auto", port_lister=lambda: fake_ports),
            "/dev/ttyACM0",
        )
        self.assertEqual(
            desktop_bridge.resolve_serial_port("AUTO", port_lister=lambda: fake_ports),
            "/dev/ttyACM0",
        )
        self.assertEqual(
            desktop_bridge.resolve_serial_port("COM3", port_lister=lambda: []),
            "COM3",
        )
        self.assertEqual(
            desktop_bridge.resolve_serial_port("/dev/ttyACM0", port_lister=lambda: []),
            "/dev/ttyACM0",
        )


class ArgParsingTests(unittest.TestCase):
    def test_defaults_to_auto_port(self) -> None:
        args = desktop_bridge.parse_args([])
        self.assertEqual(args.port, "auto")
        self.assertFalse(args.http_only)
        self.assertFalse(args.no_http)
        self.assertEqual(args.http_port, desktop_bridge.DEFAULT_HTTP_PORT)

    def test_explicit_port_and_http_only(self) -> None:
        args = desktop_bridge.parse_args(["--port", "COM5", "--no-http", "--once"])
        self.assertEqual(args.port, "COM5")
        self.assertTrue(args.no_http)
        self.assertTrue(args.once)

        http_args = desktop_bridge.parse_args(["--http-only", "--http-port", "9000"])
        self.assertTrue(http_args.http_only)
        self.assertEqual(http_args.http_port, 9000)

    def test_rejects_http_only_with_no_http(self) -> None:
        with self.assertRaises(SystemExit):
            with mock.patch("sys.stderr", new_callable=io.StringIO):
                desktop_bridge.parse_args(["--http-only", "--no-http"])


class PcstatFramingTests(unittest.TestCase):
    @staticmethod
    def _net(recv: int, sent: int) -> SimpleNamespace:
        return SimpleNamespace(bytes_recv=recv, bytes_sent=sent)

    def test_build_status_line_includes_backward_compatible_gb_fields(self) -> None:
        raw = desktop_bridge.build_status_line(
            network_before=self._net(0, 0),
            network_after=self._net(int(1.2 * 1024 * 1024), int(0.4 * 1024 * 1024)),
            elapsed=1.0,
            cpu=37.4,
            memory=58.2,
            disk=67.9,
            cpu_mhz=4200,
            gpu={"gpu": 61, "gpu_mhz": 2400, "gpu_c": 68},
            cpu_temp_c=72,
            refresh_hz=165,
            mem_used_gb=9.32,
            mem_total_gb=16.0,
            disk_used_gb=341.2,
            disk_total_gb=512.0,
            time_text="14:28",
            uptime_text="3h18m",
            host_name="WORKSTATION",
        )
        self.assertTrue(raw.endswith(b"\n"))
        self.assertLessEqual(len(raw), desktop_bridge.MAX_PCSTAT_LINE_BYTES)
        line = raw.decode("ascii").strip()
        self.assertTrue(line.startswith("PCSTAT|"))
        tokens = dict(part.split("=", 1) for part in line[7:].split("|"))
        self.assertEqual(tokens["cpu"], "37")
        self.assertEqual(tokens["mem"], "58")
        self.assertEqual(tokens["disk"], "68")
        self.assertEqual(tokens["rx"], "1.2")
        self.assertEqual(tokens["tx"], "0.4")
        self.assertEqual(tokens["cpu_mhz"], "4200")
        self.assertEqual(tokens["gpu"], "61")
        self.assertEqual(tokens["gpu_mhz"], "2400")
        self.assertEqual(tokens["gpu_c"], "68")
        self.assertEqual(tokens["cpu_c"], "72")
        self.assertEqual(tokens["hz"], "165")
        self.assertEqual(tokens["mem_used_gb"], "9.3")
        self.assertEqual(tokens["mem_total_gb"], "16.0")
        self.assertEqual(tokens["disk_used_gb"], "341.2")
        self.assertEqual(tokens["disk_total_gb"], "512.0")
        self.assertEqual(tokens["time"], "14:28")
        self.assertEqual(tokens["uptime"], "3h18m")
        self.assertEqual(tokens["host"], "WORKSTATION")

    def test_build_status_line_omits_invalid_or_missing_gb_fields(self) -> None:
        raw = desktop_bridge.build_status_line(
            network_before=self._net(0, 0),
            network_after=self._net(0, 0),
            elapsed=1.0,
            cpu=10,
            memory=20,
            disk=30,
            mem_used_gb=math.nan,
            mem_total_gb=16.0,
            disk_used_gb=10.0,
            disk_total_gb=0.0,
            time_text="09:00",
            uptime_text="10m",
            host_name="Test|Box=1",
        )
        line = raw.decode("ascii").strip()
        self.assertNotIn("mem_used_gb=", line)
        self.assertNotIn("disk_used_gb=", line)
        self.assertIn("host=Test_Box_1", line)

    def test_build_status_line_never_exceeds_firmware_line_limit(self) -> None:
        raw = desktop_bridge.build_status_line(
            network_before=self._net(0, 0),
            network_after=self._net(10**12, 10**12),
            elapsed=1.0,
            cpu=100,
            memory=100,
            disk=100,
            cpu_mhz=65535,
            gpu={"gpu": 100, "gpu_mhz": 65535, "gpu_c": 150},
            cpu_temp_c=150,
            refresh_hz=1000,
            mem_used_gb=1024.0,
            mem_total_gb=1024.0,
            disk_used_gb=99999.9,
            disk_total_gb=99999.9,
            time_text="23:59",
            uptime_text="9999d23h",
            host_name="VERY-LONG-WORKSTATION-HOST-NAME-12345",
        )
        self.assertLessEqual(len(raw), desktop_bridge.MAX_PCSTAT_LINE_BYTES)
        line = raw.decode("ascii").strip()
        tokens = dict(part.split("=", 1) for part in line[7:].split("|"))
        self.assertEqual(tokens["rx"], "6553.5")
        self.assertEqual(tokens["tx"], "6553.5")

    def test_helper_formatters(self) -> None:
        self.assertAlmostEqual(desktop_bridge.bytes_to_gb(16 * 1024**3), 16.0)
        self.assertEqual(desktop_bridge.format_uptime(45 * 60), "45m")
        self.assertEqual(desktop_bridge.format_uptime(3 * 3600 + 18 * 60), "3h18m")
        self.assertEqual(desktop_bridge.format_uptime(2 * 86400 + 5 * 3600), "2d5h")
        self.assertEqual(desktop_bridge.parse_sensor_number("72.4"), 72)
        self.assertIsNone(desktop_bridge.parse_sensor_number("N/A"))
        self.assertEqual(desktop_bridge._parse_lhm_value("65.5 °C"), 65.5)


    def test_build_status_line_handles_none_network_and_strips_crlf_host(self) -> None:
        raw = desktop_bridge.build_status_line(
            network_before=None,
            network_after=None,
            elapsed=1.0,
            cpu=15,
            memory=25,
            disk=35,
            time_text="10:12",
            uptime_text="5m",
            host_name="Bad\r\nHost|Name=1",
        )
        # Exactly one trailing newline (no frame injection from CRLF in host_name)
        self.assertEqual(raw.count(b"\n"), 1)
        self.assertNotIn(b"\r", raw)
        line = raw.decode("ascii").strip()
        tokens = dict(part.split("=", 1) for part in line[7:].split("|"))
        self.assertEqual(tokens["rx"], "0.0")
        self.assertEqual(tokens["tx"], "0.0")
        self.assertEqual(tokens["host"], "BadHost_Name_1")


class AsyncSensorCacheAndBackoffTests(unittest.TestCase):
    def test_backoff_probe_exponential_schedule_and_recovery(self) -> None:
        probe = desktop_bridge.BackoffProbe(
            success_interval=5.0,
            initial_backoff=60.0,
            max_backoff=300.0,
            backoff_factor=2.0,
        )
        self.assertTrue(probe.due(0.0))
        self.assertEqual(probe.record_failure(10.0), 60.0)
        self.assertFalse(probe.due(69.9))
        self.assertTrue(probe.due(70.0))

        self.assertEqual(probe.record_failure(70.0), 120.0)
        self.assertEqual(probe.record_failure(190.0), 240.0)
        self.assertEqual(probe.record_failure(430.0), 300.0)
        self.assertEqual(probe.record_failure(730.0), 300.0)

        probe.record_success(1030.0)
        self.assertEqual(probe.failure_count, 0)
        self.assertFalse(probe.due(1034.9))
        self.assertTrue(probe.due(1035.0))

    def test_sensor_cache_backs_off_missing_lhm_and_wmi_without_5s_powershell_spam(self) -> None:
        rest_calls = 0
        wmi_calls = 0

        def fake_rest() -> int | None:
            nonlocal rest_calls
            rest_calls += 1
            return None

        def fake_wmi() -> int | None:
            nonlocal wmi_calls
            wmi_calls += 1
            return None

        cache = desktop_bridge.SensorCache(
            cpu_temp_interval=5.0,
            gpu_interval=1.0,
            initial_backoff=60.0,
            max_backoff=300.0,
        )
        with (
            mock.patch.object(desktop_bridge.psutil, "sensors_temperatures", None, create=True),
            mock.patch.object(desktop_bridge, "read_lhm_cpu_temperature_rest", side_effect=fake_rest),
            mock.patch.object(desktop_bridge, "read_lhm_cpu_temperature_wmi", side_effect=fake_wmi),
            mock.patch.object(desktop_bridge, "read_nvidia_metrics", return_value={}),
        ):
            # Initial probe at t=10 fails -> backs off 60s
            cache.poll_once(10.0)
            self.assertEqual((rest_calls, wmi_calls), (1, 1))
            self.assertIsNone(cache.cpu_temperature)

            # Polling every 5s up to t=65 MUST NOT re-invoke LHM or PowerShell WMI
            for t in (15.0, 20.0, 25.0, 40.0, 65.0):
                cache.poll_once(t)
            self.assertEqual((rest_calls, wmi_calls), (1, 1))

            # At t=70 (after 60s backoff) one retry happens, then backs off 120s
            cache.poll_once(70.0)
            self.assertEqual((rest_calls, wmi_calls), (2, 2))
            cache.poll_once(189.0)
            self.assertEqual((rest_calls, wmi_calls), (2, 2))

    def test_sensor_cache_background_thread_never_blocks_snapshot(self) -> None:
        def slow_temp(**_kwargs: object) -> int:
            time.sleep(0.25)
            return 64

        def slow_gpu() -> dict[str, int]:
            time.sleep(0.25)
            return {"gpu": 45, "gpu_mhz": 2100, "gpu_c": 58}

        cache = desktop_bridge.SensorCache(poll_interval=0.05, cpu_temp_interval=5.0, gpu_interval=1.0)
        with (
            mock.patch.object(desktop_bridge, "read_cpu_temperature", side_effect=slow_temp),
            mock.patch.object(desktop_bridge, "read_nvidia_metrics", side_effect=slow_gpu),
        ):
            cache.start()
            try:
                t0 = time.monotonic()
                temp, gpu = cache.snapshot()
                elapsed = time.monotonic() - t0
                # Snapshot returns immediately without waiting for slow probes
                self.assertLess(elapsed, 0.05)
                self.assertIsInstance(gpu, dict)
                self.assertTrue(temp is None or isinstance(temp, int))

                # Wait for background daemon thread to populate cached metrics
                deadline = time.monotonic() + 2.0
                while time.monotonic() < deadline:
                    temp, gpu = cache.snapshot()
                    if temp == 64 and gpu.get("gpu") == 45:
                        break
                    time.sleep(0.05)
                self.assertEqual(temp, 64)
                self.assertEqual(gpu, {"gpu": 45, "gpu_mhz": 2100, "gpu_c": 58})
            finally:
                cache.stop()


class HttpServerAndReconnectTests(unittest.TestCase):
    def test_origin_whitelist_validation(self) -> None:
        self.assertTrue(desktop_bridge.is_allowed_origin(None))
        self.assertTrue(desktop_bridge.is_allowed_origin("http://localhost:8000"))
        self.assertTrue(desktop_bridge.is_allowed_origin("http://127.0.0.1:5500"))
        self.assertTrue(desktop_bridge.is_allowed_origin("https://localhost:8443"))
        self.assertTrue(desktop_bridge.is_allowed_origin("https://1beyond1.github.io"))
        self.assertFalse(desktop_bridge.is_allowed_origin("https://evil.example"))
        self.assertFalse(desktop_bridge.is_allowed_origin("https://1beyond1.github.io.evil.com"))
        self.assertFalse(desktop_bridge.is_allowed_origin("https://1beyond1.github.io@evil.com"))
        self.assertFalse(desktop_bridge.is_allowed_origin("http://1beyond1.github.io"))
        self.assertFalse(desktop_bridge.is_allowed_origin("null"))

    def test_http_health_and_options_cors_headers(self) -> None:
        hub = desktop_bridge.StatusStreamHub()
        hub.set_serial_status(port="COM4", connected=True, mode="auto")
        server = desktop_bridge.start_status_server("127.0.0.1", 0, hub)
        try:
            port = server.server_address[1]
            # 1. Local CLI request without Origin header is allowed without wildcard ACAO
            with urllib_request.urlopen(f"http://127.0.0.1:{port}/health", timeout=2) as resp:
                self.assertEqual(resp.status, 200)
                self.assertIsNone(resp.headers.get("Access-Control-Allow-Origin"))
                self.assertEqual(resp.headers.get("Vary"), "Origin")
                body = json.loads(resp.read().decode("utf-8"))
                self.assertTrue(body["ok"])
                self.assertEqual(body["protocol"], "PCSTAT")
                self.assertEqual(body["serial_port"], "COM4")
                self.assertTrue(body["serial_connected"])
                self.assertEqual(body["port_mode"], "auto")

            # 2. Allowed origins echo exact origin and Private-Network header
            for origin in ("http://localhost:8000", "http://127.0.0.1:5500", "https://1beyond1.github.io"):
                req = urllib_request.Request(
                    f"http://127.0.0.1:{port}/health",
                    headers={"Origin": origin},
                )
                with urllib_request.urlopen(req, timeout=2) as resp:
                    self.assertEqual(resp.status, 200)
                    self.assertEqual(resp.headers.get("Access-Control-Allow-Origin"), origin)
                    self.assertEqual(resp.headers.get("Access-Control-Allow-Private-Network"), "true")
                    self.assertEqual(resp.headers.get("Vary"), "Origin")

                opt_req = urllib_request.Request(
                    f"http://127.0.0.1:{port}/stream",
                    method="OPTIONS",
                    headers={"Origin": origin},
                )
                with urllib_request.urlopen(opt_req, timeout=2) as resp:
                    self.assertEqual(resp.status, 204)
                    self.assertEqual(resp.headers.get("Access-Control-Allow-Origin"), origin)
                    self.assertEqual(resp.headers.get("Access-Control-Allow-Private-Network"), "true")

            # 3. Arbitrary public origins are rejected with 403 and no ACAO header
            for bad_origin in ("https://evil.example", "https://1beyond1.github.io.evil.com", "null"):
                bad_req = urllib_request.Request(
                    f"http://127.0.0.1:{port}/health",
                    headers={"Origin": bad_origin},
                )
                with self.assertRaises(urllib_error.HTTPError) as ctx:
                    urllib_request.urlopen(bad_req, timeout=2)
                self.assertEqual(ctx.exception.code, 403)
                self.assertIsNone(ctx.exception.headers.get("Access-Control-Allow-Origin"))
                ctx.exception.close()

                bad_opt = urllib_request.Request(
                    f"http://127.0.0.1:{port}/stream",
                    method="OPTIONS",
                    headers={"Origin": bad_origin},
                )
                with self.assertRaises(urllib_error.HTTPError) as ctx_opt:
                    urllib_request.urlopen(bad_opt, timeout=2)
                self.assertEqual(ctx_opt.exception.code, 403)
                self.assertIsNone(ctx_opt.exception.headers.get("Access-Control-Allow-Origin"))
                ctx_opt.exception.close()
        finally:
            server.shutdown()
            server.server_close()

    def test_http_only_once_handles_missing_network_counters(self) -> None:
        buf = io.StringIO()
        with (
            mock.patch.object(desktop_bridge, "serial", None),
            mock.patch.object(desktop_bridge.psutil, "net_io_counters", return_value=None),
            mock.patch.object(desktop_bridge.time, "sleep", return_value=None),
            mock.patch.object(desktop_bridge, "read_nvidia_metrics", return_value={}),
            mock.patch.object(desktop_bridge, "read_cpu_temperature", return_value=None),
            mock.patch.object(desktop_bridge, "read_display_refresh_hz", return_value=None),
            redirect_stdout(buf),
        ):
            rc = desktop_bridge.main(["--http-only", "--http-port", "0", "--once", "--interval", "0.2"])
        self.assertEqual(rc, 0)
        out = buf.getvalue()
        self.assertIn("rx=0.0|tx=0.0", out)

    def test_http_only_once_works_without_serial_dependency(self) -> None:
        buf = io.StringIO()
        with (
            mock.patch.object(desktop_bridge, "serial", None),
            mock.patch.object(desktop_bridge.time, "sleep", return_value=None),
            mock.patch.object(desktop_bridge, "read_nvidia_metrics", return_value={}),
            mock.patch.object(desktop_bridge, "read_cpu_temperature", return_value=None),
            mock.patch.object(desktop_bridge, "read_display_refresh_hz", return_value=None),
            redirect_stdout(buf),
        ):
            rc = desktop_bridge.main(["--http-only", "--http-port", "0", "--once", "--interval", "0.2"])
        self.assertEqual(rc, 0)
        out = buf.getvalue()
        self.assertIn("PCSTAT|", out)
        self.assertIn("mem_used_gb=", out)
        self.assertIn("disk_total_gb=", out)

    def test_auto_port_hotplug_reconnect_flow(self) -> None:
        class FakeSerial:
            def __init__(self, port_name: str, fail_on_write: bool = False) -> None:
                self.port = port_name
                self.is_open = True
                self.fail_on_write = fail_on_write
                self.written: list[bytes] = []

            def write(self, data: bytes) -> int:
                if self.fail_on_write:
                    raise desktop_bridge.SerialException("device disconnected")
                self.written.append(data)
                return len(data)

            def close(self) -> None:
                self.is_open = False

        # Sequence of ticks:
        # 1) No Espressif device connected yet -> waits gracefully
        # 2) Device appears on COM4 -> connects, write fails (simulates unplug!)
        # 3) Device re-enumerates on COM7 -> rescans, reconnects, writes OK
        # 4) Sleep raises KeyboardInterrupt -> clean shutdown
        resolve_results = [None, "COM4", "COM7"]
        opened: list[FakeSerial] = []

        def fake_open(port_name: str) -> FakeSerial:
            conn = FakeSerial(port_name, fail_on_write=(port_name == "COM4"))
            opened.append(conn)
            return conn

        sleep_calls = 0

        def fake_sleep(_: float) -> None:
            nonlocal sleep_calls
            sleep_calls += 1
            if sleep_calls >= 4:
                raise KeyboardInterrupt

        buf = io.StringIO()
        with (
            mock.patch.object(desktop_bridge, "serial", object()),
            mock.patch.object(desktop_bridge, "resolve_serial_port", side_effect=resolve_results),
            mock.patch.object(desktop_bridge, "open_serial", side_effect=fake_open),
            mock.patch.object(desktop_bridge.time, "sleep", side_effect=fake_sleep),
            mock.patch.object(desktop_bridge, "read_nvidia_metrics", return_value={}),
            mock.patch.object(desktop_bridge, "read_cpu_temperature", return_value=None),
            mock.patch.object(desktop_bridge, "read_display_refresh_hz", return_value=None),
            redirect_stdout(buf),
        ):
            rc = desktop_bridge.main(["--port", "auto", "--no-http", "--interval", "0.2"])

        self.assertEqual(rc, 0)
        self.assertEqual([c.port for c in opened], ["COM4", "COM7"])
        self.assertFalse(opened[0].is_open)
        self.assertFalse(opened[1].is_open)
        self.assertEqual(len(opened[1].written), 1)
        self.assertTrue(opened[1].written[0].startswith(b"PCSTAT|"))
        output = buf.getvalue()
        self.assertIn("VID 0x303A", output)
        self.assertIn("Connected to COM4", output)
        self.assertIn("Serial connection lost on COM4", output)
        self.assertIn("Reconnected to COM7", output)


if __name__ == "__main__":
    unittest.main()
