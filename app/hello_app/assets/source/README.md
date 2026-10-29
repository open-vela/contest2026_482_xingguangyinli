# Approved Gemini S1 Choice Artwork

The private source-artwork directory is supplied through
`LC_CHOICE_SOURCE_DIR` and is accepted only when every size and SHA-256 matches
`tools/convert_choice_assets.py`. The repository never records a contributor's
home-directory path.

- `sitting.png`: full-screen background.
- `takeout_bag_ui.png`: primary “help me order takeout” choice.
- `mystery_box_ui.png`: random previously-liked takeout choice.
- `homedinner.png`: inspect refrigerator / eat at home choice.

The generator emits 240x320 and 320x240 RGB565 backgrounds plus three 64x64
RGB565 cards. The generated C files are self-contained; the device never reads
the desktop PNG files at runtime.
