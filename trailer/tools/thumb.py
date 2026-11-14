"""YouTube thumbnail (1280x720) and a vertical cover for the Short, made with the same drawings as the trailer."""
import math, os
from PIL import Image, ImageDraw, ImageFilter
import render as R
import ui_screens as ui
from ui_screens import F

def make(W, H, out, vertical=False):
    cv = R.Canvas(W, H)
    t = 2.2
    # a punchier background: stronger blurple with a pink corner
    bg = Image.new("RGBA", (W, H))
    d = ImageDraw.Draw(bg)
    for y in range(H):
        k = y / H
        d.line((0, y, W, y), fill=(int(58 + 40 * k), int(50 + 20 * k), int(170 - 20 * k), 255))
    glow = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    gd = ImageDraw.Draw(glow)
    gd.ellipse((W * 0.45, -H * 0.2, W * 1.3, H * 0.8), fill=(235, 69, 158, 150))
    gd.ellipse((-W * 0.3, H * 0.5, W * 0.4, H * 1.3), fill=(35, 165, 90, 90))
    glow = glow.filter(ImageFilter.GaussianBlur(int(W * 0.08)))
    bg.alpha_composite(glow)
    cv.img = bg
    u = cv.u
    s = R.console_scale(cv, 0.80 if vertical else 0.92)
    cx, cy = (W * 0.5, H * 0.34) if vertical else (W * 0.76, H * 0.52)
    R.draw_console(cv, cx, cy, s, ui.call_top(t, 1.0, 0), ui.call_bottom(t, True), rot=-5 if not vertical else -3, glow=1.0)
    # LIVE badge on the console corner
    R.chip(cv, cx - (0 if vertical else 20 * u), cy - (H * 0.27 if vertical else H * 0.40), "LIVE", F("Black", int(46 * u)), (242, 63, 67), scale=1.0)
    if vertical:
        R.put_text(cv, W // 2, H * 0.70, "DISCORD", F("Black", int(230 * u)), anchor="m", stroke=int(8 * u))
        R.put_text(cv, W // 2, H * 0.80, "ON A 3DS?!", F("Black", int(190 * u)), fill=(255, 221, 64), anchor="m", stroke=int(8 * u))
        R.chip(cv, W // 2, H * 0.90, "VOICE  •  VIDEO  •  SCREEN SHARE", F("ExtraBold", int(38 * u)), (20, 21, 27))
    else:
        R.put_text(cv, W * 0.04, H * 0.23, "DISCORD", F("Black", int(235 * u)), stroke=int(9 * u))
        R.put_text(cv, W * 0.04, H * 0.47, "ON A", F("Black", int(235 * u)), fill=(255, 255, 255), stroke=int(9 * u))
        R.put_text(cv, W * 0.04, H * 0.71, "3DS?!", F("Black", int(235 * u)), fill=(255, 221, 64), stroke=int(9 * u))
        R.chip(cv, W * 0.22, H * 0.92, "VOICE  •  VIDEO  •  SCREEN SHARE", F("ExtraBold", int(34 * u)), (20, 21, 27))
    img = cv.img.convert("RGB")
    return img

if __name__ == "__main__":
    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "out")
    big = make(1920, 1080, None)
    big.resize((1280, 720), Image.LANCZOS).save(os.path.join(out, "3DScord_thumbnail.png"))
    big.resize((1280, 720), Image.LANCZOS).save(os.path.join(out, "3DScord_thumbnail.jpg"), quality=92)
    make(1080, 1920, None, True).save(os.path.join(out, "3DScord_short_cover.png"))
