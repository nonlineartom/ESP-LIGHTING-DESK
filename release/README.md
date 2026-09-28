# Pre-built image

`meshdeck-p4-4c-merged.bin` is the complete flash image (bootloader + partition
table + app) for the Waveshare ESP32-P4-WIFI6-Touch-LCD-4C, built from this
commit with ESP-IDF v5.5.5.  Flash it at offset 0 with esptool (no ESP-IDF
install needed):

```bash
pip install esptool
esptool.py --chip esp32p4 -p /dev/cu.usbmodem* -b 460800 write_flash 0x0 meshdeck-p4-4c-merged.bin
```

Then open a serial monitor at 115200 baud (`esptool.py` does not provide one;
`python -m serial.tools.miniterm /dev/cu.usbmodem* 115200` works).
