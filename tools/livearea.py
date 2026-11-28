"""The Vita's LiveArea pictures for Endurance Motor Racing 2.0: icon0.png (128x128, shown in a
circle), the LiveArea background (840x500) and the start gate (280x158). Night at an endurance race:
the car is one of the new Blender-built hypercars (Ardenza 499), rendered on a transparent background.

  python3 tools/livearea.py <dir with la_34.png la_side.png la_icon.png> vita/sce_sys
"""
import sys, numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

ART, OUT = sys.argv[1], sys.argv[2]
FB = '/usr/share/fonts/truetype/google-fonts/ofl/montserrat/Montserrat-Italic[wght].ttf'
def font(size, weight=800):
    f = ImageFont.truetype(FB, size)
    try: f.set_variation_by_axes([weight])
    except Exception: pass
    return f
ACC = (255, 60, 40); GT = (255, 150, 30)

def cutout(name):
    import os
    if os.path.exists(f'{ART}/{name}.png'):          # a Blender render with a transparent background
        im = Image.open(f'{ART}/{name}.png').convert('RGBA')
        return im.crop(im.getbbox())
    a = np.array(Image.open(f'{ART}/{name}.ppm').convert('RGB')).astype(int)
    key = (a[..., 0] > 200) & (a[..., 1] < 90) & (a[..., 2] > 200)
    al = Image.fromarray(np.where(key, 0, 255).astype('uint8')).filter(ImageFilter.MinFilter(3)).filter(ImageFilter.GaussianBlur(0.6))
    rgb = a.copy(); m = (rgb[..., 0] > rgb[..., 1] + 60) & (rgb[..., 2] > rgb[..., 1] + 60)
    rgb[m] = rgb[m][:, [1, 1, 1]]                       # no magenta fringe
    im = Image.fromarray(rgb.astype('uint8')).convert('RGBA'); im.putalpha(al)
    return im.crop(im.getbbox())

def night(w, h, horizon):
    # the sky from deep blue to the last of the sunset on the horizon, the track below
    y = np.linspace(0, 1, h)[:, None]
    hz = horizon / h
    top, mid, glow = np.array([8, 10, 24]), np.array([24, 22, 58]), np.array([150, 60, 40])
    sky = np.where(y < hz, top + (mid - top) * (y / hz) ** 1.3 + (glow - mid) * np.clip((y / hz) ** 6, 0, 1), 0)
    ground = np.where(y >= hz, np.array([16, 16, 22]) * (1 - 0.4 * (y - hz) / (1 - hz)), 0)
    img = np.repeat((sky + ground)[:, None, :], w, 1)
    return Image.fromarray(np.clip(img, 0, 255).astype('uint8')).convert('RGBA')

def streaks(im, y0, y1, n, seed):
    # the long exposure of the cars' lights through a corner at night
    rng = np.random.default_rng(seed); d = ImageDraw.Draw(im, 'RGBA'); w, h = im.size
    for k in range(n):
        y = y0 + (y1 - y0) * k / max(1, n - 1) + rng.uniform(-4, 4)
        col = (255, 60, 40) if k % 3 else (255, 240, 220)
        for g in range(3, 0, -1):
            d.line([(-20, y + 40), (w * 0.55, y + 6), (w + 20, y - 26)], fill=col + (40 * (4 - g),), width=g * 2)
    return im.filter(ImageFilter.GaussianBlur(0.5))

def shadow(car, blur=6):
    s = Image.new('RGBA', car.size, (0, 0, 0, 0)); s.putalpha(car.split()[3].point(lambda v: v * 0.7))
    return s.filter(ImageFilter.GaussianBlur(blur))

def place(dst, car, x, y, wpx):
    k = wpx / car.width; c = car.resize((int(car.width * k), int(car.height * k)), Image.LANCZOS)
    dst.alpha_composite(shadow(c), (x + 6, y + 8)); dst.alpha_composite(c, (x, y))

def text(d, xy, s, f, fill, stroke=0, anchor='la'):
    d.text(xy, s, font=f, fill=fill, stroke_width=stroke, stroke_fill=(0, 0, 0, 200), anchor=anchor)

def save8(im, path):
    # the Vita wants 8-bit palettised PNGs
    im.convert('RGB').quantize(256, method=Image.MEDIANCUT, dither=Image.FLOYDSTEINBERG).save(path, optimize=True)

hyper, hyper_side, hyper34 = cutout('la_icon'), cutout('la_side'), cutout('la_34')   # one of the new hypercars (Blender)

# ---- the LiveArea background, 840x500: the title top left, the cars below it, the right side calm (the start gate)
bg = streaks(night(840, 500, 300), 330, 470, 9, 1)
d = ImageDraw.Draw(bg)
place(bg, hyper34, 40, 222, 560)
text(d, (36, 28), 'ENDURANCE', font(64), (245, 245, 250), 2)
text(d, (40, 100), 'MOTOR RACING', font(38, 700), (210, 214, 230), 2)
text(d, (382, 84), '2.0', font(64), ACC, 2)
d.rectangle([40, 150, 330, 154], fill=ACC); d.rectangle([330, 150, 470, 154], fill=GT)
text(d, (40, 162), 'FIA WEC 2027  ·  HYPERCAR  ·  LMGT3', font(18, 600), (190, 196, 215))
save8(bg, f'{OUT}/livearea/contents/bg.png')

# ---- the start gate, 280x158
st = streaks(night(280, 158, 92), 100, 150, 5, 2)
place(st, hyper_side, 8, 58, 264)
d = ImageDraw.Draw(st)
text(d, (12, 6), 'ENDURANCE', font(30), (245, 245, 250), 2)
text(d, (14, 38), 'MOTOR RACING', font(15, 700), (210, 214, 230), 1)
text(d, (268, 30), '2.0', font(30), ACC, 2, anchor='ra')
save8(st, f'{OUT}/livearea/contents/startup.png')

# ---- the icon, 128x128 (the Vita shows it in a circle: everything within ~58 px of the middle)
ic = night(128, 128, 82)
ImageDraw.Draw(ic).rectangle([0, 82, 128, 84], fill=ACC + (255,))
place(ic, hyper, 14, 30, 100)
d = ImageDraw.Draw(ic)
text(d, (64, 6), 'EMR', font(22), (245, 245, 250), 2, anchor='ma')
text(d, (64, 92), '2.0', font(26), ACC, 2, anchor='ma')
save8(ic, f'{OUT}/icon0.png')
print('ok')
