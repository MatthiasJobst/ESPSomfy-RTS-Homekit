# LCOM cohesion analysis

`lcom_analysis.py` measures the **Lack of Cohesion of Methods** of the C++
classes under `main/somfy/**`. It parses the real Clang JSON AST (so field vs.
local vs. free-function references are resolved exactly), and needs only the
system `clang` — no Python libclang bindings, no installs.

## Run

```bash
# Configure the host unit tests once so ArduinoJson is fetched:
cmake -B test/unit/build -S test/unit       # (skip if already built)

python3 tools/lcom/lcom_analysis.py                 # full report
python3 tools/lcom/lcom_analysis.py --json out.json # + machine-readable
python3 tools/lcom/lcom_analysis.py --class SomfyShade -v
python3 tools/lcom/lcom_analysis.py --only 'shade/'
```

A full run dumps each translation unit's AST (~600 MB each, processed one at a
time) and takes ~2-3 minutes.

## Metrics

| Metric  | Meaning | Good |
|---|---|---|
| **LCOM4** | connected components of the method graph (edge = share a field **or** one calls the other) | `1` = cohesive; `>=2` = splits into that many responsibilities |
| **LCOM-HS** | Henderson-Sellers, `(m - mean_methods_per_field) / (m - 1)` | `0` cohesive … `1` incohesive |
| **LCOM1/2** | Chidamber-Kemerer pair counts (field sharing only) | lower is better |

LCOM4 is the actionable one: when it is `>=2`, the report lists each component's
methods — those groups are candidate seams for splitting the class.

## Conventions & caveats

- **Constructors, destructors and static methods are excluded by default**
  (constructors touch every field and would mask real cohesion). Use
  `--include-ctors` is not provided; static can be re-enabled with
  `--include-static`.
- **Inherited fields are not counted toward a subclass.** `SomfyShade` accesses
  `SomfyRemote`'s fields; those references attach to `SomfyRemote`, not
  `SomfyShade`. This follows the classic LCOM convention (only fields *declared*
  in the class count) but means delegating/derived methods can show up as their
  own component.
- **Facade/delegation helpers look incohesive by design.** Classes that act on a
  back-pointer parameter rather than their own fields (e.g. some
  `Somfy*Transmitter`) have few shared-field edges, so each method is its own
  component. That is the metric working as intended, not a parsing error.
- A "1 big component + several singletons" result usually means a cohesive core
  plus a few accessors/`save()`-style methods that touch a unique field — read
  the component list before treating the LCOM4 number as a mandate to split.
