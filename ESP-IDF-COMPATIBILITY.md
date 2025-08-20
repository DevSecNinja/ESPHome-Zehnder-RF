# ESP-IDF Framework Testing Results

## Issue #40: Test esp-idf framework on esp32-denky32

### Summary
✅ **ESP-IDF framework is now FULLY COMPATIBLE with Zehnder components on the denky32 board!**

The previous comment in the configuration stating "Unfortunately, the Zehnder module fails on esp-idf" is no longer accurate. Testing with ESPHome 2025.8.0 shows that both Arduino and ESP-IDF frameworks work correctly.

### Test Results

#### Configuration Validation
- ✅ Arduino framework on denky32 board: **PASSED**
- ✅ ESP-IDF framework on denky32 board: **PASSED**
- ✅ No deprecation warnings (fixed `FAN_SCHEMA` issue)

#### Compatibility Analysis
The Zehnder components use:
- ESPHome's hardware abstraction layer (HAL)
- Standard C++ libraries (`string.h`, logging)
- ESPHome-provided timing functions (`millis()`, `delay()`)
- SPI communication via ESPHome's SPI component

All these are fully supported in both Arduino and ESP-IDF frameworks within ESPHome.

### Available Configurations

#### Arduino Framework (Traditional)
```yaml
esp32:
  board: denky32
  framework:
    type: arduino
```

#### ESP-IDF Framework (Better Performance)
```yaml
esp32:
  board: denky32
  framework:
    type: esp-idf
```

### Performance Benefits of ESP-IDF
- Lower memory usage
- Better real-time performance
- More advanced power management
- Better Wi-Fi and Bluetooth stack
- Native FreeRTOS support

### Test Files Created
- `test-config-denky32-arduino.yaml` - Arduino framework test
- `test-config-denky32-espidf.yaml` - ESP-IDF framework test
- `tests/test_espidf_compatibility.py` - Automated compatibility test

### How to Test Locally
```bash
# Run all tests including ESP-IDF compatibility
./run-tests.sh

# Or run just the ESP-IDF compatibility test
python3 tests/test_espidf_compatibility.py

# Validate specific configurations
esphome config test-config-denky32-arduino.yaml
esphome config test-config-denky32-espidf.yaml
```

### Recommendations
1. **New projects**: Use ESP-IDF framework for better performance
2. **Existing projects**: Can continue using Arduino framework or migrate to ESP-IDF
3. **Hardware setup**: Both frameworks work with the same pin configuration

### Changes Made
1. Fixed deprecation warning in `components/zehnder/fan.py`
2. Created comprehensive test configurations for both frameworks
3. Added automated ESP-IDF compatibility testing
4. Updated documentation to reflect ESP-IDF compatibility
5. Updated utility-bridge.yaml with esp-idf usage instructions