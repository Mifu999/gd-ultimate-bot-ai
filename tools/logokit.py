"""logokit - rendering primitives for Geode mod logos.

Everything renders at SS times the delivery size and reduces with Lanczos at
the end, so edges carry the softness of ink instead of stair-stepping.

The one rule that bites everyone: ImageDraw REPLACES pixels on an RGBA image
instead of blending them. Drawing a semi-transparent line straight onto the
canvas punches a hole through whatever is underneath. Every translucent mark
here is drawn on its own layer and alpha-composited. Use layer() + composite().

Typical use:

    from logokit import *

    c = Canvas()
    c.tile(ring=[(0.0, GREEN), (1.0, RED)], ring_width=26)
    c.field(top=(38, 46, 60), bottom=(18, 22, 30))
    c.grid_texture()
    pts = c.curve([(0.0, 0.6), (0.5, 0.1), (1.0, 0.9)], pad=26)
    c.stroke_path(pts, width=27, outline=8, color=lambda x, y: GREEN)
    c.save("logo.png")
    proof_sheet("logo.png", "proof.png")
"""

import os

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

# --- index defaults, measured over 400 logos from cdn.geode-sdk.org ---------
OUT = 336          # 79% of the index ships 336x336
RADIUS = 61        # median corner radius; p25 54, p75 67
MARGIN = 4         # most tiles are near full-bleed; p75 sits at 7
SS = 8             # supersampling factor

WHITE = (247, 251, 255)
INK = (9, 12, 17)


def lerp(a, b, t):
    return tuple(round(x + (y - x) * t) for x, y in zip(a, b))


def gradient_at(stops, t):
    """stops = [(position 0..1, rgb), ...] sorted by position."""
    if t <= stops[0][0]:
        return stops[0][1]
    for i in range(len(stops) - 1):
        (a, ca), (b, cb) = stops[i], stops[i + 1]
        if a <= t <= b:
            return lerp(ca, cb, (t - a) / (b - a) if b > a else 0)
    return stops[-1][1]


def catmull_rom(points, samples=34):
    """Smooth a control polygon so curve tension stays even along its length."""
    pts = [points[0]] + list(points) + [points[-1]]
    out = []
    for i in range(len(pts) - 3):
        p0, p1, p2, p3 = pts[i], pts[i + 1], pts[i + 2], pts[i + 3]
        for s in range(samples):
            t = s / samples
            t2, t3 = t * t, t * t * t
            x = 0.5 * ((2 * p1[0]) + (-p0[0] + p2[0]) * t
                       + (2 * p0[0] - 5 * p1[0] + 4 * p2[0] - p3[0]) * t2
                       + (-p0[0] + 3 * p1[0] - 3 * p2[0] + p3[0]) * t3)
            y = 0.5 * ((2 * p1[1]) + (-p0[1] + p2[1]) * t
                       + (2 * p0[1] - 5 * p1[1] + 4 * p2[1] - p3[1]) * t2
                       + (-p0[1] + 3 * p1[1] - 3 * p2[1] + p3[1]) * t3)
            out.append((x, y))
    out.append(points[-1])
    return out


class Canvas:
    """A supersampled logo canvas in design units (0..out, default 336)."""

    def __init__(self, out=OUT, ss=SS):
        self.out = out
        self.ss = ss
        self.S = out * ss
        self.img = self.layer()
        self._clip = None       # inner field mask, set by field()
        self.plot = None        # (x0, y0, x1, y1) in supersampled px

    # -- units --------------------------------------------------------------
    def px(self, v):
        """Design units -> supersampled pixels."""
        return v * self.ss

    def w(self, v):
        """Design units -> an int stroke width, which PIL requires."""
        return max(1, int(round(v * self.ss)))

    def layer(self):
        return Image.new("RGBA", (self.S, self.S), (0, 0, 0, 0))

    def composite(self, lay, clip=False):
        """The only sanctioned way to put a translucent layer on the canvas."""
        self.img.alpha_composite(self.mask_to_field(lay) if clip else lay)

    # -- masks --------------------------------------------------------------
    def rounded_mask(self, inset, radius):
        m = Image.new("L", (self.S, self.S), 0)
        ImageDraw.Draw(m).rounded_rectangle(
            [inset, inset, self.S - inset, self.S - inset],
            radius=int(radius), fill=255,
        )
        return m

    def mask_to_field(self, lay):
        """Crop a layer to the inner field, so marks are cut by the tile edge."""
        if self._clip is None:
            return lay
        a = (np.asarray(lay.getchannel("A"), float)
             * np.asarray(self._clip, float) / 255)
        out = lay.copy()
        out.putalpha(Image.fromarray(a.astype("uint8")))
        return out

    def vgradient(self, stops):
        g = Image.new("RGBA", (1, self.S))
        d = ImageDraw.Draw(g)
        for y in range(self.S):
            d.point((0, y), fill=gradient_at(stops, y / (self.S - 1)) + (255,))
        return g.resize((self.S, self.S))

    # -- structure ----------------------------------------------------------
    def tile(self, ring=None, ring_width=26, margin=MARGIN, radius=RADIUS,
             hairline=INK, sheen=True):
        """Outer hairline, then an optional bright ring.

        A distinct bright ring appears on 23% of index logos. It is a strong,
        recognisable look but not mandatory - a plain hairline reads as
        modern and is equally at home in the index.
        """
        self.margin = self.px(margin)
        self.radius = self.px(radius)

        plate = self.layer()
        ImageDraw.Draw(plate).rounded_rectangle(
            [self.margin, self.margin, self.S - self.margin, self.S - self.margin],
            radius=int(self.radius), fill=hairline + (255,),
        )
        self.composite(plate)

        self.ring_width = self.px(ring_width) if ring else self.px(4)
        if ring:
            inset = self.margin + self.px(4)
            lay = self.layer()
            lay.paste(self.vgradient(ring), (0, 0),
                      self.rounded_mask(inset, self.radius - self.px(4)))
            if sheen:
                # a top-to-bottom falloff so the ring reads as moulded, not flat
                f = np.linspace(1.18, 0.74, self.S)[:, None, None]
                arr = np.asarray(lay, dtype=float)
                arr[..., :3] = np.clip(arr[..., :3] * f, 0, 255)
                lay = Image.fromarray(arr.astype("uint8"))
            self.composite(lay)

    def field(self, top=(38, 46, 60), bottom=(18, 22, 30), seat=True):
        """The inner field. Sets the clip used by every later mark."""
        inset = self.margin + self.ring_width
        self.field_inset = inset
        self.field_radius = max(self.px(6), self.radius - self.ring_width)

        if seat:
            s = self.layer()
            ImageDraw.Draw(s).rounded_rectangle(
                [inset - self.px(4), inset - self.px(4),
                 self.S - inset + self.px(4), self.S - inset + self.px(4)],
                radius=int(self.field_radius + self.px(4)), fill=INK + (255,),
            )
            self.composite(s)

        self._clip = self.rounded_mask(inset, self.field_radius)
        lay = self.layer()
        lay.paste(self.vgradient([(0.0, top), (1.0, bottom)]), (0, 0), self._clip)
        self.composite(lay)

        p = inset
        self.plot = (p, p, self.S - p, self.S - p)

    def grid_texture(self, step=26, color=(198, 220, 245), alpha=17):
        """Graph-paper texture. Index tiles often carry halftone, grain or rays;
        pick a texture that means something for the mod rather than noise."""
        lay = self.layer()
        d = ImageDraw.Draw(lay)
        x = self.field_inset + self.px(step)
        while x < self.S - self.field_inset:
            d.line([(x, 0), (x, self.S)], fill=color + (alpha,), width=self.w(1.3))
            x += self.px(step)
        y = self.field_inset + self.px(step)
        while y < self.S - self.field_inset:
            d.line([(0, y), (self.S, y)], fill=color + (alpha,), width=self.w(1.3))
            y += self.px(step)
        self.composite(lay, clip=True)

    # -- glyphs -------------------------------------------------------------
    def box(self, pad=26, top_extra=0):
        """Plot rectangle inside the field, in supersampled px."""
        x0, y0, x1, y1 = self.plot
        p = self.px(pad)
        return x0 + p, y0 + p + self.px(top_extra), x1 - p, y1 - p

    def curve(self, control, pad=26, top_extra=6, samples=34):
        """Map normalised control points (u 0..1 left-right, v 0..1 bottom-top)
        onto the plot box and smooth them."""
        x0, y0, x1, y1 = self.box(pad, top_extra)
        pw, ph = x1 - x0, y1 - y0
        self._curve_box = (x0, y0, x1, y1)
        return catmull_rom([(x0 + u * pw, y1 - v * ph) for u, v in control], samples)

    def value_at(self, y):
        """Height of y in the last curve box, 0 at the bottom, 1 at the top."""
        x0, y0, x1, y1 = self._curve_box
        return max(0.0, min(1.0, (y1 - y) / (y1 - y0)))

    def _stamp(self, path, width, color_fn, target):
        d = ImageDraw.Draw(target)
        r = width / 2
        for i in range(len(path) - 1):
            (ax, ay), (bx, by) = path[i], path[i + 1]
            col = color_fn((ax + bx) / 2, (ay + by) / 2)
            d.line([(ax, ay), (bx, by)], fill=col, width=int(round(width)))
            d.ellipse([bx - r, by - r, bx + r, by + r], fill=col)  # round joins
        ax, ay = path[0]
        d.ellipse([ax - r, ay - r, ax + r, ay + r], fill=color_fn(ax, ay))

    def stroke_path(self, path, width=27, outline=5, color=None,
                    outline_color=WHITE, shadow=True, clip=True):
        """Draw an outlined sticker stroke: shadow, then outline, then fill.

        color is a function (x, y) -> rgb, so the stroke can carry meaning
        along its length. outline=0 skips the cerne.
        """
        wpx = self.px(width)
        opx = self.px(outline)

        def fill_fn(x, y):
            return (color(x, y) if color else WHITE) + (255,)

        if shadow:
            lay = self.layer()
            self._stamp(path, wpx + 2 * opx, lambda x, y: (0, 0, 0, 255), lay)
            lay = lay.filter(ImageFilter.GaussianBlur(self.px(5)))
            lay.putalpha(lay.getchannel("A").point(lambda a: int(a * 0.55)))
            lay = lay.transform((self.S, self.S), Image.AFFINE,
                                (1, 0, 0, 0, 1, -self.px(7)), resample=Image.BILINEAR)
            self.composite(lay, clip=clip)

        if opx > 0:
            lay = self.layer()
            self._stamp(path, wpx + 2 * opx, lambda x, y: outline_color + (255,), lay)
            self.composite(lay, clip=clip)

        lay = self.layer()
        self._stamp(path, wpx, fill_fn, lay)
        self.composite(lay, clip=clip)

    def dot(self, x, y, radius, color, core=WHITE, core_radius=None, clip=True):
        lay = self.layer()
        d = ImageDraw.Draw(lay)
        r = self.px(radius)
        d.ellipse([x - r, y - r, x + r, y + r], fill=color + (255,))
        if core:
            cr = self.px(core_radius if core_radius else radius * 0.42)
            d.ellipse([x - cr, y - cr, x + cr, y + cr], fill=core + (255,))
        self.composite(lay, clip=clip)

    def band(self, path, spread=17, color=None, alpha=52, blur=4, clip=True):
        """A soft envelope around a path - useful for min/max or uncertainty."""
        lay = self.layer()
        d = ImageDraw.Draw(lay)
        s = self.px(spread)
        for i in range(len(path) - 1):
            (ax, ay), (bx, by) = path[i], path[i + 1]
            col = (color(ax, ay) if color else WHITE) + (alpha,)
            d.polygon([(ax, ay - s), (bx, by - s), (bx, by + s), (ax, ay + s)], fill=col)
        self.composite(lay.filter(ImageFilter.GaussianBlur(self.px(blur))), clip=clip)

    # -- output -------------------------------------------------------------
    def image(self):
        return self.img.resize((self.out, self.out), Image.LANCZOS)

    def save(self, path, menu_icon=None):
        """Write logo.png, and optionally the 256px menu button sprite."""
        final = self.image()
        os.makedirs(os.path.dirname(os.path.abspath(path)) or ".", exist_ok=True)
        final.save(path)
        if menu_icon:
            os.makedirs(os.path.dirname(os.path.abspath(menu_icon)) or ".",
                        exist_ok=True)
            self.img.resize((256, 256), Image.LANCZOS).save(menu_icon)
        return final


def proof_sheet(logo_path, out_path, corpus_dir=None, neighbours=10):
    """The verification step: the logo at the sizes GD actually renders it,
    magnified nearest-neighbour, and dropped into a row of real index logos.

    30x30 is what the mod list shows in grid view (ModItem.cpp), so a logo
    that dies at 30px has failed regardless of how it looks at full size.
    """
    import glob
    import random

    mine = Image.open(logo_path).convert("RGBA")
    cell = 150
    rows_h = 250 + (cell if corpus_dir else 0)
    sheet = Image.new("RGB", (6 * cell, rows_h), (30, 34, 42))

    if corpus_dir:
        files = sorted(glob.glob(os.path.join(corpus_dir, "*.png")))
        random.seed(3)
        random.shuffle(files)
        picks = files[:neighbours]
        picks.insert(len(picks) // 2, logo_path)
        for i, f in enumerate(picks[:6]):
            im = Image.open(f).convert("RGBA").resize((cell - 16, cell - 16),
                                                      Image.LANCZOS)
            sheet.paste(im, (i * cell + 8, 8), im)

    y = cell + 10 if corpus_dir else 10
    for i, sz in enumerate((30, 45, 70)):
        small = mine.resize((sz, sz), Image.LANCZOS).resize((210, 210), Image.NEAREST)
        sheet.paste(small, (i * 220 + 20, y), small)
    sheet.save(out_path)
    return out_path
