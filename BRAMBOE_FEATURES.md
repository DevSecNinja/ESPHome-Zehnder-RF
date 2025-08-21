# Bramboe Fork Features - Implementation Summary

This document summarizes the new diagnostic features implemented from Bramboe's fork.

## New Features Added

### 1. Filter Status Monitoring

**Functionality**: Queries filter status from the Zehnder fan every 10 minutes if sensors are configured.

**New Components**:
- `queryFilterStatus()` method
- `FAN_TYPE_QUERY_FILTER_STATUS` command (0x32)
- `FAN_TYPE_FILTER_STATUS_RESPONSE` response (0x33) 
- `StateWaitFilterStatusResponse` state
- `RfPayloadFilterStatus` struct

**Sensor Data**:
- `filter_remaining` - Filter life remaining as percentage (0-100%)
- `filter_runtime` - Hours since last filter change

### 2. Error Status Monitoring

**Functionality**: Queries error status from the Zehnder fan every 5 minutes if sensors are configured.

**New Components**:
- `queryErrorStatus()` method
- `FAN_TYPE_QUERY_ERROR_STATUS` command (0x30)
- `FAN_TYPE_ERROR_STATUS_RESPONSE` response (0x31)
- `StateWaitErrorStatusResponse` state
- `RfPayloadErrorStatus` struct

**Sensor Data**:
- `error_count` - Number of active errors
- `error_code` - Formatted error codes (e.g., "E01,E02" or "No Errors")

### 3. ESPHome Configuration Support

**New Configuration Options**:
```yaml
fan:
  - platform: zehnder
    id: my_fan
    name: "My Zehnder Fan"
    nrf905: nrf905_rf
    
    # Optional filter sensors
    filter_remaining:
      name: "Filter Remaining"
      
    filter_runtime:
      name: "Filter Runtime Hours"
    
    # Optional error sensors
    error_count:
      name: "Error Count"
      
    error_code:
      name: "Error Codes"
```

### 4. Enhanced RF Reliability

**Improvements**:
- Added 150ms delay between RF retries
- Added 250ms delay before returning to idle after exhausting retries  
- Added 500ms RF stabilization delay before transmitting speed changes
- Improved timeout handling and logging

## Backward Compatibility

✅ **Fully backward compatible**: Existing configurations without sensors continue to work unchanged.

✅ **Optional sensors**: All new diagnostic sensors are optional - they are only queried if configured.

✅ **No breaking changes**: All existing functionality remains intact.

## Usage Examples

### Basic Configuration (existing functionality unchanged)
```yaml
fan:
  - platform: zehnder
    id: my_fan
    name: "My Fan" 
    nrf905: nrf905_rf
    update_interval: 30s
```

### With Diagnostic Sensors (new functionality)
```yaml
fan:
  - platform: zehnder
    id: my_fan
    name: "My Fan"
    nrf905: nrf905_rf
    update_interval: 30s
    
    filter_remaining:
      name: "Filter Life Remaining"
      
    error_count:
      name: "Fan Errors"
```

## Home Assistant Integration

The new sensors automatically appear in Home Assistant with appropriate:
- Units (% for filter remaining, hours for runtime)
- Device classes (duration for filter runtime)
- Icons (filter, timer, alert icons)

## Technical Implementation

**Query Timing**:
- Filter status: Every 10 minutes (600,000ms)
- Error status: Every 5 minutes (300,000ms) 
- Fan status: Every 15-30s (configurable, unchanged)

**Command Codes** (hypothetical - actual codes may differ):
- 0x30: Query error status
- 0x31: Error status response
- 0x32: Query filter status  
- 0x33: Filter status response

**Note**: The actual command codes (0x30-0x33) are based on Bramboe's implementation. These may need adjustment based on actual Zehnder protocol documentation or hardware testing.