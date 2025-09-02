# ESPHome-Zehnder-RF

I'm using an ESP8266 NodeMCU V2 board with an nRF905 antenna to control a Zehnder ComfoFan S. Make sure to follow the PIN structure. See my actual `utility-bridge.yaml` file in my [Home Assistant repository](https://github.com/DevSecNinja/home-assistant-config/blob/main/esphome/zehnder-rf.yaml).

## Fork notes

- The pairing didn't work for me after trying many forks. Disconnecting the CE (D2) PIN worked for me and suddenly the device started to work!

### Known issues

There are a bunch of known issues either related to the base program, the fork or my config. I'll keep them documented under the issues section.

## Testing & CI/CD

This project includes automated tests and CI/CD pipeline to validate:

- ✅ Python component syntax
- ✅ ESPHome configuration validation  
- ✅ C++ compilation (optional, requires hardware-specific dependencies)

### Quick Start Testing

```bash
# Run all tests locally
./run-tests.sh

# Or install ESPHome and run individual tests
pip install esphome
python3 tests/test_python_syntax.py
python3 tests/test_esphome_config.py
```

For detailed testing information, see [tests/README.md](tests/README.md).

### CI/CD Pipeline

GitHub Actions automatically runs tests on:
- All pull requests
- Pushes to main branches
- Optional compilation tests (add `test-compile` label to PRs)

## Configuration

1. Copy `secrets.yaml.example` to `secrets.yaml` and fill in your values
2. Generate a secure API key: `openssl rand -base64 32`
3. Modify GPIO pins in the configuration as needed for your setup

### Connection Status Monitoring

The Zehnder component includes a built-in connection health monitor that tracks communication with the ventilation system. This helps identify when RF communication issues occur.

#### How it works:
- **Healthy**: Shows `ON` when communication is working normally
- **Unhealthy**: Shows `OFF` after 3 consecutive communication failures or when no successful communication occurs for 5x the polling interval

#### Configuration:
Add this to your `binary_sensor` section:

```yaml
binary_sensor:
  - platform: template
    name: "Zehnder ComfoFan Connection Status"
    device_class: connectivity
    entity_category: diagnostic
    icon: mdi:wifi-check
    lambda: 'return id(zehnder_comfofan_ventilation)->connection_healthy_;'
```

#### Home Assistant Automation Example:
```yaml
automation:
  - alias: "Zehnder Connection Alert"
    trigger:
      platform: state
      entity_id: binary_sensor.zehnder_comfofan_connection_status
      to: 'off'
      for: '00:01:00'  # Wait 1 minute to avoid false alarms
    action:
      service: notify.mobile_app_your_device
      data:
        message: "⚠️ Zehnder ventilation system connection lost!"
```

