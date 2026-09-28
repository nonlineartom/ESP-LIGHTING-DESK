# Pre-built image

`meshdeck-p4-4c-rev1-merged.bin` is the complete flash image (bootloader +
partition table + app) for the Waveshare ESP32-P4-WIFI6-Touch-LCD-4C with an
**ESP32-P4 rev v1.x** chip (the test board is v1.3; check yours with
`esptool.py --chip esp32p4 chip_id`).  Built with ESP-IDF v5.5.4 by
`tools/make_release.sh`.  A rev v3 board needs its own build (README, "Chip
revision"); this image stops in the bootloader there.

Flash it at offset 0 with esptool (no ESP-IDF install needed):

```bash
pip install esptool
esptool.py --chip esp32p4 -p /dev/cu.usbmodem* -b 460800 write_flash 0x0 meshdeck-p4-4c-rev1-merged.bin
```

The image is contiguous, so it also **erases the NVS partition** (0x11000):
paired lights, scenes and settings are forgotten, and lights must be reset
and paired again.  To update a board without losing them, flash only the app
from a build: `idf.py -p <port> app-flash`.

Then open a serial monitor at 115200 baud (`esptool.py` does not provide one;
`python -m serial.tools.miniterm /dev/cu.usbmodem* 115200` works).
