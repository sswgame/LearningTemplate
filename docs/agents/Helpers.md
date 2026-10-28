> 질문: 헬퍼 함수와 번역 단위 전용 타입은 어디에 두고 어떻게 이름 붙이는가?
> 독자: 헬퍼를 더하거나 `.cpp` 안의 타입을 정리하는 사람.
> 결론: 여러 번역 단위가 쓰면 헤더의 `XxxUtil` 정적 구조체에, 한 `.cpp` 전용이면 그 파일의 하나뿐인 익명 네임스페이스 안 `XxxInternal`(번역 단위 이름을 따른다)에 둔다.

## Helpers: Util vs Internal

- Shared helpers used by more than one translation unit belong on a `XxxUtil` static struct in a header
  (`SerializerUtil`, `MaterialUtil`). Do not name those headers or types `Internal`.
- **One class or struct definition per named `namespace` block.** A file that defines several classes closes the block after each
  `};` and reopens it, so each class folds on its own (template specializations of one name stay together; the anonymous
  namespace stays one block).
- Helpers used only inside one `.cpp` go in a **separate** `namespace sw` block from the class implementation:

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

- Locator-only `s_*` state used by bind/get APIs stays in the implementation block's anonymous namespace (`Source/` and
  `Tools/ReflectionParser/`).
- Name the `Internal` helper after **the translation unit, not the class**: unity builds merge several `.cpp` files into one TU, and an
  anonymous namespace hides a name only per TU. `VulkanRHIResourceFactoryPipeline.cpp` uses
  `VulkanRHIResourceFactoryPipelineInternal` (`Naming/DuplicateInternalHelper`).
- A constant declared directly in the anonymous namespace (`constexpr int32 kLimit = 4;`) goes inside the TU's `XxxInternal` struct as
  `static constexpr`, or gets a name no other `.cpp` uses (`Naming/DuplicateAnonymousConstant`).
- Gather a `.cpp`'s TU-only helpers by **how many functions they have**:
  - **No functions** (constants only): no struct, the constants stay in the anonymous namespace.
  - **One function**: no struct — a free function in the anonymous namespace with a name unique to this file.
  - **Two or more functions, or a function with constants**: exactly **one** `XxxInternal` struct per file.
  `Naming/MultipleInternalHelper`, `Naming/LoneFunctionInternalHelper`, `Naming/DuplicateAnonymousFunction` (`Source/` only).

### One anonymous namespace per file, at the top of its scope

- A `.cpp` has **at most one** anonymous namespace, at the top of the namespace that encloses it (right after `SW_LOG_CALLER`, if
  present). Everything TU-local — helper structs, constants, `s_*` state, free functions — lives in it.
- **Do not write free `static` functions.** The anonymous namespace already gives internal linkage.
- Hoisting can break the build: the block may only sit above things it does not use. A file-scope `static` it references moves
  **into** the block.
- Exceptions:
  - Blocks in mutually exclusive preprocessor branches (`#if SW_PLATFORM_WINDOWS` / `#elif SW_PLATFORM_LINUX`) are one block per TU.
  - `REFLECT`-generated types cannot move into an anonymous namespace (`.gen.cpp` refers to them by qualified name).
  - `SW_GLOBAL_VARIABLE` (and its test forms) declares `extern` and stays out.
  - `main` and functions declared in a header stay at namespace scope.
