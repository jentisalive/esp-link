"""Validate the built Wi-Fi-only IPA, including the absence of Bluetooth usage keys."""

from pathlib import Path
import plistlib
import sys
import zipfile


def verify(path):
    with zipfile.ZipFile(path) as archive:
        names = [name for name in archive.namelist() if name.startswith("Payload/") and name.endswith(".app/Info.plist")]
        if len(names) != 1:
            raise ValueError("Expected exactly one application Info.plist in Payload")
        info = plistlib.loads(archive.read(names[0]))
    for key in ["CFBundleDisplayName", "NSLocalNetworkUsageDescription"]:
        if not isinstance(info.get(key), str) or not info[key].strip():
            raise ValueError(f"Built IPA is missing {key}")
    if any("Bluetooth" in key for key in info):
        raise ValueError("Wi-Fi-only IPA must not request Bluetooth access")
    if "bluetooth-central" in info.get("UIBackgroundModes", []) or "bluetooth-peripheral" in info.get("UIBackgroundModes", []):
        raise ValueError("Wi-Fi-only IPA must not enable Bluetooth background modes")
    if not info.get("NSAppTransportSecurity", {}).get("NSAllowsArbitraryLoads"):
        raise ValueError("Built IPA must allow local ESP32 HTTP")
    if "UILaunchScreen" not in info or "UIApplicationSceneManifest" not in info:
        raise ValueError("Built IPA is missing launch/scene configuration")
    if len(info.get("UISupportedInterfaceOrientations~ipad", [])) != 4:
        raise ValueError("Built IPA must declare all four iPad orientations")
    if info.get("CFBundleShortVersionString") != "1.2.4" or info.get("CFBundleVersion") != "4":
        raise ValueError("Unexpected app version")
    print(f"IPA metadata verified: {info['CFBundleDisplayName']} {info['CFBundleShortVersionString']}")
    return info


if __name__ == "__main__":
    verify(Path(sys.argv[1]))
