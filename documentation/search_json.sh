#!/bin/bash

# Usage: ./search_json.sh <word> [directory]
# Searches for a word in all JSON files under the given directory (default: current dir)

WORD="${1}"
DIR="${2:-.}"

if [[ -z "$WORD" ]]; then
    echo "Usage: $0 <word> [directory]"
    exit 1
fi

grep -rl --include="*.json" -- "$WORD" "$DIR"
