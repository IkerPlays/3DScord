"""Generates the 3DScord logo in every size the project needs. Original artwork (a chat bubble with "3D" on a blurple tile): it does not use the
logos of Discord or Nintendo. Usage: python3 tools/brand/brand.py  (from the repository root)"""
import os
from PIL import Image, ImageDraw, ImageFont, ImageFilter

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
FD = "/usr/share/fonts/opentype/inter/"

def font(name, size):
    return ImageFont.truetype(FD + "Inter-%s.otf" % name, size)

def tile(size, rounded=True, scale=4):
    """The logo at `size` pixels (drawn at `scale` times and reduced for smooth edges)."""
    S = size * scale
    im = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    grad = Image.new("RGBA", (S, S))
    gd = ImageDraw.Draw(grad)
    for y in range(S):
        k = y / S
        gd.line((0, y, S, y), fill=(int(88 + 60 * k), int(101 - 20 * k), int(242 - 10 * k), 255))
    mask = Image.new("L", (S, S), 0)
    if rounded:
        ImageDraw.Draw(mask).rounded_rectangle((0, 0, S - 1, S - 1), int(S * 0.215), fill=255)
    else:
        ImageDraw.Draw(mask).rectangle((0, 0, S, S), fill=255)
    im.paste(grad, (0, 0), mask)
    d = ImageDraw.Draw(im)
    u = S / 512.0
    d.rounded_rectangle((86 * u, 100 * u, 426 * u, 360 * u), 70 * u, fill=(255, 255, 255, 255))
    d.polygon([(150 * u, 340 * u), (130 * u, 428 * u), (236 * u, 350 * u)], fill=(255, 255, 255, 255))
    d.text((256 * u, 232 * u), "3D", font=font("Black", int(170 * u)), fill=(88, 101, 242, 255), anchor="mm")
    for k in range(3):
        d.ellipse(((190 + k * 66) * u, 312 * u, (204 + k * 66) * u, 326 * u), fill=(180, 186, 250, 255))
    return im.resize((size, size), Image.LANCZOS)

def banner():
    """The HOME Menu banner (256x128): dark background, the logo and the name."""
    W, H = 256, 128
    S = 4
    im = Image.new("RGB", (W * S, H * S))
    d = ImageDraw.Draw(im)
    for y in range(H * S):
        k = y / (H * S)
        d.line((0, y, W * S, y), fill=(int(24 + 14 * k), int(25 + 14 * k), int(34 + 18 * k)))
    glow = Image.new("RGBA", (W * S, H * S), (0, 0, 0, 0))
    ImageDraw.Draw(glow).ellipse((-40 * S, -20 * S, 150 * S, 150 * S), fill=(88, 101, 242, 120))
    ImageDraw.Draw(glow).ellipse((150 * S, 40 * S, 300 * S, 170 * S), fill=(235, 69, 158, 70))
    glow = glow.filter(ImageFilter.GaussianBlur(28 * S))
    im = Image.alpha_composite(im.convert("RGBA"), glow)
    logo = tile(76 * S)
    im.alpha_composite(logo, (16 * S, 26 * S))
    d = ImageDraw.Draw(im)
    d.text((102 * S, 56 * S), "3DScord", font=font("Black", 27 * S), fill=(255, 255, 255, 255), anchor="lm")
    d.text((103 * S, 82 * S), "Discord for Nintendo 3DS", font=font("SemiBold", 9 * S), fill=(200, 204, 212, 255), anchor="lm")
    return im.resize((W, H), Image.LANCZOS).convert("RGB")

def social(w=1280, h=640):
    im = Image.new("RGB", (w, h))
    d = ImageDraw.Draw(im)
    for y in range(h):
        k = y / h
        d.line((0, y, w, y), fill=(int(28 + 20 * k), int(29 + 18 * k), int(40 + 24 * k)))
    glow = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    ImageDraw.Draw(glow).ellipse((-200, -200, 700, 700), fill=(88, 101, 242, 150))
    ImageDraw.Draw(glow).ellipse((800, 250, 1500, 900), fill=(235, 69, 158, 90))
    glow = glow.filter(ImageFilter.GaussianBlur(120))
    im = Image.alpha_composite(im.convert("RGBA"), glow)
    im.alpha_composite(tile(300), (110, 170))
    d = ImageDraw.Draw(im)
    d.text((470, 270), "3DScord", font=font("Black", 150), fill=(255, 255, 255, 255), anchor="lm")
    d.text((474, 380), "Discord for Nintendo 3DS", font=font("SemiBold", 52), fill=(210, 214, 222, 255), anchor="lm")
    d.text((474, 450), "Chat  •  Voice  •  Camera  •  Screen sharing", font=font("Medium", 34), fill=(160, 165, 176, 255), anchor="lm")
    return im.convert("RGB")

if __name__ == "__main__":
    out = lambda *p: os.path.join(ROOT, *p)
    tile(48, rounded=False).convert("RGB").save(out("resources", "icon.png"))
    banner().save(out("resources", "banner.png"))
    tile(128).save(out("romfs", "discord.png"))
    tile(512).save(out("resources", "logo.png"))
    tile(256).save(out("docs", "logo-256.png"))
    social().save(out("docs", "social-preview.png"))
    print("done")
