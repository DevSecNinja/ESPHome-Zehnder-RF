# ESPHome-Zehnder-RF

I'm using an ESP8266 NodeMCU V2 board with an nRF905 antenna to control a Zehnder ComfoFan S. Make sure to follow the PIN structure. See my actual `utility-bridge.yaml` file in my [Home Assistant repository](https://github.com/DevSecNinja/home-assistant-config/blob/main/esphome/zehnder-rf.yaml).

## API Functions

The Zehnder component exposes the following functions for controlling your fan:

### `setSpeed(speed, timer)`
Set fan speed using preset levels (0-4):
- `speed`: 0=Auto, 1=Low, 2=Medium, 3=High, 4=Max
- `timer`: Optional timer in minutes (0 = no timer)

### `setVoltage(voltage, timer)` 
Set fan speed using voltage percentage (0-100%):
- `voltage`: Percentage of maximum fan voltage (0-100%)
- `timer`: **Note: Timer parameter is ignored** - the Zehnder protocol doesn't support timer functionality with voltage commands

This function is useful for CO2 sensors that send voltage-based speed commands rather than preset levels. For timed operation with voltage control, use external automation/timers to call `setVoltage(0)` after the desired duration.

## Fork notes

- The pairing didn't work for me after trying many forks. Disconnecting the CE (D2) PIN worked for me and suddenly the device started to work!

### Known issues

There are a bunch of known issues either related to the base program, the fork or my config. I'll keep them documented under the issues section.
