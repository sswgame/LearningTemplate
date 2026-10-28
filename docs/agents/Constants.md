> 질문: 상수는 어디에 두고 어떻게 이름 붙이는가?
> 독자: 상수나 튜닝 값을 더하거나 옮기는 사람.
> 결론: 한 의미는 한 곳에만 정의한다. 한 `.cpp` 면 그 파일의 `XxxInternal`, 한 모듈이면 소유 타입, 모듈 사이 계약이면 계약 헤더에 둔다. 게임플레이 튜닝 값은 상수가 아니라 데이터다.

## Constants — where a constant lives

One meaning, one definition. A second copy of a value compiles, passes tests, and drifts the day one side changes.

- **Used in one `.cpp`** → that file's `XxxInternal` struct as `static constexpr` (or inside the function). Do not hoist it "in case".
- **Used across one module** → the owning type as `static constexpr` (`MeshAssetFormat::kExtension`), or the module's namespace in its
  types header (`audio::kSampleRate` in `AudioTypes.h`). No `*Constants.h` grab-bag per folder.
- **A contract between modules or backends** → the contract header: `RHITypes.h` `constant` block (backend ↔ backend),
  `bindingslots.hlsli` (shader ↔ C++, pure `#define` numeric literals, read as `shaderslot::k*`),
  `Core/Common/Defines.h` · `Engine/Common/EngineDefines.h` `constant` (engine-wide). If two sides must agree, both read one definition.
  A per-side copy with a "must equal X" comment is the bug, not the fix.
- **Well-known values have one home**: π family, √2, e → `MathUtil` (shaders: `common.hlsli`); gravity →
  `PhysicsSystem::getConfiguredGravity(Magnitude)`; golden-ratio, splitmix64 and FNV-1a → `HashUtil`; four-character tags →
  `FourCcUtil::make( "SWHF" )`. `CheckWellKnownConstants.py` blocks the literals elsewhere.
- **Time and byte units** → `constant::` in `Core/Common/Defines.h` (`kNanosecondsPerSecond`, `kBytesPerMegabyte` and the other pairs);
  never `1e9`, `1024 * 1024` or a local `kKilobyte`. Limits several online modules must agree on live in `OnlineLimit.h`.
- **Sentinels** keep a domain name but take their value from `invalid_index` (`static constexpr uint32 kNotRegistered = invalid_index::kUint32;`).
  An all-bits mask (`kAllLayers`) is not a sentinel and keeps its literal.
- **No aliases.** `static constexpr uint32 kFrameCount = constant::kMaxFrameCountInFlight;` is a second name for one concept — use the original.
  A local name is fine only when it adds meaning (`kUnitQuadHalfDiagonal = MathUtil::kInvSqrt2`).
- **Gameplay tuning values** (speeds, distances, chances, intervals, model scales) are data: a `PROPERTY` on the component (default in the
  constructor), or a field of the settings / catalog struct a kit takes from the game. Performance thresholds (spin counts, parallel
  thresholds, buffer and pool limits) stay named constants next to their use; one that must change without a rebuild becomes a `gv_*`.
  Asset paths are data too (no `"engine/…"` literal in code). A missing required value stops with an error instead of falling back
  (`*OrDefault`, `fallback` parameters, discarded load results; `CheckDataDrivenStructure.py`; examples: `docs/04_CodingGuidelines.md` 4).
- **Names**: `kPascalCase`, including `MathUtil`. Same meaning, same name across formats: a file or blob format declares `kMagic` · `kVersion` ·
  `kExtension` on its owning type; a state section declares `kStateTag` · `kStateVersion`; a second format in the same scope prefixes the
  format (`kBinMagic`). A value with a unit says it in the name unless the type does (`kTimeoutSeconds`, `kMaxBytes`).
- **Literals that stay literal**: `0` · `1` · `-1` · `2` in their obvious uses, test expectations, rows of an initializer table, log and assert
  strings, reflection metadata (`PROPERTY( Max = ... )` — the parser reads literals). Everything else that repeats or needs a comment to be
  understood gets a name.
