"""USB-only password change. No password in process arguments, files or logs."""

import argparse
import getpass
import sys
import time


def validate_password(password):
    if not 8 <= len(password) <= 63 or any(not 33 <= ord(char) <= 126 for char in password):
        raise ValueError("Use 8-63 ASCII characters without spaces (letters, numbers, punctuation).")
    return password


def choose_port(explicit, ports, ask=input):
    if explicit:
        return explicit
    if not ports:
        raise ValueError("No serial ports found. Check the USB DATA cable and Windows Device Manager.")
    if len(ports) == 1:
        return ports[0].device
    for index, port in enumerate(ports, 1):
        print(f"{index}. {port.device} - {port.description}")
    try:
        choice = int(ask("Select the ESP32 port number: "))
    except ValueError as error:
        raise ValueError("Invalid port selection.") from error
    if not 1 <= choice <= len(ports):
        raise ValueError("Invalid port selection.")
    return ports[choice - 1].device


def wait_for_firmware(connection, timeout=8, *, probe=b"esplink:hello\n",
                      expected=b"ESPLINK:WIFI-ONLY:1",
                      unsupported="Wi-Fi-only firmware not detected. Flash ESP Link 1.2.0+ and retry; check the selected port."):
    deadline = time.monotonic() + timeout
    next_probe = 0.0
    while time.monotonic() < deadline:
        if time.monotonic() >= next_probe:
            connection.write(probe)
            connection.flush()
            next_probe = time.monotonic() + 0.75
        line = connection.readline().strip()
        if line == expected:
            return
        # Never echo arbitrary boot lines: they can contain the token / current password.
    raise RuntimeError(unsupported)


def change_password(connection, password, timeout=8):
    value = validate_password(password)
    connection.write(("esplink:wifi-password:" + value + "\n").encode("ascii"))
    connection.flush()
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        reply = connection.readline().strip()
        if reply == b"ESPLINK:PASSWORD-SAVED":
            return
        if reply.startswith(b"ESPLINK:ERROR:"):
            raise RuntimeError("Board refused the password update. No success confirmation received.")
    raise RuntimeError("No save confirmation. Password MAY have changed. Press EN and check USB Serial before retrying.")


def open_usb(port):
    if not sys.stdin.isatty():
        raise RuntimeError("Run this interactively from the BAT file; input must not be redirected.")
    import serial
    from serial.tools import list_ports

    port = choose_port(port, list(list_ports.comports()))
    print(f"Opening {port} at 115200 baud. Close other serial monitors first.")
    print("If the board is restarting, wait. If needed, press EN once.")
    connection = serial.Serial(port=None, baudrate=115200, timeout=0.25, write_timeout=3)
    connection.dtr = False
    connection.rts = False
    connection.port = port
    return connection


def main():
    parser = argparse.ArgumentParser(description="Change ESP Link Wi-Fi password over USB without flashing.")
    parser.add_argument("--port", help="ESP32 serial port, e.g. COM5")
    args = parser.parse_args()
    with open_usb(args.port) as connection:
        wait_for_firmware(connection)
        print("Wi-Fi-only firmware detected. Device token will not change.")
        password = validate_password(getpass.getpass("New Wi-Fi password (hidden): "))
        confirmation = getpass.getpass("Repeat password (hidden): ")
        if password != confirmation:
            raise ValueError("Passwords do not match. Nothing was sent.")
        change_password(connection, password)
    print("SUCCESS: Wi-Fi password saved. The Wi-Fi access point is restarting.")
    print("On your phone, forget the old ESP32 network and reconnect with the new password.")
    print("In ESP Link use the SAME device token as before. No reflashing is needed.")


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\nCancelled. If submission already started, check USB Serial to confirm the current password.")
        sys.exit(1)
    except Exception as error:
        # Serial exceptions do not contain the submitted password; do not print raw board frames.
        print(f"ERROR: {error}")
        sys.exit(1)
