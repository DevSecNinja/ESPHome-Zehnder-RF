# Scripts Directory

This directory contains utility scripts for maintaining the ESPHome Zehnder RF project.

## Scripts

### `update-python-versions.sh`

Automatically updates Python versions across the repository to maintain testing of current (N) and previous (N-1) Python versions. This helps with the automation requirement for testing 2 Python versions mentioned in the issue.

**Usage:**
```bash
# Show what would be changed without making changes
./scripts/update-python-versions.sh --dry-run

# Apply the changes
./scripts/update-python-versions.sh

# Show help
./scripts/update-python-versions.sh --help
```

**What it updates:**
- `.github/workflows/test.yml` - CI matrix testing versions and compile job version
- `.python-version` - Default Python version for tools like pyenv
- `pyproject.toml` - Minimum required Python version and current version
- `tests/README.md` - Documentation references to Python versions

**Automation Strategy:**
The script implements the N and N-1 Python version testing strategy by:
1. Getting the current stable Python version (currently hardcoded to 3.12)
2. Calculating the previous version (N-1)
3. Updating all relevant files to test both versions
4. Setting minimum requirements to the N-1 version
5. Using the current (N) version as the default

**Future Enhancements:**
- Could be automated to fetch the latest Python version from python.org API
- Could be run as part of a scheduled GitHub Action to automatically create PRs
- Could integrate with Renovate to trigger updates when new Python versions are released

## Integration with Renovate

These scripts work in conjunction with the Renovate configuration in `renovate.json` to:
1. Detect Python version changes in standard files (`pyproject.toml`, `.python-version`)
2. Automatically create PRs when new Python versions are available
3. Update GitHub Actions workflow Python versions via regex managers
4. Maintain the N and N-1 testing strategy automatically