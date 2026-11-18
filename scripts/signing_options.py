"""Prepare a manual GitHub Actions export using the supplied Apple profile."""

import datetime as dt
import hashlib
import os
from pathlib import Path
import plistlib
import shutil
import subprocess


def main():
    temp = Path(os.environ["RUNNER_TEMP"])
    with (temp / "profile.plist").open("rb") as source:
        profile = plistlib.load(source)
    bundle_id = os.environ["BUNDLE_ID"]
    entitlements = profile["Entitlements"]
    app_id = entitlements["application-identifier"].split(".", 1)[1]
    if app_id != bundle_id or "*" in app_id:
        raise SystemExit("Profile must contain an explicit App ID matching bundle_id.")
    if profile["ExpirationDate"] <= dt.datetime.now(dt.timezone.utc).replace(tzinfo=None):
        raise SystemExit("Provisioning profile has expired.")
    if profile.get("ProvisionsAllDevices"):
        raise SystemExit("Enterprise distribution is not supported by this example workflow.")
    development = entitlements.get("get-task-allow", False)
    method = "debugging" if development else ("release-testing" if profile.get("ProvisionedDevices") else "app-store-connect")
    team = profile["TeamIdentifier"][0]
    uuid = profile["UUID"]
    identities = subprocess.check_output(["security", "find-identity", "-v", "-p", "codesigning"], text=True)
    hashes = [hashlib.sha1(cert).hexdigest().upper() for cert in profile["DeveloperCertificates"]]
    identity = next((value for value in hashes if value in identities.upper()), None)
    if not identity:
        raise SystemExit("The imported .p12 certificate does not match this profile or has no private key.")
    target = Path.home() / "Library" / "MobileDevice" / "Provisioning Profiles"
    target.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(temp / "profile.mobileprovision", target / f"{uuid}.mobileprovision")
    options = {
        "method": method,
        "destination": "export",
        "teamID": team,
        "signingStyle": "manual",
        "signingCertificate": identity,
        "provisioningProfiles": {bundle_id: uuid},
        "stripSwiftSymbols": True,
    }
    with (temp / "ExportOptions.plist").open("wb") as output:
        plistlib.dump(options, output)
    with Path(os.environ["GITHUB_ENV"]).open("a", encoding="utf-8") as env:
        env.write(f"TEAM_ID={team}\nPROFILE_UUID={uuid}\nSIGNING_IDENTITY={identity}\n")
    print(f"Export method: {method}. No automatic upload to App Store Connect.")


if __name__ == "__main__":
    main()
