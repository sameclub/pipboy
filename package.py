"""Package only the application image for Launcher SD installation."""
from pathlib import Path
import hashlib
import json
import shutil

VERSION = "1.3"
root = Path(__file__).resolve().parent
# The version lives in the firmware too; refuse to ship a mismatched name.
if f"PIPBOY v{VERSION}" not in (root / "src/main.cpp").read_text(encoding="utf-8"):
    raise SystemExit(f"src/main.cpp does not report v{VERSION}; bump it or fix VERSION")
source = root / ".pio/build/s3ai-pipboy/firmware.bin"
data = source.read_bytes()
if len(data) < 288 or data[0] != 0xE9 or int.from_bytes(data[12:14], "little") != 9:
    raise SystemExit("Not an ESP32-S3 application image")
if len(data) > 0x300000:
    raise SystemExit("Image exceeds the build-time application size")
if data[0x8000:0x8003] == bytes.fromhex("aa5001"):
    raise SystemExit("Image resembles a merged firmware: refusing SD package")
out = root / "dist"
out.mkdir(exist_ok=True)
target = out / f"PipBoy-v{VERSION}.bin"
shutil.copyfile(source, target)
digest = hashlib.sha256(data).hexdigest()
(out / "SHA256SUMS.txt").write_text(f"{digest}  {target.name}\n", encoding="ascii")
manifest = {
    "name": "PipBoy", "version": VERSION,
    "file": target.name, "bytes": len(data), "sha256": digest,
    "chip": "ESP32-S3", "flash_mb": 16, "psram_mb": 8,
    "format": "application-only", "build": "passed", "hardware_validation": "pending",
    "launcher_return": "current test partition boot unsupported; adaptation pending",
    "animation": "4 user-supplied walk frames at 160 ms/frame",
    "animation_source": "user-provided frame_00.png..frame_03.png",
    "clock": "NTP, UTC+8; unknown time shown as --:--",
    "ui": "STAT/DATA/RADIO/SYS; bitmap clock, date, mascot, battery and Wi-Fi; list-based submenus",
    "navigation": "left/right or SELECT: page; up/down: select; A: open/run; B: back; START: Wi-Fi setup",
    "battery": "GPIO8 calibrated ADC mV x4 x user scale; estimated percentage",
    "wifi": "phone SoftAP provisioning, own NVS namespace, status and scan",
    "bluetooth": "passive BLE scan; no pairing or Classic Bluetooth",
    "screen_sleep": "APP GPIO10 light-sleep and wake; RTC time retained; radios paused",
    "auto_sleep": "10 seconds without interaction; deferred during provisioning, scans, and file operations",
}
(out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
shutil.copyfile(root / "lib/wifi-portal/LICENSE", out / "esp-wifi-connect-MIT.txt")
print(json.dumps(manifest, indent=2))
