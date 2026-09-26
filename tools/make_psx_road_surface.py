#!/usr/bin/env python3
"""Make a seamless asphalt tile from VadaCross's purchased road pack.

Run with the original ZIP as the argument. Pillow is required. The output is
game texture content; the source pack itself is not copied into the repo.
"""

from pathlib import Path
import io
import sys
import zipfile

from PIL import Image, ImageOps


def mirrored_tile(patch: Image.Image) -> Image.Image:
    width, height = patch.size
    tile = Image.new("RGBA", (width * 2, height * 2))
    tile.paste(patch, (0, 0))
    tile.paste(ImageOps.mirror(patch), (width, 0))
    tile.paste(ImageOps.flip(patch), (0, height))
    tile.paste(ImageOps.flip(ImageOps.mirror(patch)), (width, height))
    return tile


def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit("usage: make_psx_road_surface.py ROAD_PACK.zip")
    archive = Path(sys.argv[1])
    with zipfile.ZipFile(archive) as source:
        def read_texture(filename: str) -> Image.Image:
            name = next(
                (name for name in source.namelist() if name.endswith("Textures/" + filename)),
                None,
            )
            if name is None:
                raise SystemExit(f"{filename} is missing from the road pack")
            with source.open(name) as image_file:
                return Image.open(io.BytesIO(image_file.read())).convert("RGBA")

        asphalt = read_texture("2WayBaseRoad.png")
        paving = read_texture("Path.png")

    # This patch sits between the white centre line and yellow edge stripe.
    # Mirror all four quadrants so REPEAT sampling has matching opposite edges.
    root = Path(__file__).resolve().parents[1] / "assets/textures/world/roads"
    root.mkdir(parents=True, exist_ok=True)
    for filename, tile in (
        ("psx-asphalt.png", mirrored_tile(asphalt.crop((300, 192, 428, 320)))),
        ("psx-paving.png", mirrored_tile(paving.crop((0, 0, 256, 256)))),
    ):
        output = root / filename
        tile.save(output, optimize=True)
        print(output)


if __name__ == "__main__":
    main()
