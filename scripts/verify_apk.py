"""Verify the installed Wi-Fi-only APK identity and absence of Bluetooth permissions."""

import subprocess
import sys


def verify(aapt, apk):
    metadata = subprocess.check_output([aapt, "dump", "badging", apk], text=True)
    for expected in ["package: name='dev.esplink.controller'", "versionName='1.2.4'", "versionCode='4'",
                     "sdkVersion:'26'", "targetSdkVersion:'35'", "launchable-activity: name='dev.esplink.controller.MainActivity'",
                     "android.permission.INTERNET", "android.permission.ACCESS_NETWORK_STATE"]:
        if expected not in metadata:
            raise ValueError(f"APK metadata missing: {expected}")
    for forbidden in ["android.permission.BLUETOOTH", "android.permission.ACCESS_FINE_LOCATION",
                      "android.permission.ACCESS_COARSE_LOCATION", "android.hardware.bluetooth"]:
        if forbidden in metadata:
            raise ValueError(f"Wi-Fi-only APK must not contain: {forbidden}")
    if "application-debuggable" in metadata:
        raise ValueError("Release APK must not be debuggable")
    print("Signed Wi-Fi-only Android release metadata verified: ESP Link 1.2.4, Android 8+")


if __name__ == "__main__":
    verify(sys.argv[1], sys.argv[2])
