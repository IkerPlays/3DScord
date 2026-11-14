"""Renders the 3DScord trailer (16:9) and the Short (9:16) from drawings made in code. Usage: render.py main|short [seconds-limit] | still main|short T out.png"""
import math, os, subprocess, sys
from multiprocessing import Pool
import numpy as np
from PIL import Image, ImageDraw, ImageFilter
import ui_screens as ui
from ui_screens import F, clamp

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "out")
FPS = 30
BLURPLE = (88, 101, 242)

def eo(x): return 1 - (1 - clamp(x)) ** 3
def eio(x):
    x = clamp(x)
    return 3 * x * x - 2 * x ** 3
def back(x, s=1.70158):
    x = clamp(x) - 1
    return x * x * ((s + 1) * x + s) + 1
def seg(t, a, b): return clamp((t - a) / (b - a))

# ------------------------------------------------------------------ canvas helpers
class Canvas:
    def __init__(self, W, H):
        self.W, self.H = W, H
        self.u = min(W, H) / 1080.0
        self.vertical = H > W
        self.img = Image.new("RGBA", (W, H))

def background(W, H, t, tint=0):
    sw, sh = W // 8, H // 8
    yy, xx = np.mgrid[0:sh, 0:sw].astype(np.float32)
    k = (yy / sh)[..., None]
    base = np.array([20, 21, 27], np.float32) * (1 - k) + np.array([38, 40, 50], np.float32) * k
    out = np.broadcast_to(base, (sh, sw, 3)).copy()
    blobs = [((88, 101, 242), 0.42, 0.45, 0.0), ((235, 69, 158), 0.22, 0.30, 2.1), ((35, 165, 90) if tint else (80, 120, 255), 0.18, 0.28, 4.2)]
    for col, amp, sig, ph in blobs:
        cx = (0.5 + 0.36 * math.sin(t * 0.35 + ph)) * sw
        cy = (0.5 + 0.30 * math.cos(t * 0.27 + ph * 1.3)) * sh
        d2 = ((xx - cx) ** 2 + (yy - cy) ** 2) / (2 * (sig * min(sw, sh)) ** 2)
        g = (amp * np.exp(-d2))[..., None]
        out = out * (1 - g) + np.array(col, np.float32) * g
    im = Image.fromarray(np.clip(out, 0, 255).astype(np.uint8)).resize((W, H), Image.BILINEAR)
    return im.convert("RGBA")

def text_layer(s, font, fill, alpha=1.0, stroke=0):
    d0 = ImageDraw.Draw(Image.new("RGBA", (1, 1)))
    l, t, r, b = d0.textbbox((0, 0), s, font=font, stroke_width=stroke)
    pad = 6
    lay = Image.new("RGBA", (r - l + 2 * pad, b - t + 2 * pad), (0, 0, 0, 0))
    ImageDraw.Draw(lay).text((pad - l, pad - t), s, font=font, fill=fill + (255,), stroke_width=stroke, stroke_fill=(0, 0, 0, 255))
    if alpha < 1:
        lay.putalpha(lay.getchannel("A").point(lambda v: int(v * alpha)))
    return lay

def put_text(cv, x, y, s, font, fill=(255, 255, 255), alpha=1.0, anchor="l", dy=0, scale=1.0, shadow=True, stroke=0):
    if alpha <= 0.01:
        return 0, 0
    lay = text_layer(s, font, fill, alpha, stroke)
    if scale != 1.0:
        lay = lay.resize((max(1, int(lay.width * scale)), max(1, int(lay.height * scale))), Image.LANCZOS)
    w, h = lay.size
    if anchor == "m":
        x -= w // 2
    elif anchor == "r":
        x -= w
    y = int(y - h // 2 + dy)
    if shadow:
        sh = Image.new("RGBA", lay.size, (0, 0, 0, 0))
        sh.putalpha(lay.getchannel("A").point(lambda v: int(v * 0.45)))
        sh = sh.filter(ImageFilter.GaussianBlur(6))
        cv.img.alpha_composite(sh, (int(x), y + 6))
    cv.img.alpha_composite(lay, (int(x), y))
    return w, h

def chip(cv, cx, cy, text, font, fill, fg=(255, 255, 255), scale=1.0, alpha=1.0):
    d0 = ImageDraw.Draw(Image.new("RGBA", (1, 1)))
    l, t, r, b = d0.textbbox((0, 0), text, font=font)
    pw, ph = int(34 * cv.u), int(18 * cv.u)
    w, h = r - l + 2 * pw, b - t + 2 * ph
    lay = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(lay)
    d.rounded_rectangle((0, 0, w - 1, h - 1), h // 2, fill=fill + (255,))
    d.text((pw - l, ph - t), text, font=font, fill=fg + (255,))
    if scale != 1.0:
        lay = lay.resize((max(1, int(w * scale)), max(1, int(h * scale))), Image.LANCZOS)
    if alpha < 1:
        lay.putalpha(lay.getchannel("A").point(lambda v: int(v * alpha)))
    cv.img.alpha_composite(lay, (int(cx - lay.width / 2), int(cy - lay.height / 2)))

def flash(cv, a):
    if a > 0.01:
        cv.img.alpha_composite(Image.new("RGBA", cv.img.size, (255, 255, 255, int(255 * clamp(a)))))

def fade(cv, a):
    """a: 0 = black, 1 = visible."""
    if a < 0.999:
        cv.img.alpha_composite(Image.new("RGBA", cv.img.size, (0, 0, 0, int(255 * (1 - clamp(a))))))

# ------------------------------------------------------------------ console
_body_cache = {}

def console_body(s):
    """The empty console, at s pixels per native screen pixel. Returns (image, top screen rect, bottom screen rect)."""
    if s in _body_cache:
        return _body_cache[s]
    shellW = int(400 * s * 1.42)
    topH = int(240 * s * 1.30)
    botH = int(240 * s * 1.22)
    gap = int(16 * s)
    W = shellW + int(160 * s)
    H = topH + gap + botH + int(160 * s)
    im = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    sh = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    sd = ImageDraw.Draw(sh)
    ox = int(80 * s)
    sd.rounded_rectangle((ox, int(90 * s), ox + shellW, int(90 * s) + topH), int(34 * s), fill=(0, 0, 0, 170))
    sd.rounded_rectangle((ox, int(90 * s) + topH + gap, ox + shellW, int(90 * s) + topH + gap + botH), int(34 * s), fill=(0, 0, 0, 170))
    sh = sh.filter(ImageFilter.GaussianBlur(max(2, int(26 * s))))
    im.alpha_composite(sh, (0, int(18 * s)))
    d = ImageDraw.Draw(im)
    top = (ox, int(80 * s), ox + shellW, int(80 * s) + topH)
    bot = (ox, top[3] + gap, ox + shellW, top[3] + gap + botH)
    for box in (top, bot):
        d.rounded_rectangle(box, int(34 * s), fill=(24, 25, 30), outline=(70, 74, 86), width=max(2, int(3 * s)))
        d.rounded_rectangle((box[0] + int(6 * s), box[1] + int(6 * s), box[2] - int(6 * s), box[3] - int(6 * s)), int(28 * s), outline=(38, 40, 48), width=max(1, int(2 * s)))
    d.rounded_rectangle((ox + int(60 * s), top[3] - int(4 * s), ox + shellW - int(60 * s), bot[1] + int(4 * s)), int(6 * s), fill=(50, 53, 62))
    tw, th = int(400 * s), int(240 * s)
    tx = ox + (shellW - tw) // 2
    ty = top[1] + (topH - th) // 2
    bw, bh = int(320 * s), int(240 * s)
    bx = ox + (shellW - bw) // 2
    by = bot[1] + int(30 * s)
    d.rectangle((tx - 6, ty - 6, tx + tw + 6, ty + th + 6), fill=(8, 8, 10))
    d.rectangle((bx - 6, by - 6, bx + bw + 6, by + bh + 6), fill=(8, 8, 10))
    cxl = ox + (bx - ox) // 2 - int(6 * s)
    cyl = by + bh // 2
    d.rounded_rectangle((cxl - int(8 * s), cyl - int(30 * s), cxl + int(8 * s), cyl + int(30 * s)), int(4 * s), fill=(58, 62, 74))
    d.rounded_rectangle((cxl - int(30 * s), cyl - int(8 * s), cxl + int(30 * s), cyl + int(8 * s)), int(4 * s), fill=(58, 62, 74))
    cxr = bx + bw + (ox + shellW - bx - bw) // 2 + int(6 * s)
    for dx, dy, col in [(0, -22, (235, 69, 158)), (22, 0, (250, 168, 26)), (0, 22, (35, 165, 90)), (-22, 0, (88, 101, 242))]:
        d.ellipse((cxr + dx * s - 9 * s, cyl + dy * s - 9 * s, cxr + dx * s + 9 * s, cyl + dy * s + 9 * s), fill=col)
    for k in range(5):
        d.ellipse((ox + int(36 * s), ty + int((90 + k * 12) * s), ox + int(41 * s), ty + int((95 + k * 12) * s)), fill=(10, 10, 12))
        d.ellipse((ox + shellW - int(41 * s), ty + int((90 + k * 12) * s), ox + shellW - int(36 * s), ty + int((95 + k * 12) * s)), fill=(10, 10, 12))
    d.ellipse((ox + shellW // 2 - int(5 * s), top[1] + int(10 * s), ox + shellW // 2 + int(5 * s), top[1] + int(20 * s)), fill=(12, 14, 20), outline=(60, 64, 76))
    res = (im, (tx, ty, tw, th), (bx, by, bw, bh))
    _body_cache[s] = res
    return res

def draw_console(cv, cx, cy, s, top_img, bot_img, rot=0.0, alpha=1.0, glow=0.0):
    body, tr, br = console_body(s)
    comp = body.copy()
    comp.paste(top_img.resize((tr[2], tr[3]), Image.NEAREST), (tr[0], tr[1]))
    comp.paste(bot_img.resize((br[2], br[3]), Image.NEAREST), (br[0], br[1]))
    if rot:
        comp = comp.rotate(rot, Image.BICUBIC, expand=True)
    if alpha < 1:
        comp.putalpha(comp.getchannel("A").point(lambda v: int(v * alpha)))
    if glow > 0:
        pad = int(130 * s)
        g = Image.new("RGBA", (comp.width + 2 * pad, comp.height + 2 * pad), (0, 0, 0, 0))
        ImageDraw.Draw(g).rounded_rectangle((pad + comp.width * 0.1, pad + comp.height * 0.06, pad + comp.width * 0.9, pad + comp.height * 0.94), int(60 * s), fill=BLURPLE + (int(120 * glow),))
        g = g.filter(ImageFilter.GaussianBlur(int(60 * s)))
        cv.img.alpha_composite(g, (int(cx - comp.width / 2) - pad, int(cy - comp.height / 2) - pad))
    cv.img.alpha_composite(comp, (int(cx - comp.width / 2), int(cy - comp.height / 2)))
    return comp.size

def console_scale(cv, frac=0.88):
    """Pixels per native screen pixel so that the console (568 x 621 native units) fills `frac` of the width (vertical) or of the height."""
    if cv.vertical:
        return round(cv.W * frac / 568.0, 3)
    return round(cv.H * frac / 621.0, 3)

# ------------------------------------------------------------------ captions
def caption(cv, tag, head, sub, t, a0=0.15, color=BLURPLE, x_frac=0.07, y_frac=0.42):
    u = cv.u
    k1, k2, k3 = eo(seg(t, a0, a0 + 0.5)), eo(seg(t, a0 + 0.12, a0 + 0.62)), eo(seg(t, a0 + 0.3, a0 + 0.9))
    if cv.vertical:
        cx, y0 = cv.W // 2, int(cv.H * 0.745)
        chip(cv, cx, y0 + (1 - k1) * 30 * u, tag, F("ExtraBold", int(30 * u)), color, alpha=k1)
        put_text(cv, cx, y0 + 110 * u, head, F("ExtraBold", int(84 * u)), alpha=k2, anchor="m", dy=(1 - k2) * 30 * u)
        put_text(cv, cx, y0 + 200 * u, sub, F("Medium", int(40 * u)), fill=(200, 204, 212), alpha=k3, anchor="m")
    else:
        x, y0 = int(cv.W * x_frac), int(cv.H * y_frac)
        chip(cv, x + 100 * u + (1 - k1) * -40 * u, y0 - 130 * u, tag, F("ExtraBold", int(28 * u)), color, alpha=k1)
        for i, line in enumerate(head.split("\n")):
            put_text(cv, x + (1 - k2) * -60 * u, y0 + i * 100 * u, line, F("ExtraBold", int(88 * u)), alpha=k2)
        n = head.count("\n") + 1
        for i, line in enumerate(sub.split("\n")):
            put_text(cv, x + (1 - k3) * -60 * u, y0 + n * 100 * u + 12 * u + i * 52 * u, line, F("Medium", int(38 * u)), fill=(200, 204, 212), alpha=k3)

def console_pos(cv, t, dur):
    k = eo(seg(t, 0.0, 0.7))
    if cv.vertical:
        cx, cy = cv.W / 2, cv.H * 0.38
        cy += (1 - k) * 600 * cv.u
    else:
        cx, cy = cv.W * 0.70, cv.H * 0.50
        cx += (1 - k) * 700 * cv.u
    cy += math.sin(t * 1.6) * 8 * cv.u
    rot = (1 - k) * 8 + math.sin(t * 0.9) * 0.6
    return cx, cy, rot

# ------------------------------------------------------------------ scenes (t is local seconds, dur the scene length)
def scene_hook(cv, t, dur):
    u = cv.u
    cv.img.alpha_composite(Image.new("RGBA", cv.img.size, (10, 10, 14, 255)))
    rng = np.random.default_rng(3)
    d = ImageDraw.Draw(cv.img)
    for i in range(60):
        x = (rng.random() * cv.W + t * 20 * (0.5 + rng.random())) % cv.W
        y = (rng.random() * cv.H - t * 14 * (0.5 + rng.random())) % cv.H
        r = (1 + rng.random() * 2.5) * u
        d.ellipse((x - r, y - r, x + r, y + r), fill=(88, 101, 242, int(60 + 90 * rng.random())))
    cx = cv.W // 2
    a1 = eo(seg(t, 0.3, 0.9)) * (1 - seg(t, dur * 0.40, dur * 0.46))
    a2 = eo(seg(t, dur * 0.50, dur * 0.58))
    pop = 0.9 + 0.1 * back(seg(t, dur * 0.52, dur * 0.62))
    if cv.vertical:
        cy = cv.H * 0.45
        put_text(cv, cx, cy - 70 * u, "Your friends", F("ExtraBold", int(120 * u)), alpha=a1, anchor="m", dy=(1 - a1) * 30)
        put_text(cv, cx, cy + 70 * u, "are on Discord.", F("ExtraBold", int(120 * u)), alpha=a1, anchor="m", dy=(1 - a1) * 30)
        put_text(cv, cx, cy - 170 * u, "You're holding", F("ExtraBold", int(110 * u)), alpha=a2, anchor="m")
        put_text(cv, cx, cy - 30 * u, "a Nintendo", F("ExtraBold", int(130 * u)), alpha=a2, anchor="m")
        put_text(cv, cx, cy + 150 * u, "3DS.", F("Black", int(210 * u)), fill=(235, 69, 158), alpha=a2, anchor="m", scale=pop)
    else:
        f1 = F("ExtraBold", int(104 * u))
        f2 = F("Black", int(112 * u))
        d0 = ImageDraw.Draw(Image.new("RGBA", (1, 1)))
        w1 = d0.textlength("You're holding a ", font=f1)
        w2 = d0.textlength("Nintendo 3DS.", font=f2)
        x0 = cx - (w1 + w2) / 2
        put_text(cv, cx, cv.H * 0.5, "Your friends are on Discord.", f1, alpha=a1, anchor="m", dy=(1 - a1) * 30)
        put_text(cv, x0, cv.H * 0.5, "You're holding a", f1, alpha=a2)
        put_text(cv, x0 + w1, cv.H * 0.5, "Nintendo 3DS.", f2, fill=(235, 69, 158), alpha=a2, scale=pop)

_logo = {}
def get_logo(size):
    """The trailer's own logo (a chat bubble with 3D on a blurple tile): nothing of Discord's or Nintendo's."""
    if "big" not in _logo:
        S = 512
        im = Image.new("RGBA", (S, S), (0, 0, 0, 0))
        grad = Image.new("RGBA", (S, S))
        gd = ImageDraw.Draw(grad)
        for y in range(S):
            k = y / S
            gd.line((0, y, S, y), fill=(int(88 + 60 * k), int(101 - 20 * k), int(242 - 10 * k), 255))
        mask = Image.new("L", (S, S), 0)
        ImageDraw.Draw(mask).rounded_rectangle((0, 0, S - 1, S - 1), 110, fill=255)
        im.paste(grad, (0, 0), mask)
        d = ImageDraw.Draw(im)
        d.rounded_rectangle((86, 100, 426, 360), 70, fill=(255, 255, 255, 255))
        d.polygon([(150, 340), (130, 428), (236, 350)], fill=(255, 255, 255, 255))
        f = F("Black", 170)
        d.text((256, 232), "3D", font=f, fill=(88, 101, 242, 255), anchor="mm")
        for k in range(3):
            d.ellipse((190 + k * 66, 318 - 6, 190 + k * 66 + 14, 318 + 8), fill=(180, 186, 250, 255))
        _logo["big"] = im
    if size not in _logo:
        _logo[size] = _logo["big"].resize((size, size), Image.LANCZOS)
    return _logo[size]

def scene_logo(cv, t, dur):
    u = cv.u
    cx = cv.W // 2
    cy = cv.H * (0.36 if cv.vertical else 0.40)
    k = back(seg(t, 0.0, 0.55))
    size = max(8, int(300 * u * (0.4 + 0.6 * k)))
    glow = Image.new("RGBA", (size * 3, size * 3), (0, 0, 0, 0))
    ImageDraw.Draw(glow).ellipse((size * 0.6, size * 0.6, size * 2.4, size * 2.4), fill=BLURPLE + (int(110 * clamp(k)),))
    glow = glow.filter(ImageFilter.GaussianBlur(int(60 * u)))
    cv.img.alpha_composite(glow, (int(cx - size * 1.5), int(cy - size * 1.5)))
    cv.img.alpha_composite(get_logo(size), (int(cx - size / 2), int(cy - size / 2)))
    title = "3DScord"
    f = F("Black", int((170 if not cv.vertical else 190) * u))
    d0 = ImageDraw.Draw(Image.new("RGBA", (1, 1)))
    total = d0.textlength(title, font=f)
    x = cx - total / 2
    ty = cy + size * 0.5 + 120 * u
    for i, ch in enumerate(title):
        a = eo(seg(t, 0.35 + i * 0.07, 0.65 + i * 0.07))
        w = d0.textlength(ch, font=f)
        put_text(cv, x, ty, ch, f, fill=(255, 255, 255) if i else (235, 69, 158), alpha=a, dy=(1 - back(seg(t, 0.35 + i * 0.07, 0.75 + i * 0.07))) * -40 * u)
        x += w
    a = eo(seg(t, 1.2, 1.8))
    put_text(cv, cx, ty + 130 * u, "Discord for Nintendo 3DS", F("SemiBold", int(54 * u)), fill=(200, 204, 212), alpha=a, anchor="m")
    flash(cv, 0.9 * (1 - seg(t, 0.0, 0.25)))

def scene_chat(cv, t, dur):
    s = console_scale(cv, 0.84)
    cx, cy, rot = console_pos(cv, t, dur)
    q = seg(t, dur * 0.20, dur * 0.56)
    p = seg(t, 0.4, dur * 0.2) * 0.72
    if q >= 1:
        p = 0.74 + seg(t, dur * 0.57, dur) * 0.26
    draw_console(cv, cx, cy, s, ui.chat_top(min(p, 1.0)), ui.chat_bottom(q), rot, glow=0.5)
    if cv.vertical:
        caption(cv, "CHAT", "Talk to anyone.", "Replies, reactions, @mentions", t)
    else:
        caption(cv, "CHAT", "Talk to\nanyone.", "Replies, reactions, @mentions\nand voice messages", t)

def scene_call(cv, t, dur):
    s = console_scale(cv, 0.84)
    cx, cy, rot = console_pos(cv, t, dur)
    cam = t > dur * 0.30
    kind = 1 if t > dur * 0.62 else 0
    draw_console(cv, cx, cy, s, ui.call_top(t, 1.0 if cam else 0.0, kind), ui.call_bottom(t, cam_on=cam), rot, glow=0.5)
    if cv.vertical:
        caption(cv, "CALLS", "Voice & video", "Encrypted calls, front & back camera", t, color=(35, 165, 90))
    else:
        caption(cv, "CALLS", "Voice & video\nin your hands.", "End-to-end encrypted calls.\nFront or back camera.", t, color=(35, 165, 90))
    if t > dur * 0.62:
        k = back(seg(t, dur * 0.62, dur * 0.70))
        yy = cv.H * (0.665 if cv.vertical else 0.90)
        chip(cv, cx, yy, "OUTER CAMERA", F("ExtraBold", int(26 * cv.u)), (250, 168, 26), fg=(30, 30, 30), scale=max(0.01, k))

def scene_live(cv, t, dur):
    s = console_scale(cv, 0.84)
    cx, cy, rot = console_pos(cv, t, dur)
    home = t > dur * 0.58
    if home:
        top, bot = ui.home_top(t), ui.home_bottom(t)
    else:
        top, bot = ui.game_top(t), ui.live_bottom(t, 0 if t < dur * 0.3 else 2)
    draw_console(cv, cx, cy, s, top, bot, rot, glow=0.6)
    if cv.vertical:
        caption(cv, "GO LIVE", "Share your screen", "Top, bottom or both. Even from other apps.", t, color=(242, 63, 67))
    else:
        caption(cv, "GO LIVE", "Share your\nscreen.", "Top, bottom or both.\nGo Live, even from other apps.", t, color=(242, 63, 67))
    if home:
        k = back(seg(t, dur * 0.58, dur * 0.66))
        yy = cv.H * (0.665 if cv.vertical else 0.90)
        chip(cv, cx, yy, "STILL LIVE FROM THE HOME MENU", F("ExtraBold", int(28 * cv.u)), (242, 63, 67), scale=max(0.01, k))

FEATURES = [("Voice messages", (88, 101, 242)), ("MP3 / FLAC / OGG", (235, 69, 158)), ("Notifications", (250, 168, 26)), ("Custom themes", (26, 188, 156)),
            ("DMs & servers", (88, 101, 242)), ("Camera & screen", (242, 63, 67)), ("Open source", (35, 165, 90)), ("Free forever", (150, 100, 230))]

def scene_features(cv, t, dur):
    u = cv.u
    put_text(cv, cv.W // 2, cv.H * (0.18 if cv.vertical else 0.20), "And so much more.", F("Black", int(96 * u)), alpha=eo(seg(t, 0.1, 0.6)), anchor="m")
    cols = 2 if cv.vertical else 4
    for i, (name, col) in enumerate(FEATURES):
        k = back(seg(t, 0.7 + i * 0.5, 1.1 + i * 0.5))
        r, c = divmod(i, cols)
        if cv.vertical:
            x, y = cv.W * (0.27 + 0.46 * c), cv.H * (0.32 + 0.14 * r)
        else:
            x, y = cv.W * (0.145 + 0.237 * c), cv.H * (0.42 + 0.17 * r)
        chip(cv, x, y, name, F("ExtraBold", int(34 * u)), col, scale=max(0.01, k), alpha=clamp(k * 2))
    put_text(cv, cv.W // 2, cv.H * 0.88, "Video features need a New Nintendo 3DS / 2DS family console.", F("Medium", int(28 * u)), fill=(150, 154, 164), alpha=eo(seg(t, dur * 0.6, dur * 0.8)), anchor="m")

def scene_end(cv, t, dur):
    u = cv.u
    cx = cv.W // 2
    cy = cv.H * (0.34 if cv.vertical else 0.30)
    k = back(seg(t, 0.0, 0.6))
    size = int(200 * u * k)
    if size > 4:
        cv.img.alpha_composite(get_logo(size), (int(cx - size / 2), int(cy - size / 2)))
    put_text(cv, cx, cy + 190 * u, "3DScord", F("Black", int(150 * u)), alpha=eo(seg(t, 0.2, 0.7)), anchor="m")
    put_text(cv, cx, cy + 320 * u, "Free  •  Open source  •  Version 0.17", F("SemiBold", int(46 * u)), fill=(200, 204, 212), alpha=eo(seg(t, 0.5, 1.0)), anchor="m")
    kk = back(seg(t, 0.9, 1.4))
    chip(cv, cx, cy + 480 * u, "github.com/IkerPlays/3DScord", F("ExtraBold", int(50 * u)), BLURPLE, scale=max(0.01, kk), alpha=clamp(kk * 2))
    put_text(cv, cx, cy + 600 * u, "Download the .cia or .3dsx  •  Link in the description", F("Medium", int(34 * u)), fill=(200, 204, 212), alpha=eo(seg(t, 1.4, 1.9)), anchor="m")
    if cv.vertical:
        put_text(cv, cx, cv.H - 150 * u, "Not affiliated with Discord Inc. or Nintendo.", F("Medium", int(26 * u)), fill=(140, 144, 154), alpha=eo(seg(t, 1.6, 2.2)), anchor="m", shadow=False)
        put_text(cv, cx, cv.H - 110 * u, "Fan-made homebrew app for custom-firmware consoles.", F("Medium", int(26 * u)), fill=(140, 144, 154), alpha=eo(seg(t, 1.6, 2.2)), anchor="m", shadow=False)
    else:
        put_text(cv, cx, cv.H - 60 * u, "Not affiliated with Discord Inc. or Nintendo.  Fan-made homebrew app for custom-firmware consoles.", F("Medium", int(22 * u)), fill=(120, 124, 132), alpha=eo(seg(t, 1.6, 2.2)), anchor="m", shadow=False)

MAIN = [("hook", 0, 6, scene_hook, 0), ("logo", 6, 10, scene_logo, 0), ("chat", 10, 18, scene_chat, 0), ("call", 18, 26, scene_call, 1),
        ("live", 26, 34, scene_live, 0), ("features", 34, 42, scene_features, 0), ("end", 42, 48, scene_end, 0)]
SHORT = [("hook", 0, 4, scene_hook, 0), ("logo", 4, 8, scene_logo, 0), ("chat", 8, 13, scene_chat, 0), ("call", 13, 18, scene_call, 1),
         ("live", 18, 23, scene_live, 0), ("end", 23, 28, scene_end, 0)]

def frame(args):
    mode, i = args
    W, H, scenes = (1920, 1080, MAIN) if mode == "main" else (1080, 1920, SHORT)
    t = i / FPS
    chosen = scenes[-1]
    for sc in scenes:
        if sc[1] <= t < sc[2]:
            chosen = sc
            break
    name, a, b, fn, tint = chosen
    cv = Canvas(W, H)
    cv.img = background(W, H, t, tint)
    lt, dur = t - a, b - a
    fn(cv, lt, dur)
    if name not in ("hook", "logo") and lt < 0.12:
        flash(cv, 0.35 * (1 - lt / 0.12))
    total = scenes[-1][2]
    fade(cv, min(1.0, t / 0.4, (total - t) / 1.2))
    return np.asarray(cv.img.convert("RGB")).tobytes()

def render_video(mode, out, audio, limit=None):
    scenes = MAIN if mode == "main" else SHORT
    n = int((limit or scenes[-1][2]) * FPS)
    W, H = (1920, 1080) if mode == "main" else (1080, 1920)
    cmd = ["ffmpeg", "-y", "-v", "error", "-f", "rawvideo", "-pix_fmt", "rgb24", "-s", "%dx%d" % (W, H), "-r", str(FPS), "-i", "-", "-i", audio,
           "-c:v", "libx264", "-preset", "slow", "-crf", "17", "-pix_fmt", "yuv420p", "-profile:v", "high", "-af", "loudnorm=I=-14:TP=-1.5:LRA=9", "-ar", "48000", "-c:a", "aac", "-b:a", "192k",
           "-shortest", "-movflags", "+faststart", out]
    p = subprocess.Popen(cmd, stdin=subprocess.PIPE)
    with Pool(4) as pool:
        for k, raw in enumerate(pool.imap(frame, [(mode, i) for i in range(n)], chunksize=4)):
            p.stdin.write(raw)
            if k % 150 == 0:
                print("frame", k, "/", n, flush=True)
    p.stdin.close()
    p.wait()

if __name__ == "__main__":
    mode = sys.argv[1]
    if mode == "still":
        m, t, out = sys.argv[2], float(sys.argv[3]), sys.argv[4]
        W, H = (1920, 1080) if m == "main" else (1080, 1920)
        Image.frombytes("RGB", (W, H), frame((m, int(t * FPS)))).save(out)
    else:
        audio = os.path.join(OUT, "music_main.wav" if mode == "main" else "music_short.wav")
        render_video(mode, os.path.join(OUT, "3DScord_trailer.mp4" if mode == "main" else "3DScord_short.mp4"), audio, float(sys.argv[2]) if len(sys.argv) > 2 else None)
