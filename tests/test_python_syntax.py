#!/usr/bin/env python3
"""
Test script for validating Python component syntax in ESPHome Zehnder RF project.

This script validates the syntax of all Python files in the components directory
to ensure they can be imported and compiled without syntax errors.
"""

import sys
import py_compile
import glob
import os
from pathlib import Path

def test_python_syntax():
    """Test Python syntax for all component files."""
    
    # Find all Python files in components directory
    script_dir = Path(__file__).parent.parent
    python_files = []
    
    for pattern in ["components/**/*.py", "tests/**/*.py"]:
        python_files.extend(glob.glob(str(script_dir / pattern), recursive=True))
    
    if not python_files:
        print("No Python files found to test")
        return True
    
    print(f"Found {len(python_files)} Python files to validate:")
    for file_path in python_files:
        print(f"  - {os.path.relpath(file_path, script_dir)}")
    
    errors = []
    
    for file_path in python_files:
        print(f"\nValidating: {os.path.relpath(file_path, script_dir)}")
        try:
            # Compile the Python file to check for syntax errors
            py_compile.compile(file_path, doraise=True)
            print("  ✓ Syntax OK")
        except py_compile.PyCompileError as e:
            error_msg = f"Syntax error in {file_path}: {e}"
            errors.append(error_msg)
            print(f"  ✗ {error_msg}")
        except Exception as e:
            error_msg = f"Unexpected error in {file_path}: {e}"
            errors.append(error_msg)
            print(f"  ✗ {error_msg}")
    
    if errors:
        print(f"\n❌ Found {len(errors)} syntax errors:")
        for error in errors:
            print(f"  - {error}")
        return False
    else:
        print(f"\n✅ All {len(python_files)} Python files have valid syntax!")
        return True

if __name__ == "__main__":
    success = test_python_syntax()
    sys.exit(0 if success else 1)