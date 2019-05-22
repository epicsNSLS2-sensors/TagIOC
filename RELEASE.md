EPICS IOC for TI CC1350 SensorTag via Bluetooth low energy

Currently supported sensors:

- Temperature-humidity
- Temperature-pressure
- Light
- Gyroscope
- Accelerometer
- Magnetometer
- Buttons
- Battery

R1-0
=================

R1-0-2 5/22/19
-----
- Add support for buttons, preliminary support for battery
- Change default motion scan period from 1 second to 100ms
	- See macro MOTION_PERIOD in TagApp/src/bluetooth.c to configure

R1-0-1 5/20/19
-----
- Add preliminary support for gyroscope, accelerometer, magnetometer
- Protect device writes with concurrency locks
- Add OPI screens

R1-0-0 5/14/19
-----
- Supports temperature-humidity, temperature-pressure, and light sensors
- Parse device response according to specific UUID, using TI's specifications
	- http://processors.wiki.ti.com/index.php/CC2650_SensorTag_User's_Guide
