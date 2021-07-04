# suica-reader-rcs620s
Suica reader test program for RC-S620S (Felica reader/writer)

# How to build and flash to target board

## Mbed CLI 1
```
$ mbed import https://github.com/toyowata/suica-reader-rcs620s
$ cd suica-reader-rcs620s
$ mbed compile -m raspberry_pi_pico -t gcc_arm
```
## Flash the image to Raspberry-PI pico
* Install picotool from here: https://github.com/raspberrypi/picotool
* Set target to UF2 mode

```
$ picotool load ./BUILD/RASPBERRY_PI_PICO/GCC_ARM/suica-reader-rcs620s.bin
```


## Known issues
* Mbed CLI2 build dose not support
