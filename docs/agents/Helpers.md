> 질문: 헬퍼 함수와 번역 단위 전용 타입은 어디에 두고 어떻게 이름 붙이는가?
> 독자: 헬퍼를 더하거나 `.cpp` 안의 타입을 정리하는 사람.
> 결론: 여러 번역 단위가 쓰면 헤더의 `XxxUtil` 정적 구조체에, 한 `.cpp` 전용이면 그 파일의 하나뿐인 익명 네임스페이스 안 `XxxInternal`(번역 단위 이름을 따른다)에 둔다.

## Helpers: Util vs Internal

- Shared helpers used by more than one translation unit belong on a `XxxUtil`
  static struct in a header (for example `SerializerUtil`, `MaterialUtil`). Do
  not name those headers or types `Internal`.
- **One class or struct definition per named `namespace` block.** A file that defines
  several classes closes the block after each `};` and reopens it for the next, so each
  class folds on its own (`fixer/FormatNamespaceBlocks.py` does it; template
  specializations of one name stay together, the anonymous namespace stays one block).
- Helpers used only inside one `.cpp` go in a **separate** `namespace sw` block
  from the class implementation, so the two regions fold independently:

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

- Locator-only `s_*` state used by bind/get APIs stays in the implementation
  block's anonymous namespace. This applies to `Source/` and
  `Tools/ReflectionParser/`.
- Name the `Internal` helper after **the translation unit, not the class**. When one
  class is split across several `.cpp` files, naming each helper after the class makes
  them collide: unity builds (`SW_ENABLE_UNITY_BUILD`, used by the `CI-*` presets) merge
  several `.cpp` files into one translation unit, and an anonymous namespace only hides a
  name *per translation unit* — so the second definition is a redefinition error. So
  `VulkanRHIResourceFactoryPipeline.cpp` uses `VulkanRHIResourceFactoryPipelineInternal`, not
  `VulkanRHIResourceFactoryInternal`. Enforced by `CheckCodeConventions.py`
  (`Naming/DuplicateInternalHelper`, full-scan only).
- The same holds for a constant declared directly in the anonymous namespace (`constexpr int32 kLimit = 4;`):
  two `.cpp` files with the same bare name collide in a unity build. Put it inside the TU's `XxxInternal`
  struct as `static constexpr`, or give it a name no other `.cpp` uses. Enforced by `CheckCodeConventions.py`
  (`Naming/DuplicateAnonymousConstant`, full-scan only); constants inside a struct or a function are not affected.
- Gather a `.cpp`'s translation-unit-only helpers by **how many functions they have**:
  - **No functions** (constants only): no struct, the constants stay in the anonymous namespace.
  - **One function** (no constants, nested types, or other members): no struct either — a free function directly in the anonymous namespace.
    Give it a name unique to this file, because a bare function name has no struct to hide behind and two `.cpp` files with the same name collide in a unity build.
  - **Two or more functions, or a function together with constants**: exactly **one** `XxxInternal` struct per file, never several.
  Enforced by `CheckCodeConventions.py` (`Naming/MultipleInternalHelper`, `Naming/LoneFunctionInternalHelper`; `Naming/DuplicateAnonymousFunction`, full-scan only,
  `Source/` only). Files that already broke the first two when the rules were added are listed in `Scripts/lint/rules/CheckCodeConventions.toml`
  (`internalHelperBaseline`); delete a file's line when you fix it, and do not add new files to the list.

### One anonymous namespace per file, at the top of its scope

- A `.cpp` has **at most one** anonymous namespace, placed at the top of the namespace that
  encloses it (right after `SW_LOG_CALLER`, if present). Everything translation-unit-local —
  helper structs, constants, `s_*` state, free functions — lives in it.
- **Do not write free `static` functions.** The anonymous namespace already gives internal
  linkage, so drop the `static` keyword when you move one in.
- **Hoisting can break the build.** The block may only sit above things it does not use.
  Moving it past a type definition, a constant or an `s_*` variable that it references is a
  compile error, so check the references before you move one (workers do not build; report it as "compile check needed"). If the dependency is itself translation-unit-local
  (a file-scope `static`), move it **into** the block instead.
- Legitimate exceptions, all of which are load-bearing:
  - Blocks in mutually exclusive preprocessor branches (`#if SW_PLATFORM_WINDOWS` /
    `#elif SW_PLATFORM_LINUX`) are one block per translation unit. Do not merge them.
  - `REFLECT`-generated types cannot move into an anonymous namespace — the generated
    `.gen.cpp` refers to them by qualified name (`sw::MockMeshComponent`).
  - `SW_GLOBAL_VARIABLE` (and its test forms) declares `extern`; it is deliberately external linkage and stays out.
  - `main` and functions declared in a header stay at namespace scope.
