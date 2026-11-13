> 질문: 상수는 어디에 두고 어떻게 이름 붙이는가?
> 독자: 상수나 튜닝 값을 더하거나 옮기는 사람.
> 결론: 한 의미는 한 곳에만 정의한다(한 `.cpp` → `XxxInternal`, 한 모듈 → 소유 타입, 모듈 사이 → 계약 헤더). 게임플레이 튜닝 값은 데이터다. 이름 없는 리터럴 수는 파일마다 기준선을 넘지 못한다(커밋 훅 래칫).

## Constants — where a constant lives

One meaning, one definition: a second copy compiles, passes tests, and drifts the day one side changes.

| Used in | Put it in |
| --- | --- |
| One `.cpp` | That file's `XxxInternal` struct as `static constexpr` (or inside the function). Do not hoist it "in case" |
| One module | The owning type (`MeshAssetFormat::kExtension`) or the module namespace in its types header (`audio::kSampleRate` in `AudioTypes.h`). No per-folder `*Constants.h` |
| A contract between modules or backends | `sw::constant::<category>` in its layer's one file: `Core/Foundation/Constants.h` (`buffer` · `file` · `time` · `memory` · `log` · `network` · `path` · `invalid_index`), `Engine/Foundation/EngineDefines.h` (`window` · `physics` · `locale` · engine `path`), `RHIDescriptors.h` (`render`), `ReflectionConstants.h` (`reflection`); shader ↔ C++ in `binding_slots.hlsli` (pure `#define` numbers, read as `shaderslot::k*`). A new category goes in the layer's file. A per-side copy with a "must equal X" comment is the bug |

### Values with one home

Do not retype these literals elsewhere.

| Value | Home |
| --- | --- |
| π family, √2, e | `MathUtil` (shaders: `common.hlsli`) |
| Gravity | `PhysicsSystem::getConfiguredGravity(Magnitude)` |
| Golden ratio, splitmix64, FNV-1a | `HashUtil` |
| Four-character tags | `FourCCUtil::make( "SWHF" )` |
| Time and byte units | `constant::time::` (`kNanosecondsPerSecond`, `kMillisecondsPerSecond` …) · `constant::memory::` (`kBytesPerMegabyte` …); never `1e9`, `1024 * 1024` or a local copy |
| Default fixed step / per-frame catch-up limit | `constant::time::kDefaultFixedStepSeconds` (60 Hz, never `1.0f / 60.0f`) · `kDefaultMaxFrameSeconds` (`FixedStepTimer` second argument; a world that must take more passes its own named value) |
| Port range | `constant::network::kMaxPort` |
| Limits several online modules share | `OnlineLimit.h` |
| Buffer and path sizes | `constant::buffer::kMax16` .. `kMax8192`, `constant::file::kMaxPathSize`; never `char buf[64]` |

### Sentinels and aliases

Each meaning of `-1` has one form (Unreal `INDEX_NONE` · `TOptional`):

| Meaning | Form |
| --- | --- |
| No index / not found / no subject / not known yet | `constant::invalid_index::kInt32` (`kInt64` · `kUint32` … by the slot's type) or a domain name defined from it (`kNoTeam = constant::invalid_index::kInt32`). A value never stored or sent is `sw::optional` instead ([Errors](Errors.md)) |
| Unlimited, endless, "all", "use the default" | A named constant on the owning type, plus a predicate when code branches on it (`ShopStockDef::kUnlimitedStock` + `hasFiniteStock`, `AbilityComponent::kAllStacks`) — not `invalid_index` |
| Sign, direction, delta, `-1..1` ranges | Arithmetic — stays `-1` |
| External API or protocol value (POSIX descriptors, OpenSSL `-1`, RESP null) | Stays literal at the call |

- Naming a sentinel never changes its saved or replicated bytes (`ConstantsTest` pins `kInt32 == -1` and its conversions).
- A reflected `FUNCTION` default argument stays a literal (the parser copies the text). An all-bits mask (`kAllLayers`) is not a sentinel.
- **No aliases** (`kFrameCount = constant::render::kMaxFrameCountInFlight;`) — use the original. A local name is fine only when it adds meaning (`kUnitQuadHalfDiagonal = MathUtil::kInvSqrt2`).

### Tuning values are data

- Gameplay tuning (speeds, distances, chances, intervals, scales): a `PROPERTY` defaulted in the component's constructor, or a field of the settings / catalog struct a kit takes from the game.
- Performance thresholds (spin counts, parallel thresholds, pool limits): named constants next to their use; one that must change without a rebuild becomes a `gv_*`.
- Asset paths are data: no `"engine/…"` literal in code.
- A missing required value stops with an error — no fallback (`*OrDefault`, `fallback` parameters, discarded load results).

### Names

- `kPascalCase`, including `MathUtil`. A value with a unit says it unless the type does (`kTimeoutSeconds`, `kMaxBytes`).
- Format versions stay with their format even when the numbers match: seventy `kStateVersion = 1` are seventy formats.
- Same meaning, same name: a file or blob format declares `kMagic` · `kVersion` · `kExtension` on its owning type; a state section `kStateTag` · `kStateVersion`; a second format in one scope prefixes it (`kBinMagic`).

### Literals that stay literal

`0` · `1` · `-1` · `2` in obvious uses, test expectations, initializer-table rows, log and assert strings, reflection metadata (`PROPERTY( Max = ... )`). Anything else that repeats or needs a comment gets a name.

### The ratchet — literals cannot grow

`Scripts/lint/magic_literals.py` runs in the commit hook: per staged C++ file it counts **unnamed** `-1`, float, two-or-more-digit integer and hex literals and compares with `Config/Rules/Lint/MagicLiteralBaseline.json`.

| The count | Then |
| --- | --- |
| Grew | The commit stops. Name the value or move it into data |
| Shrank | The commit stops until the baseline is lowered: `py -3 -m Scripts lint magic-literals --ratchet` |
| A moved file | Keeps the old path's baseline through the staged rename |

- Not counted: tests, generated files, reflection metadata, format numbers, named definitions, initializer-table rows, field defaults (`_speed{ 3.5f }` — a `-1` there still counts), and the values `0` · `1` · `2` · `0.5`, all-bits masks, shift widths, sign and OS-contract `-1`. `--self-check` pins each; a new false positive gets a case there, not a baseline bump.
- Raising a baseline is `--accept <file…>` only (an integrator merging pre-rule code). `py -3 -m Scripts lint magic-literals` prints totals; `--meaning <m>` lists lines.
