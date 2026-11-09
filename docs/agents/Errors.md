> 질문: 값을 못 줄 수 있는 함수는 무엇을 돌려주는가?
> 독자: 실패하거나 "없음" 을 돌려줄 수 있는 함수를 새로 쓰거나 고치는 사람.
> 결론: 한 벌 `sw::expected<T, E>`(`Core/Foundation/Expected.h`)다. 이유가 하나("없다")뿐이면 별칭 `sw::optional<T>`, 둘 이상이면 `expected<T>`(기본 오류 `sw::Error`), 값 없는 성공은 `expected<void>`. `bool f( …, string& outError )` 출력 인자와 `std::optional` 은 쓰지 않는다(Core 0, 그 밖은 커밋 훅 래칫).

## Errors — what a fallible function returns

One result type for the whole codebase (user decision 2026-11-08: "absent" is a failure with exactly one reason, so it is the same type). C++17 has no `std::expected`; `sw::expected` has the same interface: `has_value` · `operator bool` · `value` · `error` · `value_or` · `operator*` · `operator->` · `and_then` · `transform` · `or_else` · `unexpected<E>`. The type is `[[nodiscard]]`, so a dropped result is a compile error.

| The function can fail because … | Return | Build a failure with |
| --- | --- | --- |
| the thing is not there (lookup, parse of an optional field, "not yet") — one reason, the caller does not branch on why | `sw::optional<T>` (= `expected<T, Absent>`) | `return nullopt;` |
| several reasons, or the caller must report why | `sw::expected<T>` (error `sw::Error`) | `return unexpected( Error{ ErrorKind::InvalidArgument, "not a boolean", value } );` |
| several reasons and no value | `sw::expected<void>` | success `return {};` |
| the reason carries data the caller needs (indices, a list, a nested report) | `sw::expected<T, DomainError>` with a domain struct | `return unexpected( DomainError{ … } );` |

### `sw::Error`

- `kind()` (`ErrorKind`: `InvalidArgument` · `NotFound` · `AlreadyExists` · `Cycle` · `Unsupported` · `ExternalFailure`) — what the caller branches on, never the text.
- `message()` — a string literal (static storage), fixed English like a log id's text.
- `detail()` — what was wrong (the key, the value, the name), copied into the error (`Error::kDetailCapacity` bytes, truncated). No heap on the failure path; the error outlives the input it describes.
- The caller turns it into text where it logs: `SW_LOG_ERROR_ID( "Area.Event", "%#: %# (%#)", key, error.message(), error.detail() )`.

### Rules

- **No output parameter for the reason.** `bool f( …, string& outError )` / `outReason` is gone: the reason is the return value. Other output parameters (filling a list the caller owns) stay.
  - The same defect in other spellings counts too (lint kinds `out-error-pointer` · `out-text-on-failure`): an optional pointer `string* pOutError` / `pOutReason` / `pOutMessage` (a caller passing `nullptr` silently drops the reason), and a text output under any name (`outReply`, `outUnknown`, `outFailureText` …) that the body writes only right before `return false` / `nullptr` / `nullopt` / `unexpected`.
- **No `std::optional`.** Use `sw::optional<T>`. A default-constructed `sw::optional` is empty; `sw::expected<T, E>` has no default constructor (a result is a value or a reason, never "nothing yet" — Unreal `TValueOrError` is the same).
- **No fallback through `value_or`** for a required value — a missing required value is an error (Constants.md). `value_or` is for "absent is normal" lookups that pick a default by design.
- **Wrong-side access stops the process.** `value()` on an error and `error()` on a value call `SW_LOW_LEVEL_FATAL` (no exceptions in the engine); `operator*` / `operator->` assert in Debug, like `std`.
- **A reply text is not a result type.** `outReply` of a dev command, console command or editor model (`DevCommandFunc`) is the user-visible answer on success *and* failure, so it stays. A string output that is filled only on failure is the out-error shape whatever its name (`outReason`, `outMessage`, a `string&` member a helper writes into) and moves to `expected`; the caller builds the reply from `message()` · `detail()`.
- **Adding context on the way out.** An outer function that knows *where* (which file, material, step) keeps the inner literal and puts the place in front of the detail: `error.prependDetail( ": " ).prependDetail( path )` (fixed storage, the tail of the old detail is dropped on overflow). A step-level formatter such as `AutomationStep::describeError` adds the location when the text is built, so step functions never repeat it.
- **Name collision with the C runtime:** MSVC's `<eh.h>` declares a global `unexpected()`. Inside `namespace sw` write `unexpected( … )`; in a file that only has `using namespace sw;` (tests) write `sw::unexpected( … )`.

### Compared with the engines

| | Unreal | This codebase |
| --- | --- | --- |
| Value or reason | `TValueOrError<T, E>` (`MakeValue` · `MakeError`, `HasValue` · `GetValue` · `GetError`) | `sw::expected<T, E>` (`return value;` · `return unexpected( e );`) |
| Value or nothing | `TOptional<T>` (a second type) | `sw::optional<T>` = `expected<T, Absent>` — the same type, so chaining (`and_then` · `transform`) and the rules are one set |
| Error payload | often `FText` / `FString` (allocates) | `sw::Error`: kind + literal + fixed inline detail (no allocation) |
| Void success | `TValueOrError<void, E>` | `expected<void, E>` |

Godot returns `Error` codes plus out parameters; Unity (C#) uses exceptions or `bool TryX( out T )`. Both are the shapes this rule removes.

### Migration state

- `Source/Core` is migrated: no `std::optional`, no `string& outError` (enforced as 0 by `py -3 -m Scripts lint result-types`, run by the commit hook). Exemptions are data, per file and kind, in the baseline's `_coreExemptions` with a reason (`Expected.h`'s bridge, one pre-rule failure-only text output).
- Outside Core the remaining count per file is in `Config/Rules/Lint/ResultTypeBaseline.json`: it may only go down. When a file drops, run `py -3 -m Scripts lint result-types --ratchet` and commit the baseline with it.
- Until the baseline is empty, `sw::optional<T>` and `std::optional<T>` convert implicitly both ways, so a migrated caller can receive a not-yet-migrated `std::optional` result and the other way round (folders migrate on separate branches). Because both ways convert, a conditional that mixes the two is ambiguous — wrap one side (`cond ? optional<T>{ f() } : nullopt`). Delete both conversions (marked `shortcut:` in `Expected.h`) with the last baseline entry.
