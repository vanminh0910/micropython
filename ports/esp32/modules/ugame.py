"""
A helper module that initializes the display and buttons for the uGame
game console. See https://hackaday.io/project/27629-game
"""

from machine import SPI, I2C, Pin
import ili9341


K_X = 0x20
K_DOWN = 0x02
K_LEFT = 0x04
K_RIGHT = 0x08
K_UP = 0x01
K_O = 0x10


class Audio:
    def __init__(self):
        pass

    def play(self, audio_file):
        pass

    def stop(self):
        pass

    def mute(self, value=True):
        pass


class Buttons:
    def __init__(self, i2c, address=0x08):
        self._i2c = i2c
        self._address = address

    def get_pressed(self):
        return self._i2c.readfrom(self._address, 1)[0] ^ 0xff

class Engine:
    def __init__(self, spi=None, dc=13, cs=12, rst=22, sck=14, mosi=4, miso=19):
        if spi is None:
            self.set_spi()
        else:
            self.spi = spi
        self.audio = Audio()
        self.i2c = I2C(sda=Pin(21), scl=Pin(22))
        self.buttons = Buttons(self.i2c)
        self.set_pins(dc, cs, rst, sck, mosi, miso)
    
    def set_display_size(self, width, height):
        self.display.update_screen_size(width, height)
    
    def set_pins(self, dc, cs, rst, sck, mosi, miso):
        self.dc = dc
        self.cs = cs
        self.rst = rst
        self.sck = sck
        self.mosi = mosi
        self.miso = miso
        self.display = ili9341.Display(self.spi, Pin(self.dc, Pin.OUT), Pin(self.cs, Pin.OUT), Pin(self.rst, Pin.OUT))
    
    def set_spi(self, spi_interface=2, sck=14, mosi=4, miso=19, baudrate=20000000):
        self.spi = SPI(spi_interface, sck=Pin(sck), mosi=Pin(mosi), miso=Pin(miso), baudrate=baudrate)

    def set_backlight(self, pin):
        self.backlight = Pin(32, Pin.OUT)
        self.backlight(1)

engine = Engine()