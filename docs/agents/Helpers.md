# Helpers

> 결론: 먼저 주인 타입을 찾는다(그 타입의 멤버). 주인이 없을 때만 여러 번역 단위가 쓰면 헤더의 `XxxUtil`, 한 `.cpp` 전용이면 그 파일의 익명 네임스페이스(`XxxInternal`)다.

## Where a helper goes

Stop at the first yes.

1. **주인 우선 — does one type own it?** A function that reads or keeps one type's data or invariants is that type's member (`static` if it needs no instance, `private` if only the type calls it): `JSONFields::hasOnlyKnownKeys`, not a `JSONFieldsUtil`. A closely related record is the owner even when not every signature takes it. Split a mixed `Util` among its owners.
   - The name does not decide: a static-only type beside a data type in the same header (`Util` · `Scanner` · `Parser` …) belongs to that type; functions over `vector<T>` belong to `T`. Lint `cpp-shape` rule 4 enforces; a real exception goes in `Config/Rules/Lint/CppShape.json` `_staticOwner` with its reason.
   - State and the logic that changes it are one type: a `.cpp` state struct whose public fields static functions change in sequence becomes a class with private fields and those functions as members (lint `cpp-shape` rule 10). A record a parser fills once stays a plain record.
2. **No owner, several translation units** → a `XxxUtil` static struct in a header (`SerializerUtil`). Typical: an enum, an algorithm across types, an external type. Never call these `Internal`.
3. **No owner, one `.cpp`** → that file's anonymous namespace (below).

## Before adding a struct

- Same shape, same meaning already there (search lower layers by field types — `Ray`, `HTTPHeader`)? Use it. Same field types with different meaning stay separate.
- Only carries one function's arguments? Pass the arguments, unless there are many.
- A struct used only in one `.cpp` lives there, in the anonymous namespace; "one user" is no reason to remove a real type.

## Helpers used by one `.cpp` — Internal 규칙

- One class or struct definition per named `namespace` block (close after each `};`, reopen).
- Mutable `s_*` state belongs to an owner ([CppStyle](CppStyle.md) "No mutable statics").
- Don't split what has one user: no new `.h`/`.cpp` pair until a second user, reflection, a registration macro or a test target needs a header.

| Ownerless TU-only helpers | Shape |
| --- | --- |
| Constants only | Constants in the anonymous namespace, names no other `.cpp` uses |
| One function | A free function in the anonymous namespace, file-unique name |
| Two or more functions, or a function with constants | One `XxxInternal` struct named after **the translation unit** (`D3D11RHIDevice.cpp` → `D3D11RHIDeviceInternal`), constants inside as `static constexpr` — unity builds keep names unique |

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

### One anonymous namespace per file, at the top of its scope

- At most one per `.cpp`, non-empty, a separate `namespace sw` block above the implementation; everything TU-local lives in it (lint `cpp-shape` rule 3).
- No free `static` functions — the anonymous namespace already gives internal linkage.
- Out of it: blocks in exclusive preprocessor branches (count as one), `REFLECT`-generated types, `SW_GLOBAL_VARIABLE`, `main`, functions declared in a header.
