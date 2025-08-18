# ESPHome Zehnder ComfoFan RF Control

ALWAYS follow these instructions first and fallback to additional search and context gathering only if the information in the instructions is incomplete or found to be in error.

This repository contains ESPHome custom components for controlling Zehnder ComfoFan S ventilation units via RF communication using an ESP32/ESP8266 microcontroller with an nRF905 radio module.

## Working Effectively

### Prerequisites and Setup
- Install ESPHome: `pip3 install esphome`
- Verify installation: `esphome version` (should show version 2025.x.x or later)
- Python 3.8+ is required for ESPHome

### Repository Structure
- `components/nrf905/` - nRF905 radio communication component (Python config + C++ implementation)  
- `components/zehnder/` - Zehnder fan control component (Python config + C++ implementation)
- `utility-bridge.yaml` - Example ESPHome configuration file
- `README.md` - Hardware setup instructions and known issues

### Key Files Explained
**Python Configuration Files** (ESPHome component definitions):
- `components/nrf905/__init__.py` - nRF905 component setup and GPIO configuration
- `components/zehnder/__init__.py` - Zehnder component basic setup  
- `components/zehnder/fan.py` - Fan platform implementation and ESPHome integration

**C++ Implementation Files** (actual hardware control):
- `components/nrf905/nRF905.h/.cpp` - Low-level nRF905 radio communication
- `components/zehnder/zehnder.h/.cpp` - Zehnder protocol implementation and fan control logic

**Configuration File**:
- `utility-bridge.yaml` - Complete working example with Home Assistant integration, web interface, and OTA updates

### Configuration Validation - ALWAYS RUN THIS FIRST
1. Create a `secrets.yaml` file with required secrets:
```yaml
# Generate a secure base64 API key: openssl rand -base64 32
zehnder_comfofan_api_key: "YOUR_BASE64_API_KEY_HERE"
zehnder_comfofan_ota_password: "your_ota_password"
zehnder_comfofan_wifi_ssid: "your_wifi_ssid"
zehnder_comfofan_wifi_password: "your_wifi_password"  
zehnder_comfofan_ap_password: "your_ap_password"
zehnder_comfofan_web_password: "your_web_password"
```

2. Validate configuration: `esphome config utility-bridge.yaml` -- takes <1 second. This validates syntax and schema.

3. Validate Python component syntax:
```bash
python3 -m py_compile components/zehnder/fan.py
python3 -m py_compile components/zehnder/__init__.py  
python3 -m py_compile components/nrf905/__init__.py
```
Each command takes <1 second.

### Common Validation Errors
- **Missing secrets.yaml**: "Error reading file secrets.yaml: No such file or directory" - Create the secrets file first
- **Invalid API key format**: "Invalid key format, please check it's using base64" - Generate key with `openssl rand -base64 32`  
- **Python syntax errors**: Shows line number and syntax issue - Fix the Python code syntax
- **GPIO warnings**: Strapping pin warnings are expected and can be ignored

### Build Process
**NEVER CANCEL BUILDS - Set timeout to 60+ minutes**

- Full compilation: `esphome compile utility-bridge.yaml` 
  - **NEVER CANCEL: Takes 10-20 minutes on first build due to PlatformIO dependencies**
  - Subsequent builds: 2-5 minutes
  - Requires internet connectivity to download ESP32 toolchain and Arduino framework
  - If build fails with network errors to `api.registry.platformio.org`, wait and retry

### Component Development and Testing

#### When modifying Python components (`*.py` files):
- Always validate syntax: `python3 -m py_compile <filename>`
- Run config validation after changes: `esphome config utility-bridge.yaml`
- The deprecated `fan.FAN_SCHEMA` warning can be ignored (upstream ESPHome API change)

#### When modifying C++ components (`*.cpp`, `*.h` files):
- Changes require full compilation to validate
- Use `esphome compile utility-bridge.yaml` to test C++ changes
- **NEVER CANCEL: C++ compilation takes 10-20 minutes - wait for completion**

#### Creating new configurations:
- Copy `utility-bridge.yaml` as a template
- Modify GPIO pin mappings for your hardware setup
- Update device names and IDs in substitutions section
- Always validate with `esphome config <your-config>.yaml` before compiling

## Hardware Limitations and Testing

**CRITICAL**: This is a hardware-specific project. Full functionality testing requires:
- ESP32 or ESP8266 microcontroller  
- nRF905 radio module connected via SPI
- Actual Zehnder ComfoFan S ventilation unit for RF communication

### What you CAN validate without hardware:
- Configuration syntax and schema validation
- Python component syntax
- C++ compilation (if PlatformIO dependencies are available)

### What you CANNOT validate without hardware:
- Actual RF communication with Zehnder fans
- Hardware GPIO functionality
- Real-world pairing and control operations

### Manual Testing Scenarios (requires hardware):
If you have the hardware setup:
1. Flash firmware: `esphome run utility-bridge.yaml`
2. Monitor logs: `esphome logs utility-bridge.yaml`  
3. Test fan control via Home Assistant integration
4. Verify RF communication in device logs

### Complete Development Workflow Example:
**Scenario**: Adding a new fan speed preset

1. **Preparation**:
   ```bash
   # Ensure secrets.yaml exists and is valid
   esphome config utility-bridge.yaml
   ```

2. **Modify C++ code** (components/zehnder/zehnder.h):
   ```cpp
   // Add new speed constant
   FAN_SPEED_TURBO = 0x05,  // Turbo: 120% or 12.0 volt
   ```

3. **Validate changes**:
   ```bash
   # Test Python syntax
   python3 -m py_compile components/zehnder/*.py components/nrf905/*.py
   
   # Test configuration validity  
   esphome config utility-bridge.yaml
   
   # NEVER CANCEL: Test C++ compilation - takes 10-20 minutes
   esphome compile utility-bridge.yaml
   ```

4. **With hardware** - Flash and test:
   ```bash
   esphome run utility-bridge.yaml
   esphome logs utility-bridge.yaml
   ```

## Known Issues and Workarounds

- **Pairing issues**: If pairing fails, disconnect the CE pin (D2) - this resolves pairing problems in many cases
- **GPIO warnings**: GPIO12 and GPIO15 strapping pin warnings are expected and can be ignored
- **Schema deprecation**: `fan.FAN_SCHEMA` deprecation warning is expected due to ESPHome API changes
- **Network failures**: PlatformIO registry connectivity issues may cause build failures - retry builds if network errors occur

### Compilation Troubleshooting
**PlatformIO Download Failures**:
```
ERROR: HTTPClientError (Failed to resolve 'api.registry.platformio.org')
```
- **Solution**: Wait and retry - this is a temporary network connectivity issue
- **Alternative**: Check internet connectivity and DNS resolution
- **Never cancel**: Even with network errors, wait for ESPHome to retry

**Build Directory Issues**:
- If builds fail with permission errors, run: `esphome clean utility-bridge.yaml`
- Remove `.esphome/build/` directory if persistent issues occur

## Common Tasks

### Adding new fan speed modes:
1. Modify `components/zehnder/zehnder.h` - add new speed constants to the fan speed enum:
   ```cpp
   enum {
     FAN_SPEED_AUTO = 0x00,    // Off:      0% or  0.0 volt
     FAN_SPEED_LOW = 0x01,     // Low:     30% or  3.0 volt
     FAN_SPEED_MEDIUM = 0x02,  // Medium:  50% or  5.0 volt
     FAN_SPEED_HIGH = 0x03,    // High:    90% or  9.0 volt
     FAN_SPEED_MAX = 0x04,     // Max:    100% or 10.0 volt
     FAN_SPEED_TURBO = 0x05    // New: Turbo mode
   };
   ```
2. Update `components/zehnder/zehnder.cpp` - implement speed logic in `setSpeed()` and `control()` methods
3. Update example configuration `utility-bridge.yaml` - add new buttons/services for the new speed
4. Validate: `esphome config utility-bridge.yaml`
5. Test compilation: `esphome compile utility-bridge.yaml` (NEVER CANCEL - 10-20 minutes)

### Modifying GPIO pin mappings:
1. Update the nRF905 section in YAML configuration
2. Ensure pin compatibility with your ESP32/ESP8266 variant
3. Validate: `esphome config <your-config>.yaml`
4. Note: GPIO12 and GPIO15 strapping pin warnings are normal

### Creating minimal test configurations:
Use local components instead of GitHub external components for faster iteration:

```yaml
external_components:
  - source: components  # Uses local components instead of GitHub
    components: [ nrf905, zehnder ]
```

## Build Times and Expectations

- **Configuration validation**: <1 second (local components) to ~1 second (with GitHub external components)
- **Python syntax validation**: <0.1 seconds per file  
- **First-time compilation**: 10-20 minutes (downloads dependencies)
- **Incremental compilation**: 2-5 minutes
- **Full clean build**: 10-20 minutes

**ALWAYS set timeouts of 60+ minutes for compilation commands. NEVER CANCEL builds.**

## Validation Checklist

Before committing changes:
- [ ] Run `python3 -m py_compile` on all modified `.py` files
- [ ] Run `esphome config utility-bridge.yaml` to validate configuration  
- [ ] Run `esphome compile utility-bridge.yaml` if C++ code was modified (NEVER CANCEL - wait 10-20 minutes)
- [ ] Update documentation if adding new features
- [ ] Test with actual hardware if available

## Quick Reference Commands

```bash
# Install ESPHome
pip3 install esphome

# Generate secure API key  
openssl rand -base64 32

# Validate configuration (always run first)
esphome config utility-bridge.yaml

# Validate Python components
python3 -m py_compile components/zehnder/*.py components/nrf905/*.py

# Compile firmware (NEVER CANCEL - 10-20 minutes)
esphome compile utility-bridge.yaml

# Flash and monitor (requires hardware)
esphome run utility-bridge.yaml
esphome logs utility-bridge.yaml

# Clean build artifacts
esphome clean utility-bridge.yaml
```