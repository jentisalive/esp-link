"""Offline tests for source packaging and Apple export options. No credentials required."""

import datetime as dt
import hashlib
import os
from pathlib import Path
import plistlib
import tempfile
import unittest
from unittest.mock import patch
import zipfile

import package_source
import signing_options
import verify_ios_package
import verify_apk


class SourcePackageTests(unittest.TestCase):
    def test_archive_has_workflows_but_not_secrets_or_builds(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            files = ["README.md", ".gitignore", ".gitattributes", ".github/workflows/build.yml",
                     "ios/ESPLink/App.swift", "firmware/src/main.cpp", "firmware/include/secrets.h",
                     "firmware/.pio/build/test.bin", "ios/certificate.p12", "scripts/__pycache__/test.pyc",
                     "get_token.bat", "flash_esp32.bat", "change_wifi_password.bat", "android/app/src/Main.java", "android/local.properties",
                     "android/.gradle/cache.bin", "android/app/build/app.apk", "android/release.keystore",
                     ".signing/android-release-password.txt"]
            for name in files:
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("test", encoding="utf-8")
            with patch.object(package_source, "ROOT", root):
                package_source.main()
            with zipfile.ZipFile(root / "dist/ESPLink-source.zip") as archive:
                names = set(archive.namelist())
            self.assertIn("ESPLink/.github/workflows/build.yml", names)
            self.assertIn("ESPLink/firmware/src/main.cpp", names)
            self.assertIn("ESPLink/android/app/src/Main.java", names)
            self.assertIn("ESPLink/get_token.bat", names)
            self.assertIn("ESPLink/change_wifi_password.bat", names)
            self.assertFalse(any("secrets.h" in name or ".p12" in name or ".pio/" in name or "__pycache__" in name for name in names))
            self.assertFalse(any("local.properties" in name or "/.signing/" in name or "/.gradle/" in name
                                 or name.endswith((".apk", ".keystore")) for name in names))


class SigningOptionsTests(unittest.TestCase):
    def run_export(self, development=False, devices=False, app_id="dev.esplink.controller", expired=False):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            cert = b"test-certificate-not-a-real-key"
            profile = {
                "UUID": "test-profile",
                "TeamIdentifier": ["TESTTEAM01"],
                "DeveloperCertificates": [cert],
                "ExpirationDate": dt.datetime.now() + dt.timedelta(days=-1 if expired else 1),
                "Entitlements": {"application-identifier": "TESTTEAM01." + app_id, "get-task-allow": development},
            }
            if devices:
                profile["ProvisionedDevices"] = ["test-device"]
            with (root / "profile.plist").open("wb") as output:
                plistlib.dump(profile, output)
            (root / "profile.mobileprovision").write_bytes(b"test-profile")
            env = {"RUNNER_TEMP": str(root), "GITHUB_ENV": str(root / "env"), "BUNDLE_ID": "dev.esplink.controller"}
            identity = hashlib.sha1(cert).hexdigest().upper()
            with patch.dict(os.environ, env), patch.object(Path, "home", return_value=root), patch.object(
                signing_options.subprocess, "check_output", return_value=identity
            ):
                signing_options.main()
            with (root / "ExportOptions.plist").open("rb") as source:
                return plistlib.load(source)

    def test_development_export(self):
        self.assertEqual(self.run_export(development=True, devices=True)["method"], "debugging")

    def test_adhoc_export(self):
        self.assertEqual(self.run_export(devices=True)["method"], "release-testing")

    def test_appstore_export_does_not_upload(self):
        options = self.run_export()
        self.assertEqual(options["method"], "app-store-connect")
        self.assertEqual(options["destination"], "export")

    def test_wrong_app_id_rejected(self):
        with self.assertRaises(SystemExit):
            self.run_export(app_id="dev.other.app")

    def test_expired_profile_rejected(self):
        with self.assertRaises(SystemExit):
            self.run_export(expired=True)


class IPAMetadataTests(unittest.TestCase):
    def source_info(self):
        path = Path(__file__).resolve().parents[1] / "ios/ESPLink/Info.plist"
        with path.open("rb") as source:
            info = plistlib.load(source)
        info["CFBundleShortVersionString"] = "1.2.4"
        info["CFBundleVersion"] = "4"
        return info

    def verify(self, info):
        with tempfile.TemporaryDirectory() as directory:
            ipa = Path(directory) / "test.ipa"
            with zipfile.ZipFile(ipa, "w") as archive:
                archive.writestr("Payload/ESPLink.app/Info.plist", plistlib.dumps(info))
            return verify_ios_package.verify(ipa)

    def test_source_has_required_metadata(self):
        self.assertEqual(self.verify(self.source_info())["CFBundleDisplayName"], "ESP Link")

    def test_missing_permission_is_rejected(self):
        for key in ["NSLocalNetworkUsageDescription", "NSAppTransportSecurity"]:
            with self.subTest(key=key):
                info = self.source_info()
                del info[key]
                with self.assertRaises(ValueError):
                    self.verify(info)

    def test_overwritten_plist_is_rejected(self):
        with self.assertRaises(ValueError):
            self.verify({"CFBundleIdentifier": "dev.esplink.controller"})

    def test_bluetooth_permission_is_rejected(self):
        info = self.source_info()
        info["NSBluetoothAlwaysUsageDescription"] = "Unexpected access"
        with self.assertRaises(ValueError):
            self.verify(info)


class APKMetadataTests(unittest.TestCase):
    METADATA = """package: name='dev.esplink.controller' versionCode='4' versionName='1.2.4'
sdkVersion:'26'
targetSdkVersion:'35'
launchable-activity: name='dev.esplink.controller.MainActivity'
uses-permission: name='android.permission.INTERNET'
uses-permission: name='android.permission.ACCESS_NETWORK_STATE'
"""

    def test_valid_apk_metadata(self):
        with patch.object(verify_apk.subprocess, "check_output", return_value=self.METADATA):
            verify_apk.verify("aapt", "test.apk")

    def test_missing_permission_rejected(self):
        metadata = self.METADATA.replace("android.permission.INTERNET", "missing")
        with patch.object(verify_apk.subprocess, "check_output", return_value=metadata):
            with self.assertRaises(ValueError):
                verify_apk.verify("aapt", "test.apk")

    def test_debuggable_release_rejected(self):
        with patch.object(verify_apk.subprocess, "check_output", return_value=self.METADATA + "application-debuggable"):
            with self.assertRaises(ValueError):
                verify_apk.verify("aapt", "test.apk")

    def test_bluetooth_permission_rejected(self):
        with patch.object(verify_apk.subprocess, "check_output", return_value=self.METADATA + "android.permission.BLUETOOTH_SCAN"):
            with self.assertRaises(ValueError):
                verify_apk.verify("aapt", "test.apk")


if __name__ == "__main__":
    unittest.main()
