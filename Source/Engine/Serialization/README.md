# Serialization — 리플렉션 값을 XML · JSON · 바이너리로

리플렉션(`TypeInfo` · `PropertyInfo`)이 설명하는 값을 파일과 바이트로 옮기는 계층입니다. 형식은 셋입니다. 사람이 고치는 저작 파일은 XML 과 JSON 으로 쓰고,
쿠커와 세이브 같은 빌드 · 실행 산출물은 바이너리로 씁니다. 언리얼의 `FArchive` · `FPropertyTag` 와 유니티의 YAML 직렬화에 해당합니다.

- `Base/` 에는 형식이 함께 쓰는 부품이 있습니다. 컨테이너 순회(`ContainerVisitor`), 버전 이관(`SchemaMigrate`), 값 하나 다루기(`SerializerUtil`)가 여기 있습니다.
- `Format/` 에는 형식마다의 직렬화기(`XmlSerializer` · `JsonSerializer` · `BinarySerializer`)와 바이트 스트림 `Archive` 가 있습니다.
- `Object/` 에는 오브젝트 상태의 차이를 쓰는 직렬화기가 있습니다. 오브젝트 상태를 언제 · 어떤 순서로 읽는지는 [Object README](../Object/README.md) 가 다룹니다.

리플렉션 매크로와 타입 등록은 [Reflection README](../Reflection/README.md) 에 있습니다.

## 함정 · 계약

- **엔진 데이터는 별칭을 쓰지 않는다**(사용자 결정 2026-10-03 — 실제 게임 데이터가 없다). 이름을 바꾸면 `Resource/` 데이터를 다시 쓴다. 모르는 키 · 타입 · 열거자는
  안쪽 원소까지 orphan 경고가 나고, `ResourceDataSchemaTest` 가 Resource/ 의 데이터 파일 전부(종류 표에 없는 파일도 실패)를 실제 로더로 읽어 경고 0 을 단언한다.
  Alias · ValueAlias 기능과 그 시험은 실제 게임 데이터가 생긴 뒤의 창구로 남긴다. 판 체계(registerXmlMigrator · 바이너리 판 필드)도 기능으로 남고, 지금 판만 읽는다.
- **orphan 정책은 `SchemaMigrate.h` 의 계약** — XML · JSON(사람이 고치는 저작 파일)은 `Ignore`(로드마다 경고, UE `FPropertyTag` · Unity YAML), 바이너리(쿠커 ·
  빌드 산출물)는 `Reject`(UE `FPackageFileSummary` 판 검사). 필드를 버려도 되는 오브젝트 상태는 그 migrate 함수가 말한다(`skipFieldsTheTypeNoLongerHas`).
- **바이너리 Archive 읽기는 읽은 만큼 자리를 옮긴다**(이어 쓴 객체를 차례로 읽는다). 같은 자리를 다시 보려면 새 Archive 를 만든다.
- **set 원소 편집은 `replaceElement`(지우고 다시 넣기)로만** — 같은 값이 되면 하나로 합쳐진다. 맵은 `forEachMutable` · `eraseAt`, 고정 배열은 `appendElement` 가
  원소 순번의 칸을 채운다(칸보다 많으면 실패).
- **저장되는 상태는 PROPERTY 이고, 모든 PROPERTY 타입은 직렬화기가 실어 나를 수 있어야 한다**(`SerializerUtil::canCarryProperty`,
  `ReflectionSerializationTest.EveryPropertyHasATypeTheSerializersCanCarry`, 모듈판은 SmokeTest 의 `ModuleApiTest`). enum 에 `ENUM()` 이 없으면 `"null"` 로 저장된다.
  런타임 핸들(`void*`)은 `Transient`. `PROPERTY()` 를 빼먹은 필드는 매 실행 "모르는 필드" 경고를 내고 값은 기본값으로 돈다.
- **모르는 칸 · 모르는 열거자는 그 칸만 실패한다**(컨테이너면 그 원소, 맵이면 그 항목) — 세 형식이 같은 규칙이다. 기록 타입 해시를 모르는 칸(지운 enum · 타입)은 크기로
  짐작해 읽지 않는다. 모르는 타입의 컴포넌트는 `MissingComponent` 가 원문을 맡아 같은 형식으로 다시 쓴다. 프리팹을 못 찾은 엔티티는 `SceneDocument::SceneObjectNode` 로 보존한다.
- **바이너리는 enum 을 열거자 이름 해시로 싣는다**(플래그는 켜진 이름 수 + 해시, 이름 없는 값만 `0 + int64`). 열거자 이름을 바꾸면 데이터를 다시 쓴다(옛 바이너리는
  읽히지 않는다 — 실제 게임 데이터가 생긴 뒤라면 `ValueAlias`). 한 enum 안의 `Red` · `RED` 는 해시가 같다 — `registerEnum` 이 알린다. 판 `BinaryWireVersion` 은 스트림 머리마다 있다. `kObjectReflectedSchemaVersion` 은 일부러
  올리지 않았다(올리면 옛 상태가 모두 거절된다).
- **버전 절차는 `runVersionedDeserialize` 한 벌**이고 형식 사이 차이는 `SchemaVersionSource` · `SchemaOrphanPolicy` 두 enum 뿐이다. 버린 orphan 은 로드마다 한 줄 알린다 —
  새 이관도 `findOrphan` · `findOrphanHash` · `applyOrphanTo…` 로 찾아야 경고에서 빠진다(`_bClaimed`). 이관은 기록 타입이 같을 때만 제자리, 스칼라 → 스칼라는 텍스트를 거쳐
  (`tryCoerceBinaryPayload`), 비트 재해석은 금지다. 판단은 전선이 싣고 온 타입 해시로(payload 크기로 짐작하면 `1.5f` 가 `1069547520` 이 된다).
- **실패는 버릴 수 없다.** 실패할 수 있는 동사(load · save · read · write · parse · (de)serialize · apply · restore · import · export · cook · compile · revert · convert ·
  try · open · attach · spawn · instantiate · reload · remove · copy · create · delete · move · rename)의 bool 은 `[[nodiscard]]` — `-Werror=unused-result` + `CheckFallibleNodiscard`.
  의도된 버림은 `(void)` + 이유. `Archive` 읽기 연산자는 끈적한 `isError` 상태라 버려도 된다.
- **제자리 로드는 원자적이다**(`ObjectLoadContext::_bRestorePreviousOnFailure`). `BinarySerializer::deserialize` 자체는 실패해도 되돌리지 않는다 — 원자성이 필요한 쪽이 스냅샷을 뜬다.
  틱 중 제자리 상태 읽기는 거절된다(`executeOrDeferPostTick` 으로 감싼다).
- **밖에서 온 바이트를 믿지 않는다.** 경계는 뺄셈으로(`count > size - offset` — 덧셈은 넘쳐서 통과한다), 개수는 남은 바이트 / 최소 항목 크기로 상한, 해제 크기는
  `CompressionStream::kMaxUncompressedSize`(1 GiB), 32 비트 축소는 `narrowToUint32` 한 곳, bool 은 바이트를 `!= 0` 으로, 리소스 id 의 `..` 거절. 손상된 개수 칸 하나로 `reserve`
  가 72 초를 쓴 적이 있다. 넘침 회귀 시험은 위치를 먼저 옮긴 뒤 되감기는 크기를 줘야 문다.
- **디스크 · 네트워크로 나가는 바이트는 결정적이어야 한다** — 해시맵은 키 순 정렬, 구조체는 필드 순서대로(패딩 쓰레기), 판 번호는 읽는 쪽이 대조한다. 다형 소유 포인터는
  원소마다 `[이름][본문크기][본문]`(본문 크기가 있어야 모르는 타입을 건너뛴다). `serializeCompact` 는 프로퍼티를 이름이 아니라 **인덱스**로 짝짓는다.
- **XML** — 쓰기는 `XmlNode::toString` 하나, float 는 `std::to_chars` 최단 왕복(그래서 되돌리기 스냅샷을 바이너리로 바꾸지 않는다), 긴 줄 접기는 시작 태그 속성에만(pugixml 은 텍스트
  안 `"` 를 이스케이프하지 않는다), 태그는 `sanitizeTag` 가 `::` → `__`. 정수 속성은 `tryGetAttributeIntInRange`, 불리언 글은 `StringUtil::tryParseBool`(관대한 `parseBool` 은 실패를
  알아야 하는 자리에 쓰지 말 것). Windows 헤더가 `small` 을 매크로로 정의한다.
- **JSON** — `JsonValue` 는 빌린 포인터다: 같은 부모에 `set( 새 키 )` · `pushBack()` 을 하면 앞서 꺼낸 형제 핸들이 죽는다("하나 받아 다 채우고 다음"). nlohmann
  `is_number_integer()` 는 부호 없는 수에도 참 — unsigned 를 먼저 본다. `JsonSerializer::loadFile` 은 실패해도 그 앞까지 읽힌 값이 남는다. `SerializeContext` 는 `deriveFromDefault()`.
- **컨테이너를 어떻게 채울지는 컨테이너가 정한다** — 역직렬화는 `appendElement`, 인스펙터는 `allowsInPlaceElementWrite()`. 왕복 시험은 세 형식 모두, 값은 정렬되지 않은 순서로.
- **컨테이너 순회는 `ContainerVisitor` 하나다**(`Serialization/Base/ContainerVisitor.h`). 원소 모양(중첩 · 소유 포인터 · 값 구조체 · 스칼라)은 컨테이너마다 한 번
  `ContainerElementPlan` 이 정하고, 형식은 `IContainerWriter` · `IContainerReader` 만 구현한다(형식 TU 의 `…Internal::ContainerWriter` · `ContainerReader`). 실패는 세 형식이
  `ContainerReadResult` 로 같다 — 자리를 알면 그 원소 · 항목만 빼고 칸 실패(`FieldFailed`), 모르면 멈춘다(`StreamBroken`: 바이너리의 enum 아닌 값 실패 · 넣을 칸 없는 원소).
- **직렬화 출력이 그대로인지는 덤프로 본다** — `SW_SERIALIZATION_DUMP_DIR=<폴더>` 로 `SerializationRoundTripTest.DumpEveryResourceObjectState` **하나만** 돌리면 씬 · 프리팹의
  오브젝트 상태를 세 형식으로 덤프한다. 고치기 전 · 후 덤프의 `diff -r` 이 비어야 한다. 실제 데이터의 쓰기 → 되읽기 → 쓰기 고정점은 `…EveryResourceComponentRewritesToTheSameBytes`.
- **JSON 은 시퀀스를 배열, 맵을 오브젝트로 쓰고, 값 구조체 원소는 타입 래핑 없이 본문만 쓰고 읽습니다.** `{ "TypeName": {...} }` 래핑은 런타임 타입이 필요한 소유 포인터(다형) 원소에만 씁니다. 래핑을 짐작해 벗기는 리더 분기를 되살리지 않습니다.
