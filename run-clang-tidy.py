#!/usr/bin/env python3
"""
Run clang-tidy on the project's own source files (main/ and components/).

The ESP-IDF build uses an Xtensa cross-compiler with GCC-specific flags that
clang-tidy doesn't understand.  This script rewrites compile_commands.json to:
  - Keep only entries under main/ and components/ (not managed_components/).
  - Strip flags clang doesn't know (e.g. -mlongcalls, -fstrict-volatile-bitfields).
  - Replace the Xtensa compiler path with "clang++" so clang-tidy picks up the
    right implicit include paths for each entry's language.
  - Add --target=xtensa-esp32s3-elf so clang models the right architecture.

Usage:
    python3 run-clang-tidy.py [clang-tidy options / file filters]

Examples:
    python3 run-clang-tidy.py                     # all project files
    python3 run-clang-tidy.py main/MQTT.cpp        # single file
    python3 run-clang-tidy.py --fix               # apply fixes
"""

import json
import os
import re
import subprocess
import sys
import tempfile

PROJECT_ROOT = os.path.dirname(os.path.abspath(__file__))
COMPILE_DB   = os.path.join(PROJECT_ROOT, "build", "compile_commands.json")

# Directories whose files we want to analyse (relative to PROJECT_ROOT).
KEEP_DIRS = (
    os.path.join(PROJECT_ROOT, "main"),
    os.path.join(PROJECT_ROOT, "components"),
)

# Flags that are GCC- or Xtensa-specific and unknown / harmful to clang.
STRIP_FLAGS = {
    "-mlongcalls",
    "-fstrict-volatile-bitfields",
    "-fno-jump-tables",
    "-fno-tree-switch-conversion",
    "-fno-shrink-wrap",
    # GCC warning spellings clang doesn't accept as-is.
    "-Werror=all",
    "-Wno-error=deprecated-declarations",
    "-Wno-error=extra",
    "-Wno-error=unused-but-set-variable",
    "-Wno-error=unused-function",
    "-Wno-error=unused-variable",
}

# Regex for flags that take a value as the next token (so we strip both).
STRIP_FLAG_ARGS_RE = re.compile(r"^(-MF|-MT|-MQ|-dependency-file)$")


def toolchain_isystem_flags(compiler_path: str) -> list[str]:
    """
    Derive the standard-library -isystem flags from the Xtensa cross-compiler
    path.  clang needs these because it doesn't share GCC's implicit sysroot.

    Given a path like:
      .../xtensa-esp-elf/<ver>/xtensa-esp-elf/bin/xtensa-esp32s3-elf-g++
    the toolchain root is the directory two levels above 'bin/'.
    """
    # Walk up: bin/ -> <toolchain-root>
    toolchain_root = os.path.dirname(os.path.dirname(compiler_path))

    # Detect the GCC version from the lib/gcc subdirectory.
    gcc_lib = os.path.join(toolchain_root, "lib", "gcc", "xtensa-esp-elf")
    if os.path.isdir(gcc_lib):
        versions = sorted(os.listdir(gcc_lib))
        gcc_ver = versions[-1] if versions else None
    else:
        gcc_ver = None

    candidates = [
        # newlib C headers (stdlib.h, string.h, …)
        os.path.join(toolchain_root, "xtensa-esp-elf", "include"),
        # libstdc++ C++ headers (<functional>, <vector>, …)
        os.path.join(toolchain_root, "xtensa-esp-elf", "include", "c++",
                     gcc_ver or ""),
        # Target-specific C++ headers (bits/…)
        os.path.join(toolchain_root, "xtensa-esp-elf", "include", "c++",
                     gcc_ver or "", "xtensa-esp-elf"),
    ]
    if gcc_ver:
        candidates += [
            # GCC built-in headers (stdint.h, stddef.h, …)
            os.path.join(toolchain_root, "lib", "gcc", "xtensa-esp-elf",
                         gcc_ver, "include"),
            os.path.join(toolchain_root, "lib", "gcc", "xtensa-esp-elf",
                         gcc_ver, "include-fixed"),
        ]

    flags = []
    for path in candidates:
        if os.path.isdir(path):
            flags += ["-isystem", path]
    return flags


def rewrite_entry(entry: dict, extra_isystem: list[str]) -> dict:
    """Return a new compile_commands entry suitable for clang-tidy."""
    tokens = entry["command"].split()

    new_tokens = []
    skip_next = False
    for i, tok in enumerate(tokens):
        if skip_next:
            skip_next = False
            continue
        # Replace the cross-compiler with clang++ (or clang for C files).
        if i == 0:
            lang = "clang" if entry["file"].endswith(".c") else "clang++"
            new_tokens.append(lang)
            # Tell clang which architecture we're targeting.
            new_tokens.append("--target=xtensa-esp32s3-elf")
            # Inject the toolchain's standard-library headers.
            new_tokens.extend(extra_isystem)
            continue
        if tok in STRIP_FLAGS:
            continue
        if STRIP_FLAG_ARGS_RE.match(tok):
            skip_next = True
            continue
        new_tokens.append(tok)

    return {
        "directory": entry["directory"],
        "command":   " ".join(new_tokens),
        "file":      entry["file"],
    }


def main() -> None:
    if not os.path.exists(COMPILE_DB):
        sys.exit(
            f"compile_commands.json not found at {COMPILE_DB}.\n"
            "Run 'idf.py build' first."
        )

    with open(COMPILE_DB) as f:
        db = json.load(f)

    # Extract the Xtensa compiler path from the first entry so we can derive
    # the toolchain sysroot for injecting -isystem flags.
    first_compiler = db[0]["command"].split()[0] if db else ""
    extra_isystem = toolchain_isystem_flags(first_compiler) if first_compiler else []
    if extra_isystem:
        print("Toolchain sysroot headers:")
        for flag in extra_isystem:
            if not flag.startswith("-"):
                print(f"  {flag}")

    filtered = [
        rewrite_entry(e, extra_isystem)
        for e in db
        if any(e["file"].startswith(d) for d in KEEP_DIRS)
    ]

    if not filtered:
        sys.exit("No project source files found in compile_commands.json.")

    # Write the filtered DB to a temp directory so we don't clobber the original.
    tmp_dir = tempfile.mkdtemp(prefix="clang-tidy-db-")
    tmp_db  = os.path.join(tmp_dir, "compile_commands.json")
    with open(tmp_db, "w") as f:
        json.dump(filtered, f, indent=2)

    # Collect the list of source files to analyse.
    # If the caller passed explicit file paths, use those; otherwise use all
    # files in the filtered DB.
    extra_args = []
    file_args  = []
    for arg in sys.argv[1:]:
        if arg.startswith("-"):
            extra_args.append(arg)
        else:
            # Resolve relative to cwd.
            file_args.append(os.path.abspath(arg))

    if not file_args:
        file_args = [e["file"] for e in filtered]

    # Prefer the ESP-IDF bundled clang-tidy (matches the toolchain version),
    # fall back to whatever is on PATH.
    esp_clang_tidy = os.path.expanduser(
        "~/.espressif/tools/esp-clang/esp-19.1.2_20250312/esp-clang/bin/clang-tidy"
    )
    clang_tidy_bin = esp_clang_tidy if os.path.isfile(esp_clang_tidy) else "clang-tidy"

    cmd = [clang_tidy_bin, "-p", tmp_dir] + extra_args + file_args
    print("Running:", " ".join(cmd[:4]), f"[{len(file_args)} file(s)]")
    result = subprocess.run(cmd)
    sys.exit(result.returncode)


if __name__ == "__main__":
    main()
