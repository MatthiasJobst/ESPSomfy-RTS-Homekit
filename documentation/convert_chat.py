#!/usr/bin/env python3
"""
Convert a VS Code Copilot Chat NDJSON session file to a single-document JSON
that can be imported via the "Import Chat Session" command.

Usage: python3 convert_chat.py homekitchat.json homekitchat_fixed.json
"""
import json, sys, os

def set_nested(obj, path, value):
    for key in path[:-1]:
        obj = obj[key]
    obj[path[-1]] = value

def extend_nested(obj, path, items):
    for key in path:
        obj = obj[key]
    obj.extend(items)

input_file = sys.argv[1] if len(sys.argv) > 1 else "homekitchat.json"
output_file = sys.argv[2] if len(sys.argv) > 2 else os.path.splitext(input_file)[0] + "_fixed.json"

state = None
with open(input_file, "r", encoding="utf-8") as f:
    for lineno, line in enumerate(f, 1):
        line = line.strip()
        if not line:
            continue
        try:
            obj = json.loads(line)
        except json.JSONDecodeError as e:
            print(f"Warning: line {lineno} is invalid JSON – skipping: {e}")
            continue

        kind = obj.get("kind")
        if kind == 0:
            state = obj["v"]
        elif kind == 1:
            if state is None:
                print("Error: got patch before initial state (kind:0 missing)")
                sys.exit(1)
            try:
                set_nested(state, obj["k"], obj["v"])
            except (IndexError, KeyError) as e:
                print(f"Warning: line {lineno} kind:1 patch {obj['k']} failed ({e}) – skipping")
        elif kind == 2:
            if state is None:
                print("Error: got patch before initial state (kind:0 missing)")
                sys.exit(1)
            try:
                extend_nested(state, obj["k"], obj["v"])
            except (IndexError, KeyError) as e:
                print(f"Warning: line {lineno} kind:2 extend {obj['k']} failed ({e}) – skipping")

if state is None:
    print("Error: no kind:0 entry found – is this a valid chat session file?")
    sys.exit(1)

with open(output_file, "w", encoding="utf-8") as f:
    json.dump(state, f, indent=2, ensure_ascii=False)

print(f"Converted {input_file} → {output_file}")
