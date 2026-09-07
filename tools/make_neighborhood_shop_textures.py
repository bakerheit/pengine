"""Reproducible readable, low-resolution shop signs using the game's font."""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
FONT = ROOT / 'assets/fonts/BebasNeue-Regular.ttf'
OUT = ROOT / 'assets/textures/world/neighborhood'

def sign(filename, title, subtitle, background, foreground, accent):
    image = Image.new('RGBA', (1024, 128), background)
    draw = ImageDraw.Draw(image)
    draw.rectangle((4, 4, 1019, 123), outline=accent, width=4)
    size = 74
    while True:
        font = ImageFont.truetype(str(FONT), size)
        if draw.textlength(title, font=font) < 970:
            break
        size -= 1
    draw.text((512, 4), title, font=font, fill=foreground, anchor='mt')
    small = ImageFont.truetype(str(FONT), 28)
    draw.text((512, 91), subtitle, font=small, fill=accent, anchor='mt')
    image.save(OUT / filename)

if __name__ == '__main__':
    OUT.mkdir(parents=True, exist_ok=True)
    sign('rooks-auto-repair.png', "ROOK'S AUTO REPAIR", 'BRAKES  /  TIRES  /  TUNE-UPS',
         '#77372e', '#fff0cf', '#e4b95f')
    sign('spin-cycle.png', 'SPIN CYCLE LAUNDROMAT', 'SELF SERVICE  /  WASH  /  DRY  /  FOLD',
         '#214c54', '#fff2d2', '#80cbc4')
