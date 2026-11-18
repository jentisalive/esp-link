"""Set or rotate the ESP Link device token over USB without storing it on the PC."""

import argparse
import re
import sys
import time

from change_wifi_password import open_usb, wait_for_firmware


def rotate_token(connection, ask=input, timeout=8):
    wait_for_firmware(connection, timeout, probe=b"esplink:token-capability\n",
                      expected=b"ESPLINK:TOKEN-ROTATION:1",
                      unsupported="Token rotation not detected. Flash the updated ESP Link firmware (1.2.1+) or check the COM port.")
    if ask("Replace device token? The old token will stop working. [y/N]: ").strip().lower() != "y":
        return False
    return submit(connection, b"esplink:rotate-token\n", timeout)


def normalize_token(value):
    value = value.strip()
    if re.fullmatch(r"[0-9a-zA-Z]*", value) is None:
        raise ValueError("Use Latin letters (A-Z) and digits (0-9) only. No spaces inside the token.")
    if not 8 <= len(value) <= 64:
        raise ValueError(f"Token length: {len(value)}. Use 8 to 64 characters.")
    return value.lower()


def set_token(connection, ask=input, timeout=8):
    wait_for_firmware(connection, timeout, probe=b"esplink:alphanumeric-token-capability\n",
                      expected=b"ESPLINK:ALPHANUMERIC-TOKEN:2",
                      unsupported="Flexible token length requires ESP Link firmware 1.2.4+. Flash it or check the COM port.")
    token = normalize_token(ask("New token (8-64 characters: A-Z, 0-9): "))
    if token != normalize_token(ask("Repeat new token: ")):
        raise ValueError("Tokens do not match. Nothing was changed.")
    return submit(connection, b"esplink:set-token:" + token.encode("ascii") + b"\n", timeout)


def submit(connection, frame, timeout):
    connection.write(frame)
    connection.flush()
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        reply = connection.readline().strip()
        if reply == b"ESPLINK:TOKEN-SAVED":
            return True
        if reply == b"ESPLINK:ERROR:TOKEN-REUSED":
            raise ValueError("Choose a token different from the current token and Wi-Fi password.")
        if reply.startswith(b"ESPLINK:ERROR:"):
            raise RuntimeError("The board did not confirm saving a new token. Check get_token.bat before retrying.")
    raise RuntimeError("No confirmation. The token MAY have changed. Check get_token.bat before retrying.")


def main():
    parser = argparse.ArgumentParser(description="Set or generate the device token over USB without changing the Wi-Fi password.")
    parser.add_argument("--port", help="ESP32 serial port, e.g. COM5")
    args = parser.parse_args()
    with open_usb(args.port) as connection:
        mode = input("[1] Generate a random token  [2] Enter my own token  [Enter] Cancel: ").strip()
        if mode not in ("", "1", "2"):
            raise ValueError("Select 1, 2, or press Enter to cancel.")
        changed = rotate_token(connection) if mode == "1" else set_token(connection) if mode == "2" else False
    if changed:
        print("SUCCESS: New token saved. Wi-Fi password unchanged.")
        print("Run get_token.bat, press EN, and enter the new Device token in ESP Link.")
    else:
        print("Cancelled. No change sent.")


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\nCancelled. If you already confirmed the change, check get_token.bat.")
        sys.exit(1)
    except (OSError, ValueError, RuntimeError) as error:
        print(f"ERROR: {error}")
        sys.exit(1)
