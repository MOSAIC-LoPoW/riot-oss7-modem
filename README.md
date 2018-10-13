- Prepare the hardware:
    - flash the modem app from OSS7 on the Murata modem OCTA shield. Use LRWAN1 platform for now, with following build options: `PLATFORM_CONSOLE_BAUDRATE:STRING=9600` and `PLATFORM_CONSOLE_UART:STRING=1`
    - mount the Murata modem shield on P2
    - attach a USB cable to the FTDI connector of the OCTA shield for the serial console
- Firmware:
    - clone IDLAB fork of RIOT (forked until all changes are pushed upstream): `git clone https://github.com/MOSAIC-LoPoW/RIOT`
    - clone this repository: `git clone https://github.com/MOSAIC-LoPoW/riot-oss7-modem`
    - `cd riot-oss7modem/apps/sensor_push/`
    - `make all flash term`
    - after pressing the reset button on the Nucleo board output like below should appear in the terminal: 
```
2018-09-28 11:28:34,366 - INFO # Connect to serial port /dev/ttyUSB0
Welcome to pyterm!
Type '/exit' to exit.
2018-09-28 11:28:45,011 - INFO # found header, payload size = 24
2018-09-28 11:28:45,011 - INFO # payload not complete yet
2018-09-28 11:28:45,012 - INFO # payload not complete yet
2018-09-28 11:28:45,012 - INFO # payload not complete yet
2018-09-28 11:28:45,013 - INFO # payload not complete yet
2018-09-28 11:28:45,013 - INFO # payload not complete yet
2018-09-28 11:28:45,014 - INFO # payload not complete yet
2018-09-28 11:28:45,014 - INFO # payload not complete yet
2018-09-28 11:28:45,015 - INFO # payload not complete yet
2018-09-28 11:28:45,015 - INFO # payload not complete yet
2018-09-28 11:28:45,016 - INFO # payload not complete yet
2018-09-28 11:28:45,016 - INFO # received resp from: 
2018-09-28 11:28:45,260 - INFO # found header, payload size = 2
2018-09-28 11:28:45,261 - INFO # command with tag 0 completed
2018-09-28 11:28:45,262 - INFO # modem command completed (success = 1)
2018-09-28 11:28:54,167 - INFO # slept until 20009162
2018-09-28 11:28:54,168 - INFO # Sending msg with counter 1
> 2018-09-28 11:28:54,183 - INFO #  13 bytes
2018-09-28 11:28:54,456 - INFO # found header, payload size = 24
2018-09-28 11:28:54,457 - INFO # payload not complete yet
2018-09-28 11:28:54,458 - INFO # payload not complete yet
2018-09-28 11:28:54,459 - INFO # payload not complete yet
2018-09-28 11:28:54,459 - INFO # payload not complete yet
2018-09-28 11:28:54,460 - INFO # payload not complete yet
2018-09-28 11:28:54,460 - INFO # payload not complete yet
2018-09-28 11:28:54,461 - INFO # payload not complete yet
2018-09-28 11:28:54,462 - INFO # payload not complete yet
2018-09-28 11:28:54,462 - INFO # payload not complete yet
2018-09-28 11:28:54,463 - INFO # payload not complete yet
2018-09-28 11:28:54,464 - INFO # received resp from: 
2018-09-28 11:28:54,464 - INFO # found header, payload size = 2
2018-09-28 11:28:54,465 - INFO # command with tag 1 completed
2018-09-28 11:28:54,466 - INFO # modem command completed (success = 1)

```