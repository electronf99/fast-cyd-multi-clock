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

    #
    # No rotation
    #
    img.thumbnail((240, 320), Image.LANCZOS)

    #
    # Create 240x320 canvas
    #
    canvas = Image.new("RGB", (240, 320), (0, 0, 0))

    x = (240 - img.width) // 2
    y = (320 - img.height) // 2

    canvas.paste(img, (x, y))

    with open(output_file, "wb") as f:
        for py in range(320):
            for px in range(240):
                r, g, b = canvas.getpixel((px, py))

                rgb565 = (
                    ((r & 0xF8) << 8)
                    | ((g & 0xFC) << 3)
                    | (b >> 3)
                )

                f.write(
                    rgb565.to_bytes(
                        2,
                        "little"))

    print(f" -> {output_file}")

print("Done")
