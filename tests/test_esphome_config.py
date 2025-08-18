#!/usr/bin/env python3
"""
Test script for validating ESPHome configuration files.

This script validates the utility-bridge.yaml configuration file to ensure
it has valid syntax and schema according to ESPHome requirements.
"""

import sys
import subprocess
import os
import shutil
from pathlib import Path

def setup_secrets_for_testing():
    """Create a temporary secrets.yaml file for testing if it doesn't exist."""
    script_dir = Path(__file__).parent.parent
    secrets_path = script_dir / "secrets.yaml"
    secrets_example_path = script_dir / "secrets.yaml.example"
    
    if not secrets_path.exists():
        if secrets_example_path.exists():
            print("Creating secrets.yaml from example for testing...")
            shutil.copy(secrets_example_path, secrets_path)
            return True
        else:
            print("Error: No secrets.yaml or secrets.yaml.example found")
            return False
    else:
        print("Using existing secrets.yaml file")
        return True

def test_esphome_config():
    """Test ESPHome configuration validation."""
    script_dir = Path(__file__).parent.parent
    config_file = script_dir / "test-config.yaml"
    
    if not config_file.exists():
        print(f"❌ Configuration file not found: {config_file}")
        return False
    
    print(f"Validating ESPHome configuration: {config_file}")
    
    # Setup secrets file
    if not setup_secrets_for_testing():
        return False
    
    try:
        # Check if esphome is available
        result = subprocess.run(
            ["esphome", "version"], 
            capture_output=True, 
            text=True, 
            cwd=script_dir,
            timeout=30
        )
        
        if result.returncode != 0:
            print("❌ ESPHome not available - trying to install...")
            # Try to install esphome
            install_result = subprocess.run(
                [sys.executable, "-m", "pip", "install", "esphome"], 
                capture_output=True, 
                text=True,
                timeout=300
            )
            if install_result.returncode != 0:
                print(f"❌ Failed to install ESPHome: {install_result.stderr}")
                return False
            print("✓ ESPHome installed successfully")
        else:
            print(f"✓ ESPHome version: {result.stdout.strip()}")
        
        # Run esphome config validation
        print("Running ESPHome config validation...")
        result = subprocess.run(
            ["esphome", "config", str(config_file)], 
            capture_output=True, 
            text=True, 
            cwd=script_dir,
            timeout=120
        )
        
        if result.returncode == 0:
            print("✅ ESPHome configuration is valid!")
            if result.stdout:
                print("Config output:")
                print(result.stdout)
            return True
        else:
            print("❌ ESPHome configuration validation failed!")
            if result.stderr:
                print("Errors:")
                print(result.stderr)
            if result.stdout:
                print("Output:")
                print(result.stdout)
            return False
            
    except subprocess.TimeoutExpired:
        print("❌ ESPHome configuration validation timed out")
        return False
    except FileNotFoundError:
        print("❌ ESPHome command not found - please install ESPHome first")
        print("   Run: pip3 install esphome")
        return False
    except Exception as e:
        print(f"❌ Unexpected error during validation: {e}")
        return False

def cleanup_test_files():
    """Clean up temporary files created during testing."""
    script_dir = Path(__file__).parent.parent
    secrets_path = script_dir / "secrets.yaml"
    secrets_example_path = script_dir / "secrets.yaml.example"
    
    # Only remove secrets.yaml if it was created from example
    if secrets_path.exists() and secrets_example_path.exists():
        try:
            with open(secrets_path) as f1, open(secrets_example_path) as f2:
                if f1.read() == f2.read():
                    os.remove(secrets_path)
                    print("Cleaned up temporary secrets.yaml file")
        except Exception as e:
            print(f"Warning: Could not clean up secrets.yaml: {e}")

if __name__ == "__main__":
    try:
        success = test_esphome_config()
        sys.exit(0 if success else 1)
    finally:
        cleanup_test_files()