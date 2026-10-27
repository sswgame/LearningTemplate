> 질문: C++ 본문(분기 · 반복 · 생성자 · 타입 · 문자열)은 어떻게 쓰는가?
> 독자: C++ 본문을 쓰거나 고치는 사람.
> 결론: `.clang-format` 을 따르고, 불리언은 `== false` 로 비교하고, 생성자에서만 필드를 초기화하며, 반복문에는 항상 중괄호를 쓴다. 많은 규칙을 `CheckCodeConventions` 가 막는다.

## C++ style

- Follow `.clang-format`.
- A function body goes on its own lines. Only a function defined inside a class body may stay on one line
  (`AllowShortFunctionsOnASingleLine: InlineOnly` — getters and trivial accessors). Free `inline` functions
  (operators, math helpers) and out-of-class definitions are expanded: `{` on the next line, then the body.
- When a constructor exists, initialize fields in the constructor (not in the
  header), in declaration order. Use brace initialization — except an iterator pair, which takes
  parentheses (`_listValue( list.begin(), list.end() )`): braces pick the `initializer_list` constructor and store
  the two iterators as elements (`Style/IteratorPairBraces`). Put one initializer
  per line, with subsequent lines beginning with `,`. An initial value has exactly
  one home: writing it in both places hides which one wins (the constructor does). Enforced by `CheckCodeConventions.py`
  (`Style/HeaderMemberInitializer`, full-scan only). Three cases keep their header defaults: a class whose default
  constructor is `= default` or defined inline in the header, a delegating constructor (`: Self( ... )` — the target
  carries the initializers), and a type with no constructor at all.
- A field that default-initialization leaves indeterminate (integer, float, `bool`, enum, pointer, an array of those,
  `atomic<scalar>`, a bit-field) has its value **somewhere**: every constructor with an initializer list (copy and move
  constructors included) lists it, or it has a header default. A class whose default constructor is `= default` needs
  header defaults for them — a bit-field cannot have one before C++20, so such a class writes its constructor.
  `-Wreorder-ctor` and `Style/ConstructorOrder` only order the fields that are listed; a field missing from the list
  starts as garbage. Byte arrays used as buffers (`utf8 _arrStaticBuffer[N]`) are exempt.
  Enforced by `Style/ConstructorInitializesEveryField` (full-scan only); a type it cannot classify from the text is skipped.
- Arrange fields to minimize byte padding; use bit packing where appropriate.
- Do not compare booleans through negation: write explicit comparisons such as
  `if (_bValid == false)`. Explicitly compare pointers to `nullptr` when needed.
- `uint8` boolean members (`_b*`, whether or not they are bitfields like `uint8 _bFlag : 1;`) are assigned and
  compared with `SW_TRUE` / `SW_FALSE` — never `true`/`false`, and never a bare `1`/`0`: write
  `_bFlag = SW_TRUE;`, `if (_bFlag == SW_TRUE)`, `if (_bFlag == SW_FALSE)`. A bare `1` reads as a count;
  the macro says it is a state. Enforced by `Style/BitfieldBoolean` (full-scan only; it skips any name declared `bool`
  elsewhere, preferring a miss over a false positive). Real `bool` members keep `true`/`false`.
- Omit braces for a single-line `if` body. If an `else`/`else if` is present,
  use braces for any multi-line blocks — when one branch of the chain needs
  braces, every branch keeps them. Loops (`for`/`while`/`do`) always keep their
  braces, even for a single-statement body (an empty body is `{}`, not `;`). Enforced by `FormatBranchBraces.py`
  and, for loops, the gate `CheckLoopBraces` (a loop with a comment or preprocessor line between head and body is reported for a manual fix).
- A `switch` over an enum that handles **every** enumerator has **no** `default:` (LLVM coding standard): adding an
  enumerator and forgetting a `case` then stops the build (`-Werror=switch`), and a `default:` in a fully covered
  switch is itself an error (`-Werror=covered-switch-default`) because it would silence that check. A switch that
  handles only some enumerators uses `default:` and does not list the rest (`-Wswitch-enum` and `-Wswitch-default`
  are off). Out-of-range values are rejected where they enter (deserialization refuses unknown enumerators); a
  function that returns from every `case` returns its fallback after the switch. No per-file `#pragma` for switch
  diagnostics.
- A `switch` `case`/`default` whose body is more than one statement takes braces;
  a one-statement body does not. `break;` counts as a statement, so
  `case A: doIt(); break;` across two lines gets braces and `case A: return X;`
  does not. The `break;` goes **inside** the braces. A body holding a preprocessor
  directive is left alone. **Within one `switch`, if any case takes braces, every case with a body takes
  them** (a fall-through label with no body stays bare). A switch whose cases are all one statement stays a table.
  Enforced by `FormatBranchBraces.py`.
- Do not use `if` initializers. For unclear conditions or conditions with three
  or more parts, name the condition in a local variable first. `CheckConditionParts.py`
  checks the lines a commit adds (the whole tree is held to a baseline count).
- Use `auto` only for iterators, structured bindings, or similarly complex
  types.
- Avoid lambdas unless they offer a performance benefit.
- Apply `const` wherever it is appropriate unless doing so harms performance.
- For range comparisons, place the variable in the middle (between lower and upper bounds) to reflect mathematical range notation: write `kMin <= value && value <= kMax` instead of `value >= kMin && value <= kMax`.

- Build strings with `formatstring` and its `%#` placeholder, never by chaining `to_string` or `+`.
- Do not add a wrapper function that has one caller. Change the existing function's shape instead of adding a new dedicated one.
- A static registration table is a `RegistrationList`. On the user-configuration side the name is `Handler`.
- A registry keyed by name does not reject empty or duplicate names with its own `if`: borrowed pointers go in a `RegistrationList` (`bRequireName`), owned values in a `NameRegistry`, and the caller only reports the result of `add`.
- When the program cannot go on, stop with `SW_FATAL( "...%#", value )` (below the log tier: `SW_LOW_LEVEL_FATAL`, `printf` format) — never `std::abort()` directly. It writes the reason to stderr first, logs it, flushes the async sink and aborts (`[[noreturn]]`). Enforced by `CheckDirectAbort.py`.
- Never discard a graphics device result (`VkResult`, `HRESULT`). Take it with `isVkSuccess` / `isHrSuccess` (`[[nodiscard]]`, error log + false) or `requireVk` / `requireHr` (`SW_FATAL`) from `VulkanResultCheck.h` / `RHIDxResultCheck.h`, or with `if ( FAILED( … ) )` when failure is a normal answer (say so in a comment). The SDK types cannot carry `[[nodiscard]]`, so `CheckDiscardedDeviceResult.py` enforces it.
- A boolean in text (`true` / `1` / `on` ...) is read with `StringUtil::tryParseBool`, never with a hand-written spelling list.
- Do not erase from a vector one element at a time inside a loop (`erase( begin() + index )` — it moves the tail every time, O(T²) when T elements finish in one frame). Do the per-element work first (mark finished elements), then erase once with `VectorUtil::removeIf` (order kept) or `VectorUtil::removeAtSwap` (order free). Dedupe with a set (`unordered_set`), not `contains` before `push_back`; a pointer list that must stay unique is `IndexedPtrList` (O(1) add/remove, order not kept); collect first and `VectorUtil::sortUnique` once when order is free. `CheckIndexLoopErase.py` and `CheckContainsPushBack.py` check the lines a commit adds (the whole tree is held to a baseline count; the latter keeps a size-reason table in its rules file).
