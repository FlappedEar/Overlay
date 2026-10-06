"""Regenerate the app icon files from native/resources/branding/app-icon.svg (KAN-202).

Writes FlappedEar.icns (macOS, the tile on Apple's icon grid), FlappedEar.ico (Windows)
and app-logo.png (window icon and the editor's header logo), both cropped to the tile.
Every size is rendered from the SVG rather than scaled down from a large bitmap.

Needs CairoSVG and Pillow: python3 -m pip install cairosvg pillow
"""
import argparse
import io
import struct
from pathlib import Path

import cairosvg
from PIL import Image

BRANDING = Path(__file__).resolve().parent.parent / "native" / "resources" / "branding"
# The tile in the master's 1024 px canvas (x, y, size); see app-icon.svg.
TILE = (100, 100, 824)
# ICNS element types, each a PNG of the given size; Pillow's writer leaves out 16 px.
ICNS_TYPES = ((b"icp4", 16), (b"icp5", 32), (b"ic11", 32), (b"ic12", 64), (b"ic07", 128),
              (b"ic13", 256), (b"ic08", 256), (b"ic14", 512), (b"ic09", 512), (b"ic10", 1024))
ICO_SIZES = (16, 24, 32, 48, 64, 128, 256)
LOGO_SIZE = 512


def render(svg, size, crop_to_tile):
    if crop_to_tile:
        x, y, s = TILE
        svg = svg.replace('viewBox="0 0 1024 1024"', f'viewBox="{x} {y} {s} {s}"', 1)
    png = cairosvg.svg2png(bytestring=svg.encode(), output_width=size, output_height=size)
    return Image.open(io.BytesIO(png)).convert("RGBA")


def png_bytes(image):
    out = io.BytesIO()
    image.save(out, "PNG", optimize=True)
    return out.getvalue()


def write_icns(path, svg):
    rendered = {}
    elements = b""
    for kind, size in ICNS_TYPES:
        if size not in rendered:
            rendered[size] = png_bytes(render(svg, size, False))
        elements += kind + struct.pack(">I", 8 + len(rendered[size])) + rendered[size]
    path.write_bytes(b"icns" + struct.pack(">I", 8 + len(elements)) + elements)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--out", type=Path, default=BRANDING,
                        help="output folder (default: the branding folder)")
    args = parser.parse_args()
    svg = (BRANDING / "app-icon.svg").read_text(encoding="utf-8")
    if 'viewBox="0 0 1024 1024"' not in svg:
        raise SystemExit("app-icon.svg must keep viewBox=\"0 0 1024 1024\"")
    args.out.mkdir(parents=True, exist_ok=True)

    write_icns(args.out / "FlappedEar.icns", svg)

    ico = [render(svg, size, True) for size in ICO_SIZES]
    ico[-1].save(args.out / "FlappedEar.ico", sizes=[(s, s) for s in ICO_SIZES],
                 append_images=ico[:-1])

    render(svg, LOGO_SIZE, True).save(args.out / "app-logo.png", optimize=True)


if __name__ == "__main__":
    main()
