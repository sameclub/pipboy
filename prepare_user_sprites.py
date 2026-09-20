"""Pack the four user-supplied mascot frames as RGB565 bitmaps.

The source images remain unchanged under research/user-frames/. The firmware
uses a scaled RGB copy so the supplied outlines and horizontal scanlines stay
visible instead of collapsing into a solid 1-bit silhouette.
"""
from pathlib import Path
from PIL import Image

root = Path(__file__).resolve().parent
source_dir = root / "research/user-frames"
source_paths = sorted(source_dir.glob("frame_*.png"))
if len(source_paths) != 4:
    raise SystemExit(f"expected exactly 4 frame_*.png files, found {len(source_paths)}")

images = []
green_points = []
for path in source_paths:
    image = Image.open(path).convert("RGB")
    if image.size != (358, 498):
        raise ValueError(f"{path.name}: expected 358x498, got {image.size}")
    pixels = image.load()
    for y in range(image.height):
        for x in range(image.width):
            r, g, b = pixels[x, y]
            if g > 3 and g > r * 1.2 and g > b * 1.1:
                green_points.append((x, y))
    images.append(image)

left = min(x for x, _ in green_points)
top = min(y for _, y in green_points)
right = max(x for x, _ in green_points) + 1
bottom = max(y for _, y in green_points) + 1
common = (left, top, right, bottom)

for old in root.glob("assets/user-walk-*.png"):
    old.unlink()

GREEN_BOOST = 2.20
UI_GREEN = (0, 252, 96)  # RGB expansion of the firmware's GREEN = 0x07ec

def rgb565(rgb):
    r, g, b = rgb
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)

packed = []
for index, image in enumerate(images):
    cropped = image.crop(common).resize((52, 101), Image.Resampling.LANCZOS)
    recolored = []
    for red, green, blue in cropped.getdata():
        if green == 0:
            recolored.append((0, 0, 0))
            continue
        intensity = min(1.0, green * GREEN_BOOST / 255.0)
        recolored.append((0, round(UI_GREEN[1] * intensity), round(UI_GREEN[2] * intensity)))
    cropped.putdata(recolored)
    canvas = Image.new("RGB", (72, 104), (0, 0, 0))
    canvas.paste(cropped, (10, 2))
    canvas.save(root / f"assets/user-walk-{index:02d}.png")
    packed.append([rgb565(pixel) for pixel in canvas.getdata()])

header = "#pragma once\n#include <Arduino.h>\n"
header += f"constexpr uint8_t WALK_FRAME_COUNT = {len(packed)};\n"
header += "static uint16_t walkFrames[WALK_FRAME_COUNT][72 * 104] PROGMEM = {\n"
for frame in packed:
    header += "{" + ",".join(f"0x{value:04x}" for value in frame) + "},\n"
header += "};\n"
(root / "src/walk_frames.h").write_text(header, encoding="ascii")

contact = Image.new("RGB", (72 * 4, 104), (0, 0, 0))
for index in range(4):
    contact.paste(Image.open(root / f"assets/user-walk-{index:02d}.png").convert("RGB"), (index * 72, 0))
contact.resize((72 * 4 * 4, 104 * 4), Image.Resampling.NEAREST).save(
    root / "dist/user-walk-contact-v0.6.png"
)
print(f"Packed {len(packed)} user RGB565 frames from {common} into 72x104, {len(packed) * 72 * 104 * 2} bytes")
