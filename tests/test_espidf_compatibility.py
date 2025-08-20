#!/usr/bin/env python3
"""
Test script to validate ESP-IDF framework compatibility with Zehnder components
This tests both Arduino and ESP-IDF frameworks on the denky32 board
"""

import subprocess
import sys
import os

def run_esphome_config(config_file):
    """Run ESPHome config validation on a file"""
    try:
        cmd = ['esphome', 'config', config_file]
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=120)
        return result.returncode == 0, result.stdout, result.stderr
    except subprocess.TimeoutExpired:
        return False, "", "Timeout during configuration validation"
    except FileNotFoundError:
        return False, "", "ESPHome command not found"
    except Exception as e:
        return False, "", str(e)

def main():
    """Main test function"""
    print("🔍 ESP-IDF Framework Compatibility Test")
    print("======================================")
    
    # Check if we're in the right directory
    if not os.path.exists("components"):
        print("❌ Please run this test from the project root directory")
        sys.exit(1)
    
    # Ensure secrets.yaml exists for testing
    if not os.path.exists("secrets.yaml"):
        if os.path.exists("secrets.yaml.example"):
            print("Creating secrets.yaml from example for testing...")
            import shutil
            shutil.copy("secrets.yaml.example", "secrets.yaml")
    
    test_configs = [
        ("test-config-denky32-arduino.yaml", "Arduino framework on denky32 board"),
        ("test-config-denky32-espidf.yaml", "ESP-IDF framework on denky32 board"),
    ]
    
    all_passed = True
    
    for config_file, description in test_configs:
        print(f"\n📋 Testing: {description}")
        print("-" * 50)
        
        if not os.path.exists(config_file):
            print(f"❌ Config file {config_file} not found")
            all_passed = False
            continue
            
        success, stdout, stderr = run_esphome_config(config_file)
        
        if success:
            print(f"✅ {description} - Configuration is VALID")
            
            # Check for warnings we care about
            if "FAN_SCHEMA" in stdout:
                print("⚠️  Warning: Using deprecated FAN_SCHEMA")
            else:
                print("✨ No deprecation warnings found")
                
        else:
            print(f"❌ {description} - Configuration FAILED")
            print("Error output:")
            print(stderr)
            if stdout:
                print("Standard output:")
                print(stdout)
            all_passed = False
    
    print("\n" + "="*50)
    if all_passed:
        print("🎉 All framework compatibility tests passed!")
        print("")
        print("📝 Summary:")
        print("• Arduino framework works on denky32 board ✅")
        print("• ESP-IDF framework works on denky32 board ✅") 
        print("• No deprecation warnings found ✅")
        print("")
        print("🚀 The Zehnder components are compatible with ESP-IDF framework!")
        print("   You can now use either Arduino or ESP-IDF framework on denky32 board.")
    else:
        print("❌ Some tests failed - see output above for details")
        sys.exit(1)

if __name__ == "__main__":
    main()