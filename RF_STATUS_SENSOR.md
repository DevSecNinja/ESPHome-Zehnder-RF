# RF Status Sensor Implementation Details

This document provides technical details about the RF Status sensor implementation.

## Overview

The RF Status sensor is a binary sensor that monitors the health of RF communication between the ESP32/ESP8266 and the Zehnder ComfoFan unit. It provides real-time feedback on connectivity status.

## How It Works

### Health Tracking Logic

The sensor tracks RF communication success/failure using these variables:
- `rf_healthy`: Current health status (true/false)
- `lastSuccessfulRfTime_`: Timestamp of last successful RF communication  
- `rfFailureCount_`: Count of consecutive RF failures

### Success Conditions

RF communication is marked as successful when:
1. **Query responses**: Main unit responds to device queries with fan settings
2. **Speed change responses**: Main unit acknowledges speed/timer changes
3. **Discovery completion**: Successful pairing/joining with main unit

### Failure Conditions  

RF communication is marked as failed when:
1. **Discovery timeout**: No response during pairing process
2. **Query timeout**: Main unit doesn't respond to device queries
3. **Speed command timeout**: No acknowledgment of speed/timer changes
4. **General RF timeout**: All retry attempts exhausted
5. **Airway busy timeout**: RF channel unavailable for transmission

### Health Status Logic

The sensor considers RF **unhealthy** when:
- 3+ consecutive failures occur, OR
- No successful communication for 5+ minutes

The sensor considers RF **healthy** when:
- Any successful communication occurs (resets failure counter)

## Log Messages

### Success Messages
```
[I][zehnder:xxx] RF communication restored - status sensor healthy
```

### Failure Messages
```
[W][zehnder:xxx] RF communication failed (3 failures, last success 125000 ms ago) - status sensor unhealthy
[W][zehnder:xxx] Query timeout - main unit did not respond after 10 retries
[W][zehnder:xxx] Set speed command timeout - no response received after 10 retries
[W][zehnder:xxx] Discovery timeout - no response from main unit during pairing process
[W][zehnder:xxx] All retry attempts exhausted - no response received from fan unit
[W][zehnder:xxx] Airway busy timeout after 5 seconds - aborting transmission
```

## Home Assistant Integration

The sensor appears in Home Assistant as:
- **Entity Type**: Binary Sensor
- **Device Class**: Connectivity  
- **Icon**: mdi:wifi-check
- **State**: On (healthy) / Off (unhealthy)

### Automation Example

```yaml
automation:
  - alias: "Zehnder RF Communication Alert"
    trigger:
      platform: state
      entity_id: binary_sensor.zehnder_rf_status
      to: "off"
      for:
        minutes: 2
    action:
      service: notify.mobile_app
      data:
        title: "Zehnder RF Issue"
        message: "RF communication with ventilation system has failed"
```

## Troubleshooting

If the RF Status sensor shows unhealthy:

1. **Check logs** for specific timeout messages
2. **Verify hardware connections** (nRF905 module, antenna, power)
3. **Check interference** (other 868MHz devices)
4. **Restart ESP** to reinitialize RF module
5. **Re-pair device** if discovery timeouts persist

## Technical Implementation

### Files Modified
- `zehnder.h`: Added health tracking variables and status sensor class
- `zehnder.cpp`: Added `updateRfHealth()` method and sensor implementation  
- `binary_sensor.py`: New ESPHome platform for status sensor
- `__init__.py`: Updated namespace definitions
- `fan.py`: Updated imports to use shared namespace

### Key Methods
- `updateRfHealth(bool success)`: Updates health status based on RF operations
- `ZehnderRFStatusSensor::loop()`: Publishes current health status to ESPHome