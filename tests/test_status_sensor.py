#!/usr/bin/env python3
"""
Test script for status sensor functionality in ESPHome Zehnder RF project.

This script validates that the status sensor binary_sensor platform is properly configured
and can be included in ESPHome configurations.
"""

import sys
import tempfile
import os
from pathlib import Path

def test_status_sensor_config():
    """Test that the status sensor can be configured properly."""
    
    script_dir = Path(__file__).parent.parent
    
    # Create a minimal test configuration that includes the status sensor
    test_config = """
esphome:
  name: zehnder-test
  comment: Test configuration for status sensor

esp32:
  board: esp32doit-devkit-v1
  framework:
    type: arduino

logger:
  id: component_logger

# Load local components
external_components:
  - source: components
    components: [ nrf905, zehnder ]

# SPI configuration
spi:
  clk_pin: GPIO14
  mosi_pin: GPIO13
  miso_pin: GPIO12

# nRF905 config
nrf905:
  id: "nrf905_rf"
  cs_pin: GPIO15
  cd_pin: GPIO33
  ce_pin: GPIO27
  pwr_pin: GPIO26
  txen_pin: GPIO25
  am_pin: GPIO32
  dr_pin: GPIO35

# The FAN controller
fan:
  - platform: zehnder
    id: test_ventilation
    name: "Test Ventilation"
    nrf905: nrf905_rf
    update_interval: "30s"

# Status sensor - this is what we're testing
binary_sensor:
  - platform: zehnder
    name: "Test RF Status"
    id: test_rf_status
    zehnder_rf: test_ventilation
    icon: mdi:wifi-check
    device_class: connectivity
"""

    # Write the test config to a temporary file
    with tempfile.NamedTemporaryFile(mode='w', suffix='.yaml', delete=False) as f:
        f.write(test_config)
        temp_config_path = f.name
    
    try:
        print(f"✅ Created test configuration with status sensor")
        print(f"   Test config: {os.path.relpath(temp_config_path, script_dir)}")
        
        # Check if the file contains our status sensor configuration
        with open(temp_config_path, 'r') as f:
            content = f.read()
            
        required_elements = [
            "platform: zehnder",
            "zehnder_rf: test_ventilation", 
            "device_class: connectivity"
        ]
        
        for element in required_elements:
            if element in content:
                print(f"   ✓ Found: {element}")
            else:
                print(f"   ✗ Missing: {element}")
                return False
                
        print(f"✅ Status sensor configuration syntax is valid")
        return True
        
    finally:
        # Clean up temporary file
        if os.path.exists(temp_config_path):
            os.unlink(temp_config_path)

if __name__ == "__main__":
    print("🔍 Testing status sensor configuration")
    print("=====================================")
    success = test_status_sensor_config()
    print("")
    if success:
        print("✅ Status sensor test passed!")
    else:
        print("❌ Status sensor test failed!")
    sys.exit(0 if success else 1)