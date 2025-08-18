# ESPHome-Zehnder-RF

I'm using an ESP8266 NodeMCU V2 board with an nRF905 antenna to control a Zehnder ComfoFan S. Make sure to follow the PIN structure. See my actual `utility-bridge.yaml` file in my [Home Assistant repository](https://github.com/DevSecNinja/home-assistant-config/blob/main/esphome/zehnder-rf.yaml).

## Fork notes

- The pairing didn't work for me after trying many forks. Disconnecting the CE (D2) PIN worked for me and suddenly the device started to work!

## Status Sensor

This component now includes an optional status sensor that monitors the health of the RF communication with your Zehnder ventilation system. The sensor will:

- Report `true` when communication is working normally
- Report `false` when RF timeouts occur or timer commands fail
- Help detect when ESPHome loses connection to the ventilation system

To use the status sensor, add it to your fan configuration:

```yaml
fan:
  - platform: zehnder
    id: my_ventilation
    name: "Zehnder Ventilation"
    nrf905: nrf905_rf
    status_sensor:
      name: "Ventilation Communication Status"
      icon: mdi:wifi-check
      device_class: connectivity
```

### Known issues

There are a bunch of known issues either related to the base program, the fork or my config. I'll keep them documented under the issues section.
