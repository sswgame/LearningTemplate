> 질문: C++ 본문(분기 · 반복 · 생성자 · 타입 · 문자열 · 병렬 안전)은 어떻게 쓰는가?
> 독자: C++ 본문을 쓰거나 고치는 사람.
> 결론: `.clang-format` 을 따르고, 불리언은 `== false` 로 비교하고, 생성자에서만 필드를 초기화하며, 반복문에는 항상 중괄호를 쓴다. 공유 가변 상태에 쓰지 않는다.

## C++ style

- Follow `.clang-format`.
- A function body goes on its own lines. Only a function defined inside a class body may stay on one line
  (getters and trivial accessors). Free `inline` functions and out-of-class definitions are expanded.
- When a constructor exists, initialize fields in the constructor (not in the header), in declaration order, with brace
  initialization — except an iterator pair, which takes parentheses (`_listValue( list.begin(), list.end() )`): braces pick
  the `initializer_list` constructor. One initializer per line, subsequent lines beginning with `,`. An initial value has exactly
  one home (`Style/HeaderMemberInitializer`). Three cases keep their header defaults: a class whose default constructor is
  `= default` or defined inline in the header, a delegating constructor, and a type with no constructor at all.
- A field that default-initialization leaves indeterminate (integer, float, `bool`, enum, pointer, an array of those,
  `atomic<scalar>`, a bit-field) has its value **somewhere**: every constructor with an initializer list (copy and move
  included) lists it, or it has a header default. A bit-field cannot have a header default before C++20, so such a class writes
  its constructor. Byte arrays used as buffers (`utf8 _arrStaticBuffer[N]`) are exempt (`Style/ConstructorInitializesEveryField`).
- Arrange fields to minimize byte padding; use bit packing where appropriate.
- Do not compare booleans through negation: write `if (_bValid == false)`. Compare pointers to `nullptr` explicitly.
- `uint8` boolean members (`_b*`, bit-fields included) are assigned and compared with `SW_TRUE` / `SW_FALSE` — never `true`/`false`
  or a bare `1`/`0` (`Style/BitfieldBoolean`). Real `bool` members keep `true`/`false`.
- Omit braces for a single-line `if` body. With an `else`/`else if`, a multi-line block takes braces, and when one branch needs
  braces, every branch keeps them. Loops (`for`/`while`/`do`) always keep braces (an empty body is `{}`, not `;`).
- A `switch` over an enum that handles **every** enumerator has **no** `default:` (`-Werror=switch` stops a forgotten `case`;
  `-Werror=covered-switch-default` rejects the `default:`). A switch that handles only some enumerators uses `default:`. Out-of-range
  values are rejected where they enter (deserialization refuses unknown enumerators); a function that returns from every `case`
  returns its fallback after the switch. No per-file `#pragma` for switch diagnostics.
- A `switch` `case`/`default` whose body is more than one statement takes braces; one statement does not. `break;` counts as a
  statement and goes **inside** the braces. A body holding a preprocessor directive is left alone. Within one `switch`, if any case
  takes braces, every case with a body takes them.
- Do not use `if` initializers. For unclear conditions or conditions with three or more parts, name the condition in a local
  variable first.
- Use `auto` only for iterators, structured bindings, or similarly complex types.
- Avoid lambdas unless they offer a performance benefit.
- Apply `const` wherever it is appropriate unless doing so harms performance.
- For range comparisons, put the variable in the middle: `kMin <= value && value <= kMax`.
- Build strings with `formatstring` and its `%#` placeholder, never by chaining `to_string` or `+`.
- Do not add a wrapper function that has one caller. Change the existing function's shape instead.
- A static registration table is a `RegistrationList`. On the user-configuration side the name is `Handler`.
- A registry keyed by name does not reject empty or duplicate names with its own `if`: borrowed pointers go in a `RegistrationList`
  (`bRequireName`), owned values in a `NameRegistry`, and the caller only reports the result of `add`.
- When the program cannot go on, stop with `SW_FATAL( "...%#", value )` (below the log tier: `SW_LOW_LEVEL_FATAL`) — never
  `std::abort()`.
- Never discard a graphics device result (`VkResult`, `HRESULT`). Take it with `isVkSuccess` / `isHrSuccess` or `requireVk` /
  `requireHr`, or with `if ( FAILED( … ) )` when failure is a normal answer (say so in a comment). SDK types cannot carry
  `[[nodiscard]]`, so the rule is kept by review.
- Read a boolean in text with `StringUtil::tryParseBool`, never a hand-written spelling list.
- Do not erase from a vector one element at a time inside a loop (O(T²)). Mark finished elements, then erase once with
  `VectorUtil::removeIf` (order kept) or `VectorUtil::removeAtSwap` (order free). Dedupe with a set, not `contains` before
  `push_back`; a pointer list that must stay unique is `IndexedPtrList`; collect first and `VectorUtil::sortUnique` once when order
  is free.

### Parallel-safe code

Anything that may run in parallel (job system, cooker, ticks, test processes) shares no mutable place. Prefer a shape that cannot race.

- A test never writes to `Bin`, a fixed file name or a fixed port: it uses a per-run temporary path and sockets bind port 0
  (the test framework is being rebuilt; its helpers are named when it exists).
- A lint script does not `rglob` files itself: it uses `common.Search.collectRepositoryFiles`,
  and depends on no global state or output order.
- `importSource` of an asset importer is `const` and stateless; its result is returned per file. A source that needs another's
  result declares that order as data. Editor asset code adds no mutable `static` and no global cache.
- A cook or import step produces the same bytes serially or in parallel: each job writes its own result slot, and a serial pass
  merges the slots in a fixed order.
- Code inside a tick, a service or a worker does not read mutable global or singleton state; pass what it needs in. A worker body
  touches pointers and scratch slots, not containers another worker may resize.
- A job-style interface declares what it reads and writes as pure virtuals with no default (`getPoseAccess`).
- Lazy initialization is a function-local static (`static const Loader s_loader = makeLoader();`) or `std::call_once` — never a bare
  `static bool s_bTried` checked by an `if` (two threads both pass it). The same goes for check-then-fill of
  a static or file-scope variable; a main-thread-only site says so in a comment.
