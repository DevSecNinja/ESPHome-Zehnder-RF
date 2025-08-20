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

### `resetToAutoMode()`
Reset the fan to automatic CO2 control mode:

This function is specifically designed for systems with CO2 sensors. CO2 sensors continuously monitor air quality and automatically override manual commands within seconds to maintain proper ventilation. After manual control, call this function to cleanly return control to the CO2 sensor.

**Example usage:**
```yaml
# Manual control for 5 minutes, then return to CO2 automatic control
- service: esphome.zehnder_ventilation_set_voltage
  data:
    voltage: 50
- delay: 5min  
- service: esphome.zehnder_ventilation_reset_to_auto_mode
```

## CO2 Sensor Behavior

If you have a Zehnder CO2 sensor in your system, be aware of the following behavior:

**CO2 Sensor Override**: CO2 sensors are designed to continuously monitor air quality and automatically override manual commands within seconds. This is intentional protocol design for safety reasons - the sensor maintains proper ventilation based on actual air quality readings.

**Workaround**: After manual voltage control, use the `resetToAutoMode()` function to cleanly return control to the CO2 sensor rather than having it forcefully override your commands.

**For sustained manual control**: The CO2 sensor would need to be disconnected or configured to disable its automatic control (if supported by your model).

## Fork notes

- The pairing didn't work for me after trying many forks. Disconnecting the CE (D2) PIN worked for me and suddenly the device started to work!

### Known issues

There are a bunch of known issues either related to the base program, the fork or my config. I'll keep them documented under the issues section.
