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

### Available Sensors

The component provides several sensors for monitoring your Zehnder ComfoFan system:

#### Fan Control
- **Fan platform**: Main ventilation control with speed settings (Auto, Low, Medium, High, Max)
- **Timer sensor**: Binary sensor showing if timer mode is active
- **Ventilation percentage**: Current fan speed as percentage (0-100%)
- **Ventilation mode**: Text sensor showing current mode (Auto, Low, Medium, High, Max)

#### RF Health Monitoring (NEW)
- **RF Status sensor**: Binary sensor that monitors RF communication health
  - **ON**: RF communication is working normally
  - **OFF**: RF communication has failed (timeouts, no responses)
  - Triggers on multiple failure conditions:
    - Discovery timeouts during pairing
    - Query timeouts when main unit doesn't respond
    - Speed command timeouts
    - General RF communication failures
    - Airway busy conditions
  - Automatically recovers when RF communication is restored
  
This health sensor helps detect when RF communication breaks, making it easier to troubleshoot connectivity issues that occur "every 6 months or so" as described in the issue.

### Example Configuration

```yaml
binary_sensor:
  - platform: zehnder
    name: "Zehnder RF Status" 
    id: zehnder_rf_status
    zehnder_rf: zehnder_ventilation  # Reference to your fan component
    icon: mdi:wifi-check
    device_class: connectivity
```

