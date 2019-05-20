EPICS IOC for TI CC1350 SensorTag via Bluetooth low energy

Currently supported sensors:

- Temperature-humidity
- Temperature-pressure
- Light
- Gyroscope
- Accelerometer
- Magnetometer

R1-0
=================

R1-0-1 5/20/19
-----
- Add preliminary support for gyroscope, accelerometer, magnetometer
	- Note: Default scan period for motion is 1 second, IOC sets it to 100ms. See line 283 in ThingyApp/src/bluetooth.c to configure
- Add OPI screens

R1-0-0 5/14/19
-----
- Supports temperature-humidity, temperature-pressure, and light sensors
- Parse device response according to specific UUID, using TI's specifications
	- http://processors.wiki.ti.com/index.php/CC2650_SensorTag_User's_Guide
