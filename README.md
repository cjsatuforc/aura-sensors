# Aura Sensors

This is a teensy 3.x (teensyduino) sketch.  Aura Sensors turns an
inexpensive teensy board into a sensor collector/aggregater,
communications hub, and servo controller.  It is not a full fledged
autopilot itself, but designed to pair with a host linux board
(raspberry pi, gumstix, beaglebone, etc) for all the higher level AP
functions.  It supports the mpu9250 imu, ublox8 gps, bme280/bmp280
pressure sensors, sbus receiver, and attopilot volt/amp sensor.
Supports an external airdata systems via the i2c bus.

This is one component of a research grade autopilot system that anyone
can assemble with basic soldering skills.

When paired with a beaglebone, or raspberry pi linux computer running
the Aura-core AP software, the result is a very high quality and very
capable autopilot system at a very inexpensive price point.

# Preliminary working features

* MPU9250 via spi or i2c.
* MPU9250 DMP, interrupt generatation, scaling
* Gyro zeroing (calibration) automatically on startup if unit is still enough.
* SBUS input (direct) with support for 16 channels.
* UBLOX8 support
* BME280/BMP280 support
* Eeprom support for saving/loading config as well as assigning a serial #.
* 8 channel PWM output support
* Onboard 3-axis stability (simple dampening) system
* "Smart reciever" capability.  Handles major mixing and modes on the
  airplane side allowing flight with a 'dumb' radio.
* i2c airdata system (BFS, mRobotics, 3dr)
* Full 2-way serial communication with any host computer enabling a 
  "big processor/little processor" architecture.  Hard real time tasks run on
  the "little" processor.  High level functions run on the big processor (like
  EKF, PID's, navigation, logging, communication, mission, etc.)

# Flight Testing

* The first real flight of the aura-sensors firmware running on a
  teensy was performed in late Feb, 2018.  (The APM2 version of this
  firmware has been flying successfully since 2012.)

  
