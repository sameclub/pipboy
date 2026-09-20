# User animation integration — v0.6

The runtime mascot animation uses the four PNG frames supplied by the user in
this task. The original files are retained under `research/user-frames/` and
are read in lexical filename order (`frame_00.png` through `frame_03.png`).

`prepare_user_sprites.py` uses a shared green-pixel crop and a fixed 72×104
RGB565 canvas so every frame has a stable anchor on the 240×240 LCD. The
firmware displays the four frames in the supplied order, one every 160 ms, and
retains the source's horizontal scanline gaps. Its green pixels are remapped to
the firmware's `GREEN` color (0x07ec) while their intensity is retained for
shading. Vault Boy and Fallout marks remain the
property of their respective rights holders.
