- Prepare the hardware:
    - For serial terminal: mount a GPIO expander shield on P3 of the OCTA and attach an FTDI cable to GND, RX and TX pins
    - flash the modem app from OSS7 on the Murata modem OCTA shield. Use LRWAN1 platform for now, with following build options: `PLATFORM_CONSOLE_BAUDRATE:STRING=9600` and `PLATFORM_CONSOLE_UART:STRING=1`
`
    - mount the Murata modem shield on P2
- Firmware:
    - clone IDLAB fork of RIOT (forked until all changes are pushed upstream): `git clone https://github.com/MOSAIC-LoPoW/RIOT`
    - clone this repository: `git clone https://github.com/MOSAIC-LoPoW/riot-oss7-modem`
    - `cd riot-oss7modem/apps/test/`
    - `make all flash term`
    - after pressing the reset button on the Nucleo board output like below should appear in the terminal: 
```
2018-09-20 13:05:09,193 - INFO # Connect to serial port /dev/ttyUSB0
Welcome to pyterm!
Type '/exit' to exit.
2018-09-20 13:05:10,196 - INFO # 
2018-09-20 13:05:13,796 - INFO # �main(): This is RIOT! (Version: 2018.10-devel-814-gce0e4-glenn-ThinkPad-X1-Carbon-3rd)
2018-09-20 13:05:13,797 - INFO # Welcome to RIOT!
> 2018-09-20 13:05:13,838 - INFO #  6 bytes
> 2018-09-20 13:05:13,840 - INFO #  found header, payload size = 14
2018-09-20 13:05:13,841 - INFO # payload not complete yet
2018-09-20 13:05:13,842 - INFO # payload not complete yet
2018-09-20 13:05:13,843 - INFO # payload not complete yet
2018-09-20 13:05:13,844 - INFO # payload not complete yet
2018-09-20 13:05:13,845 - INFO # payload not complete yet
2018-09-20 13:05:13,846 - INFO # modem return file data file 0 offset 0 size 8 buffer 0x200013a0command with tag 0 completed
```