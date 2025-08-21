# Python Version Management with Renovate

This document explains how Python versions are managed automatically in this repository using Renovate.

## Overview

The repository has been configured to automatically manage Python versions using Renovate, implementing the N and N-1 testing strategy (testing current and previous Python versions).

## Files Involved

### 1. `pyproject.toml`
Standard Python project configuration file that specifies:
- **Minimum Python version** (`requires-python = ">=3.11"`) - This is the N-1 version
- **ESPHome dependency** with minimum version requirements
- **Current/preferred Python version** in tool configuration

**Renovate detects**: Python version requirements, ESPHome version updates

### 2. `.python-version`
Standard file used by Python version management tools (pyenv, asdf, etc.):
- Contains the **current/preferred Python version** (N version)
- Single line with version number (e.g., `3.12`)

**Renovate detects**: Python version updates

### 3. `renovate.json`
Renovate configuration with specific rules for Python:
- **Python support enabled** for standard files
- **Regex managers** to detect Python versions in GitHub Actions workflows
- **Package rules** to handle Python version updates appropriately

### 4. `.github/workflows/test.yml`
GitHub Actions workflow with Renovate-friendly Python version specifications:
- **Matrix strategy** tests both N-1 and N versions: `[3.11, 3.12]`
- **Compile job** uses current version: `3.12`
- **Renovate comments** help identify manageable versions

## How It Works

### Automatic Version Detection
Renovate automatically detects Python versions in:

1. **`pyproject.toml`** - Standard Python project file format
   - `requires-python = ">=3.11"` - Minimum supported version
   - `dependencies = ["esphome>=2024.6.0"]` - ESPHome version

2. **`.python-version`** - Standard version file
   - `3.12` - Current/preferred version

3. **GitHub Actions workflow** - Via regex managers
   - `python-version: [3.11, 3.12]` - Testing matrix
   - `python-version: 3.12` - Compile job version

### Version Update Strategy

When new Python versions are released:

1. **Renovate creates PR** to update:
   - `.python-version` to new current version (e.g., `3.13`)
   - `pyproject.toml` tool configuration
   - GitHub Actions workflow versions

2. **N and N-1 strategy maintained**:
   - Testing matrix updated to `[3.12, 3.13]`
   - Minimum requirement stays at previous version
   - Compile job uses latest version

3. **Manual review** allows validation that:
   - ESPHome supports the new Python version
   - All tests pass on both versions
   - Dependencies are compatible

## Manual Version Management

### Using the Automation Script

For manual updates or when customizing the process:

```bash
# See what would be updated
./scripts/update-python-versions.sh --dry-run

# Apply updates
./scripts/update-python-versions.sh

# See help
./scripts/update-python-versions.sh --help
```

### Manual Updates

If needed, you can manually update versions in these files:

1. **Update .python-version**:
   ```bash
   echo "3.13" > .python-version
   ```

2. **Update pyproject.toml**:
   ```toml
   requires-python = ">=3.12"  # N-1 version
   [tool.renovate]
   python-version = "3.13"     # N version
   ```

3. **Update GitHub Actions**:
   ```yaml
   python-version: [3.12, 3.13]  # Test both N-1 and N
   python-version: 3.13          # Use N for compile job
   ```

4. **Update documentation**:
   ```markdown
   - Python 3.12 or 3.13
   - Runs on Python 3.12 and 3.13
   ```

## Validation

After any version changes, validate:

```bash
# Test Python syntax
python tests/test_python_syntax.py

# Test ESPHome configuration
python tests/test_esphome_config.py

# Run all tests
./run-tests.sh
```

## Benefits

1. **Automatic updates**: Renovate creates PRs when new Python versions are available
2. **Consistent strategy**: Always tests N and N-1 versions automatically
3. **Early adoption**: Quickly move to new Python versions while maintaining compatibility
4. **Centralized management**: All version references are automatically kept in sync
5. **CI/CD integration**: GitHub Actions matrix automatically tests correct versions

## Troubleshooting

### Renovate Not Detecting Versions

- Check that `renovate.json` is valid JSON
- Verify Python support is enabled: `"python": {"enabled": true}`
- Ensure files follow standard formats (pyproject.toml, .python-version)

### GitHub Actions Not Updating

- Check regex managers in `renovate.json` are working
- Verify renovate comments are present: `# renovate: datasource=python-version`
- Test regex patterns manually

### ESPHome Compatibility

- Check ESPHome release notes for Python version support
- Test configuration validation: `esphome config test-config.yaml`
- Review compilation logs for version-specific issues

## Future Enhancements

- **Automated Python version detection**: Script could fetch latest versions from python.org API
- **Scheduled updates**: GitHub Actions could run version update script periodically
- **Version compatibility matrix**: Automated testing across multiple Python/ESPHome combinations
- **Release automation**: Automatic releases when all version tests pass