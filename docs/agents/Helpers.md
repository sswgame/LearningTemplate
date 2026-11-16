> 질문: 헬퍼 함수와 번역 단위 전용 타입은 어디에 두고 어떻게 이름 붙이는가?
> 독자: 헬퍼를 더하거나 `.cpp` 안의 타입을 정리하는 사람.
> 결론: 먼저 주인 타입을 찾는다(그 타입의 멤버). 주인이 없을 때만 여러 번역 단위가 쓰면 헤더의 `XxxUtil`, 한 `.cpp` 전용이면 그 파일의 익명 네임스페이스(`XxxInternal`)다.

## Where a helper goes

Ask in this order; stop at the first yes. Reviews check step 1 first.

1. **Does one type own it?** A function that reads or keeps one type's data or invariants is that type's member — `static` if it needs no instance, `private` if only the type calls it (`JSONFields::hasOnlyKnownKeys`, not a `JSONFieldsUtil`; crash-report steps are `CrashHandler` members). A closely related record is the owner even when not every signature takes it (`PackBuildRecord::parse`, `TimeZoneInfo::computeOffsetMinutes`). Split a mixed `Util` among its owners.
   - **The name does not decide.** A static-only type next to a data type in the same header is the same shape whatever it is called (`Util` · `Scanner` · `Rules` · `Parser` · `Codec` …): a tag scanner beside `MarkupToken` is `MarkupToken::read`. Functions over `vector<T>` belong to `T`. Lint `cpp-shape` rule 4 flags it; a real exception (a registry over many registrations, an algorithm across several types) goes in `Config/Rules/Lint/CppShapeExceptions.json` `staticOwner` with its reason.
   - **State and the logic that changes it are one type.** A `.cpp` state struct whose public fields static functions change in sequence becomes a class with private fields and those functions as members (`EditorWindowTitleState::update`, `TerrainBrushToolState`, `IocpConnection::postSendLocked`). A record a parser fills once stays a plain record — the parser is its static (`PackagingProgress::parseLine`).
2. **No owner, several translation units use it** → a `XxxUtil` static struct in a header (`SerializerUtil`, `MeshUtil`). Typical: an enum (no members — `TextureImportFormatUtil`), an algorithm across types, an external type. Never call these `Internal`.
3. **No owner, one `.cpp` uses it** → that file's anonymous namespace (below).

## Before adding a struct

- **Same shape, same meaning already there?** Use it; search lower layers (`Core/Math`, `HTTPTypes.h` …) by field types. Picking everywhere uses `Ray` (`Core/Math/Ray.h`); HPACK uses `HTTPHeader`. Same field types with different meaning stay separate (`HTTPHeader` vs `LoginCredential`).
- **Only carries one function's arguments?** Pass the arguments, unless there are many or callers use designated initializers.
- A struct used only in one `.cpp` lives there, in the anonymous namespace. "Only one file uses it" is no reason to remove it — table rows, event payloads and per-thread state are real types.

## Helpers used by one `.cpp`

- **One class or struct definition per named `namespace` block**: close the block after each `};` and reopen, so each class folds on its own (specializations of one name stay together).
- Mutable `s_*` state is a hidden singleton and belongs to an owner ([CppStyle](CppStyle.md) "No mutable statics"); listed exceptions (`Config/Rules/Lint/MutableStaticExceptions.json`) stay in the anonymous namespace.
- **Don't split what has one user.** A helper exactly one `.cpp` uses stays in that `.cpp`, not a new `.h`/`.cpp` pair. It gets a header only when a second user appears, reflection or a registration macro needs one, or a test target compiles the file directly. Small headers many files include are fine — merging them slows incremental builds.

The anonymous namespace is a **separate** `namespace sw` block above the implementation:

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
| Constants only | No struct; constants in the anonymous namespace, with names no other `.cpp` uses |
| One function | No struct; a free function in the anonymous namespace with a file-unique name |
| Two or more functions, or a function with constants | One `XxxInternal` struct named after **the translation unit** (`D3D11RHIDevice.cpp` → `D3D11RHIDeviceInternal`), constants inside as `static constexpr`, so a unity build or a moved file keeps names unique |

### One anonymous namespace per file, at the top of its scope

- A `.cpp` has **at most one**, non-empty, at the top of the enclosing namespace. Everything TU-local lives in it: helper structs, constants, listed `s_*` exceptions, free functions. A file-scope `static` it references moves into it.
- **No free `static` functions** — the anonymous namespace already gives internal linkage.
- Exceptions: blocks in mutually exclusive preprocessor branches count as one; `REFLECT`-generated types stay out (`.gen.cpp` names them qualified); `SW_GLOBAL_VARIABLE` (and its test forms) declares `extern` and stays out; `main` and functions declared in a header stay at namespace scope.
