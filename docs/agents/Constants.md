> 질문: 상수는 어디에 두고 어떻게 이름 붙이는가?
> 독자: 상수나 튜닝 값을 더하거나 옮기는 사람.
> 결론: 한 의미는 한 곳에만 정의한다. 한 `.cpp` 면 그 파일의 `XxxInternal`, 한 모듈이면 소유 타입, 모듈 사이 계약이면 계약 헤더. 게임플레이 튜닝 값은 상수가 아니라 데이터다. 일반 코드의 이름 없는 리터럴 수는 파일마다 기준선을 넘지 못한다(커밋 훅 래칫).

## Constants — where a constant lives

One meaning, one definition. A second copy of a value compiles, passes tests, and drifts the day one side changes.

| Used in | Put it in |
| --- | --- |
| One `.cpp` | That file's `XxxInternal` struct as `static constexpr` (or inside the function). Do not hoist it "in case" |
| One module | The owning type (`MeshAssetFormat::kExtension`) or the module namespace in its types header (`audio::kSampleRate` in `AudioTypes.h`). No per-folder `*Constants.h` |
| A contract between modules or backends | `sw::constant::<category>` in its layer's one file: `Core/Foundation/Constants.h` (`buffer` · `file` · `time` · `memory` · `log` · `network` · `path` · `invalid_index`), `Engine/Foundation/EngineDefines.h` (`window` · `physics` · `locale` · engine `path`), `RHIDescriptors.h` (`render`, backend ↔ backend — needs RHI types), `ReflectionConstants.h` (`reflection`); `binding_slots.hlsli` (shader ↔ C++, pure `#define` numeric literals, read as `shaderslot::k*`). A new category goes in the layer's file, not a new header. A per-side copy with a "must equal X" comment is the bug |

### Values with one home

| Value | Home |
| --- | --- |
| π family, √2, e | `MathUtil` (shaders: `common.hlsli`) |
| Gravity | `PhysicsSystem::getConfiguredGravity(Magnitude)` |
| Golden ratio, splitmix64, FNV-1a | `HashUtil` |
| Four-character tags | `FourCCUtil::make( "SWHF" )` |
| Time and byte units | `constant::time::` (`kNanosecondsPerSecond`, `kMillisecondsPerSecond` ...) · `constant::memory::` (`kBytesPerMegabyte` ...); never `1e9`, `1024 * 1024`, a local `kKilobyte` or `kMillisecondsPerSecond` |
| Default fixed simulation step | `constant::time::kDefaultFixedStepSeconds` (60 Hz); never a retyped `1.0f / 60.0f` |
| Default per-frame catch-up limit of a fixed-step world | `constant::time::kDefaultMaxFrameSeconds` (`FixedStepTimer` second argument); a world that must take more (fast-forward) passes its own named value |
| Port range | `constant::network::kMaxPort` |
| Limits several online modules share | `OnlineLimit.h` |
| Buffer and path sizes | `constant::buffer::kMax16` .. `kMax8192`, `constant::file::kMaxPathSize`; never `char buf[64]` |

Do not retype these literals elsewhere.

### Sentinels and aliases

A `-1` says one of several things; each has one form (Unreal: `INDEX_NONE` · `TOptional`):

| Meaning | Form |
| --- | --- |
| No index / not found / no subject in an event or a record / not known yet | `constant::invalid_index::kInt32` (`kInt64` · `kUint32` … by the slot's type) in code, or a domain name whose value comes from it (`static constexpr int32 kNoTeam = constant::invalid_index::kInt32;`). A value that is never stored or sent may be `sw::optional` instead ([Errors](Errors.md)) |
| Unlimited, endless, "all", "use the default" | A named constant on the owning type with a predicate when code branches on it (`ShopStockDef::kUnlimitedStock` + `hasFiniteStock`, `MonsterBattle::kEndlessWeather`, `AbilityComponent::kAllStacks`) — not `invalid_index`: it is not an index |
| Sign, direction, delta, a loop that starts below zero, `-1..1` ranges | Arithmetic — stays `-1` (the ratchet does not count sign ternaries and `clamp( x, -1, 1 )`) |
| An external API or protocol value (POSIX descriptors, OpenSSL `-1` = append, RESP null) | Stays literal at the call |

- A value that is saved or replicated keeps its bytes: naming a sentinel never changes its value (`ConstantsTest` pins `kInt32 == -1` and the narrowing and unsigned conversions).
- A reflected `FUNCTION` default argument stays a literal: the reflection parser copies the argument text into generated code.
- An all-bits mask (`kAllLayers`) is not a sentinel and keeps its literal.
- **No aliases.** `kFrameCount = constant::render::kMaxFrameCountInFlight;` is a second name for one concept; use the original. A local name is fine only when it adds meaning (`kUnitQuadHalfDiagonal = MathUtil::kInvSqrt2`).

### Tuning values are data

- Gameplay tuning (speeds, distances, chances, intervals, model scales): a `PROPERTY` on the component (default in the constructor), or a field of the settings / catalog struct a kit takes from the game.
- Performance thresholds (spin counts, parallel thresholds, buffer and pool limits): named constants next to their use; one that must change without a rebuild becomes a `gv_*`.
- Asset paths are data too: no `"engine/…"` literal in code.
- A missing required value stops with an error. No fallback (`*OrDefault`, `fallback` parameters, discarded load results); examples in `docs/04_CodingGuidelines.md` 4.

### Names

- `kPascalCase`, including `MathUtil`.
- Format versions stay with their format even when the numbers match (`kStateVersion = 1` in seventy places is seventy formats, not one value): merging them would bump every format when one changes.
- Same meaning, same name across formats: a file or blob format declares `kMagic` · `kVersion` · `kExtension` on its owning type; a state section declares `kStateTag` · `kStateVersion`; a second format in one scope prefixes the format (`kBinMagic`).
- A value with a unit says it in the name unless the type does (`kTimeoutSeconds`, `kMaxBytes`).

### Literals that stay literal

`0` · `1` · `-1` · `2` in obvious uses, test expectations, rows of an initializer table, log and assert strings, reflection metadata (`PROPERTY( Max = ... )`; the parser reads literals). Anything else that repeats or needs a comment to be understood gets a name.

### The ratchet — literals cannot grow

`Scripts/lint/MagicLiterals.py` runs in the commit hook. For every staged C++ file it counts the **unnamed** `-1`, float, two-digit-or-longer integer and hex literals in ordinary code and compares each count with `Config/Dev/MagicLiteralBaseline.json` (one line per file).

| The count | Then |
| --- | --- |
| Grew | The commit stops. Name the value (`XxxInternal::k…`, the owning type, a `constant::` contract) or move it into data (a field of a settings / catalog struct, a `PROPERTY` defaulted in the constructor) |
| Shrank | The commit stops until the baseline is lowered with it: `py -3 -m Scripts lint magic-literals --ratchet` (lowers only, drops deleted files) |
| A moved file | Keeps the old path's baseline through the staged rename |

Not counted, by place: tests, generated files, reflection metadata, format numbers (`kVersion` · `kMagic` · `kTag`), named definitions (`constexpr … kName = …`), rows of an initializer table, and field defaults (`float32 _speed{ 3.5f };`, `, _speed{ 3.5f }` in a constructor — the home of tuning values; a `-1` there is still counted as a sentinel). Not counted, by value: `0` · `1` · `2` · `0.5` floats, all-bits hex masks, shift widths (`<< 16`), sign and direction `-1`, OS-contract `-1`. `py -3 -m Scripts lint magic-literals --self-check` pins every one of these; a new false-positive shape gets a case there, not a baseline bump.

Raising a baseline is `--accept <file…>` and nothing else — for an integrator merging code written before the rule; the JSON diff shows it. `py -3 -m Scripts lint magic-literals` prints the totals and the `-1` meanings; `--meaning <m>` lists those lines.
