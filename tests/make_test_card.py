#!/usr/bin/env python3
"""Generate tests/test_card.tiff - a clean-room fax test card.

The card is generated from scratch (no third-party imagery), so it can be
distributed under the project's MIT licence.

Output: baseline TIFF, 600x200, 8 bit, PhotometricInterpretation=1
(MinIsBlack = 0 is black), no compression.
"""
from PIL import Image, ImageDraw

W, H = 600, 200

img = Image.new("L", (W, H), 255)
d = ImageDraw.Draw(img)

# 6 vertical bars, alternating black/white (resolution check)
for i in range(6):
    d.rectangle([i * 50, 0, i * 50 + 49, 99], fill=0 if i % 2 == 0 else 255)

# 12-step grey ramp (threshold check)
for i in range(12):
    d.rectangle([i * 50, 100, i * 50 + 49, 199], fill=int(i * 255 / 11))

d.text((250, 150), "TIFF2WAV TEST CARD", fill=0)

img.save("test_card.tiff")
print(f"wrote test_card.tiff ({W}x{H}, 8 bit, MinIsBlack)")
