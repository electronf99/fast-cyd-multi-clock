from PIL import Image

files = [
    "nixie-bg",
    "nixie-C",
    "nixie-0",
    "nixie-1",
    "nixie-2",
    "nixie-3",
    "nixie-4",
    "nixie-5",
    "nixie-6",
    "nixie-7",
    "nixie-8",
    "nixie-9",
]

for name in files:
    input_file = f"{name}.png"
    output_file = f"{name}.raw"

    print(f"Converting {input_file}...")

    img = Image.open(input_file).convert("RGB")

    # Rotate the other direction
    img = img.rotate(-90, expand=True)

    # Preserve aspect ratio
    img.thumbnail((320, 240), Image.LANCZOS)

    # Centre on black background
    canvas = Image.new("RGB", (320, 240), (0, 0, 0))

    x = (320 - img.width) // 2
    y = (240 - img.height) // 2

    canvas.paste(img, (x, y))

    with open(output_file, "wb") as f:
        for py in range(240):
            for px in range(320):
                r, g, b = canvas.getpixel((px, py))

                rgb565 = (
                    ((r & 0xF8) << 8)
                    | ((g & 0xFC) << 3)
                    | (b >> 3)
                )

                # Little-endian RGB565
                # Matches your:
                # tft.setSwapBytes(true)
                f.write(rgb565.to_bytes(2, "little"))

    print(f"  -> {output_file}")

print("Done")
