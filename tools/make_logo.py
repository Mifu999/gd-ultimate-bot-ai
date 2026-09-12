"""GD Ultimate Bot AI - logo.

One idea: the route the bot found through the level, anchored by checkpoints.

Everything here is syntax, not decoration:

  cyan     ground that is committed - a checkpoint is planted, it will never be
           replayed. This is the colour the HUD uses for certified progress.
  violet   the stretch currently being planned.
  magenta  the frontier - the anchor the solver is working from right now.

The route climbs left to right because that is what progress looks like in a
classic level, and the checkpoint markers are diamonds because that is the shape
Geometry Dash draws for a practice checkpoint. The frontier marker sits proud of
the last one and is the brightest thing in the tile: the eye should land on
"where the bot is", not on where it has been.

    python3 tools/make_logo.py

Regenerating with a changed control point is the point of keeping this file.
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from logokit import Canvas, lerp, proof_sheet  # noqa: E402

# --- the mod's own palette ------------------------------------------------
C_COMMIT = (58, 226, 255)    # cyan    - committed, checkpointed
C_PLAN = (150, 126, 255)     # violet  - being planned
C_FRONTIER = (255, 72, 205)  # magenta - the working anchor

FIELD_TOP = (26, 30, 44)
FIELD_BOTTOM = (11, 13, 20)


def route_color(t):
    """t in 0..1 along the route: committed behind, frontier ahead."""
    if t <= 0.55:
        return lerp(C_COMMIT, C_PLAN, t / 0.55)
    return lerp(C_PLAN, C_FRONTIER, (t - 0.55) / 0.45)


c = Canvas()

# The ring carries the same left-to-right ramp as the route inside it, so the
# frame states the idea even at sizes where the route itself is a smudge.
c.tile(
    ring=[(0.0, C_COMMIT), (0.55, C_PLAN), (1.0, C_FRONTIER)],
    ring_width=30,
    margin=6,
)
c.field(top=FIELD_TOP, bottom=FIELD_BOTTOM)

# Faint grid: a level is a grid of blocks, and it gives the route something to
# sit on without competing with it.
c.grid_texture(step=30, alpha=14)

# The route. Flat, a jump, a drop, then a climb - the shape of a solved
# gameplay section rather than an abstract graph.
route = c.curve(
    [
        (0.00, 0.30),
        (0.20, 0.34),
        (0.38, 0.72),
        (0.56, 0.40),
        (0.76, 0.58),
        (1.00, 0.86),
    ],
    pad=30,
)

x0 = route[0][0]
x1 = route[-1][0]


def stroke_color(x, y):
    t = 0.0 if x1 == x0 else (x - x0) / (x1 - x0)
    return route_color(max(0.0, min(1.0, t)))


c.stroke_path(route, width=21, outline=6, color=stroke_color)


def diamond(cx, cy, radius, color, outline_width=7, core=True):
    """A checkpoint marker.

    Diamond because that is the shape Geometry Dash draws for a practice
    checkpoint - a circle would read as a generic graph point.

    The dark core is what makes it survive 30px. Without it the marker is the
    same value as the route it sits on and the two merge into one smudge; the
    first version of this logo failed exactly that way.
    """
    lay = c.layer()
    from PIL import ImageDraw

    d = ImageDraw.Draw(lay)
    r = c.px(radius)
    o = c.px(outline_width)

    rings = [(r + o, (247, 251, 255)), (r, color)]
    if core:
        rings.append((r * 0.40, FIELD_BOTTOM))

    for rr, col in rings:
        d.polygon(
            [(cx, cy - rr), (cx + rr, cy), (cx, cy + rr), (cx - rr, cy)],
            fill=col + (255,),
        )
    c.composite(lay, clip=True)


# Three planted checkpoints along the committed part of the route, then the
# frontier. Three and not five: at 30px a fourth turns the line to mush, and the
# rule is to simplify rather than add.
for u in (0.38, 0.66):
    idx = int(u * (len(route) - 1))
    px, py = route[idx]
    t = 0.0 if x1 == x0 else (px - x0) / (x1 - x0)
    diamond(px, py, 16.0, route_color(t))

# The frontier: where the solver is working. Biggest, brightest, and it
# terminates the route on the right - "history behind, searching here".
fx, fy = route[-1]
diamond(fx, fy, 22.0, C_FRONTIER, outline_width=8, core=False)

c.save("logo.png", menu_icon="resources/gdubai.png")
proof_sheet("logo.png", "proof.png", corpus_dir="corpus")
print("logo.png, resources/gdubai.png, proof.png")
