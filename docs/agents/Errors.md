> 질문: 값을 못 줄 수 있는 함수는 무엇을 돌려주는가?
> 독자: 실패하거나 "없음" 을 돌려줄 수 있는 함수를 새로 쓰거나 고치는 사람.
> 결론: 한 벌 `sw::expected<T, E>`(`Core/Foundation/Expected.h`)다. 이유가 "없다" 하나면 `sw::optional<T>`, 둘 이상이면 `expected<T>`(오류 `sw::Error`), 값 없는 성공은 `expected<void>`. `string& outError` 출력 인자와 `std::optional` 은 쓰지 않는다.

## Errors — what a fallible function returns

One result type: "absent" is a failure with exactly one reason, so it is the same type. `sw::expected` has the C++23 `std::expected` interface (`has_value` · `operator bool` · `value` · `error` · `value_or` · `*` · `->` · `and_then` · `transform` · `or_else` · `unexpected<E>`) and is `[[nodiscard]]`.

| The function can fail because … | Return | Build a failure with |
| --- | --- | --- |
| the thing is not there — one reason, the caller does not branch on why | `sw::optional<T>` (= `expected<T, Absent>`) | `return nullopt;` |
| several reasons, or the caller must report why | `sw::expected<T>` (error `sw::Error`) | `return unexpected( Error{ ErrorKind::InvalidArgument, "not a boolean", value } );` |
| several reasons and no value | `sw::expected<void>` | success `return {};` |
| the reason carries data the caller needs | `sw::expected<T, DomainError>` | `return unexpected( DomainError{ … } );` |

### `sw::Error`

- `kind()` (`ErrorKind`: `InvalidArgument` · `NotFound` · `AlreadyExists` · `Cycle` · `Unsupported` · `ExternalFailure`) — what the caller branches on, never the text.
- `message()` — a string literal, fixed English like a log id's text.
- `detail()` — what was wrong (key, value, name), copied inline (`Error::kDetailCapacity`, truncated). No heap on the failure path.
- The caller turns it into text where it logs: `SW_LOG_ERROR_ID( "Area.Event", "%#: %# (%#)", key, error.message(), error.detail() )`.
- Adding context on the way out: keep the inner literal and prepend the place, `error.prependDetail( ": " ).prependDetail( path )`. A step-level formatter (`AutomationStep::describeError`) adds the location once.

### Rules

- **No output parameter for the reason.** Not `string& outError` / `outReason`, not `string* pOutError` (a `nullptr` caller drops the reason), not a text output under any name that the body writes only right before a failure return (lint kinds `out-error-pointer` · `out-text-on-failure`). Other outputs (filling a list the caller owns) stay. A check that collects several problems fills a caller's list (`CharacterIssueList& outIssues`) or returns a report as the error (`expected<void, AppearanceLoadReport>`).
- **A reply text is not a result type.** `outReply` of a dev command, console command or editor model (`DevCommandFunc`) is the answer on success *and* failure, so it stays; a helper that only finds a target returns it (or `nullptr`) and the command writes the reply.
- **No sentinel for "none".** A lookup (`find*` · `pick*` · `select*`) returns `sw::optional<uint32>` (or `optional<int32>` where the API indexes in `int32`), never -1 · 0 · `kUint32` · `size()`.
  - A closed set of slots is a scoped enum with a none enumerator (`ReflectBuiltinIndex::kNone`); "one of a closed set or nothing" is `optional<Enum>`. A table or result field whose entries may be missing stores `optional` (`HitInfo::_bodyIndex`, `SRPGForecast::_supportDefender`).
  - Several special values packed into one `int` become a kind enum plus the index (`RigInstance::TargetAnchor`); a flag beside -1 fields becomes `optional<Struct>` (`SRPGCommanderAIPlan::_attack`).
  - An API whose "none" means "nothing to do" takes the optional (`ControlIntent::isDown( optional<int32> )`), so callers pass the lookup through.
  - Only a stored data or wire format keeps its -1 (skeleton bone parent, prefab node parent, `serializeState` kit state, AT-SPI `GetIndexInParent` — Unreal `INDEX_NONE`, Godot bone parent). Name it (`kNoSegment`) and write it with `value_or` where the lookup feeds it.
- **No `std::optional`.** `sw::optional<T>` does not convert to or from it; wrap a third-party `std::optional` API where it is called. A default-constructed `sw::optional` is empty; `sw::expected<T, E>` has no default constructor.
- **No fallback through `value_or`** for a required value ([Constants](Constants.md)); `value_or` is for lookups where absent is normal.
- **Wrong-side access stops the process.** `value()` on an error and `error()` on a value call `SW_LOW_LEVEL_FATAL`; `*` / `->` assert in Debug.
- **`unexpected` name collision:** MSVC `<eh.h>` declares a global `unexpected()`. Inside `namespace sw` write `unexpected( … )`; in a file with only `using namespace sw;` (tests) write `sw::unexpected( … )`.
- **Enforced:** `py -3 -m Scripts lint result-types` (commit hook) keeps Core at 0 and the rest at the empty baseline `Config/Rules/Lint/ResultTypeBaseline.json`; per-file exemptions with a reason go in its `_coreExemptions`.

### Compared with the engines

| | Unreal | This codebase |
| --- | --- | --- |
| Value or reason | `TValueOrError<T, E>` (`MakeValue` · `MakeError`) | `sw::expected<T, E>` (`return value;` · `return unexpected( e );`) |
| Value or nothing | `TOptional<T>` (a second type) | `sw::optional<T>` = `expected<T, Absent>` — one type, one set of chaining rules |
| Error payload | often `FText` / `FString` (allocates) | `sw::Error`: kind + literal + fixed inline detail |
| Void success | `TValueOrError<void, E>` | `expected<void, E>` |

Godot's `Error` codes plus out parameters and Unity's `bool TryX( out T )` are the shapes this rule removes.
