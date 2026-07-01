"""
bgremove.py - Remove black backgrounds and watermark icons from PNGs.

Usage:
    python bgremove.py <input.png> <output.png> [options]

Options:
    --threshold <n>     RGB cutoff for "black" (default 30).
                        Raise if faint dark fringe remains after processing.
    --icon <x> <y>      Seed point inside a watermark/icon to erase.
                        Can be specified multiple times for multiple icons.
                        Uses flood-fill, so only connected pixels are removed.
    --no-bg             Skip background removal (only erase icons).

Examples:
    python bgremove.py logo.png logo_clean.png
    python bgremove.py logo.png logo_clean.png --icon 1948 1948
    python bgremove.py logo.png logo_clean.png --threshold 50 --icon 1948 1948
"""

import sys
import argparse
from collections import deque
from PIL import Image


def remove_background(px, width, height, threshold):
    """BFS flood-fill from every border pixel. Any pixel connected to the
    image edge whose RGB channels are all below `threshold` (or is already
    transparent) is treated as background and made fully transparent."""
    visited = [[False] * height for _ in range(width)]
    q = deque()

    def try_enqueue(x, y):
        if x < 0 or x >= width or y < 0 or y >= height:
            return
        if visited[x][y]:
            return
        r, g, b, a = px[x, y]
        if a == 0 or (r < threshold and g < threshold and b < threshold):
            visited[x][y] = True
            q.append((x, y))

    for x in range(width):
        try_enqueue(x, 0)
        try_enqueue(x, height - 1)
    for y in range(height):
        try_enqueue(0, y)
        try_enqueue(width - 1, y)

    count = 0
    while q:
        x, y = q.popleft()
        px[x, y] = (0, 0, 0, 0)
        count += 1
        try_enqueue(x + 1, y)
        try_enqueue(x - 1, y)
        try_enqueue(x, y + 1)
        try_enqueue(x, y - 1)

    return count


def erase_icon(px, width, height, seed_x, seed_y, limit=500_000):
    """BFS flood-fill from a seed point inside an icon/watermark.
    Erases all connected non-transparent pixels. Aborts if more than
    `limit` pixels are found (safety guard against hitting the main image)."""
    r, g, b, a = px[seed_x, seed_y]
    if a <= 10:
        print(f"  Seed ({seed_x},{seed_y}) is already transparent — skipping.")
        return 0

    visited = {(seed_x, seed_y)}
    q = deque([(seed_x, seed_y)])
    count = 0

    while q:
        x, y = q.popleft()
        r, g, b, a = px[x, y]
        if a > 10:
            px[x, y] = (0, 0, 0, 0)
            count += 1
            if count > limit:
                print(f"  ABORTED at {limit} pixels — seed may be inside the main image.")
                return count
            for nx, ny in [(x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)]:
                if 0 <= nx < width and 0 <= ny < height and (nx, ny) not in visited:
                    visited.add((nx, ny))
                    q.append((nx, ny))

    return count


def main():
    parser = argparse.ArgumentParser(
        description="Remove black backgrounds and watermark icons from PNGs.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    parser.add_argument("input",  help="Input PNG path")
    parser.add_argument("output", help="Output PNG path")
    parser.add_argument("--threshold", type=int, default=30,
                        help="RGB cutoff for black background (default 30)")
    parser.add_argument("--icon", nargs=2, type=int, metavar=("X", "Y"),
                        action="append", default=[],
                        help="Seed point inside an icon to erase (repeatable)")
    parser.add_argument("--no-bg", action="store_true",
                        help="Skip background removal")
    args = parser.parse_args()

    img = Image.open(args.input).convert("RGBA")
    px = img.load()
    width, height = img.size
    print(f"Loaded {width}x{height} PNG: {args.input}")

    if not args.no_bg:
        print(f"Removing background (threshold={args.threshold})...")
        n = remove_background(px, width, height, args.threshold)
        print(f"  Erased {n:,} background pixels.")

    for seed_x, seed_y in args.icon:
        print(f"Erasing icon at seed ({seed_x},{seed_y})...")
        n = erase_icon(px, width, height, seed_x, seed_y)
        print(f"  Erased {n:,} icon pixels.")

    img.save(args.output)
    print(f"Saved: {args.output}")


if __name__ == "__main__":
    main()
