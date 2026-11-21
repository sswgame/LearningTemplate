# Constants

> 결론: 한 의미는 한 곳에만 정의한다(한 `.cpp` → `XxxInternal`, 한 모듈 → 소유 타입, 모듈 사이 → 계약 헤더). 게임플레이 튜닝 값은 데이터다. 이름 없는 리터럴 수는 린트 `magic-literals` 래칫이 막는다.

## Constants — where a constant lives

A second copy compiles, passes tests, and drifts the day one side changes.

| Used in | Put it in |
| --- | --- |
| One `.cpp` | That file's `XxxInternal` struct as `static constexpr` (or inside the function). Do not hoist it "in case" |
| One module | The owning type (`MeshAssetFormat::kExtension`) or the module namespace in its types header (`audio::kSampleRate`). No per-folder `*Constants.h` |
| A contract between modules or backends | `sw::constant::<category>` in its layer's one file: `Core/Foundation/Constants.h`, `Engine/Foundation/EngineDefines.h`, `RHIDescriptors.h` (`render`), `ReflectionConstants.h`; shader ↔ C++ in `binding_slots.hlsli` (read as `shaderslot::k*`). A per-side copy with a "must equal X" comment is the bug |

### Values with one home

| Value | Home |
| --- | --- |
| π family, √2, e | `MathUtil` (shaders: `common.hlsli`) |
| Gravity | `PhysicsSystem::getConfiguredGravity(Magnitude)` |
| Golden ratio, splitmix64, FNV-1a | `HashUtil` |
| Four-character tags | `FourCCUtil::make( "SWHF" )` |
| Time and byte units | `constant::time::` · `constant::memory::`; never `1e9`, `1024 * 1024` |
| Default fixed step / catch-up limit | `constant::time::kDefaultFixedStepSeconds` (never `1.0f / 60.0f`) · `kDefaultMaxFrameSeconds` |
| Port range | `constant::network::kMaxPort` |
| Limits several online modules share | `OnlineLimit.h` |
| Buffer and path sizes | `constant::buffer::kMax16` .. `kMax8192`, `constant::file::kMaxPathSize`; never `char buf[64]` |

### Sentinels and aliases

| Meaning of `-1` | Form |
| --- | --- |
| No index / not found / not known yet | `constant::invalid_index::kInt32` (`kInt64` · `kUint32` … by the slot's type) or a domain name defined from it. A value never stored or sent is `sw::optional` ([Errors](Errors.md)) |
| Unlimited, "all", "use the default" | A named constant on the owning type plus a predicate (`ShopStockDef::kUnlimitedStock` + `hasFiniteStock`) — not `invalid_index` |
| Sign, direction, `-1..1` ranges | Arithmetic — stays `-1` |
| External API or protocol value | Stays literal at the call |

- Naming a sentinel never changes its saved or replicated bytes.
- A reflected `FUNCTION` default argument stays a literal. An all-bits mask is not a sentinel.
- **No aliases** (`kFrameCount = constant::render::kMaxFrameCountInFlight;`); a local name only when it adds meaning.

### Tuning values are data

- Gameplay tuning (speeds, distances, chances, intervals): a `PROPERTY` defaulted in the constructor, or a field of the kit's settings / catalog struct.
- Performance thresholds: named constants next to their use; one that must change without a rebuild becomes a `gv_*`.
- Asset paths are data: no `"engine/…"` literal in code.
- A missing required value stops with an error — no fallback (`*OrDefault`, `fallback` parameters, discarded load results).

### Names

- `kPascalCase`. A value with a unit says it unless the type does (`kTimeoutSeconds`).
- Format versions stay with their format even when the numbers match.
- A file or blob format declares `kMagic` · `kVersion` · `kExtension` on its owning type; a state section `kStateTag` · `kStateVersion`.

### Literals that stay literal

`0` · `1` · `-1` · `2` in obvious uses, test expectations, initializer-table rows, log and assert strings, reflection metadata. Anything else that repeats or needs a comment gets a name.

### The ratchet

Lint `magic-literals` (commit hook) counts unnamed literals per staged C++ file against `Config/Rules/Lint/MagicLiteralsBaseline.json`: grown → name it or move it to data; shrunk → lower the baseline with `py -3 -m Scripts lint magic-literals --ratchet`. Raise a baseline only with `--accept <file…>` (integrator merging pre-rule code). A false positive gets a `--self-check` case, not a baseline bump.
