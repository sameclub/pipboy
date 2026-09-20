"""Software layout preview: sample data, not a hardware capture."""
from pathlib import Path
import re

from PIL import Image, ImageDraw


ROOT = Path(__file__).resolve().parent
OUT = ROOT / "docs" / "preview"
GREEN = (0, 255, 99)
DIM = (0, 162, 57)
BLACK = (0, 0, 0)
TABS = ("STAT", "DATA", "RADIO", "SYS")

def _glcdfont() -> Path:
    """Locate Arduino_GFX's 5x7 glyph table.

    Prefer the copy PlatformIO installs under .pio/libdeps (present after one
    `pio run`), and fall back to a sibling Launcher checkout for the shared
    development layout.
    """
    candidates = list(ROOT.glob(".pio/libdeps/*/GFX Library for Arduino/src/font/glcdfont.h"))
    candidates.append(ROOT.parent / "Launcher" / "lib" / "Arduino_GFX" / "src" / "font" / "glcdfont.h")
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    raise SystemExit(
        "glcdfont.h not found. Run `pio run` once so PlatformIO fetches "
        "Arduino_GFX, then run this script again."
    )


font_source = _glcdfont().read_text()
font_source = font_source.split("font[] PROGMEM = {", 1)[1].split("};", 1)[0]
font_source = re.sub(r"//[^\n]*", "", font_source)
FONT = bytes(int(value, 16) for value in re.findall(r"0x([0-9A-Fa-f]{2})", font_source))


def text_width(value, scale=2, advance=None):
    advance = advance if advance is not None else 6 * scale
    return (len(value) - 1) * advance + 5 * scale if value else 0


def text(image, x, y, value, scale=2, color=GREEN, advance=None):
    advance = advance if advance is not None else 6 * scale
    width = text_width(value, scale, advance)
    assert 0 <= x and x + width <= image.width, (x, y, value)
    assert 0 <= y and y + 8 * scale <= image.height, (x, y, value)
    draw = ImageDraw.Draw(image)
    for char in value:
        assert 32 <= ord(char) < 127, char
        for column, bits in enumerate(FONT[ord(char) * 5:ord(char) * 5 + 5]):
            for row in range(8):
                if bits & (1 << row):
                    px, py = x + column * scale, y + row * scale
                    draw.rectangle((px, py, px + scale - 1, py + scale - 1), fill=color)
        x += advance


def centered(image, y, value, scale=2, color=GREEN):
    text(image, (image.width - text_width(value, scale)) // 2, y, value, scale, color)


def page(tab, hint):
    image = Image.new("RGB", (240, 240), BLACK)
    draw = ImageDraw.Draw(image)
    for index, title in enumerate(TABS):
        active = index == tab
        if active:
            draw.rectangle((index * 60 + 2, 4, index * 60 + 57, 25), fill=GREEN)
        x = index * 60 + (60 - text_width(title, advance=11)) // 2
        text(image, x, 8, title, color=BLACK if active else GREEN, advance=11)
    draw.rectangle((8, 29, 231, 30), fill=GREEN)
    draw.line((8, 216, 231, 216), fill=DIM)
    centered(image, 222, hint)
    return image


def menu_row(image, y, title, selected=False):
    if selected:
        ImageDraw.Draw(image).rectangle((6, y - 4, 233, y + 19), fill=GREEN)
    text(image, 12, y, title, color=BLACK if selected else GREEN)


def stat_page(frame_path=None):
    image = page(0, "L/R PAGE")
    centered(image, 40, "12:48", scale=5)
    centered(image, 89, "SAT 2026-09-19")
    with Image.open(frame_path or ROOT / "assets" / "user-walk-00.png") as source:
        sprite = source.convert("RGB")
    assert sprite.size == (72, 104), sprite.size
    image.paste(sprite, (84, 109))
    draw = ImageDraw.Draw(image)
    draw.rectangle((24, 133, 55, 148), outline=GREEN, width=2)
    draw.rectangle((56, 138, 59, 143), fill=GREEN)
    draw.rectangle((28, 137, 45, 144), fill=GREEN)
    text(image, 19, 160, "~76%")
    text(image, 24, 186, "BAT", color=DIM)
    for index, height in enumerate((4, 8, 12, 16)):
        x = 182 + index * 7
        draw.rectangle((x, 148 - height, x + 3, 147), fill=GREEN)
    text(image, 168, 160, "WI-FI")
    text(image, 187, 186, "ON", color=DIM)
    return image


def files_page():
    image = page(1, "A OPEN  B PARENT")
    text(image, 8, 41, "/")
    text(image, 164, 41, "1/4", color=DIM)
    for index, title in enumerate(("+ LOGS", "+ DOCUMENTS", "  notes.txt", "  pipboy.ini")):
        menu_row(image, 72 + index * 28, title, selected=index == 0)
    text(image, 8, 192, "DIRECTORY", color=DIM)
    return image


def radio_page():
    image = page(2, "A OPEN  B BACK")
    text(image, 8, 41, "WIRELESS", color=DIM)
    menu_row(image, 78, "WI-FI", selected=True)
    menu_row(image, 112, "BLUETOOTH LE")
    text(image, 8, 166, "WI-FI: CONNECTED")
    text(image, 8, 190, "BLE: OFF", color=DIM)
    return image


def system_page():
    image = page(3, "A OPEN  B BACK")
    text(image, 8, 41, "SYSTEM", color=DIM)
    for index, title in enumerate(("DEVICE", "BATTERY", "CLOCK", "KEY TEST")):
        menu_row(image, 70 + index * 29, title, selected=index == 0)
    text(image, 8, 194, "DEVICE & STORAGE", color=DIM)
    return image


def wifi_page():
    image = page(2, "A SCAN  X SETUP")
    text(image, 8, 41, "WI-FI", color=DIM)
    text(image, 152, 41, "B BACK", color=DIM)
    for index, value in enumerate((
        "CONNECTED", "HOME-NET", "192.168.1.42",
        "-52dBm CH 6", "02:00:00:00:00:01",
    )):
        text(image, 8, 74 + index * 26, value)
    return image


def device_page():
    image = page(3, "A TEST  B BACK")
    text(image, 8, 41, "DEVICE", color=DIM)
    for index, value in enumerate((
        "CPU       240 MHZ", "PSRAM        8 MB", "HEAP       186 KB",
        "TF          READY", "SD TEST PASS",
    )):
        text(image, 8, 74 + index * 26, value)
    return image


def main():
    screens = (
        ("01-stat", "01 / STAT", stat_page()),
        ("02-data", "02 / DATA", files_page()),
        ("03-radio", "03 / RADIO", radio_page()),
        ("04-sys", "04 / SYS", system_page()),
        ("05-wifi", "05 / RADIO > WI-FI", wifi_page()),
        ("06-device", "06 / SYS > DEVICE", device_page()),
    )
    OUT.mkdir(parents=True, exist_ok=True)
    overview = Image.new("RGB", (1520, 1152), (12, 18, 14))
    text(overview, 20, 16, "PIPBOY / UI LAYOUT", scale=3)
    text(overview, 20, 48, "SAMPLE DATA - NOT A DEVICE CAPTURE / EACH SCREEN: 240 X 240", color=DIM)
    draw = ImageDraw.Draw(overview)
    for index, (name, title, image) in enumerate(screens):
        image.save(OUT / f"{name}.png")
        x, y = 20 + (index % 3) * 500, 84 + (index // 3) * 530
        text(overview, x, y, title)
        draw.rectangle((x - 1, y + 25, x + 480, y + 506), outline=DIM)
        overview.paste(image.resize((480, 480), Image.Resampling.NEAREST), (x, y + 26))
    text(overview, 20, 1130, "L/R PAGE | U/D SELECT | A OPEN / RUN | B BACK", color=DIM)
    overview.save(OUT / "overview.png")
    print(f"UI layout: {len(screens)} native screens + overview saved to {OUT}")


if __name__ == "__main__":
    main()
