"""USB password protocol tests with a fake serial port; no hardware is touched."""

from collections import deque
from contextlib import redirect_stdout
import io
from pathlib import Path
from types import SimpleNamespace
import unittest
import change_wifi_password as password_tool
import change_device_token as token_tool


class FakeSerial:
    def __init__(self, replies):
        self.replies = deque(replies)
        self.writes = []
    def write(self, value):
        self.writes.append(value)
    def flush(self):
        pass
    def readline(self):
        return self.replies.popleft() if self.replies else b""


class USBPasswordTests(unittest.TestCase):
    def test_valid_passwords(self):
        for value in ["a" * 8, "x" * 63, "Robot-password!42"]:
            self.assertEqual(password_tool.validate_password(value), value)

    def test_invalid_passwords(self):
        for value in ["", "short", "x" * 64, "pass word", "password\n", "пароль123"]:
            with self.subTest(value=value), self.assertRaises(ValueError):
                password_tool.validate_password(value)

    def test_saved_ack_and_no_echo(self):
        serial = FakeSerial([b"ESPLINK:WIFI-ONLY:1\n", b"ESPLINK:PASSWORD-SAVED\n"])
        output = io.StringIO()
        with redirect_stdout(output):
            password_tool.wait_for_firmware(serial)
            password_tool.change_password(serial, "Robot-private!42")
        self.assertEqual(serial.writes, [b"esplink:hello\n", b"esplink:wifi-password:Robot-private!42\n"])
        self.assertNotIn("Robot-private!42", output.getvalue())

    def test_wrong_firmware_never_receives_password(self):
        serial = FakeSerial([b"Old firmware\n"])
        with self.assertRaises(RuntimeError):
            password_tool.wait_for_firmware(serial, timeout=0.001)
        self.assertFalse(any(b"wifi-password:" in frame for frame in serial.writes))

    def test_error_is_not_success(self):
        serial = FakeSerial([b"ESPLINK:ERROR:NVS\n"])
        with self.assertRaises(RuntimeError):
            password_tool.change_password(serial, "Robot-private!42")

    def test_missing_ack_is_uncertain_and_not_retried(self):
        serial = FakeSerial([])
        with self.assertRaisesRegex(RuntimeError, "MAY have changed"):
            password_tool.change_password(serial, "Robot-private!42", timeout=0.001)
        self.assertEqual(len(serial.writes), 1)

    def test_port_selection(self):
        ports = [SimpleNamespace(device="COM5", description="ESP32")]
        self.assertEqual(password_tool.choose_port(None, ports), "COM5")
        self.assertEqual(password_tool.choose_port("COM7", ports), "COM7")
        with self.assertRaises(ValueError):
            password_tool.choose_port(None, [])


class USBTokenTests(unittest.TestCase):
    PROBE = b"esplink:token-capability\n"
    ROTATE = b"esplink:rotate-token\n"
    SUPPORTED = b"ESPLINK:TOKEN-ROTATION:1\n"
    MANUAL = b"ESPLINK:ALPHANUMERIC-TOKEN:2\n"
    MANUAL_PROBE = b"esplink:alphanumeric-token-capability\n"

    def test_manual_token_is_normalized_and_not_printed(self):
        serial = FakeSerial([self.MANUAL, b"ESPLINK:TOKEN-SAVED\n"])
        output = io.StringIO()
        with redirect_stdout(output):
            self.assertTrue(token_tool.set_token(serial, ask=lambda _: "ZetaControl7"))
        self.assertEqual(serial.writes, [self.MANUAL_PROBE, b"esplink:set-token:zetacontrol7\n"])
        self.assertEqual(output.getvalue(), "")

    def test_manual_invalid_and_mismatched_tokens_are_not_sent(self):
        for answers in [("abc def123456",), ("ZetaControl!",), ("ЖetaControl7",), ("abcdef123456", "abcdef123457")]:
            serial = FakeSerial([self.MANUAL])
            values = iter(answers)
            with self.assertRaises(ValueError):
                token_tool.set_token(serial, ask=lambda _: next(values))
            self.assertEqual(serial.writes, [self.MANUAL_PROBE])

    def test_token_length_boundaries(self):
        for length in (8, 12, 13, 64):
            self.assertEqual(token_tool.normalize_token(" Z" + "x" * (length - 1) + " "), "z" + "x" * (length - 1))
        for length in (0, 7, 65):
            with self.assertRaisesRegex(ValueError, f"Token length: {length}"):
                token_tool.normalize_token("x" * length)

    def test_rotation_is_sent_once_after_confirmation(self):
        serial = FakeSerial([self.SUPPORTED, b"ESPLINK:TOKEN-SAVED\n"])
        self.assertTrue(token_tool.rotate_token(serial, ask=lambda _: "Y"))
        self.assertEqual(serial.writes, [self.PROBE, self.ROTATE])

    def test_cancel_sends_no_rotation(self):
        serial = FakeSerial([self.SUPPORTED])
        self.assertFalse(token_tool.rotate_token(serial, ask=lambda _: ""))
        self.assertEqual(serial.writes, [self.PROBE])

    def test_unsupported_firmware_sends_no_rotation(self):
        serial = FakeSerial([b"ESPLINK:ERROR:COMMAND\n"])
        with self.assertRaisesRegex(RuntimeError, "1.2.1"):
            token_tool.rotate_token(serial, ask=lambda _: "y", timeout=0.001)
        self.assertEqual(serial.writes, [self.PROBE])

    def test_no_retry_without_ack(self):
        serial = FakeSerial([self.SUPPORTED])
        with self.assertRaisesRegex(RuntimeError, "MAY have changed"):
            token_tool.rotate_token(serial, ask=lambda _: "y", timeout=0.001)
        self.assertEqual(serial.writes, [self.PROBE, self.ROTATE])


class WiFiOnlySourceTests(unittest.TestCase):
    ROOT = Path(__file__).resolve().parents[1]

    def test_no_app_bluetooth_sdk(self):
        paths = list((self.ROOT / "ios/ESPLink").glob("*.swift"))
        paths += list((self.ROOT / "android/app/src/main/java").rglob("*.java"))
        for path in paths:
            source = path.read_text(encoding="utf-8")
            self.assertNotIn("import CoreBluetooth", source, str(path))
            self.assertNotIn("android.bluetooth", source, str(path))
            self.assertNotIn("BluetoothLink", source, str(path))

    def test_firmware_only_disables_bluetooth(self):
        source = (self.ROOT / "firmware/src/main.cpp").read_text(encoding="utf-8")
        for forbidden in ["BLEDevice", "BLEServer", "BluetoothSerial", "btStart(", "esp_bt_controller_enable("]:
            self.assertNotIn(forbidden, source)
        self.assertIn("btStop()", source)
        self.assertIn("esp_bt_controller_mem_release(ESP_BT_MODE_BTDM)", source)

    def test_password_change_is_not_a_network_endpoint(self):
        source = (self.ROOT / "firmware/src/main.cpp").read_text(encoding="utf-8")
        self.assertNotIn('http.on("/api/password', source)
        self.assertIn('"esplink:wifi-password:"', source)


if __name__ == "__main__":
    unittest.main()
