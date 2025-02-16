import lvgl_esp32

# Adapt these values for your own configuration
spi = lvgl_esp32.SPI(
    2,
    baudrate=80_000_000,
    sck=21,
    mosi=47,
    miso=8,
)
spi.init()

display = lvgl_esp32.Display(
    spi=spi,
    width=240,
    height=240,
    swap_xy=True,
    mirror_x=True,
    mirror_y=True,
    invert=False,
    bgr=True,
    reset=42,
    dc=43,
    cs=44,
    pixel_clock=20_000_000,
)
display.init()
