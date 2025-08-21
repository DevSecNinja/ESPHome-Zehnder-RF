#!/bin/bash
# update-python-versions.sh
# Script to automatically update Python versions to test current (N) and previous (N-1) versions
# This script demonstrates automation for maintaining N and N-1 Python version testing

set -e

# Get the current stable Python version from python.org
# This is a simplified approach - in a real scenario, you might want to use a more robust method
get_latest_python_version() {
    # For this example, we'll use a hardcoded approach, but this could be automated
    # to fetch from python.org API or other sources
    echo "3.12"
}

get_previous_python_version() {
    local current_version=$1
    local major=$(echo "$current_version" | cut -d. -f1)
    local minor=$(echo "$current_version" | cut -d. -f2)
    local prev_minor=$((minor - 1))
    echo "${major}.${prev_minor}"
}

update_github_actions() {
    local current_version=$1
    local previous_version=$2
    
    echo "Updating GitHub Actions workflow to test Python $previous_version and $current_version"
    
    # Update the matrix strategy in the workflow file
    sed -i "s/python-version: \[.*\]/python-version: [$previous_version, $current_version]/" .github/workflows/test.yml
    
    # Update the compile-test job to use the current version
    sed -i "s/python-version: [0-9]\+\.[0-9]\+/python-version: $current_version/" .github/workflows/test.yml
}

update_python_version_file() {
    local current_version=$1
    echo "Updating .python-version to $current_version"
    echo "$current_version" > .python-version
}

update_pyproject_toml() {
    local previous_version=$1
    local current_version=$2
    
    echo "Updating pyproject.toml to require Python >= $previous_version"
    sed -i "s/requires-python = \">=.*\"/requires-python = \">=$previous_version\"/" pyproject.toml
    sed -i "s/python-version = \".*\"/python-version = \"$current_version\"/" pyproject.toml
}

update_documentation() {
    local previous_version=$1
    local current_version=$2
    
    echo "Updating documentation to reference Python $previous_version and $current_version"
    
    # Update tests/README.md
    sed -i "s/Python [0-9]\+\.[0-9]\+ and [0-9]\+\.[0-9]\+/Python $previous_version and $current_version/g" tests/README.md
    sed -i "s/Python [0-9]\+\.[0-9]\+ or [0-9]\+\.[0-9]\+/Python $previous_version or $current_version/g" tests/README.md
}

main() {
    if [ ! -f ".github/workflows/test.yml" ]; then
        echo "Error: This script must be run from the repository root"
        exit 1
    fi
    
    echo "🐍 Python Version Update Script"
    echo "================================"
    
    local current_version=$(get_latest_python_version)
    local previous_version=$(get_previous_python_version "$current_version")
    
    echo "Current Python version: $current_version"
    echo "Previous Python version: $previous_version"
    echo ""
    
    if [ "$1" = "--dry-run" ]; then
        echo "DRY RUN: Would update the following:"
        echo "- GitHub Actions workflow: Python [$previous_version, $current_version]"
        echo "- .python-version: $current_version"
        echo "- pyproject.toml: requires-python >= $previous_version, current: $current_version"
        echo "- Documentation: Python $previous_version and $current_version"
        exit 0
    fi
    
    update_github_actions "$current_version" "$previous_version"
    update_python_version_file "$current_version"
    update_pyproject_toml "$previous_version" "$current_version"
    update_documentation "$previous_version" "$current_version"
    
    echo ""
    echo "✅ Successfully updated Python versions"
    echo "   Testing versions: $previous_version (N-1) and $current_version (N)"
    echo "   Minimum required: $previous_version"
    echo "   Default/latest: $current_version"
    echo ""
    echo "💡 Next steps:"
    echo "   1. Review the changes: git diff"
    echo "   2. Test the changes: ./run-tests.sh" 
    echo "   3. Commit the changes: git add . && git commit -m 'Update Python versions'"
}

# Show help if requested
if [ "$1" = "--help" ] || [ "$1" = "-h" ]; then
    echo "Usage: $0 [--dry-run|--help]"
    echo ""
    echo "This script automatically updates Python versions across the repository"
    echo "to maintain testing of current (N) and previous (N-1) Python versions."
    echo ""
    echo "Options:"
    echo "  --dry-run    Show what would be changed without making changes"
    echo "  --help       Show this help message"
    echo ""
    echo "Files updated:"
    echo "  - .github/workflows/test.yml (CI matrix and compile job)"
    echo "  - .python-version (default Python version)"
    echo "  - pyproject.toml (minimum required version and current version)"
    echo "  - tests/README.md (documentation)"
    exit 0
fi

main "$@"