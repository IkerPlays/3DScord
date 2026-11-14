"""Drawings of 3DScord-style 3DS screens at the console's native resolution (top 400x240, bottom 320x240).
They are illustrations made for the trailer (invented people and messages), in the look of the app."""
import math
from PIL import Image, ImageDraw, ImageFont

FD = "/usr/share/fonts/opentype/inter/"
_fc = {}

def F(name, size):
    key = (name, size)
    if key not in _fc:
        _fc[key] = ImageFont.truetype(FD + "Inter-%s.otf" % name, size)
    return _fc[key]

BG = (49, 51, 56)
PANEL = (43, 45, 49)
DARK = (30, 31, 34)
TEXT = (242, 243, 245)
MUTED = (181, 186, 193)
BLURPLE = (88, 101, 242)
GREEN = (35, 165, 90)
RED = (242, 63, 67)
PINK = (235, 69, 158)
TEAL = (26, 188, 156)
AMBER = (250, 168, 26)

def clamp(x, a=0.0, b=1.0):
    return max(a, min(b, x))

def avatar(d, cx, cy, r, color, letter, size=None):
    d.ellipse((cx - r, cy - r, cx + r, cy + r), fill=color)
    f = F("Bold", size or int(r * 1.1))
    d.text((cx, cy + 1), letter, font=f, fill=(255, 255, 255), anchor="mm")

def pill(d, box, r, fill, outline=None):
    d.rounded_rectangle(box, r, fill=fill, outline=outline)

# ---------------------------------------------------------------- chat
MESSAGES = [
    ("Lia", PINK, "L", ["anyone up for a call tonight?"], 0.00),
    ("Max", TEAL, "M", ["sure, give me five minutes", "grabbing my 3DS"], 0.16),
    ("Lia", PINK, "L", ["wait... Discord on a 3DS?!"], 0.36),
    ("You", BLURPLE, "Y", ["@Lia yep. join voice, I'll show you"], 0.74),
]

def chat_top(p):
    im = Image.new("RGB", (400, 240), BG)
    d = ImageDraw.Draw(im)
    d.rectangle((0, 0, 400, 22), fill=PANEL)
    d.text((10, 11), "# general", font=F("SemiBold", 12), fill=TEXT, anchor="lm")
    d.text((390, 11), "3 online", font=F("Medium", 10), fill=MUTED, anchor="rm")
    vis = [m for m in MESSAGES if p >= m[4]]
    # heights, bottom aligned
    hs = [14 + 14 * len(m[3]) + 8 for m in vis]
    y = 232 - sum(hs)
    for m, h in zip(vis, hs):
        age = clamp((p - m[4]) / 0.07)
        off = int((1 - age) * 8)
        yy = y + off
        if yy > 22:
            avatar(d, 22, yy + 12, 11, m[1], m[2], 11)
            d.text((42, yy + 2), m[0], font=F("Bold", 12), fill=m[1] if m[0] != "You" else (201, 205, 251), anchor="la")
            for i, line in enumerate(m[3]):
                ty = yy + 17 + i * 14
                if line.startswith("@"):
                    word, rest = line.split(" ", 1)
                    w = d.textlength(word, font=F("Medium", 12))
                    d.rounded_rectangle((41, ty - 1, 43 + w, ty + 14), 3, fill=(60, 68, 160))
                    d.text((42, ty), word, font=F("Medium", 12), fill=(201, 205, 251), anchor="la")
                    d.text((46 + w, ty), rest, font=F("Medium", 12), fill=TEXT, anchor="la")
                else:
                    d.text((42, ty), line, font=F("Medium", 12), fill=TEXT, anchor="la")
        y += h
    # a reaction under the last message of Lia
    if p > 0.55:
        yy = 232 - sum(hs) + hs[0] + hs[1] + hs[2] - 10 if len(hs) > 3 else None
    return im

def chat_bottom(q):
    """q: 0..1 how much of the message is typed."""
    im = Image.new("RGB", (320, 240), BG)
    d = ImageDraw.Draw(im)
    d.rectangle((0, 0, 320, 24), fill=PANEL)
    for k in range(3):
        d.rectangle((10, 8 + k * 4, 24, 9 + k * 4), fill=MUTED)
    d.text((36, 12), "# general", font=F("SemiBold", 12), fill=TEXT, anchor="lm")
    # keyboard-ish keys
    rows = ["qwertyuiop", "asdfghjkl", "zxcvbnm"]
    for r, row in enumerate(rows):
        x0 = 12 + r * 14
        for c, ch in enumerate(row):
            x = x0 + c * 29
            d.rounded_rectangle((x, 112 + r * 30, x + 25, 112 + r * 30 + 25), 4, fill=(66, 69, 77))
            d.text((x + 12, 112 + r * 30 + 13), ch, font=F("Medium", 13), fill=TEXT, anchor="mm")
    d.rounded_rectangle((12, 202, 254, 230), 4, fill=(66, 69, 77))
    d.text((133, 216), "space", font=F("Medium", 11), fill=MUTED, anchor="mm")
    d.rounded_rectangle((260, 202, 308, 230), 4, fill=BLURPLE)
    d.text((284, 216), "Send", font=F("Bold", 11), fill=(255, 255, 255), anchor="mm")
    # input bar
    text = "@Lia yep. join voice, I'll show you"
    n = int(len(text) * clamp(q))
    d.rounded_rectangle((10, 34, 310, 98), 6, fill=DARK)
    shown = text[:n]
    # wrap in two lines
    full1 = "@Lia yep. join voice, I'll"
    if len(shown) <= len(full1):
        l1, l2 = shown, ""
    else:
        l1, l2 = full1, shown[len(full1) + 1:]
    d.text((18, 44), l1, font=F("Medium", 13), fill=TEXT, anchor="la")
    if l2:
        d.text((18, 64), l2, font=F("Medium", 13), fill=TEXT, anchor="la")
    if int(q * 40) % 2 == 0 and q < 1:
        w = d.textlength(l2 if l2 else l1, font=F("Medium", 13))
        d.rectangle((18 + w + 2, (64 if l2 else 44), 19 + w + 2, (64 if l2 else 44) + 15), fill=TEXT)
    if n == 0:
        d.text((18, 44), "Message #general", font=F("Medium", 13), fill=(110, 114, 122), anchor="la")
    return im

# ---------------------------------------------------------------- picture drawings
def scene_picture(w, h, t, kind=0):
    """A tiny landscape that stands for a camera picture (nobody's real picture)."""
    im = Image.new("RGB", (w, h))
    d = ImageDraw.Draw(im)
    top, bot = ((255, 150, 90), (255, 214, 140)) if kind == 0 else ((70, 120, 200), (170, 215, 255))
    for y in range(h):
        k = y / h
        d.line((0, y, w, y), fill=tuple(int(top[i] * (1 - k) + bot[i] * k) for i in range(3)))
    sx = int(w * 0.68 + math.sin(t * 0.8) * 6)
    d.ellipse((sx - 14, h * 0.28 - 14, sx + 14, h * 0.28 + 14), fill=(255, 245, 200))
    for i, (c, amp, base) in enumerate([((70, 60, 110), 14, 0.62), ((45, 40, 85), 18, 0.74)]):
        pts = [(0, h)]
        for x in range(0, w + 8, 8):
            pts.append((x, h * base + math.sin(x * 0.05 + i * 2 + t * 0.4) * amp * 0.5))
        pts.append((w, h))
        d.polygon(pts, fill=c)
    return im

def person_picture(w, h, t):
    im = Image.new("RGB", (w, h), (60, 64, 80))
    d = ImageDraw.Draw(im)
    for y in range(h):
        k = y / h
        d.line((0, y, w, y), fill=(int(70 + 40 * k), int(80 + 30 * k), int(110 + 20 * k)))
    cx = w // 2 + int(math.sin(t * 1.3) * 3)
    d.ellipse((cx - 22, h * 0.22, cx + 22, h * 0.22 + 46), fill=(236, 190, 160))
    d.pieslice((cx - 46, h * 0.62, cx + 46, h * 0.62 + 110), 180, 360, fill=(88, 101, 242))
    d.ellipse((cx - 22, h * 0.22 - 4, cx + 22, h * 0.22 + 20), fill=(60, 40, 30))
    d.ellipse((cx - 9, h * 0.22 + 20, cx - 5, h * 0.22 + 24), fill=(40, 30, 30))
    d.ellipse((cx + 5, h * 0.22 + 20, cx + 9, h * 0.22 + 24), fill=(40, 30, 30))
    return im

# ---------------------------------------------------------------- call
def tile(im, box, label, color=None, picture=None, speaking=0.0):
    d = ImageDraw.Draw(im)
    x0, y0, x1, y1 = box
    d.rounded_rectangle(box, 8, fill=DARK)
    if picture is not None:
        mask = Image.new("L", (x1 - x0, y1 - y0), 0)
        ImageDraw.Draw(mask).rounded_rectangle((0, 0, x1 - x0 - 1, y1 - y0 - 1), 8, fill=255)
        im.paste(picture.resize((x1 - x0, y1 - y0)), (x0, y0), mask)
    else:
        cx, cy = (x0 + x1) // 2, (y0 + y1) // 2 - 6
        avatar(d, cx, cy, 24, color, label[0], 24)
    if speaking > 0:
        a = int(255 * speaking)
        d.rounded_rectangle(box, 8, outline=(35, int(120 + 45 * speaking), 90), width=2)
    pill(d, (x0 + 6, y1 - 22, x0 + 14 + int(d.textlength(label, font=F("SemiBold", 10))), y1 - 6), 4, (0, 0, 0))
    d.text((x0 + 10, y1 - 14), label, font=F("SemiBold", 10), fill=TEXT, anchor="lm")

def call_top(t, you_cam=0.0, you_kind=0):
    im = Image.new("RGB", (400, 240), PANEL)
    pad = 6
    w, h = (400 - 3 * pad) // 2, (240 - 3 * pad) // 2
    boxes = [(pad, pad, pad + w, pad + h), (2 * pad + w, pad, 2 * pad + 2 * w, pad + h),
             (pad, 2 * pad + h, pad + w, 2 * pad + 2 * h), (2 * pad + w, 2 * pad + h, 2 * pad + 2 * w, 2 * pad + 2 * h)]
    tile(im, boxes[0], "Lia", PINK, speaking=0.5 + 0.5 * math.sin(t * 7))
    tile(im, boxes[1], "Max", None, scene_picture(w, h, t, 1))
    tile(im, boxes[2], "Sam", TEAL)
    if you_cam > 0:
        tile(im, boxes[3], "You", None, scene_picture(w, h, t, 0) if you_kind == 1 else person_picture(w, h, t))
    else:
        tile(im, boxes[3], "You", BLURPLE)
    return im

def glyph_mic(d, cx, cy, c):
    d.rounded_rectangle((cx - 4, cy - 9, cx + 4, cy + 3), 4, fill=c)
    d.arc((cx - 8, cy - 5, cx + 8, cy + 8), 20, 160, fill=c, width=2)
    d.line((cx, cy + 8, cx, cy + 12), fill=c, width=2)

def glyph_phones(d, cx, cy, c):
    d.arc((cx - 9, cy - 9, cx + 9, cy + 9), 180, 360, fill=c, width=3)
    d.rounded_rectangle((cx - 11, cy - 1, cx - 5, cy + 8), 2, fill=c)
    d.rounded_rectangle((cx + 5, cy - 1, cx + 11, cy + 8), 2, fill=c)

def glyph_cam(d, cx, cy, c):
    d.rounded_rectangle((cx - 10, cy - 6, cx + 4, cy + 6), 3, fill=c)
    d.polygon([(cx + 6, cy - 3), (cx + 11, cy - 6), (cx + 11, cy + 6), (cx + 6, cy + 3)], fill=c)

def glyph_screen(d, cx, cy, c):
    d.rounded_rectangle((cx - 11, cy - 8, cx + 11, cy + 4), 2, outline=c, width=2)
    d.line((cx - 5, cy + 9, cx + 5, cy + 9), fill=c, width=2)

def glyph_hang(d, cx, cy, c):
    d.arc((cx - 11, cy - 4, cx + 11, cy + 16), 200, 340, fill=c, width=4)
    d.rounded_rectangle((cx - 12, cy + 1, cx - 6, cy + 5), 1, fill=c)
    d.rounded_rectangle((cx + 6, cy + 1, cx + 12, cy + 5), 1, fill=c)

def call_bottom(t, cam_on=False, share_on=False, seconds=252):
    im = Image.new("RGB", (320, 240), BG)
    d = ImageDraw.Draw(im)
    m, s = divmod(int(seconds + t), 60)
    d.text((160, 34), "%02d:%02d" % (m, s), font=F("ExtraBold", 30), fill=TEXT, anchor="mm")
    d.text((160, 64), "Voice connected", font=F("Medium", 12), fill=GREEN, anchor="mm")
    # lock + end to end
    d.rounded_rectangle((100, 86, 112, 96), 2, fill=MUTED)
    d.arc((101, 80, 111, 90), 180, 360, fill=MUTED, width=2)
    d.text((118, 90), "End-to-end encrypted", font=F("Medium", 11), fill=MUTED, anchor="lm")
    buttons = [(glyph_mic, False, False), (glyph_phones, False, False), (glyph_cam, cam_on, False), (glyph_screen, share_on, False), (glyph_hang, False, True)]
    x = 22
    for g, on, red in buttons:
        col = RED if red else (GREEN if on else (66, 69, 77))
        d.rounded_rectangle((x, 150, x + 48, 198), 12, fill=col)
        g(d, x + 24, 174, (255, 255, 255))
        x += 58
    d.text((160, 222), "Camera: inner, outer, top, bottom or both screens", font=F("Medium", 9), fill=(120, 124, 132), anchor="mm")
    return im

# ---------------------------------------------------------------- live (screen share)
def game_top(t, live=True, watching=1):
    im = Image.new("RGB", (400, 240))
    d = ImageDraw.Draw(im)
    for y in range(240):
        k = y / 240
        d.line((0, y, 400, y), fill=(int(88 + 70 * k), int(150 + 60 * k), int(250 - 10 * k)))
    for i in range(4):  # clouds
        cx = (i * 140 - t * 22) % 560 - 80
        cy = 40 + (i * 37) % 60
        for dx, dy, r in [(0, 0, 18), (18, 4, 14), (-18, 6, 13), (6, -8, 12)]:
            d.ellipse((cx + dx - r, cy + dy - r, cx + dx + r, cy + dy + r), fill=(255, 255, 255))
    for i in range(3):  # hills
        base = 170 - i * 14
        pts = [(0, 240)]
        for x in range(0, 416, 16):
            pts.append((x, base + math.sin((x + t * (30 + i * 15)) * 0.025 + i) * (14 - i * 3)))
        pts.append((400, 240))
        d.polygon(pts, fill=(64 + i * 10, 150 + i * 14, 80 + i * 10))
    d.rectangle((0, 196, 400, 240), fill=(120, 80, 50))
    d.rectangle((0, 196, 400, 204), fill=(80, 190, 90))
    for x in range(-int(t * 90) % 32 - 32, 400, 32):
        d.rectangle((x, 212, x + 14, 226), fill=(140, 96, 60))
    # hero
    px = 90
    jump = abs(math.sin(t * 3.2))
    py = 196 - 20 - int(jump * 46)
    d.rectangle((px, py, px + 18, py + 20), fill=(230, 60, 60))
    d.rectangle((px + 2, py - 5, px + 16, py + 3), fill=(240, 80, 80))
    d.rectangle((px + 11, py + 4, px + 15, py + 8), fill=(255, 255, 255))
    d.rectangle((px + 3, py + 20, px + 8, py + 25), fill=(60, 60, 150))
    d.rectangle((px + 11, py + 20, px + 16, py + 25), fill=(60, 60, 150))
    for i in range(5):  # coins
        cx = (i * 90 + 200 - t * 90) % 460 - 30
        cy = 150 + math.sin(t * 4 + i) * 6
        d.ellipse((cx - 6, cy - 8, cx + 6, cy + 8), fill=(255, 205, 50), outline=(200, 150, 20))
    if live:
        pill(d, (10, 10, 60, 28), 4, RED)
        d.ellipse((17, 16, 23, 22), fill=(255, 255, 255))
        d.text((42, 19), "LIVE", font=F("ExtraBold", 10), fill=(255, 255, 255), anchor="mm")
        pill(d, (66, 10, 66 + 70, 28), 4, (0, 0, 0))
        d.text((101, 19), "%d watching" % watching, font=F("SemiBold", 9), fill=(255, 255, 255), anchor="mm")
    return im

def live_bottom(t, mode=2):
    im = Image.new("RGB", (320, 240), BG)
    d = ImageDraw.Draw(im)
    d.rectangle((0, 0, 320, 24), fill=PANEL)
    d.text((160, 12), "Go Live", font=F("SemiBold", 12), fill=TEXT, anchor="mm")
    d.text((160, 48), "Sharing your screen", font=F("ExtraBold", 18), fill=TEXT, anchor="mm")
    d.text((160, 70), "in the voice call", font=F("Medium", 11), fill=MUTED, anchor="mm")
    labels = ["Top", "Bottom", "Both"]
    for i, lab in enumerate(labels):
        x = 22 + i * 96
        on = i == mode
        d.rounded_rectangle((x, 96, x + 84, 150), 10, fill=BLURPLE if on else (66, 69, 77))
        # tiny screens
        if lab == "Top":
            d.rectangle((x + 22, 106, x + 62, 126), fill=(255, 255, 255))
        elif lab == "Bottom":
            d.rectangle((x + 27, 106, x + 57, 126), fill=(255, 255, 255))
        else:
            d.rectangle((x + 22, 104, x + 62, 116), fill=(255, 255, 255))
            d.rectangle((x + 27, 118, x + 57, 130), fill=(255, 255, 255))
        d.text((x + 42, 140), lab, font=F("SemiBold", 11), fill=(255, 255, 255), anchor="mm")
    d.rounded_rectangle((22, 170, 298, 204), 10, fill=RED)
    d.text((160, 187), "Stop sharing", font=F("Bold", 13), fill=(255, 255, 255), anchor="mm")
    d.text((160, 224), "Works from the HOME Menu and other apps too", font=F("Medium", 9), fill=(120, 124, 132), anchor="mm")
    return im

def home_top(t):
    im = Image.new("RGB", (400, 240))
    d = ImageDraw.Draw(im)
    for y in range(240):
        k = y / 240
        d.line((0, y, 400, y), fill=(int(36 + 16 * k), int(40 + 14 * k), int(52 + 10 * k)))
    d.text((200, 12), "9:41", font=F("SemiBold", 12), fill=(210, 214, 222), anchor="mm")
    cols = [(88, 101, 242), (235, 69, 158), (35, 165, 90), (250, 168, 26), (26, 188, 156), (150, 100, 230), (80, 160, 230), (230, 90, 70), (110, 190, 120), (200, 120, 200)]
    sel = int(t * 1.4) % 5
    for r in range(2):
        for c in range(5):
            i = r * 5 + c
            x = 28 + c * 70
            y = 58 + r * 80
            on = (c == sel and r == 0)
            d.rounded_rectangle((x - (4 if on else 0), y - (4 if on else 0), x + 56 + (4 if on else 0), y + 56 + (4 if on else 0)), 10,
                                fill=cols[i], outline=(255, 255, 255) if on else None, width=2)
    return im

def home_bottom(t):
    im = Image.new("RGB", (320, 240))
    d = ImageDraw.Draw(im)
    for y in range(240):
        k = y / 240
        d.line((0, y, 320, y), fill=(int(40 + 10 * k), int(44 + 10 * k), int(58 + 8 * k)))
    d.rounded_rectangle((20, 30, 300, 130), 14, fill=(58, 62, 78))
    d.text((160, 80), "HOME Menu", font=F("Bold", 20), fill=(210, 214, 222), anchor="mm")
    for i in range(4):
        d.ellipse((60 + i * 62, 170, 100 + i * 62, 210), fill=(70, 74, 92))
    return im
