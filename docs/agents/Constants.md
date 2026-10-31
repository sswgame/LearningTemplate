> 질문: 상수는 어디에 두고 어떻게 이름 붙이는가?
> 독자: 상수나 튜닝 값을 더하거나 옮기는 사람.
> 결론: 한 의미는 한 곳에만 정의한다. 한 `.cpp` 면 그 파일의 `XxxInternal`, 한 모듈이면 소유 타입, 모듈 사이 계약이면 계약 헤더. 게임플레이 튜닝 값은 상수가 아니라 데이터다.

## Constants — where a constant lives

One meaning, one definition. A second copy of a value compiles, passes tests, and drifts the day one side changes.

| Used in | Put it in |
| --- | --- |
| One `.cpp` | That file's `XxxInternal` struct as `static constexpr` (or inside the function). Do not hoist it "in case" |
| One module | The owning type (`MeshAssetFormat::kExtension`) or the module namespace in its types header (`audio::kSampleRate` in `AudioTypes.h`). No per-folder `*Constants.h` |
| A contract between modules or backends | The contract header: `RHIDescriptors.h` `constant` block (backend ↔ backend), `binding_slots.hlsli` (shader ↔ C++, pure `#define` numeric literals, read as `shaderslot::k*`), `Core/Foundation/Defines.h` · `Engine/Foundation/EngineDefines.h` `constant` (engine-wide). A per-side copy with a "must equal X" comment is the bug |

### Values with one home

| Value | Home |
| --- | --- |
| π family, √2, e | `MathUtil` (shaders: `common.hlsli`) |
| Gravity | `PhysicsSystem::getConfiguredGravity(Magnitude)` |
| Golden ratio, splitmix64, FNV-1a | `HashUtil` |
| Four-character tags | `FourCcUtil::make( "SWHF" )` |
| Time and byte units | `constant::` in `Core/Foundation/Defines.h` (`kNanosecondsPerSecond`, `kBytesPerMegabyte` ...); never `1e9`, `1024 * 1024`, a local `kKilobyte` |
| Limits several online modules share | `OnlineLimit.h` |
| Buffer and path sizes | `constant::kMaxBuffer16` .. `kMaxBuffer8192`, `kMaxPathSize`; never `char buf[64]` |

Do not retype these literals elsewhere.

### Sentinels and aliases

- A sentinel keeps a domain name but takes its value from `invalid_index` (`static constexpr uint32 kNotRegistered = invalid_index::kUint32;`). An all-bits mask (`kAllLayers`) is not a sentinel and keeps its literal.
- **No aliases.** `kFrameCount = constant::kMaxFrameCountInFlight;` is a second name for one concept; use the original. A local name is fine only when it adds meaning (`kUnitQuadHalfDiagonal = MathUtil::kInvSqrt2`).

### Tuning values are data

- Gameplay tuning (speeds, distances, chances, intervals, model scales): a `PROPERTY` on the component (default in the constructor), or a field of the settings / catalog struct a kit takes from the game.
- Performance thresholds (spin counts, parallel thresholds, buffer and pool limits): named constants next to their use; one that must change without a rebuild becomes a `gv_*`.
- Asset paths are data too: no `"engine/…"` literal in code.
- A missing required value stops with an error. No fallback (`*OrDefault`, `fallback` parameters, discarded load results); examples in `docs/04_CodingGuidelines.md` 4.

### Names

- `kPascalCase`, including `MathUtil`.
- Same meaning, same name across formats: a file or blob format declares `kMagic` · `kVersion` · `kExtension` on its owning type; a state section declares `kStateTag` · `kStateVersion`; a second format in one scope prefixes the format (`kBinMagic`).
- A value with a unit says it in the name unless the type does (`kTimeoutSeconds`, `kMaxBytes`).

### Literals that stay literal

`0` · `1` · `-1` · `2` in obvious uses, test expectations, rows of an initializer table, log and assert strings, reflection metadata (`PROPERTY( Max = ... )`; the parser reads literals). Anything else that repeats or needs a comment to be understood gets a name.
