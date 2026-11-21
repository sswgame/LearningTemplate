# Errors

> 결론: 값을 못 줄 수 있는 함수는 `sw::expected<T, E>`(`Core/Foundation/Expected.h`) 한 벌을 돌려준다. 이유가 "없다" 하나면 `sw::optional<T>`, 둘 이상이면 `expected<T>`(오류 `sw::Error`), 값 없는 성공은 `expected<void>`. 린트 `result-types` 강제(커밋 훅, 예외 `Config/Rules/Lint/ResultTypes.json` `_exemptions`).

## Errors — what a fallible function returns

`sw::expected` has the C++23 `std::expected` interface and is `[[nodiscard]]`. One type for "absent" and "failed" (Unreal needs two: `TValueOrError` · `TOptional`).

| The function can fail because … | Return | Failure |
| --- | --- | --- |
| the thing is not there (one reason) | `sw::optional<T>` (= `expected<T, Absent>`) | `return nullopt;` |
| several reasons, or the caller reports why | `sw::expected<T>` | `return unexpected( Error{ ErrorKind::InvalidArgument, "not a boolean", value } );` |
| several reasons, no value | `sw::expected<void>` | success `return {};` |
| the reason carries data the caller needs | `sw::expected<T, DomainError>` | `return unexpected( DomainError{ … } );` |

### `sw::Error`

- Branch on `kind()` (`ErrorKind`), never on the text. `message()` is a fixed English literal; `detail()` (key, value, name) is copied inline — no heap on the failure path.
- Log where it is reported: `SW_LOG_ERROR_ID( "Area.Event", "%#: %# (%#)", key, error.message(), error.detail() )`.
- Add context on the way out with `error.prependDetail( ": " ).prependDetail( path )`; keep the inner literal.

### Rules

- **No output parameter for the reason** (`string& outError`, `string* pOutError`, a text output written only before a failure return). Other outputs stay. A check collecting several problems fills a caller's list or returns a report as the error.
- A reply text that answers on success *and* failure (`outReply` of a dev or console command) stays.
- **No sentinel for "none".** A lookup (`find*` · `pick*` · `select*`) returns `sw::optional<uint32>` (`optional<int32>` where the API indexes in `int32`), never -1 · 0 · `size()`.
  - A closed set of slots is a scoped enum with a none enumerator; "one of a set or nothing" is `optional<Enum>`; a field that may be missing stores `optional`.
  - Several special values in one `int` become a kind enum plus the index; a flag beside -1 fields becomes `optional<Struct>`.
  - An API whose "none" means "nothing to do" takes the optional, so callers pass the lookup through.
  - Only a stored data or wire format keeps its -1: name it (`kNoSegment`) and write it with `value_or`.
- **No `std::optional`.** Wrap a third-party `std::optional` API where it is called.
- **No fallback through `value_or`** for a required value ([Constants](Constants.md)); `value_or` is for lookups where absent is normal.
- Wrong-side access (`value()` on an error, `error()` on a value) stops the process.
- MSVC `<eh.h>` declares a global `unexpected()`: inside `namespace sw` write `unexpected( … )`, with only `using namespace sw;` (tests) write `sw::unexpected( … )`.
