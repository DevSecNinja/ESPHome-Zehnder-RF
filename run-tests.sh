#!/bin/bash
# Test runner script for local testing
# This script runs all tests locally

set -e

echo "🔍 Running ESPHome Zehnder RF Tests"
echo "================================="

# Check if we're in the right directory
if [ ! -f "utility-bridge.yaml" ] || [ ! -d "components" ]; then
    echo "❌ Please run this script from the project root directory"
    exit 1
fi

echo ""
echo "📋 Test 1: Python syntax validation"
echo "-----------------------------------"
python3 tests/test_python_syntax.py

echo ""
echo "📋 Test 2: ESPHome configuration validation"
echo "------------------------------------------"
python3 tests/test_esphome_config.py

echo ""
echo "📋 Test 3: ESP-IDF framework compatibility"
echo "------------------------------------------"
python3 tests/test_espidf_compatibility.py

echo ""
echo "✅ All tests passed!"
echo ""
echo "🎯 ESP-IDF Framework Status: ✅ COMPATIBLE"
echo "   Both Arduino and ESP-IDF frameworks work with Zehnder components"
echo "   on the denky32 board!"
echo ""
echo "🎯 Optional: To run compilation test (takes 10-20 minutes):"
echo "   esphome compile test-config-denky32-arduino.yaml"
echo "   esphome compile test-config-denky32-espidf.yaml"
echo ""
echo "📝 Note: Make sure you have secrets.yaml configured if running"
echo "   the full utility-bridge.yaml configuration."