from PIL import Image, ImageDraw, ImageFilter

WIDTH = 240
HEIGHT = 320

BG = (0, 0, 0)

ON = (255, 0, 0)
OFF = (25, 0, 0)

DIGITS = {
    "0": "abcedf",
    "1": "bc",
    "2": "abged",
    "3": "abgcd",
    "4": "fgbc",
    "5": "afgcd",
    "6": "afgcde",
    "7": "abc",
    "8": "abcdefg",
    "9": "abcfgd",
    "C": "afed",
}

# geometry
SEG_W = 24            # segment thickness
H_LEN = 95            # horizontal length
V_LEN = 88            # vertical length

GAP = 8               # important!

LEFT = 60
TOP = 25

RIGHT_LEAN = 0.08


def hseg(x, y):
    c = 10

    return [
        (x + c, y),
        (x + H_LEN - c, y),
        (x + H_LEN, y + SEG_W // 2),
        (x + H_LEN - c, y + SEG_W),
        (x + c, y + SEG_W),
        (x, y + SEG_W // 2),
    ]


def vseg(x, y):
    c = 10

    return [
        (x + SEG_W // 2, y),
        (x + SEG_W, y + c),
        (x + SEG_W, y + V_LEN - c),
        (x + SEG_W // 2, y + V_LEN),
        (x, y + V_LEN - c),
        (x, y + c),
    ]


segments = {}

#
# Horizontal segments
#
segments["a"] = hseg(
    LEFT + GAP,
    TOP
)

segments["g"] = hseg(
    LEFT + GAP,
    TOP + V_LEN + GAP + 5
)

segments["d"] = hseg(
    LEFT + GAP,
    TOP + 2 * (V_LEN + GAP) + 10
)

#
# Vertical segments
#
segments["f"] = vseg(
    LEFT,
    TOP + GAP
)

segments["b"] = vseg(
    LEFT + H_LEN + GAP,
    TOP + GAP
)

segments["e"] = vseg(
    LEFT,
    TOP + V_LEN + GAP + 10
)

segments["c"] = vseg(
    LEFT + H_LEN + GAP,
    TOP + V_LEN + GAP + 10
)


for digit, active in DIGITS.items():

    img = Image.new(
        "RGB",
        (WIDTH, HEIGHT),
        BG)

    draw = ImageDraw.Draw(img)

    #
    # draw dim segments first
    #
    for pts in segments.values():
        draw.polygon(
            pts,
            fill=OFF)

    #
    # glow layer
    #
    glow = Image.new(
        "RGB",
        (WIDTH, HEIGHT),
        BG)

    gd = ImageDraw.Draw(glow)

    for seg in active:
        gd.polygon(
            segments[seg],
            fill=ON)

    glow = glow.filter(
        ImageFilter.GaussianBlur(10))

    img = Image.blend(
        img,
        glow,
        0.75)

    #
    # active segments
    #
    draw = ImageDraw.Draw(img)

    for seg in active:
        draw.polygon(
            segments[seg],
            fill=ON)

    #
    # slight alarm-clock lean
    #
    img = img.transform(
        (WIDTH, HEIGHT),
        Image.AFFINE,
        (
            1,
            RIGHT_LEAN,
            -10,
            0,
            1,
            0
        ),
        Image.BICUBIC)

    outfile = f"led7seg-{digit}.png"

    img.save(outfile)

    print(outfile)

print("Done")
