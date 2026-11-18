"""Merge the exact binaries built by PlatformIO. No board credentials are embedded."""

import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def main():
    core = Path(os.environ.get("PLATFORMIO_CORE_DIR", Path.home() / ".platformio"))
    packages = core / "packages"
    build = ROOT / "firmware" / ".pio" / "build" / "upesy_wroom"
    esptool = packages / "tool-esptoolpy" / "esptool.py"
    boot_app = packages / "framework-arduinoespressif32" / "tools" / "partitions" / "boot_app0.bin"
    images = [
        ("0x1000", build / "bootloader.bin"),
        ("0x8000", build / "partitions.bin"),
        ("0xe000", boot_app),
        ("0x10000", build / "firmware.bin"),
    ]
    for path in [esptool, *(path for _, path in images)]:
        if not path.is_file():
            raise SystemExit(f"Missing {path}. Run pio run --project-dir firmware -e upesy_wroom first.")
    destination = ROOT / "dist" / "firmware"
    destination.mkdir(parents=True, exist_ok=True)
    output = destination / "ESPLink-upesy-wroom.bin"
    command = [sys.executable, str(esptool), "--chip", "esp32", "merge_bin", "-o", str(output),
               "--flash_mode", "dio", "--flash_freq", "40m", "--flash_size", "4MB"]
    for offset, path in images:
        command.extend([offset, str(path)])
    subprocess.run(command, check=True)
    shutil.copyfile(ROOT / "docs" / "FLASH.txt", destination / "FLASH.txt")
    print(output)


if __name__ == "__main__":
    main()
