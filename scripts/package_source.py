"""Package only project sources, excluding build tools, device credentials and signing files."""

from pathlib import Path
import zipfile

ROOT = Path(__file__).resolve().parents[1]
EXCLUDED = {".venv", ".pio", ".git", ".gradle", "build", "dist", "DerivedData", "__pycache__", "xcuserdata"}
SUFFIXES = {".ipa", ".p12", ".mobileprovision", ".pyc", ".apk", ".aab", ".jks", ".keystore"}


def main():
    destination = ROOT / "dist" / "ESPLink-source.zip"
    destination.parent.mkdir(exist_ok=True)
    with zipfile.ZipFile(destination, "w", zipfile.ZIP_DEFLATED) as archive:
        # Explicit allowlist prevents accidentally packaging unrelated local files.
        paths = [ROOT / "README.md", ROOT / ".gitignore", ROOT / ".gitattributes"]
        paths.extend(path for path in [ROOT / "flash_esp32.bat", ROOT / "get_token.bat", ROOT / "change_wifi_password.bat"] if path.is_file())
        for name in ["ios", "android", "firmware", ".github", "scripts", "docs"]:
            paths.extend((ROOT / name).rglob("*"))
        for path in sorted(paths):
            relative = path.relative_to(ROOT)
            if not path.is_file() or any(part in EXCLUDED or part.endswith(".xcodeproj") for part in relative.parts):
                continue
            if path.suffix in SUFFIXES or path.name in {".env", "secrets.h", "local.properties"}:
                continue
            archive.write(path, Path("ESPLink") / relative)
    print(destination)


if __name__ == "__main__":
    main()
