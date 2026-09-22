#!/usr/bin/env python3
"""Compare the video area of a hardware-decode screenshot with the same area of
a software-decode screenshot of the same clip.

Used to prove the zero-copy display path is correct end to end: decoded pixels
have to reach the screen with the right geometry, the right colour plane order
and the right colours.  Both screenshots are taken while mpv plays a 2x2
solid-colour clip (red | lime over blue | white), so:

  * each quadrant must come out the colour it was encoded as - a swapped U/V
    plane or a wrong stride shows up here immediately, and
  * the hardware picture must match the software one.

A strict pixel-identical match is not required: the hardware path converts
YUV to RGB on the GPU while the control converts on the CPU, and those two
round differently.  Differences of one LSB are expected and accepted; anything
larger, or concentrated in a block (which would mean a torn or stale buffer),
is a failure.

Usage: screenshot_compare.py hardware.png software.png
"""
import sys

import numpy as np
from PIL import Image

COLORS = {"red": (255, 0, 0), "lime": (0, 255, 0), "blue": (0, 0, 255)}
TOL = 40
MAX_ACCEPTED_DIFF = 1


def quadrant_means(rgb):
    h, w = rgb.shape[:2]
    return {
        "top-left": rgb[: h // 2, : w // 2].reshape(-1, 3).mean(axis=0),
        "top-right": rgb[: h // 2, w // 2:].reshape(-1, 3).mean(axis=0),
        "bottom-left": rgb[h // 2:, : w // 2].reshape(-1, 3).mean(axis=0),
    }


def video_window(path):
    """Bounding box of the three uniquely-coloured quadrants."""
    array = np.asarray(Image.open(path).convert("RGB")).astype(np.int16)
    x0 = y0 = 1 << 30
    x1 = y1 = -1
    for rgb in COLORS.values():
        mask = np.all(np.abs(array - np.array(rgb, dtype=np.int16)) <= TOL,
                      axis=-1)
        ys, xs = np.nonzero(mask)
        if len(ys) < 1000:
            return None
        x0, y0 = min(x0, int(xs.min())), min(y0, int(ys.min()))
        x1, y1 = max(x1, int(xs.max())), max(y1, int(ys.max()))
    return (x0, y0, x1 + 1, y1 + 1)


def main(hardware_path, software_path):
    hardware = Image.open(hardware_path).convert("RGB")
    software = Image.open(software_path).convert("RGB")
    if hardware.size != software.size:
        print(f"FAIL: screen sizes differ: {hardware.size} vs {software.size}")
        return 1

    box = video_window(hardware_path)
    if box is None:
        print(f"FAIL: {hardware_path} does not contain the test pattern at all "
              f"- the screenshot caught the desktop, not the player. That is a "
              f"capture problem, not a decode result.")
        return 1
    print(f"video window: {box[2] - box[0]}x{box[3] - box[1]} "
          f"at ({box[0]},{box[1]})")

    # The control shot has to show the same clip, otherwise the comparison
    # below would just be measuring "desktop pixels vs video pixels".
    software_box = video_window(software_path)
    if software_box is None:
        print(f"FAIL: {software_path} does not contain the test pattern at all "
              f"- the control playback was not captured. Capture problem, not "
              f"a hardware decode failure.")
        return 1
    if software_box != box:
        print(f"FAIL: the two screenshots show the pattern at different places: "
              f"{box} vs {software_box}")
        return 1

    hw = np.asarray(hardware.crop(box)).astype(np.int16)
    sw = np.asarray(software.crop(box)).astype(np.int16)
    failures = 0

    # 1. Plane order and colour conversion, from the quadrants themselves.
    expected = {"top-left": (255, 0, 0), "top-right": (0, 255, 0),
                "bottom-left": (0, 0, 255)}
    names = {"top-left": "red", "top-right": "lime", "bottom-left": "blue"}
    for quadrant, mean in quadrant_means(hw).items():
        want = np.array(expected[quadrant], dtype=float)
        good = np.all(np.abs(mean - want) <= TOL)
        print(f"  {'ok  ' if good else 'FAIL'} {quadrant:12s} mean RGB "
              f"{np.round(mean, 1)} ~ {names[quadrant]}")
        failures += 0 if good else 1

    # 2. Hardware vs software pixels.
    diff = np.abs(hw - sw)
    worst = int(diff.max())
    differing = int(np.count_nonzero(diff.max(axis=-1)))
    pixels = int(diff.shape[0] * diff.shape[1])
    print(f"  differing pixels {differing}/{pixels} "
          f"({100.0 * differing / pixels:.3f}%), max channel diff {worst}")
    within = worst <= MAX_ACCEPTED_DIFF
    print(f"  {'ok  ' if within else 'FAIL'} within {MAX_ACCEPTED_DIFF} LSB "
          f"of the software render")
    failures += 0 if within else 1

    # 3. Reject block-shaped damage: a torn or stale buffer shows up as a dense
    #    band of *materially* wrong pixels.  Rounding noise is excluded on
    #    purpose - it is uniformly scattered and, at one LSB, harmless; counting
    #    it would make this fire on perfectly good full-screen captures.
    serious = diff.max(axis=-1) > MAX_ACCEPTED_DIFF
    if serious.any():
        worst_row = int(serious.sum(axis=1).max())
        worst_col = int(serious.sum(axis=0).max())
        row_limit = serious.shape[1] // 4
        col_limit = serious.shape[0] // 4
        spread = worst_row < row_limit and worst_col < col_limit
        print(f"  {'ok  ' if spread else 'FAIL'} no dense band of wrong pixels "
              f"(worst row {worst_row} < {row_limit}, worst column {worst_col} "
              f"< {col_limit})")
        failures += 0 if spread else 1
    else:
        print("  ok   no dense band of wrong pixels (nothing above 1 LSB)")

    print("\nRESULT:", "PASS" if failures == 0 else "FAIL")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1], sys.argv[2]))
