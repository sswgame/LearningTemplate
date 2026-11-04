> 질문: 헬퍼 함수와 번역 단위 전용 타입은 어디에 두고 어떻게 이름 붙이는가?
> 독자: 헬퍼를 더하거나 `.cpp` 안의 타입을 정리하는 사람.
> 결론: 먼저 주인 타입을 찾는다 — 한 타입의 값 · 불변식을 다루는 함수는 그 타입의 멤버다. 주인이 없을 때만 여러 번역 단위가 쓰면 헤더의 `XxxUtil`, 한 `.cpp` 전용이면 그 파일의 익명 네임스페이스(`XxxInternal`)다.

## Where a helper goes

Ask in this order. Stop at the first yes.

1. **Does one type own it?** A function that reads or keeps one type's data or invariants is that type's member — `static` if it needs no instance, `private` if only the type's own code calls it. `ConfigKeyDoc::hasOnlyKnownKeys`, not a `ConfigKeyDocUtil` beside it; a crash handler's report-writing steps are `CrashHandler` members, not free functions next to it. Moving such a function out only because it is "a helper" splits one type's rules across two names.
2. **No owner, several translation units use it** → a `XxxUtil` static struct in a header (`SerializerUtil`, `MeshUtil`). Typical owners-that-cannot-own: an enum (it has no members — `TextureImportFormatUtil`), an algorithm across several types, an external type. Never name those headers or types `Internal`.
3. **No owner, one `.cpp` uses it** → that file's anonymous namespace (below).

`Util` and `Internal` are where ownerless helpers go, not a shape every helper must take. Reviews check step 1 first.

## Helpers used by one `.cpp`

- **One class or struct definition per named `namespace` block.** A file with several classes closes the block after each `};` and reopens it, so each class folds on its own (template specializations of one name stay together; the anonymous namespace stays one block).
- Locator-only `s_*` state used by bind/get APIs stays in the implementation block's anonymous namespace (`Source/` and `Source/Programs/ReflectionParser/`).

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

| Ownerless TU-only helpers | Shape |
| --- | --- |
| Constants only | No struct; constants stay in the anonymous namespace |
| One function | No struct; a free function in the anonymous namespace with a name unique to this file |
| Two or more functions, or a function with constants | One `XxxInternal` struct, named after **the translation unit** (`D3D11RHIDevice.cpp` → `D3D11RHIDeviceInternal`) so the group reads as this file's helpers and a moved file keeps unique names |

A constant declared directly in the anonymous namespace (`constexpr int32 kLimit = 4;`) goes inside the `XxxInternal` struct as `static constexpr`, or gets a name no other `.cpp` uses.

**Don't split what has one user.** A helper type or function that exactly one `.cpp` uses lives in that `.cpp` (its anonymous namespace or a plain section), not in a new `.h`/`.cpp` pair: every pair is one more translation unit and one more file to jump to. Give it its own header only when a second user appears, when reflection or a registration macro needs a header, or when a test target compiles the file directly. Small headers that many files include are fine — merging them into a bigger header slows incremental builds.

### One anonymous namespace per file, at the top of its scope

- A `.cpp` has **at most one** anonymous namespace, at the top of the enclosing namespace, and never an empty one. Everything TU-local lives in it: helper structs, constants, `s_*` state, free functions.
- **No free `static` functions.** The anonymous namespace already gives internal linkage.
- The block may only sit above things it does not use: a file-scope `static` it references moves **into** the block.
- Exceptions:
  - Blocks in mutually exclusive preprocessor branches (`#if SW_PLATFORM_WINDOWS` / `#elif SW_PLATFORM_LINUX`) are one block per TU.
  - `REFLECT`-generated types stay out (`.gen.cpp` refers to them by qualified name).
  - `SW_GLOBAL_VARIABLE` (and its test forms) declares `extern` and stays out.
  - `main` and functions declared in a header stay at namespace scope.
