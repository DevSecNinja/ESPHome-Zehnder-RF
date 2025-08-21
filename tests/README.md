# Testing Infrastructure

This directory contains automated tests for the ESPHome Zehnder RF project.

## Test Types

### 1. Python Syntax Validation (`test_python_syntax.py`)
- Validates syntax of all Python files in the `components/` directory
- Uses `py_compile` to ensure files can be imported without syntax errors
- Tests both component configuration files and implementation files

### 2. ESPHome Configuration Validation (`test_esphome_config.py`)
- Validates ESPHome YAML configuration files using `esphome config` command
- Tests configuration schema and component integration
- Uses test-specific configuration with local components for faster validation
- Automatically creates temporary `secrets.yaml` from example if needed

## Running Tests

### Local Testing

1. **Quick test runner** (recommended):
   ```bash
   ./run-tests.sh
   ```

2. **Individual tests**:
   ```bash
   # Python syntax validation
   python3 tests/test_python_syntax.py

   # ESPHome configuration validation  
   python3 tests/test_esphome_config.py
   ```

3. **Optional compilation test** (takes 10-20 minutes):
   ```bash
   esphome compile test-config.yaml
   ```

### CI/CD Pipeline

The GitHub Actions workflow (`.github/workflows/test.yml`) automatically runs:

1. **Basic tests** (on all PRs and pushes):
   - Python syntax validation
   - ESPHome configuration validation
   - Runs on Python 3.11 and 3.12 (N-1 and N versions, managed by Renovate)

2. **Compilation test** (optional, requires label or push to main):
   - Full C++ compilation test using ESPHome
   - Takes up to 90 minutes due to PlatformIO dependency downloads
   - Only runs on pushes to main or PRs with `test-compile` label
   - Uses aggressive caching to speed up subsequent runs

## Test Files

- `test_python_syntax.py` - Python syntax validation
- `test_esphome_config.py` - ESPHome configuration validation
- `../test-config.yaml` - Simplified configuration for testing
- `../secrets.yaml.example` - Template secrets file for testing
- `../run-tests.sh` - Local test runner script

## Prerequisites

- Python 3.11 or 3.12 (versions automatically managed by Renovate - see `pyproject.toml`)
- ESPHome installed (`pip install esphome`)
- All dependencies from the project (automatically handled in CI)

## What's Tested

✅ **Can be tested without hardware:**
- Python component syntax
- ESPHome configuration schema validation
- C++ compilation (via ESPHome/PlatformIO)

❌ **Cannot be tested without hardware:**
- Actual RF communication with Zehnder fans
- Hardware GPIO functionality
- Real-world pairing and control operations

## Troubleshooting

### Configuration Validation Fails

1. Check that `secrets.yaml.example` has valid format (base64 API key)
2. Ensure local components in `components/` directory are valid
3. Run `esphome version` to verify ESPHome installation

### Compilation Test Times Out

- First-time compilation can take 10-20+ minutes due to dependency downloads
- Subsequent runs should be faster due to caching
- CI has 90-minute timeout for compilation tests

### Python Syntax Errors

- Check that all `.py` files in `components/` have valid Python syntax
- Run individual file check: `python3 -m py_compile path/to/file.py`

## Contributing

When adding new components or modifying existing ones:

1. Ensure all Python files pass syntax validation
2. Test that configuration changes don't break ESPHome validation  
3. Run local tests before submitting PRs
4. Add `test-compile` label to PRs if you want to test compilation in CI