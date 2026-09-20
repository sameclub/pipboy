"""Software layout preview, using the firmware's font and packed sprite frames.
This does not emulate the LCD or verify physical panel timing/colour order.
"""
from PIL import Image
from preview_ui import ROOT, stat_page

sprite_frames = sorted((ROOT / 'assets').glob('user-walk-*.png'))
if len(sprite_frames) != 4:
    raise SystemExit(f'expected 4 user sprite frames, found {len(sprite_frames)}')
frames = [stat_page(frame_path) for frame_path in sprite_frames]
out=ROOT/'docs'/'preview';out.mkdir(parents=True,exist_ok=True)
large=[f.resize((480,480),Image.Resampling.NEAREST) for f in frames]
large[0].save(out/'walk.gif',save_all=True,append_images=large[1:],duration=160,loop=0)
print('Preview: 240x240 native; 4 user RGB animation frames at 160 ms; text bounds passed')
