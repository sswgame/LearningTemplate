> 질문: 헬퍼 함수와 번역 단위 전용 타입은 어디에 두고 어떻게 이름 붙이는가?
> 독자: 헬퍼를 더하거나 `.cpp` 안의 타입을 정리하는 사람.
> 결론: 먼저 주인 타입을 찾는다 — 한 타입의 값 · 불변식을 다루는 함수는 그 타입의 멤버다. 주인이 없을 때만 여러 번역 단위가 쓰면 헤더의 `XxxUtil`, 한 `.cpp` 전용이면 그 파일의 익명 네임스페이스(`XxxInternal`)다.

## Where a helper goes

Ask in this order. Stop at the first yes.

1. **Does one type own it?** A function that reads or keeps one type's data or invariants is that type's member — `static` if it needs no instance, `private` if only the type's own code calls it. `JSONFields::hasOnlyKnownKeys`, not a `JSONFieldsUtil` beside it; a crash handler's report-writing steps are `CrashHandler` members, not free functions next to it. Moving such a function out only because it is "a helper" splits one type's rules across two names. A closely related struct counts as the owner even when not every signature takes it: parsing and formatting a record belong on that record (`PackBuildRecord::parse`), and a query over a type belongs on it (`TimeZoneInfo::computeOffsetMinutes`). Split a mixed `Util` among its owners; do not keep it because one function has none.
   - **The name does not decide.** A type that holds only `static` functions next to a data type in the same header is the same shape whatever it is called (`Util` · `Scanner` · `Rules` · `Logic` · `Parser` · `Codec` …). On 2026-11-08 `MarkupTagScanner` beside `MarkupToken` became `MarkupToken::read` · `hasSameTags`. Functions over a list of that type (`vector<T>`) also belong to `T`. Lint `cpp-shape` rule 4 flags a static-only type whose signatures mostly take one type of its header; a real exception (a registry over many registrations, an algorithm across several types) goes in `Config/Dev/CppShapeExceptions.json` `staticOwner` with its reason.
   - **State and the logic that changes it are one type.** A `.cpp` state struct whose public fields other static functions change in sequence becomes a class with private fields and those functions as members (`EditorWindowTitleState::update`, `TerrainBrushToolState`, `IocpConnection::postSendLocked`). Records that a parser or factory fills once and hands back stay plain records — but the parser itself is the record's static (`PackagingProgress::parseLine`). Lint `cpp-shape` rule 10 flags a `.cpp` state type whose public fields `stateMutatorMinFunctions` or more statics of a `…Internal` change through a non-`const`, non-`outX` reference or pointer; exceptions go in `Config/Dev/CppShapeExceptions.json` `stateMutator` with a reason.
2. **No owner, several translation units use it** → a `XxxUtil` static struct in a header (`SerializerUtil`, `MeshUtil`). Typical owners-that-cannot-own: an enum (it has no members — `TextureImportFormatUtil`), an algorithm across several types, an external type. Never name those headers or types `Internal`.
3. **No owner, one `.cpp` uses it** → that file's anonymous namespace (below).

`Util` and `Internal` are where ownerless helpers go, not a shape every helper must take. Reviews check step 1 first.

## Before adding a struct

- **Is the same shape with the same meaning already there?** If so, use it. Search the lower layers (`Core/Math`, `HTTPTypes.h` …) by field types, not by name. On 2026-11-05 three identical rays (`GameRay`, `WorldWidgetRay`, `EditorPickRay`) became `Ray` in `Core/Math/Ray.h`. `HpackHeader` was folded into `HTTPHeader`, and `HTTP2ClientConfig` into `HTTPClientConfig`. Two structs may share field types yet mean different things (`HTTPHeader` and `LoginCredential` are both two strings); those stay separate.
- **Does it only carry one function's arguments?** Pass the arguments, unless there are many of them or callers name them with designated initializers.
- A struct used only inside one `.cpp` belongs there, in the anonymous namespace. "Only one file uses it" is no reason to remove it. Table rows, event payloads and per-thread state are real types.

## Helpers used by one `.cpp`

- **One class or struct definition per named `namespace` block.** A file with several classes closes the block after each `};` and reopens it, so each class folds on its own (template specializations of one name stay together; the anonymous namespace stays one block).
- Mutable `s_*` state is a hidden singleton and belongs to an owner object ([CppStyle](CppStyle.md) "No mutable statics"). The few listed exceptions (`Config/Dev/MutableStaticExceptions.json` — e.g. the bind/get root of the engine service table) stay in the implementation block's anonymous namespace.

The anonymous namespace is a **separate** `namespace sw` block above the class implementation:

```cpp
namespace sw
{
	namespace
	{
		struct FooInternal
		{
			static void helper();
		};
	} // namespace
} // namespace sw

namespace sw
{
	void Foo::process() { FooInternal::helper(); }
} // namespace sw
```

The table below is only for helpers that passed step 1 with no owner.

| Ownerless TU-only helpers | Shape |
| --- | --- |
| Constants only | No struct; constants stay in the anonymous namespace |
| One function | No struct; a free function in the anonymous namespace with a name unique to this file |
| Two or more functions, or a function with constants | One `XxxInternal` struct, named after **the translation unit** (`D3D11RHIDevice.cpp` → `D3D11RHIDeviceInternal`) so the group reads as this file's helpers and a moved file keeps unique names |

A constant declared directly in the anonymous namespace (`constexpr int32 kLimit = 4;`) goes inside the `XxxInternal` struct as `static constexpr`, or gets a name no other `.cpp` uses.

**Don't split what has one user.** A helper type or function that exactly one `.cpp` uses lives in that `.cpp` (its anonymous namespace or a plain section), not in a new `.h`/`.cpp` pair: every pair is one more translation unit and one more file to jump to. Give it its own header only when a second user appears, when reflection or a registration macro needs a header, or when a test target compiles the file directly. Small headers that many files include are fine — merging them into a bigger header slows incremental builds.

### One anonymous namespace per file, at the top of its scope

- A `.cpp` has **at most one** anonymous namespace, at the top of the enclosing namespace, and never an empty one. Everything TU-local lives in it: helper structs, constants, listed `s_*` exceptions, free functions.
- **No free `static` functions.** The anonymous namespace already gives internal linkage.
- The block may only sit above things it does not use: a file-scope `static` it references moves **into** the block.
- Exceptions:
  - Blocks in mutually exclusive preprocessor branches (`#if SW_PLATFORM_WINDOWS` / `#elif SW_PLATFORM_LINUX`) are one block per TU.
  - `REFLECT`-generated types stay out (`.gen.cpp` refers to them by qualified name).
  - `SW_GLOBAL_VARIABLE` (and its test forms) declares `extern` and stays out.
  - `main` and functions declared in a header stay at namespace scope.
