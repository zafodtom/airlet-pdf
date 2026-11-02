"""Rebuild the committed icon assets (development only; requires Pillow)."""
from pathlib import Path
from math import cos, sin, radians
from PIL import Image, ImageDraw

root = Path(__file__).resolve().parents[1] / 'assets'
scale = 4
image = Image.new('RGBA', (512 * scale, 512 * scale))
draw = ImageDraw.Draw(image)
def polygon(points, color):
    draw.polygon([(round(x * scale), round(y * scale)) for x, y in points], fill=color)
def arc(x, y, radius, start, end):
    return [(x + radius * cos(radians(a)), y + radius * sin(radians(a)))
            for a in range(start, end + 1, 3)]
draw.rounded_rectangle((16*scale,16*scale,496*scale,496*scale),108*scale,fill='#12344a')
polygon([(152,88),(294,88),(376,170),(376,388)] + arc(340,388,36,0,90) + [(152,424)] + arc(152,388,36,90,180) + [(116,124)] + arc(152,124,36,180,270), '#f6fbff')
polygon([(294,88),(294,148)] + [(316+22*cos(radians(a)),148+22*sin(radians(a))) for a in range(180,89,-3)] + [(376,170)], '#80dcd6')
polygon([(164,324),(348,212),(270,366),(240,303)], '#12a99f')
polygon([(240,303),(348,212),(262,325),(270,366)], '#12344a')
image = image.resize((256,256),Image.Resampling.LANCZOS)
image.save(root / 'airlet.png')
image.save(root / 'airlet.ico',sizes=[(n,n) for n in (16,24,32,48,64,128,256)])
