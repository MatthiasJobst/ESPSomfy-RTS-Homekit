#!/usr/bin/env python3
"""LCOM (Lack of Cohesion of Methods) analyser for the Somfy C++ classes.

Why a Clang-AST approach instead of regex: member access in this codebase is a
mix of `m_`-prefixed and plain public fields (``proto``, ``flags``, ``gpioUp``),
so textual heuristics can't reliably tell a field access from a local variable
or free function. We therefore parse the real Clang JSON AST (``clang
-ast-dump=json``) and resolve every ``MemberExpr`` to the exact field/method it
references. Requires only the system ``clang`` — no Python libclang bindings and
no extra installs.

Metrics reported (per class):

  * LCOM4  — number of connected components in the method graph, where two
             methods are connected if they share an instance field OR one calls
             the other. 1 == cohesive; >=2 == the class splits cleanly into that
             many responsibilities (the actionable "should I split this" number).
  * LCOM-HS — Henderson-Sellers: (m - (1/f)*sum_f mu_f) / (m - 1), in [0,1].
             0 == every method touches every field (max cohesion); near 1 ==
             incohesive. Undefined when m<2 or f==0.
  * LCOM1  — Chidamber-Kemerer: count of method pairs sharing no field.
  * LCOM2  — max(0, LCOM1 - pairsSharingAField).

Method identity is the Clang mangled name, so methods of a class that are split
across several .cpp files (e.g. SomfyShade in main/somfy/shade/*.cpp) are
aggregated into a single class. By default constructors, destructors and static
methods are excluded (constructors touch every field and would mask the real
cohesion); pass --include-ctors / --include-static to change that.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
import tempfile
from collections import defaultdict
from dataclasses import dataclass, field
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
SOMFY_DIR = REPO_ROOT / "main" / "somfy"
TEST_UNIT = REPO_ROOT / "test" / "unit"


# ──────────────────────────────────────────────────────────────────────────────
# Toolchain / compile flags (mirror the host unit-test build in test/unit)
# ──────────────────────────────────────────────────────────────────────────────
def find_arduinojson() -> Path | None:
    candidate = TEST_UNIT / "build" / "_deps" / "arduinojson-src" / "src"
    if candidate.is_dir():
        return candidate
    hits = list(TEST_UNIT.glob("build/**/arduinojson-src/src"))
    return hits[0] if hits else None


def compile_flags() -> list[str]:
    """Include order matches test/unit/CMakeLists.txt (stubs intercept first)."""
    includes = [
        TEST_UNIT / "stubs",
        REPO_ROOT / "main",
        REPO_ROOT / "main" / "services",
        REPO_ROOT / "main" / "somfy",
        REPO_ROOT / "main" / "somfy" / "shade",
    ]
    aj = find_arduinojson()
    if aj:
        includes.append(aj)
    else:
        print(
            "WARNING: ArduinoJson source not found under test/unit/build; "
            "configure the host tests first (cmake -B build -S test/unit). "
            "Files that include ArduinoJson may fail to parse.",
            file=sys.stderr,
        )
    flags = ["-std=gnu++23", "-DUSE_NVS", "-x", "c++"]
    for inc in includes:
        flags += ["-I", str(inc)]
    return flags


# ──────────────────────────────────────────────────────────────────────────────
# Class allowlist: only measure classes declared in main/somfy/**/*.h
# ──────────────────────────────────────────────────────────────────────────────
CLASS_DECL_RE = re.compile(r"\b(?:class|struct)\s+([A-Za-z_]\w*)")


def discover_target_classes() -> set[str]:
    names: set[str] = set()
    for header in SOMFY_DIR.rglob("*.h"):
        for line in header.read_text(errors="ignore").splitlines():
            stripped = line.lstrip()
            if stripped.startswith(("class ", "struct ")):
                m = CLASS_DECL_RE.match(stripped)
                if m:
                    names.add(m.group(1))
    return names


def discover_sources() -> list[Path]:
    return sorted(SOMFY_DIR.rglob("*.cpp"))


# ──────────────────────────────────────────────────────────────────────────────
# AST traversal helpers
# ──────────────────────────────────────────────────────────────────────────────
def iter_nodes(root: dict):
    """Iterative pre-order walk over the Clang JSON AST."""
    stack = [root]
    while stack:
        node = stack.pop()
        yield node
        inner = node.get("inner")
        if inner:
            stack.extend(reversed(inner))


def has_body(node: dict) -> bool:
    for child in node.get("inner") or ():
        if child.get("kind") == "CompoundStmt":
            return True
    return False


def referenced_member_ids(node: dict):
    """Yield every referencedMemberDecl id reachable from a method body."""
    for sub in iter_nodes(node):
        if sub.get("kind") == "MemberExpr":
            ref = sub.get("referencedMemberDecl")
            if ref:
                yield ref


# ──────────────────────────────────────────────────────────────────────────────
# Per-class accumulated data (aggregated across translation units)
# ──────────────────────────────────────────────────────────────────────────────
@dataclass
class ClassData:
    name: str
    fields: set[str] = field(default_factory=set)
    methods: set[str] = field(default_factory=set)  # mangled names
    method_label: dict[str, str] = field(default_factory=dict)  # mangled -> readable
    # mangled -> set of own field names it reads/writes
    field_access: dict[str, set[str]] = field(default_factory=lambda: defaultdict(set))
    # mangled -> set of own methods (mangled) it calls
    calls: dict[str, set[str]] = field(default_factory=lambda: defaultdict(set))


def process_tu(ast: dict, targets: set[str], classes: dict[str, ClassData],
               include_static: bool) -> None:
    """Fold one translation unit's AST into the cross-TU `classes` accumulator."""
    # Pass 1: for every target class definition in this TU, map the TU-local ids
    # of its fields and methods to stable names. Field references in method
    # bodies use these TU-local ids, so the maps must be rebuilt per TU.
    field_id_to_name: dict[str, tuple[str, str]] = {}   # id -> (class, field)
    method_id_to_key: dict[str, tuple[str, str]] = {}   # id -> (class, mangled)
    mangled_to_class: dict[str, str] = {}

    for node in iter_nodes(ast):
        if node.get("kind") != "CXXRecordDecl":
            continue
        cname = node.get("name")
        if not cname or cname not in targets or not node.get("completeDefinition"):
            continue
        if node.get("isImplicit"):
            continue
        cdata = classes.setdefault(cname, ClassData(cname))
        for child in node.get("inner") or ():
            kind = child.get("kind")
            if kind == "FieldDecl" and child.get("name"):
                fname = child["name"]
                cdata.fields.add(fname)
                field_id_to_name[child["id"]] = (cname, fname)
            elif kind == "CXXMethodDecl":
                # Plain methods only: CXXConstructorDecl / CXXDestructorDecl are
                # distinct kinds and thus excluded here by design.
                if child.get("isImplicit"):
                    continue
                if not include_static and child.get("storageClass") == "static":
                    continue
                mangled = child.get("mangledName") or child.get("name")
                if not mangled:
                    continue
                cdata.methods.add(mangled)
                cdata.method_label.setdefault(mangled, readable_method(cname, child))
                method_id_to_key[child["id"]] = (cname, mangled)
                mangled_to_class[mangled] = cname

    if not method_id_to_key and not mangled_to_class:
        return

    # Pass 2: scan method bodies (in-class inline + out-of-line definitions) and
    # resolve their member references against this TU's id maps.
    for node in iter_nodes(ast):
        if node.get("kind") != "CXXMethodDecl" or not has_body(node):
            continue
        if node.get("isImplicit"):
            continue
        if not include_static and node.get("storageClass") == "static":
            continue
        key = method_id_to_key.get(node["id"])
        if key is None:
            mangled = node.get("mangledName") or node.get("name")
            cname = mangled_to_class.get(mangled) if mangled else None
            if cname is None:
                continue
            key = (cname, mangled)
        cname, mangled = key
        cdata = classes[cname]
        cdata.methods.add(mangled)
        cdata.method_label.setdefault(mangled, readable_method(cname, node))
        for ref in referenced_member_ids(node):
            fref = field_id_to_name.get(ref)
            if fref and fref[0] == cname:
                cdata.field_access[mangled].add(fref[1])
                continue
            mref = method_id_to_key.get(ref)
            if mref and mref[0] == cname and mref[1] != mangled:
                cdata.calls[mangled].add(mref[1])


def readable_method(cname: str, node: dict) -> str:
    name = node.get("name", "?")
    sig = (node.get("type") or {}).get("qualType", "")
    params = ""
    if "(" in sig:
        params = sig[sig.index("("):]
    return f"{name}{params}"


# ──────────────────────────────────────────────────────────────────────────────
# Metric computation
# ──────────────────────────────────────────────────────────────────────────────
@dataclass
class Metrics:
    name: str
    n_methods: int
    n_fields: int
    lcom4: int
    components: list[list[str]]
    lcom_hs: float | None
    lcom1: int
    lcom2: int


def compute_metrics(cdata: ClassData) -> Metrics:
    methods = sorted(cdata.methods)
    fields_of = {m: cdata.field_access.get(m, set()) for m in methods}
    calls_of = {m: cdata.calls.get(m, set()) for m in methods}
    n = len(methods)
    f = len(cdata.fields)

    # LCOM4: connected components over shared-field OR call edges.
    parent = {m: m for m in methods}

    def find(x):
        while parent[x] != x:
            parent[x] = parent[parent[x]]
            x = parent[x]
        return x

    def union(a, b):
        ra, rb = find(a), find(b)
        if ra != rb:
            parent[ra] = rb

    for i in range(n):
        for j in range(i + 1, n):
            a, b = methods[i], methods[j]
            if fields_of[a] & fields_of[b]:
                union(a, b)
    for m in methods:
        for callee in calls_of[m]:
            if callee in parent:
                union(m, callee)

    comp_map: dict[str, list[str]] = defaultdict(list)
    for m in methods:
        comp_map[find(m)].append(m)
    components = sorted(
        ([cdata.method_label.get(m, m) for m in sorted(group)]
         for group in comp_map.values()),
        key=len, reverse=True,
    )
    lcom4 = len(components) if methods else 0

    # Classic Chidamber-Kemerer LCOM1/2 over field sharing only.
    p = q = 0
    for i in range(n):
        for j in range(i + 1, n):
            if fields_of[methods[i]] & fields_of[methods[j]]:
                q += 1
            else:
                p += 1
    lcom1 = p
    lcom2 = max(0, p - q)

    # Henderson-Sellers LCOM*.
    lcom_hs: float | None = None
    if n > 1 and f > 0:
        accesses_per_field = sum(
            1 for fld in cdata.fields for m in methods if fld in fields_of[m]
        )
        mean = accesses_per_field / f
        lcom_hs = (n - mean) / (n - 1)

    return Metrics(cdata.name, n, f, lcom4, components, lcom_hs, lcom1, lcom2)


# ──────────────────────────────────────────────────────────────────────────────
# Driver
# ──────────────────────────────────────────────────────────────────────────────
def dump_ast(src: Path, flags: list[str], clang: str) -> dict | None:
    with tempfile.NamedTemporaryFile("w+b", suffix=".json", delete=False) as tmp:
        tmp_path = Path(tmp.name)
    try:
        with open(tmp_path, "wb") as out:
            proc = subprocess.run(
                [clang, "-Xclang", "-ast-dump=json", "-fsyntax-only", *flags, str(src)],
                stdout=out, stderr=subprocess.PIPE, cwd=REPO_ROOT,
            )
        if proc.returncode != 0:
            print(f"  ! clang failed for {src.name}: "
                  f"{proc.stderr.decode(errors='ignore').splitlines()[:1]}",
                  file=sys.stderr)
            return None
        with open(tmp_path) as fh:
            return json.load(fh)
    finally:
        tmp_path.unlink(missing_ok=True)


def print_report(results: list[Metrics], verbose: bool) -> None:
    results = [m for m in results if m.n_methods >= 2]
    results.sort(key=lambda m: (m.lcom4, m.lcom_hs or 0.0), reverse=True)

    print()
    print(f"{'Class':<28}{'Meth':>5}{'Fld':>5}{'LCOM4':>7}{'LCOM-HS':>9}"
          f"{'LCOM1':>7}{'LCOM2':>7}")
    print("-" * 68)
    for m in results:
        hs = f"{m.lcom_hs:.3f}" if m.lcom_hs is not None else "  n/a"
        flag = "  <-- split?" if m.lcom4 >= 2 else ""
        print(f"{m.name:<28}{m.n_methods:>5}{m.n_fields:>5}{m.lcom4:>7}"
              f"{hs:>9}{m.lcom1:>7}{m.lcom2:>7}{flag}")

    print()
    print("LCOM4 >= 2 means the class separates into independent method groups "
          "(candidates to split):")
    for m in results:
        if m.lcom4 >= 2:
            print(f"\n  {m.name} -> {m.lcom4} components:")
            for idx, group in enumerate(m.components, 1):
                shown = group if verbose else group[:8]
                print(f"    [{idx}] {len(group)} method(s): "
                      + ", ".join(shown)
                      + ("" if len(shown) == len(group)
                         else f", ... (+{len(group) - len(shown)} more)"))
    print("\nGuide: LCOM4 1=cohesive, >=2=splittable | "
          "LCOM-HS 0=cohesive..1=incohesive | LCOM1/2=Chidamber-Kemerer.")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--clang", default=os.environ.get("CLANG", "clang"),
                    help="clang executable (default: clang)")
    ap.add_argument("--include-static", action="store_true",
                    help="include static methods (excluded by default)")
    ap.add_argument("--only", metavar="REGEX",
                    help="only analyse source files whose path matches REGEX")
    ap.add_argument("--class", dest="klass", metavar="NAME", action="append",
                    help="restrict report to these class name(s); repeatable")
    ap.add_argument("--json", metavar="FILE",
                    help="also write machine-readable results to FILE")
    ap.add_argument("-v", "--verbose", action="store_true",
                    help="list every method in each LCOM4 component")
    args = ap.parse_args()

    targets = discover_target_classes()
    if args.klass:
        targets &= set(args.klass)
    flags = compile_flags()
    sources = discover_sources()
    if args.only:
        rx = re.compile(args.only)
        sources = [s for s in sources if rx.search(str(s))]

    print(f"Analysing {len(sources)} translation unit(s); "
          f"{len(targets)} target class name(s).")
    classes: dict[str, ClassData] = {}
    for i, src in enumerate(sources, 1):
        print(f"  [{i}/{len(sources)}] {src.relative_to(REPO_ROOT)}", flush=True)
        ast = dump_ast(src, flags, args.clang)
        if ast is None:
            continue
        process_tu(ast, targets, classes, args.include_static)
        del ast  # free the (large) tree before the next TU

    results = [compute_metrics(c) for c in classes.values()]
    print_report(results, args.verbose)

    if args.json:
        payload = [
            {
                "class": m.name, "methods": m.n_methods, "fields": m.n_fields,
                "lcom4": m.lcom4, "lcom_hs": m.lcom_hs,
                "lcom1": m.lcom1, "lcom2": m.lcom2,
                "components": m.components,
            }
            for m in sorted(results, key=lambda x: x.lcom4, reverse=True)
            if m.n_methods >= 2
        ]
        Path(args.json).write_text(json.dumps(payload, indent=2))
        print(f"\nWrote JSON results to {args.json}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
