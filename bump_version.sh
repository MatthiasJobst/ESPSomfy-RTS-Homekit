#!/usr/bin/env bash
set -euo pipefail

# Usage: ./bump_version.sh <version>
# Example: ./bump_version.sh 2.4.8
# The version should NOT include the 'v' prefix — the script adds it where needed.

if [[ $# -ne 1 ]]; then
  echo "Usage: $0 <version>  (e.g. 2.4.8)" >&2
  exit 1
fi

VERSION="$1"
TAG="v${VERSION}"

# Strip optional leading 'v' in case the user included it
VERSION="${VERSION#v}"

# Validate format: digits.digits.digits with optional suffix like -Homekit
if ! [[ "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+(-[A-Za-z0-9]+)?$ ]]; then
  echo "Error: version must be in the form X.Y.Z or X.Y.Z-Suffix" >&2
  exit 1
fi

# Derive suffix from current FW_VERSION (e.g. "-Homekit"), keep it unless overridden
CURRENT=$(sed -n 's/.*FW_VERSION "v\([^"]*\)".*/\1/p' main/ConfigSettings.h | head -1)
CURRENT_SUFFIX="${CURRENT#${CURRENT%%[-]*}}"  # everything from first '-' onward (empty if none)
SUFFIX="${CURRENT_SUFFIX}"

FULL_VERSION="${VERSION}${SUFFIX}"
FULL_TAG="v${FULL_VERSION}"

echo "Bumping to: ${FULL_TAG}"

# 1. main/ConfigSettings.h
sed -i '' "s|#define FW_VERSION \"v[^\"]*\"|#define FW_VERSION \"${FULL_TAG}\"|" main/ConfigSettings.h

# 2. data/settings.js
sed -i '' "s|appVersion = 'v[^']*'|appVersion = '${FULL_TAG}'|" data/settings.js

# 3. data/appversion
echo "${FULL_TAG}" > data/appversion

echo "Updated files:"
grep "FW_VERSION" main/ConfigSettings.h
grep "appVersion = " data/settings.js | head -1
echo "data/appversion:  $(cat data/appversion)"

# Commit and tag
git add main/ConfigSettings.h data/settings.js data/appversion
git commit -m "Bump version to ${FULL_TAG}"
git tag "${FULL_TAG}"

echo ""
echo "Done. Push with:"
echo "  git push && git push origin ${FULL_TAG}"
echo ""
echo "Then publish the release on GitHub to trigger the release workflow."
