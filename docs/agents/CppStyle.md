> 질문: C++ 본문(분기 · 반복 · 생성자 · 타입 · 문자열 · 병렬 안전)은 어떻게 쓰는가?
> 독자: C++ 본문을 쓰거나 고치는 사람.
> 결론: `.clang-format` 을 따르고, 불리언은 `== false` 로 비교하고, 생성자에서만 필드를 초기화하며, 반복문에는 항상 중괄호를 쓴다. 공유 가변 상태에 쓰지 않는다.

## C++ style

### Layout and branches

- Follow `.clang-format`.
- A function body goes on its own lines. Only a function defined inside a class body may stay on one line (trivial accessors). Free `inline` functions and out-of-class definitions are expanded.
- Booleans: `if (_bValid == false)`, never negation. Compare pointers to `nullptr` explicitly.
- `uint8` boolean members (`_b*`, bit-fields included) use `SW_TRUE` / `SW_FALSE`, never `true`/`false`/`1`/`0`. Real `bool` members keep `true`/`false`.
- **Braces**: omit them for a single-line `if` body. With `else`/`else if`, a multi-line block takes braces, and when one branch needs them every branch keeps them. Loops always keep braces (an empty body is `{}`, not `;`).
- **`switch` over an enum**: handling **every** enumerator means **no** `default:` (`-Werror=switch` catches a forgotten `case`, `-Werror=covered-switch-default` rejects `default:`). Handling only some means `default:`. Out-of-range values are rejected where they enter (deserialization refuses unknown enumerators); a function that returns from every `case` returns its fallback after the switch. No per-file `#pragma` for switch diagnostics.
- **`case` bodies**: more than one statement takes braces, one does not. `break;` counts as a statement and goes **inside** the braces. A body holding a preprocessor directive is left alone. If any case in a `switch` takes braces, every case with a body does.
- No `if` initializers. Name an unclear or three-part condition in a local first.
- Range comparison puts the variable in the middle: `kMin <= value && value <= kMax`.

### Constructors and fields

- With a constructor, initialize fields there (not in the header), in declaration order, with brace initialization. One initializer per line, later lines begin with `,`. An initial value has exactly one home.
- An iterator pair takes parentheses (`_listValue( list.begin(), list.end() )`): braces pick the `initializer_list` constructor.
- Header defaults are kept in three cases: a default constructor that is `= default` or defined inline in the header, a delegating constructor, a type with no constructor.
- A field that default-initialization leaves indeterminate (integer, float, `bool`, enum, pointer, array of those, `atomic<scalar>`, bit-field) gets its value **somewhere**: every constructor with an initializer list (copy and move included) lists it, or it has a header default. A bit-field cannot have a header default before C++20, so such a class writes its constructor. Byte buffers (`utf8 _arrStaticBuffer[N]`) are exempt.
- Arrange fields to minimize padding; use bit packing where appropriate.

### Types, strings, functions

- `auto` only for iterators, structured bindings, or similarly complex types.
- Avoid lambdas unless they offer a performance benefit.
- `const` wherever appropriate unless it harms performance.
- Build strings with `formatstring` and its `%#` placeholder, never by chaining `to_string` or `+`.
- Read a boolean in text with `StringUtil::tryParseBool`, never a hand-written spelling list.
- No wrapper function with one caller; change the existing function's shape instead.
- Inheritance: at most one base that holds state, plus interfaces without data members (`I*`, like Unreal's `UObject` + `IInterface`). `std::enable_shared_from_this` and third-party callback bases (Jolt listeners) are the only other bases allowed. Never two stateful bases, never a diamond. To add a capability (dev-only reload, tooling), use a separate object that holds a reference to the target and is registered from its own build unit (composition). Do not bolt another interface onto the class. When the choice is fixed per build (Dev or Shipping), pick an implementation file in CMake and use no interface at all.

### Registries and failure

- A static registration table is a `RegistrationList`. On the user-configuration side the name is `Handler`.
- A name-keyed registry does not reject empty or duplicate names with its own `if`: borrowed pointers go in a `RegistrationList` (`bRequireName`), owned values in a `NameRegistry`, and the caller only reports the result of `add`.
- A registration that returns a `Subscription` for a borrowed pointer never turns the subscription id back into an address by hand: a `RegistrationList` issues it (`addSubscribed`, release function taking `T*` given to the list's constructor), an O(1) or order-keeping list uses an `ItemSubscriptionList<T>` per kind (no kind bits in the id), and a callback list is a `MulticastDelegate` / `LockedMulticastDelegate`. `SubscriptionList` (plain `uint64` ids) is for id-keyed entries that carry values.
- When the program cannot go on: `SW_FATAL( "...%#", value )` (below the log tier: `SW_LOW_LEVEL_FATAL`), never `std::abort()`.
- Never discard a graphics device result (`VkResult`, `HRESULT`). Take it with `isVkSuccess` / `isHrSuccess` or `requireVk` / `requireHr`, or `if ( FAILED( … ) )` when failure is a normal answer (say so in a comment). SDK types cannot carry `[[nodiscard]]`, so review keeps this rule.

### Vectors

- No erasing one element at a time inside a loop (O(T²)). Mark finished elements, then erase once with `VectorUtil::removeIf` (order kept) or `removeAtSwap` (order free).
- Dedupe with a set, not `contains` before `push_back`. A pointer list that must stay unique is `IndexedPtrList`. When order is free, collect first and `VectorUtil::sortUnique` once.

## Parallel-safe code

Anything that may run in parallel (job system, cooker, ticks, test processes) shares no mutable place. Prefer a shape that cannot race.

- **Tests** never write to `Bin`, a fixed file name or a fixed port: use a per-run temporary path and bind sockets to port 0.
- **Asset importers**: `importSource` is `const` and stateless, and returns its result per file. A source that needs another's result declares that order as data. Editor asset code adds no mutable `static` and no global cache.
- **Cook and import** produce the same bytes serially or in parallel: each job writes its own result slot, and a serial pass merges the slots in a fixed order.
- **Ticks, services, workers** do not read mutable global or singleton state; pass what they need in. A worker body touches pointers and scratch slots, not containers another worker may resize.
- A job-style interface declares what it reads and writes as pure virtuals with no default (`getPoseAccess`).
- **Lazy initialization** is a function-local static (`static const Loader s_loader = makeLoader();`) or `std::call_once`, never a bare `static bool s_bTried` checked by an `if` (two threads both pass it). The same goes for check-then-fill of a static or file-scope variable; a main-thread-only site says so in a comment.
