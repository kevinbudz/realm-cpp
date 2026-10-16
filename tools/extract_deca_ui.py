#!/usr/bin/env python3
"""Extract the Deca UI texture atlas from the final Flash client.swf.

io.decagames.rotmg.ui.texture.TextureParser draws every Deca UI element from one embedded atlas
(UIAssets_UI, a DefineBitsLossless2 bitmap) and two embedded JSON documents: UIAssets_UI_CONFIG
(TexturePacker frames) and UIAssets_UI_SLICE_CONFIG (3grid/9grid slice rectangles). Decompiled
sources such as Zolmex/RotMG-Flash-Client keep only the class stubs, so the art comes from the
SWF itself, e.g. https://static.drips.pw/rotmg/production/current/client.swf (build 1599033809).

The atlas is only a reference for authoring and checking ui/deca/*.svg (tools/compare_deca_ui.py);
the client never loads it.

Usage: extract_deca_ui.py CLIENT.swf OUTPUT_DIR [--frames]
Writes atlas.png (straight alpha), frames.json, slices.json and, with --frames, frames/NAME.png.
Requires Pillow.
"""
import argparse
import json
import lzma
import struct
import sys
import zlib
from pathlib import Path

from PIL import Image

SYMBOLS = {
    "io.decagames.rotmg.ui.assets.UIAssets_UI": "atlas",
    "io.decagames.rotmg.ui.assets.UIAssets_UI_CONFIG": "frames.json",
    "io.decagames.rotmg.ui.assets.UIAssets_UI_SLICE_CONFIG": "slices.json",
}
SYMBOL_CLASS = 76
DEFINE_BINARY_DATA = 87
DEFINE_BITS_LOSSLESS = (20, 36)


def swf_body(data: bytes) -> bytes:
    signature = data[:3]
    if signature == b"FWS":
        return data[8:]
    if signature == b"CWS":
        return zlib.decompress(data[8:])
    if signature == b"ZWS":
        properties = data[12:17]
        decoder = lzma.LZMADecompressor(
            lzma.FORMAT_RAW,
            filters=[lzma._decode_filter_properties(lzma.FILTER_LZMA1, properties)],
        )
        return decoder.decompress(data[17:])
    raise SystemExit("not a SWF file")


def tags(body: bytes):
    bits = body[0] >> 3
    at = (5 + 4 * bits + 7) // 8 + 4  # frame RECT, frame rate and count
    while at < len(body):
        header = struct.unpack_from("<H", body, at)[0]
        at += 2
        code, size = header >> 6, header & 0x3F
        if size == 0x3F:
            size = struct.unpack_from("<I", body, at)[0]
            at += 4
        yield code, body[at:at + size]
        at += size
        if code == 0:
            return


def lossless_image(code: int, tag: bytes) -> Image.Image:
    fmt, width, height = struct.unpack_from("<BHH", tag, 2)
    if fmt != 5:
        raise SystemExit(f"unsupported lossless bitmap format {fmt}")
    pixels = zlib.decompress(tag[7:])
    image = Image.frombuffer("RGBA", (width, height), pixels, "raw", "ARGB", 0, 1).copy()
    if code == 20:
        image.putalpha(255)
        return image
    # DefineBitsLossless2 stores premultiplied ARGB.
    data = bytearray(image.tobytes())
    for at in range(0, len(data), 4):
        alpha = data[at + 3]
        if 0 < alpha < 255:
            for c in range(at, at + 3):
                data[c] = min(255, (data[c] * 255 + alpha // 2) // alpha)
    return Image.frombytes("RGBA", image.size, bytes(data))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("swf", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--frames", action="store_true", help="also write one PNG per frame")
    args = parser.parse_args()

    all_tags = list(tags(swf_body(args.swf.read_bytes())))
    symbols = {}
    for code, tag in all_tags:
        if code == SYMBOL_CLASS:
            count = struct.unpack_from("<H", tag)[0]
            at = 2
            for _ in range(count):
                character = struct.unpack_from("<H", tag, at)[0]
                end = tag.index(b"\0", at + 2)
                symbols[tag[at + 2:end].decode()] = character
                at = end + 1
    characters = {}
    for code, tag in all_tags:
        if code == DEFINE_BINARY_DATA or code in DEFINE_BITS_LOSSLESS:
            characters[struct.unpack_from("<H", tag)[0]] = (code, tag)

    args.output.mkdir(parents=True, exist_ok=True)
    atlas = None
    for symbol, target in SYMBOLS.items():
        if symbol not in symbols or symbols[symbol] not in characters:
            raise SystemExit(f"{symbol} is not in {args.swf}")
        code, tag = characters[symbols[symbol]]
        if target == "atlas":
            atlas = lossless_image(code, tag)
            atlas.save(args.output / "atlas.png")
        else:
            document = json.loads(tag[6:].decode("utf-8-sig"))
            (args.output / target).write_text(json.dumps(document, indent=1, sort_keys=True) + "\n")

    if args.frames:
        frames = json.loads((args.output / "frames.json").read_text())["frames"]
        (args.output / "frames").mkdir(exist_ok=True)
        for name, frame in frames.items():
            box = frame["frame"]
            crop = atlas.crop((box["x"], box["y"], box["x"] + box["w"], box["y"] + box["h"]))
            crop.save(args.output / "frames" / name)
    print(f"wrote {args.output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
