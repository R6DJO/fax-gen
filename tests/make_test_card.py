#!/usr/bin/env python3
"""Generate the test cards used by `make test`.

Both cards are drawn from scratch (no third-party imagery), so they can be
distributed under the project's MIT licence.

  test_card.tiff      600x200, 8 bit/sample, PhotometricInterpretation=1
                      (MinIsBlack) - bars + grey ramp
  test_card_1bit.tiff 600x200, 1 bit/sample, PhotometricInterpretation=1
                      (MinIsBlack) - bilevel input, the common FAX case

Requires Pillow. Run from the tests/ directory:

    python3 make_test_card.py
"""
from PIL import Image, ImageDraw

W, H = 600, 200


def card_8bit() -> Image.Image:
    img = Image.new("L", (W, H), 255)
    d = ImageDraw.Draw(img)

    # 6 vertical bars, alternating black/white (resolution check)
    for i in range(6):
        d.rectangle([i * 50, 0, i * 50 + 49, 99], fill=0 if i % 2 == 0 else 255)

    # 12-step grey ramp (threshold check)
    for i in range(12):
        d.rectangle([i * 50, 100, i * 50 + 49, 199], fill=int(i * 255 / 11))

    d.text((250, 150), "TIFF2WAV TEST CARD", fill=0)
    return img


def card_1bit() -> Image.Image:
    img = Image.new("1", (W, H), 1)          # 1 = white in Pillow mode "1"
    d = ImageDraw.Draw(img)

    for i in range(6):
        d.rectangle([i * 50, 0, i * 50 + 49, 99], fill=0 if i % 2 == 0 else 1)

    # half black / half white lower band (no grey levels in a bilevel image)
    for i in range(12):
        d.rectangle([i * 50, 100, i * 50 + 49, 199], fill=0 if i < 6 else 1)

    d.text((250, 150), "TIFF2WAV 1BIT", fill=0)
    return img


card_8bit().save("test_card.tiff")
card_1bit().save("test_card_1bit.tiff", photometric="minisblack")
print("wrote test_card.tiff (600x200, 8 bit, MinIsBlack)")
print("wrote test_card_1bit.tiff (600x200, 1 bit, MinIsBlack)")
