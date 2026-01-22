# ESPHome-Zehnder-RF

I'm using an ESP32 board with an nRF905 antenna to control a Zehnder ComfoFan S. Make sure to follow the PIN structure. See my actual `utility-bridge.yaml` file in my [Home Assistant repository]([https://github.com/DevSecNinja/home-assistant-config/blob/main/esphome/zehnder-rf.yaml](https://github.com/DevSecNinja/home-assistant-config/blob/main/esphome/ventilation.yaml)) where I use it as a component.

## Fork notes

- The pairing didn't work for me after trying many forks. Disconnecting the CE (D2) PIN worked for me and suddenly the device started to work!

### Known issues

There are a bunch of known issues either related to the base program, the fork or my config. I'll keep them documented under the issues section.


## Configuration

1. Copy `secrets.yaml.example` to `secrets.yaml` and fill in your values
2. Generate a secure API key: `openssl rand -base64 32`
3. Modify GPIO pins in the configuration as needed for your setup
