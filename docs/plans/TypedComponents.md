# 타입별 컴포넌트 목록과 타입 핸들 구현 계획 (2026-10-17)

> 상태: 끝 · 선행: ReflectionFormats · 영역: engine · 옛번호: C-52 · 진행: 6/6

[리플렉션 직렬화 형식 계획](ReflectionFormats.md) 9절의 단계 7-c(매니저의 타입별 컴포넌트 목록)와 7-d(타입 핸들 `TComponentHandle<T>`)를 구현하고 병합했습니다(7-c · 7-d-1 · 7-d-2 · 7-d-3, T3 · T4, `design-reviewer` 검토 ⓐ~ⓗ 반영).
단위는 모두 끝났고, 이 문서는 코드 주석이 가리키는 절(3.3 ⓑ · 3.7 · 3.8 · 8.4)의 규칙과 남은 확인만 남깁니다. 결정은 [결정 기록](../09_Decisions.md) 5-6 의 "타입별 버킷"(757)과 "삭제 대기 불변식"(760)에 있습니다.

이 문서에서 쓰는 말은 다음과 같습니다.

- **매니저 순회**는 `GameObjectManager::forEachComponentOfType<T>` 입니다. 씬 전체에서 `T` 와 그 파생 컴포넌트를 찾습니다. **오브젝트 순회**는 `GameObject::forEachComponentOfType<T>` 이고 오브젝트 하나만 봅니다.
- **버킷**은 매니저가 보관하는, 정확히 한 타입의 컴포넌트 포인터 목록입니다.
- **타입 핸들**은 `TComponentHandle<T>` 입니다. `ComponentHandle`(objectID 와 componentID)에 대상 타입 `T` 를 컴파일 시간 정보로 더한 값 타입입니다.

## 구현된 곳

| 내용 | 위치 |
|---|---|
| 버킷(풀 항목에 합침), 갱신, 비템플릿 순회 | `Engine/Object/GameObject/GameObjectStore.*` (`ComponentPoolEntry`), `GameObjectManager.h`, `GameObject.cpp`, `Component.h` (`_pPoolEntry` · `_poolEntrySlot`) |
| 쿠커 정렬 | `Engine/Scene/SceneNavigationCooker.cpp` |
| 삭제 대기 불변식(오브젝트 표시가 컴포넌트를 먼저 표시) | `GameObject::tryMarkPendingDestroy`, `GameObjectStore.h` (`attachCreatedComponent` · `allocateComponentStorage`) |
| 타입 핸들, 확인 변환, 찾기 | `Core/Container/ComponentHandle.h`, `GameObjectManager.h` (`makeTypedHandle` · `resolveComponent<T>` · `executeOrDeferPostTickOnComponent( self, func )`) |
| 복원 키 FQN | `ObjectIdentity::ComponentEntry::_typeFqn`, `takeRestoredComponentID` |
| `PROPERTY` 타입 핸들(파서 · 직렬화 · 배치 끝 확인) | `Tools/ReflectionParser/AstVisitor.cpp` (`resolveComponentHandleProperty`), `PropertyInfo::_handleTargetTypeName`, `ObjectStateSerializer.cpp` (`ObjectStateBatch::finish`) |
| 인스펙터 선택기 | `Editor/Panels/Inspector/InspectorComponentPicker.*`, `InspectorPropertyManager.cpp` |
| 호출부 마이그레이션(조종 · 애니메이션 · 상호작용 · 씬 · 벤치) | 그룹 A~E, 커밋 `4bcafd7fc` · `aea84474c` · `b7cf26b84` · `9a00d263f` · `413788a17` |
| 테스트 | `TestComponentRegistry.cpp`(버킷 대 훑기), `TestObjectIdentity.cpp`(복원 키), `TypedComponentHandleTest.*`(`TestReflection.cpp`), `ReflectionParserTest.ComponentHandlePropertyShapes`, `TestInspectorComponentPicker.cpp`, 벤치 `TypedComponentBenchTest`(`TestTypedComponentBench.cpp`) |

## 남은 확인

- **인스펙터 선택기 화면 확인**: 테스트 무대 컴포넌트 `TypedHandleProbeComponent`(Empty 게임)로 시나리오 `editor.inspectorhandle`(DX12)이 콤보 · `(none)` · 되돌리기 · Paste Value 비활성을 눈으로 확인했다(2026-10-20). 이 확인이 선택기가 고른 값을 쓰지 못하는 결함을 찾았다(`formatPropertyText` 에 값 주소를 넘겨 오프셋이 한 번 더 더해짐) — 수정과 함께 통과한다. 근본 원인(객체 주소와 값 주소를 한 함수가 받음)은 함수를 나누고 주소를 형식으로 갈라 막았다: 값 주소는 프로퍼티를 받지 않는 `SerializerUtil::formatValueText` · `parseTextValue` 만 받고, 오프셋 0 짜리 가짜 `PropertyInfo` 로 텍스트를 바꾸던 인스펙터 세 곳(핸들 선택기, 곡선 편집기, 맵 키 · 정렬 시퀀스 원소)을 옮겼다. 네 함수는 `const void*` 대신 `ObjectAddress` · `MutableObjectAddress`(프로퍼티 쪽)와 `ValueAddress` · `MutableValueAddress`(값 쪽)를 받아, 맨 포인터나 반대쪽 주소를 넘기면 컴파일이 안 된다(테스트 `ValueAddressTextDoesNotAddPropertyOffset` 의 `static_assert`). 값이나 객체를 `void*` 로 받던 나머지 여섯도 같은 형식으로 옮겼다(2026-10-20): 값 주소는 `valueToText` · `serializeValueBinary` · `deserializeValueBinary` · `parseTextValueCoerced`, 객체 주소는 `copyPropertyValue` · `applyPropertyBinary`. 주소 형식은 `Engine/Reflection/TextAddress.h` 로 옮겼다(`SchemaMigrate.h` 도 쓰므로). 나머지(`arePropertyValuesEqual` · `applyPropertyDefault` · `tryCoerceBinaryPayload` · 컨테이너 방문자 인터페이스, 세 형식의 값 · 프로퍼티 쓰기 읽기, `resolvePropertyPath` · `runVersionedDeserialize` · `collectRangeErrors` · `isWithinDeclaredRanges`, `ReflectedValueWalk`, `PropertyRoleUtil`)도 같은 형식으로 옮겼다(2026-10-21). `PropertyInfo` 와 주소를 함께 받던 에디터 · UI 쪽(`IInspectorProperty::draw`, `EditorPropertyGrid` 의 프로퍼티 단위 그리기 · 적용 · 기본값 되돌리기, `InspectorPropertyLayout::getNumericRange`, `InspectorPropertyUndo::trackPod`, `EditorDefaultObjects::isDefaultValue`, `EditorMultiEditUtil::hasMixedValues`, `EditorPreferencesStore::isPropertyModified`, UI 바인딩 종류 표의 읽기 · 쓰기)과 `PropertyEditCondition`, `PropertyInfo::PropertyBindingDelegate` 도 옮겼다(2026-10-21). 컨테이너 원소 · 맵 값은 오프셋 0 인 프로퍼티와 함께 `MutableObjectAddress{ 값 주소 }` 로 적어 넘긴다. 객체 전체를 받는 진입점(`BinarySerializer::serialize` 처럼 `TypeInfo` 와 그 객체)은 오프셋을 헷갈릴 자리가 없어 `void*` 그대로다. 주소 형식이 시작되는 곳은 `PropertyInfo::getRawPtr` 의 결과를 감싸는 자리다(그 함수 자체는 리플렉션 전체가 쓰는 맨 포인터 API 로 남는다). Vulkan · DX11 · OpenGL 도 체인이 통과하고 콤보 · `(none)` · 되돌리기 · Paste Value 비활성을 눈으로 확인했다(2026-10-20). 빈틈 둘(`set<TComponentHandle<T>>`, `FUNCTION` 인자)은 후보 `TypedHandleInspectorGaps`([Candidates-editor](Candidates-editor.md))입니다.
- **앱 측정 넷**은 2026-10-20 에 끝냈다(결과는 8.4, 재현 절차는 8.5).
- 확인함(2026-10-20): 비동기 씬 로드 워커는 자기 전용 새 씬(`SceneManager.cpp:298`)의 저장소에만 넣고, 게임 스레드가 `updateTransitions` 에서 슬롯 뮤텍스로 꺼낸 뒤 활성화하므로 `_listComponent` 순회와 겹치지 않는다.
- 확인함(2026-10-20, 코드 추적, 실행은 안 함): `destroyComponentInstance` 의 배타 잠금(`GameObjectStore.cpp:434`)은 저장소 `_mutex` 를 쥔 블록 안에서 부르는 경로가 없다(지연 파괴는 잠금을 푼 뒤 `removeComponent` 를 부르고, 순회 안 호출은 `SW_ASSERT( isInsideWalk == false )` 가 막는다). 순회 콜백(shared 잠금)에서 사용자 코드가 부르는 경로는 단언이 실행 중에만 잡는다. `ComponentHandle` 바이너리는 16 바이트(objectID, componentID)이고 파일 형태 `{대상 저장 id, 키 해시(최상위 비트 표지)}` · 런타임 형태 `{런타임 id, id}` 를 읽는 쪽이 표지 비트로 가르며 텍스트는 `오브젝트:컴포넌트` 다(`ComponentHandle.h:47`, `ObjectStateSerializer.cpp:336`). 7-b(다중 상속 금지)는 병합돼 있고 린트가 아니라 반사 생성기가 둘째 기반 · 가상 기반을 오류로 막는다(`AstVisitor.cpp` 의 `collectBase`).
- **허용 여부 판단**: 타입 없는 `ComponentHandle` 의 `PROPERTY` 는 막혀 있습니다(3.7). 안정 키 `이름#n` 은 같은 이름의 컴포넌트 순서가 바뀌면 다른 컴포넌트를 가리킵니다(부착과 같은 한계).
- 모든 헤더 템플릿이 멤버 위치를 컴파일해 넣으므로(`GameObjectStore.h`) 이 영역을 바꾸면 게임 · 킷 · 에디터 · RHI 를 함께 다시 빌드합니다. 부분 빌드로 확인하지 않습니다.

## 2. 7-c 매니저의 타입별 컴포넌트 목록

### 2.2 자료구조: 정확한 타입마다 버킷 하나

- 버킷은 컴포넌트 풀 항목(`_mapComponentPool`, 키는 타입 FQN)에 합칩니다. `ComponentPoolEntry` 는 `{ _pool, _listComponent(버킷), _pType }` 이고, 버킷이 비어 있을 때 `_pType` 은 nullptr 입니다.
  항목 목록(`_listComponentPoolEntry`)은 처음 나온 순서이고 조회가 이것을 돕니다. `Component` 는 `_pPoolEntry`(항목 포인터. 힙에서 잡았으면 nullptr, 버킷에 들지 않음)와 `_poolEntrySlot`(버킷 안 위치, 제거 O(1))을 가집니다.
- 조회 `forEachComponentOfType<T>` 는 `T` 의 `TypeInfo` 를 한 번 구하고, `_pType` 이 있는 버킷마다 `isDerivedFrom` 을 한 번 물어 맞는 버킷만 방문합니다. 런타임 `TypeInfo` 를 받는 비템플릿 버전도 있고 인스펙터 선택기가 씁니다.
- **조상 목록(ReflectionFormats 7.2)을 쓰지 않은 이유.** 컴포넌트마다 조상 깊이만큼 위치를 보관해야 해서 메모리가 약 여덟 배(깊이 8 에서 96 B 대 12 B)이고 추가 · 제거도 깊이만큼 일합니다. 정확한 타입 버킷은 조회가 버킷 수 K 에 비례하지만, 측정으로 K 500 에서도 약 1 µs 였습니다(8.4).
- 키를 `TypeInfo` 포인터가 아니라 FQN 으로 잡아, 같은 클래스에 `TypeInfo` 사본이 둘 생겨도 같은 버킷을 씁니다. 비운 버킷의 벡터 용량과 항목은 남겨 같은 이름으로 스폰과 파괴를 거듭하는 게임(총알)이 매번 할당하지 않게 합니다.

### 2.3 갱신 규칙

모든 갱신은 저장소 `_mutex` 의 배타 잠금 안에서 하고, 조회는 공유 잠금입니다.

1. **추가.** `attachCreatedComponent` 가 오브젝트 목록에 넣는 줄 바로 옆에서 버킷에 넣습니다(`onRegister` 안에서 씬을 찾는 코드가 지금과 같은 답을 받도록). 버킷이 비어 있었으면 `_pType` 을 채웁니다.
2. **제거.** 해체는 `destroyComponentInstance` 하나를 지나고, 그 함수의 맨 앞(`dispatchEndPlay` 보다 먼저)에서 뺍니다. 마지막 원소를 빈 위치로 옮기고 그 원소의 슬롯을 고칩니다.
3. **지연 제거와 오브젝트 파괴.** 삭제 대기 컴포넌트는 실제 해체까지 버킷에 남고, 조회는 `pComp->isPendingDestroy()` 하나만 봅니다. 근거는 불변식 "삭제 대기 오브젝트의 컴포넌트는 항상 삭제 대기"이고 형식으로 지킵니다.
   오브젝트 표시는 비공개 `tryMarkPendingDestroy` 하나이며 컴포넌트를 먼저 표시합니다. 삭제 대기 오브젝트에 붙는 컴포넌트는 `attachCreatedComponent` 가 삭제 대기로 시작시키고, 해체 중 오브젝트에는 `allocateComponentStorage` 가 붙이기를 거절합니다. Debug 는 순회와 지연 파괴에서 불변식을 단언합니다.
4. **`clear`.** 규칙 2 로 모든 버킷이 비고 `_pType` 이 모두 nullptr 인지 Debug 단언으로 확인합니다.
5. **버킷이 비면 `_pType` 을 nullptr 로.** 모듈 언로드의 묘비 처리입니다. `destroyComponentsOfModule` 이 그 모듈 타입의 인스턴스를 모두 지우므로 언로드된 `TypeInfo` 를 가리키는 버킷은 구조적으로 생기지 않고, 훅을 거치지 않는 매니저(프리팹 원형)도 안전합니다.
6. 오브젝트는 매니저를 옮기지 않습니다(`_pOwnerManager` 는 만들 때 한 번). 핫 리로드의 같은 FQN 재등록은 버킷에 영향이 없습니다.

### 2.4 순회 중 변경과 순서

- 조회 콜백은 공유 잠금과 `WalkScope` 안에서 불리고, 그 안에서 구조를 바꾸면 Debug 단언에 걸립니다. 버킷 등록과 `destroyComponentInstance` 의 제거 앞에 `SW_ASSERT( WalkScope::isInsideWalk() == false )` 가 있습니다(배타 잠금이 멈춤이 되므로 단언으로 알림).
- 병렬 틱 중의 구조 변경은 틱 뒤로 미뤄집니다(`StructuralChangeBuffer`). 삭제 표시는 저장소 잠금 밖에서 하므로 다른 워커의 조회와 겹칠 수 있는데, 표시 전에 읽은 컴포넌트를 방문하는 것은 파괴가 방문 뒤에 일어난 것과 같은 답입니다. 해체는 틱 뒤 게임 스레드의 배타 잠금이라 방문한 포인터는 콜백 동안 유효합니다.
  컴포넌트 표시(release)가 오브젝트 표시보다 먼저라서 오브젝트가 삭제 대기인 것을 본(acquire) 스레드는 컴포넌트도 삭제 대기로 봅니다.
- **순서.** 매니저 순회는 순서를 약속하지 않습니다(헤더 주석). 새 순서는 "버킷 순서(타입이 처음 나온 순서), 그 안의 추가 순서"이고, 오브젝트 파괴가 이미 순서를 바꿨으므로 새로 생기는 약속 위반은 없습니다.
  순서가 결과에 나오던 곳은 `SceneNavigationCooker`(수집 목록을 이름순 정렬하고 중복 제거)와 `ComponentStateStore`(안정 키와 타입으로 짝짓기, 8.2)였고 둘 다 고쳤습니다. "첫 것"을 고르는 디렉터 찾기는 씬에 하나만 있다고 가정합니다.

## 3. 7-d 타입 핸들 `TComponentHandle<T>`

### 3.1 형식

`Core/Container/ComponentHandle.h`. 크기는 `ComponentHandle` 과 같은 16 B 이고 trivially copyable 입니다.

| 멤버 | 뜻 |
|---|---|
| 기본 생성자 | 무효 핸들(대상 없음의 정상 값) |
| `explicit TComponentHandle( const T* )` | `T` 나 그 파생에서 만듭니다. nullptr 이면 무효 |
| `TComponentHandle( TComponentHandle<U> )`, `U` 가 `T` 의 파생일 때만 | 업캐스트 |
| `untyped()` | 타입을 지운 `ComponentHandle`. 레지스트리 · 지연 파괴 목록처럼 타입을 모르는 곳에 넘길 때 |
| `objectID()` · `componentID()` · `isValid()` · `operator bool` · `==` · `!=` | `ComponentHandle` 과 같음 |

- **ⓓ 비교.** 타입 핸들과 타입 없는 핸들을 섞은 `==` · `!=` 를 허용합니다. 비교는 id 만 보고 주소를 만들지 않아 안전합니다. 서로 관계없는 두 타입 핸들의 비교는 컴파일 오류이고, 파생과 기반은 업캐스트로 비교합니다. C++17 이라 양쪽 순서를 모두 적습니다.
- 타입 없는 핸들에서 바로 만드는 공개 생성자는 없습니다. 확인 함수(3.2)만 만들 수 있게 패스키 태그를 받고, 태그는 `GameObjectManager` 만 만듭니다. 다운캐스트도 3.2 를 거칩니다.
- 키로 쓰는 해시 · 정렬 특수화는 없습니다.

### 3.2 타입 없는 핸들에서 만들기

`GameObjectManager::makeTypedHandle<T>( ComponentHandle, TComponentHandle<T>& )` 는 `[[nodiscard]]` 결과를 반환합니다.

| 입력 | 결과 | 출력 |
|---|---|---|
| 무효 핸들 | 성공 | 무효 타입 핸들 |
| 대상이 있고 `isDerivedFrom( T )` | 성공 | 타입 핸들 |
| 대상이 있고 타입이 다름 | **타입 불일치 오류**, 오류 로그 | 바꾸지 않음 |
| 대상이 없음(파괴됨) | **대상 없음 오류**, 오류 로그 | 바꾸지 않음 |

실패를 빈 핸들로 바꾸지 않습니다. 빈 핸들은 "대상 없음"이라는 정상 값과 구분되지 않아 타입이 틀린 핸들이 조용히 사라집니다. 이 변환을 쓰는 곳은 역직렬화 · 에디터 · 타입을 모르는 `Component*` 에서 만든 핸들뿐이어야 합니다.

### 3.3 찾기

`resolveComponent<T>( TComponentHandle<T> ) const` 는 `T*` 를 반환합니다. 타입 없는 찾기 뒤 `static_cast<T*>` 이고 삭제 대기의 처리는 같습니다.

- **ⓑ Shipping 이 아닌 모든 구성에서 타입을 확인합니다**(Debug 단언만이 아님). nullptr 이 아닌데 `isDerivedFrom( T::StaticType() )` 가 아니면 오류 로그와 Debug 단언을 남기고 nullptr 을 반환합니다. 타입이 바뀌는 길(핫 리로드, 되돌리기의 id 복원)이 Shipping 에 없어 Shipping 만 뺍니다.
- 이 확인이 필요한 이유: id 는 프로세스 전체에서 한 번만 발급되고 같은 id 를 다시 쓰는 곳은 `ComponentIDRestoreScope` 뿐인데, **그 복원이 `TypeInfo::_name`(짧은 이름)으로 맞추면 짧은 이름이 같은 다른 네임스페이스의 타입이 원래 id 를 가져가** 핸들이 다른 타입을 가리킵니다.
  그래서 복원 키를 FQN 으로 바꿨습니다(`ObjectIdentity::ComponentEntry::_typeFqn`, `takeRestoredComponentID`, `captureIdentity`; 테스트 `TestObjectIdentity.cpp`). 복원 목록은 같은 프로세스에서만 읽으므로 저장 형식 호환 문제는 없습니다.
- `static_cast` 는 `T` 가 `Component` 를 가상이 아닌 방식으로 상속할 때 포인터 보정까지 맞습니다. 다중 상속 `REFLECT` 타입은 단계 7-b 가 생성 오류로 막습니다.
- **ⓒ `executeOrDeferPostTickOnComponent( const TComponent& self, func )`.** `TComponent` 는 인자에서 추론하고, 함수가 `self` 에서 타입 핸들을 만들며 람다 안의 `static_cast` 와 단언은 `resolveComponent<T>` 로 대신합니다. 호출부에서 `getHandle()` 과 템플릿 인자가 함께 사라져 둘이 어긋나는 일이 형식으로 막힙니다. `const` 참조인 것은 `const` 멤버 함수의 `*this` 를 받기 위해서이고, 일은 다시 찾은 `T&` 로 받습니다.
- `getComponent<T>` 처럼 `T` 가 자기 `REFLECT_BODY` 를 가졌는지 `static_assert` 로 확인합니다.

### 3.4 호출부 마이그레이션의 규칙

"찾고 바로 캐스트"하던 호출부(`castTo` 17곳, 확인 없는 `static_cast` 35곳)와 `ComponentHandle` 필드는 7-d-2 그룹 A~E 로 모두 타입 핸들로 바꿨습니다.
**ⓖ 결함이 숨는 곳은 API 경계를 넘는 핸들**(인자 · 대기 항목 · 접근자 반환값)입니다. 자기 핸들과 `T*` 에서 바로 만든 지역 핸들은 타입이 어긋날 수 없고, 경계를 넘는 핸들은 받는 쪽이 출처를 모릅니다.
예가 `ControlSystem::queuePossess( controller, pawn )` 로, 서로 다른 타입의 핸들 둘을 같은 형식으로 받으면 인자를 바꿔 넘겨도 컴파일됐고 타입 핸들로 바꾸면 컴파일 오류가 됩니다.
`castTo` 를 `resolveComponent<T>` 로 바꾸면 동작이 하나 달라집니다. 타입이 다를 때 nullptr 이 아니라 Debug 단언에 걸리므로(결함이 드러남), 핸들에 다른 타입이 들어갈 수 있는 곳은 타입 없는 채로 둡니다.
**타입 없는 채로 둔 곳**: 여러 타입을 받는 `AnimationNotifyComponent` 의 `_animator`(스켈레탈 · 스프라이트 애니메이터), `BenchScene` 의 `_listBenchExtra`(라이트와 바닥), 엔진 내부의 일반 경로(`ComponentRegistry` 항목, 저장소의 지연 파괴 · 시작 대기열, `SceneTransformHierarchy`).

### 3.7 직렬화 (7-d-3)

**구현됨 (2026-10-19).** `PROPERTY` 로 둘 수 있는 것은 `TComponentHandle<T>` 한 값과 그 시퀀스(한 겹)입니다. 타입 없는 `ComponentHandle` · 맵 · 중첩 컨테이너는 대상 타입을 확인할 자리가 없어 파서 생성 오류입니다(ⓔ).
이유: 한 `GameObject` 에 컴포넌트가 여럿 들어 있을 때 서로를 가리키는 값이 저장되어야 합니다(사용자 결정). 막아 둔 이유였던 "옮길 테이블 없음"은 8.2 로 풀렸습니다.
파서는 값 · 원소의 타입 이름을 내장 `ComponentHandle` 로 기록하고(직렬화기 · 인스펙터의 기존 처리기가 그대로 동작) 대상 `T` 를 `PropertyInfo::_handleTargetTypeName` 에 싣습니다(`AstVisitor.cpp` `resolveComponentHandleProperty`).
- **파일 형태.** `{대상 오브젝트의 저장 id, 컴포넌트 안정 키}` 입니다(텍스트는 `0:TestProbeComponent#1`, 바이너리는 키의 해시). 오브젝트 자리가 0 이면 이 오브젝트 자신입니다. 저장 id 표가 있을 때(씬), 프리팹, 세이브가 이 형태로 씁니다.
  스냅샷 · 되돌리기 · 복제는 런타임 id 를 그대로 적습니다. 읽는 쪽은 키 해시의 표지 비트(최상위 비트)로 두 형태를 가립니다. 그래서 세이브를 같은 실행(Live)에서 복원하든 다른 실행(Saved)에서 읽든 맞습니다.
- **읽기와 타입 확인.** 배치 로드는 여러 오브젝트를 차례로 만들어 필드를 읽을 때 대상이 아직 없을 수 있으므로 **확인은 배치가 끝날 때**(`ObjectStateBatch::finish`)에 합니다. 오브젝트 핸들과 같은 깊이의 값 방문(`ReflectedValueWalk`)으로 컴포넌트 핸들을 옮깁니다.
  오브젝트나 키를 못 찾으면 핸들을 비우고 경고합니다(약한 참조라 대상이 사라진 핸들은 정상 값). **대상이 있는데 대상 타입이 아니면 로드 오류**이고, 런타임 id 로 쓴 핸들을 파일 id 공간(Saved)으로 읽는 것도 로드 오류입니다(우연히 같은 id 를 받은 다른 컴포넌트를 가리키므로).
  `finish` 는 `[[nodiscard]] bool` 을 반환하고 씬 로드 · 플레이 세션 복원 · 복제 · 붙여넣기 · 세이브 복원 · 씬 넘김 · 단독 상태 로드가 실패로 처리합니다. 저장 id 공간에서 `ComponentHandle` 을 경고와 함께 비우던 폴백은 지웠습니다.
- **ⓕ 같은 근본 원인.** 핸들 옮기기는 컴포넌트 타입의 최상위 `PROPERTY` 만 보던 것을 값 방문으로 바꿨습니다(8.1). 구조체 · 컨테이너의 모든 깊이를 옮깁니다.
- 테스트: `TypedComponentHandleTest`(왕복 세 형식, 잘못된 타입, 사라진 대상, 런타임 형태와 세이브), `ReflectionParserTest.ComponentHandlePropertyShapes`.

### 3.8 인스펙터

**구현됨 (2026-10-19).** 타입 핸들 필드는 선택기(콤보)로 그리고 정수 입력은 두지 않아, 인스펙터로는 잘못된 타입을 넣을 수 없습니다. 항목은 "오브젝트 이름 / 컴포넌트 이름"이고 목록은 비템플릿 매니저 순회 `forEachComponentOfType( 대상 TypeInfo, ... )` 로 채웁니다.
판단은 ImGui 없는 `InspectorComponentPicker` 가 하고(단위 테스트 6건), 시퀀스 원소의 `PropertyInfo` 는 `makeElementProperty` 한 함수로 만들어 대상 타입이 빠지지 않습니다.
선택 값은 `EditorPropertyGrid::applyPropertyTextAsEdit` 로 써서 되돌리기가 되고, 목록은 편집 대상 씬(`getEditOwner`)에서만 채웁니다. 선택기가 쓰는 값은 열거한 컴포넌트의 `getHandle()` 뿐입니다. 타입 핸들의 Paste Value 는 막았습니다.
`resolveComponent` 는 삭제 대기 컴포넌트도 돌려주므로 선택기가 `isPendingDestroy()` 를 따로 봅니다. 타입 없는 `ComponentHandle` 위젯(정수 두 개 입력)은 그대로입니다. 남은 것은 화면 확인입니다(위 "남은 확인").

## 8. 7-c 와 7-d 뒤의 남은 정리

### 8.1 핸들 옮기기를 값 방문으로 (끝)

`ObjectStateBatch::resolveObjectReferences` 가 리플렉션 값 방문(`Engine/Reflection/ReflectedValueWalk.h`)으로 구조체 필드 · 컨테이너의 모든 깊이를 옮깁니다. 같은 방문은 쿠킹 의존성 수집에도 씁니다.

### 8.2 컴포넌트 상태 저장소 짝짓기 (끝)

`ComponentStateStore::appendComponentIDRemap` 이 안정 키(`이름#n`)와 타입으로 잇습니다. 오브젝트 id 묶음이 키를 싣습니다(게임 상태 봉투 4 판).

### 8.3 등록형 목록을 버킷으로 (끝: 옮기지 않는다)

**결론 (2026-10-19).** 옮길 목록이 없습니다. 호출부 전수 조사에서 `ComponentRegistry` 의 타입 대부분이 등록 순서에 기대는 것으로 나왔고, 버킷 순회는 순서를 약속하지 않으므로 옮기면 동작이 바뀝니다.

| 타입 | 순서에 기댐 | 근거 |
|---|---|---|
| `LightComponent`(종류별 칸) | 예 | `ComponentRegistry.h:17`, `LightComponent.cpp:66` |
| `PlayerControllerComponent` · `ControllerComponent` · `PawnComponent` | 예(첫 일치 또는 마지막 일치) | `ControlSystem.cpp:99,149,159`, `ShooterPlayerComponent.cpp:144` |
| `CameraDirectorComponent` | 예(마지막 일치가 이김) | `ShooterPlayerComponent.cpp:646`, `ShooterDirectorComponent.cpp:748` |
| `WaterBodyComponent` · `TerrainComponent` · `WindComponent` · `FoliageInfluencerComponent` | 예(첫 일치 · 동률 순서) | `WaterBodyComponent.cpp:451`, `TerrainComponent.cpp:640`, `WindComponent.cpp:47` |
| `VehicleSeatComponent`(대부분) · `GimmickCircuitComponent` · `ClimbZoneComponent` · `ShadowCaster2DComponent` | 예 | `MountUtil.cpp:249`, `GimmickStateBundle.cpp:44`, `PlatformerGimmicks.cpp:208`, `GPULightBuffer.cpp:67` |
| `InteractableComponent` · `SmartObjectComponent` | 미확인 | `InteractorComponent.cpp:137`, `SmartObjectScheduleLocator.cpp:77` |
| `TerrainComponent`(개수만) · `VehicleSeatComponent`(존재 검사) | 아니요 | `AutomationRunner.cpp:447`, `WesternHorseMountComponent.cpp:95` |

- 세 기준은 (1) 등록 순서가 결과에 나오는가 (2) 채널(한 타입 안의 하위 목록)이 있는가 (3) 잠금 없이 읽어야 하는가입니다. 세 번째는 전 타입 미확인이라 "셋 모두 아니요"로 확인된 타입은 0 개입니다.
- 조건부 후보 둘(개수, 존재 검사)은 질의 한 번에 몇 ns 도 안 되는 호출이라 옮겨도 얻는 것이 없고, 같은 타입의 다른 호출부와 목록이 둘로 갈라집니다. `TickBatchRegistry` 는 컴포넌트 목록이 아니라 타입에서 일괄 틱 함수를 찾는 표입니다.
- 같은 모양의 약점: 마지막 일치가 이기는 루프와 최근접 동률(`MountUtil.cpp:249`)은 등록 순서에 기댄 코드입니다. 순서를 형식으로 드러내는 일(우선순위를 받는 질의)은 이 계획 밖이라 후보로 두지 않았습니다.

### 8.4 측정

벤치는 `TypedComponentBenchTest`(`Test/EngineTest/Object/TestTypedComponentBench.cpp`)이고 `build/Ninja-Release/Bin` 에서
`../TestBin/EngineTest.exe --test_filter=TypedComponentBenchTest.* --load_suites=only --host_suites=all` 로 돕니다(GPU 를 쓰지 않아 셰이더 베이크 · VSync 는 해당 없음). 측정 PC `SW` · Ryzen 7 6800H, Release.
같은 바이너리를 다섯 번 돌려 판마다 질의 방식을 번갈아 재고, 값은 질의 한 번의 ns, 다섯 판 p50 · p99 의 가운데 값입니다(표본 30 개). 타입 500 개는 `BenchBucketComponent<N>` 인스턴스에 `REFLECT_BODY()` 를 쓰고 `TypeInfo` 를 손으로 만들어 `TypeRegistry::registerClass` 로 올립니다
(코드젠 파서는 템플릿 인스턴스마다 `TypeInfo` 를 만들지 않으므로 게임 코드의 템플릿 컴포넌트는 여전히 안 됨). "훑기"는 7-c 전 방식(오브젝트 순회 + `castTo`)입니다.

**결과 (2026-10-19, 삭제 대기 불변식 뒤, p50).** 단위 ns.

| N · K | 버킷 말단 | 등록형 말단 | 등록형 + 검사 하나 | 버킷 기반 | 등록형 기반 | 훑기 말단 | 훑기 기반 |
|---|---|---|---|---|---|---|---|
| 1만 · 50 | 385 | 96 | 319 | 15,791 | 7,739 | 73,690 | 69,006 |
| 1만 · 500 | 1,081 | 12 | 35 | 23,758 | 7,442 | 75,643 | 72,646 |
| 10만 · 50 | 2,952 | 1,492 | 3,181 | 184,355 | — | 2,643,460 | 2,742,540 |
| 10만 · 500 | 1,178 | 95 | 319 | 192,333 | — | 2,753,210 | 2,845,220 |

- 7-c 의 목적은 이뤘습니다. 버킷 순회는 옛 훑기보다 말단에서 70 ~ 2000 배, 기반에서 약 4 ~ 15 배 빠릅니다(10만 기반 2.7 ms → 0.18 ms).
- 첫 버킷 순회(컴포넌트와 소유 오브젝트를 모두 검사)는 등록형 목록보다 느렸고(10만 · 50 말단 8.3 µs 대 1.4 µs), 차의 대부분은 소유 오브젝트 읽기(컴포넌트 풀과 다른 메모리)였습니다. 불변식으로 검사를 컴포넌트 표시 하나로 줄여 10만 · 50 말단이 8.3 → 3.0 µs, 10만 기반이 1.2 ms → 0.18 ms 가 됐고
  "등록형 + 같은 검사"(3.18 µs)와 같은 수준입니다(결정 기록 760). 이 검사 몫의 크기는 소유 오브젝트 읽기가 검사 전체 비용의 46 %(1만) ~ 75 %(10만)였습니다.
- 검사 없는 등록형 목록보다는 여전히 느립니다(10만 · 50 말단 2.0 배). 남은 차는 컴포넌트마다 읽는 삭제 대기 표시(필드 위치별 측정은 안 함 [추정])와 K 에 비례하는 버킷 훑기와 `isDerivedFrom`(버킷 하나에 약 2 ns, K 500 에서 약 1 µs)입니다. 등록형 목록은 삭제 대기를 걸러 주지 않으므로 같은 답을 내려면 호출부가 검사를 스스로 내야 합니다.
- 오브젝트 하나 만들고 지우기(1만 · K 200, 컴포넌트 하나)는 p50 433 · p99 543 ns 로 불변식 전 422 · 481 ns 와 같은 크기입니다. 경합 없는 `std::shared_mutex` 배타 잠금 한 쌍은 4.2 ~ 4.3 ns 라 버킷이 더한 잠금 두 쌍은 약 9 ns(2 %)로 추정합니다.
  `sizeof( Component )` 는 88 B, `GameObject` 는 새 플래그 `_bBeingDestroyed` 가 꼬리 패딩에 들어가 크기를 묶는 `static_assert` 를 통과합니다.
- 등록형 기반 목록은 1만에서만 쟀습니다(`add` 의 중복 검사와 순서를 지키는 `remove` 가 길이에 비례해 10만에서 장면 만들기가 O(n²)).
- **앱 측정 결과 (2026-10-20, 독점 Release, 같은 PC, 5 판 순서 교대, 프로파일 표가 정수 µs 라 낮은 값은 해상도 한 칸 이하).** 전 = 7-c 바로 앞 `a458db532`, 후 = 측정 시점 HEAD. **이 비교는 7-c 만이 아니라 그 뒤 453 커밋을 함께 견준 값이다.**
  - ① `tickCostChurn`(benchMeshes 4000): churn20 은 `GT.Game.update` p50 28 → 30 µs 로 해상도 안이다. churn500 은 `GT.Game.update` p50 294 → 327 µs(+11 %), `GT.Frame` p50 589 → 720 µs 로 후가 일관되게 10 ~ 20 % 높다(만들고 지우기 500 회 기준 약 66 ns 씩). 원인이 7-c 인지 그 뒤 커밋인지는 가르지 못했다.
  - ③ 큐브 20,000 개: `GT.Frame` p50 1310 → 1179 µs, p99 1703 → 1703 µs, `GT.Game.update` p50 720 → 720 µs, `RT.Frame` p50 1179 → 1048 µs 로 차이가 5 판 퍼짐 안이다. `startup`(버킷 넣기 2 만 번 포함)은 632 → 639 ms.
  - ② 비동기 씬 로드 중 조회(N 1 만 · K 200 leaf, 로드 5 만 개): 조회 한 번 p50 · p99 가 idle 과 같다(p50 400 ~ 500 ns, p99 500 ~ 600 ns). 로드 35 ~ 37 ms 동안 6.1 ~ 7.2 만 번 돌았고 max 는 두 경우 모두 한 자릿수 ~ 십수 µs(로드 중 한 판만 28.8 µs). 로드 워커가 조회를 막지 않는다는 코드 읽기 결과와 맞는다. 시계 해상도 100 ns.
  - ④ `resolveComponent<T>` 확인 비용: base 1 만 핸들 typed(checked) 약 110 µs 대 untyped + `static_cast` 약 75 µs, 차 약 35 µs(핸들당 약 3.5 ns). leaf 50 핸들은 423 ~ 457 대 217 ~ 232 ns(핸들당 약 4 ~ 4.5 ns).
  - ⑤ 게임별 버킷 수 K(600 프레임 시점): Empty 3 · AbilityArena 3 · NileCity 6 · MeadowVillage 7 · StarSkirmish 7 · ThemeParkTycoon 7 · VoxelCraft 10 · PixelTown 11 · HarvestValley 13 · Shooter3D 19 · WildWest 21. 모두 수십 이하라 위 표의 K 50 줄(버킷 훑기 몫이 작다)이 실제 게임에 해당한다. 같은 짧은 이름은 하나로 세어 약간 낮을 수 있다.

K 가 수백인 씬에서 자주 묻는 질의는 조회 타입마다 맞는 버킷 목록을 캐시하는 안(B 안)이 먼저 필요합니다. 넣을 때는 타입 레지스트리 세대로 무효화하고, 측정으로 필요가 확인될 때만 넣습니다.

### 8.5 앱 측정 실행 절차 (독점 Release, 한 번에)

앱 측정을 독점 Release 에서 한 번에 돌리는 절차입니다. 2026-10-20 에 이 절차로 한 번 돌렸고 값은 8.4 에 있습니다. 다시 재려면(예: 측정 PC 가 바뀔 때) 같은 절차를 되풀이합니다.

| 측정 | 도구 | 읽을 지표 |
|---|---|---|
| ① `tickCostChurn` 전후 | 앱 `-gv_` 스위치, 전 바이너리와 후 바이너리 | `GT.Game.update`(벤치의 스폰 · 파괴가 든다) · `GT.Frame` · `GT.Scene.tick.components` 의 p50 · p99 |
| ② 비동기 씬 로드 중 조회 | `TypedComponentBenchTest.QueryWhileAsyncSceneLoad` | `[Bench] single bucket query, idle` 대 `… while an async scene loads` 의 p50 · p99 · max 와 표본 수 |
| ③ 큐브 20,000 개 `GT.Frame` | 앱 `-gv_benchMeshes=20000`, 전 · 후 | `GT.Frame` · `GT.Game.update` · `RT.Frame` 의 p50 · p99, `startup N ms`(장면 만들기 = 버킷 넣기 2만 번) |
| ④ `resolveComponent<T>` 확인 비용 | `TypedComponentBenchTest.ResolveTypedCheckCost` | `typed (checked)` 줄과 `untyped + static_cast` 줄의 차 |
| ⑤ 게임별 버킷 수 K | 임시 시나리오 + 개발 명령 `scene.tree` | 활성 씬의 구성 요소 이름 가짓수 |

**준비 (독점 전에).** 모두 빌드 칸 안에서 합니다([작업 방식](../11_Workflow.md) 2절).

- 후 바이너리: 측정할 체크아웃에서 `cmake --preset Ninja-Release -DSW_GAMES=ALL`(vcpkg 잠금 안에서) → `cmake --build --preset Ninja-Release --target all AllTests` → `build\Ninja-Release\Bin\App.exe --cook-shaders`. ⑤ 는 게임 전부가 든 이 빌드를 씁니다.
- 전 바이너리(①③): 7-c 바로 앞 커밋 `1828fc879^`(= `a458db532`)을 따로 짓습니다. `py -3 -m Scripts worktree-make tcbefore --base 'a458db5328b6dfc050403a68bf44d1518116f6cc'` → 그 폴더에서 `cmake --preset Ninja-Release`(활성 게임 Empty) → `cmake --build --preset Ninja-Release` → `App.exe --cook-shaders`.
  전과 후는 `vcpkg.json` 이 같지만(`git diff 1828fc879^ HEAD -- vcpkg.json` 이 비어 있음) 오버레이 포트 폴더(`ThirdParty/imgui-node-editor/vcpkg-port`)와 `Vcpkg.cmake` 가 다릅니다. 워크트리는 `build/vcpkg_installed` 를 공유하므로, 구성할 때 vcpkg 가 공유 설치 폴더를 다시 설치하지 않는지 로그로 봅니다(다시 설치하면 멈추고 사용자에게 알립니다).
  이 비교는 7-c 만이 아니라 그 뒤 `HEAD` 까지의 453 커밋을 함께 견줍니다. 7-c 만 가르려면 후 쪽을 `1828fc879` 로 짓습니다.
- 독점: 다른 빌드 · 테스트를 멈추고 `py -3 -m Scripts shared-lock acquire gpu --owner <이름>`(독점, `--shared` 없이). 끝나면 `release`. 측정 PC 가 바뀌면 8.4 값과 견주지 않고 같은 PC 의 전 · 후만 견줍니다.

**앱 측정 도우미 (①③).** 한 판은 `-gv_profileFrames=600`(앞 60 프레임은 엔진이 버립니다) 뒤 `[Profile]` 표를 찍고 끝납니다. 아래 도우미는 필요한 줄만 고릅니다.
`$After` 와 `$Before` 는 두 `App.exe` 의 경로입니다.

```powershell
function Measure-App( $App, $Tag, $ExtraArg ) {
    $log = Join-Path $env:TEMP "tcbench_$Tag.log"
    Start-Process -FilePath $App -ArgumentList ( @( '-game=Empty', '-dx12', '-gv_profileFrames=600' ) + $ExtraArg ) -WorkingDirectory ( Split-Path $App ) -Wait -NoNewWindow -RedirectStandardOutput $log
    Select-String -Path $log -Pattern '\[Profile\] (GT\.Frame|GT\.Game\.update|GT\.Scene\.tick\.components|RT\.Frame|wall) |startup' | ForEach-Object { $_.Line.Trim() }
}
# 전 · 후를 판마다 순서를 바꿔 다섯 번(기계 상태의 흐름이 한쪽에 실리지 않게). 판별 표에서 가운데 값을 읽는다.
function Measure-AB( $Case, $ExtraArg ) {
    foreach ( $run in 1..5 ) {
        $order = if ( $run % 2 -eq 1 ) { 'Before', 'After' } else { 'After', 'Before' }
        foreach ( $side in $order ) {
            "--- $Case run $run $side"
            Measure-App ( Get-Variable $side -ValueOnly ) "$Case-$side-$run" $ExtraArg
        }
    }
}
```

- ① 기본: `Measure-AB churn20 '-gv_benchMeshes=4000','-gv_benchTickMovers=1','-gv_benchSpawnChurn=20'`. 이 값은 합성 케이스 `tickCostChurn` 의 인자와 같습니다.
  프레임마다 20 개 교체는 8.4 의 "만들고 지우기" 433 ns 로 어림하면 약 9 µs 라 잡음에 묻힐 수 있습니다[추정, 측정 아님]. 키운 판 `Measure-AB churn500 '-gv_benchMeshes=4000','-gv_benchTickMovers=1','-gv_benchSpawnChurn=500'` 도 함께 돌립니다.
  전 바이너리에도 두 스위치가 있어야 합니다(확인함: `1828fc879^` 의 `BenchScene.cpp` 에 `-gv_benchSpawnChurn` · `-gv_benchTickMovers` 있음).
- ③: `Measure-AB cubes20000 '-gv_benchMeshes=20000'`. 큐브가 많으면 병목이 GPU 대기에서 게임 스레드로 뒤집히므로(검증 문서 2절) `GT.Frame` 과 `RT.Frame` 을 함께 보고, `RT.Frame` 이 주사율의 역수면 VSync 가 켜진 것입니다(`-vsync` 를 주지 않았는데 그러면 측정을 버립니다).
- 후 바이너리 하나로도 절대값을 얻을 수 있지만, 판단(7-c 의 비용이 프레임에서 보이는가)은 전 · 후 차로 합니다.

**② ④ 벤치 케이스.** GPU 를 쓰지 않아 `build\Ninja-Release\Bin` 에서 8.4 와 같은 명령입니다(독점 안에서 5 번).

```powershell
cd build\Ninja-Release\Bin
foreach ( $run in 1..5 ) { ..\TestBin\EngineTest.exe --test_filter=TypedComponentBenchTest.ResolveTypedCheckCost --load_suites=only --host_suites=all | Select-String '\[Bench\]' }
foreach ( $run in 1..5 ) { ..\TestBin\EngineTest.exe --test_filter=TypedComponentBenchTest.QueryWhileAsyncSceneLoad --load_suites=only --host_suites=all | Select-String '\[Bench\]' }
```

- ④ 값은 핸들 목록 전체를 푸는 ns 입니다(`base` 는 1만 개, `leaf` 는 50 개). 핸들 하나의 확인 비용 = (`typed` − `untyped`) ÷ 개수. 두 값의 합계(`_payloadSum`)가 같은지는 케이스가 확인합니다. Shipping 에서는 확인이 빠지므로 이 케이스는 Release 에서만 뜻이 있습니다.
  앱에서 프레임당 비용은 (프레임당 `resolveComponent<T>` 횟수) × 그 차입니다. 앱에서 직접 크게 보려면 `-gv_benchMaterialChurnAdd=N`(프레임마다 `updateMaterialChurn` 이 큐브 N 개를 타입 핸들로 푼다)이 있지만 머티리얼 인스턴스 생성이 섞여 확인 비용만 가르지 못합니다.
- ② 의 `idle` 과 `while an async scene loads` 는 같은 방식(조회 한 번을 시계 사이에 끼움)으로 잰 값이라 서로 견줍니다. 표본이 수십 개 아래면 `kLoadedObjectCount` 를 키웁니다(`[Bench] async load of … took … ms` 줄이 로드에 걸린 시간과 그 사이의 조회 수를 보입니다).
  p50 이 같고 p99 · max 도 같은 크기면 "로드 워커가 조회를 막지 않는다"(남은 확인의 코드 읽기 결과)와 맞습니다. 꼬리만 튀면 그것이 잠금 경합인지 워커가 코어를 쓰는 탓인지 이 케이스로는 갈리지 않으므로 `-gv_tracy=1` 타임라인으로 봅니다.
  앱 쪽 시각은 `py -3 -m Scripts scene-load-bench --app <후>\App.exe --count 8000 --runs 5` 의 "로드 중 `GT.Frame` 최악"(기준값은 검증 문서 2절)입니다.

**⑤ 게임별 버킷 수 K.** 비어 있지 않은 버킷 수 = 활성 씬에 지금 살아 있는 정확한 컴포넌트 타입의 가짓수입니다. 개발 명령 `scene.tree`(`SceneInspectCommands.cpp`)가 오브젝트마다 `components=타입,…` 를 찍으므로, 한 번 부르고 이름을 모아 센다. 시나리오는 리소스 ID 로만 읽혀서 임시 파일을 `Resource` 에 두고 끝나면 지웁니다(커밋하지 않는다).

`Resource\engine\automation\tmp_bucketcount.scenario.xml`:

```xml
<?xml version="1.0" encoding="utf-8"?>
<Scenario name="tmp.bucketcount" fixedDelta="0.0166667" timeoutFrames="1200" startAfter="ScenePlaying" startTimeoutFrames="900" input="exclusive">
	<At frame="600"><DevCommand line="scene.tree 1000000"/><Pass/></At>
</Scenario>
```

```powershell
$App   = 'build\Ninja-Release\Bin\App.exe'
$games = ( Get-Content Test\QA\Games.json -Raw | ConvertFrom-Json ).games
foreach ( $name in $games.PSObject.Properties.Name ) {
    $log = Join-Path $env:TEMP "tcbench_k_$name.log"
    $arg = @( "-game=$name" ) + @( $games.$name.arguments ) + @( '-dx12', '-scenario=engine/automation/tmp_bucketcount.scenario.xml' )
    Start-Process -FilePath $App -ArgumentList $arg -WorkingDirectory ( Split-Path $App ) -Wait -NoNewWindow -RedirectStandardOutput $log
    $type = Select-String -Path $log -Pattern 'components=(\S*)' | ForEach-Object { $_.Matches[0].Groups[1].Value -split ',' } | Where-Object { $_ } | Sort-Object -Unique
    '{0}: K = {1}' -f $name, @( $type ).Count
}
Remove-Item Resource\engine\automation\tmp_bucketcount.scenario.xml
```

- 인자는 `Test/QA/Games.json` 의 게임별 자동 플레이 인자를 그대로 씁니다(지금 11 개 게임, Empty 포함). 어느 게임을 "실제 게임"으로 셀지는 표에서 고릅니다.
- K 는 600 프레임 시점의 한 번 찍은 값입니다. 비어 있는 버킷은 세지 않는 `getTypedComponentBucketCount()` 와 같은 값이라고 코드를 읽고 판단했을 뿐, 둘을 대조하지는 않았습니다. 서로 다른 이름공간의 같은 짧은 이름은 하나로 세어 K 를 조금 낮게 셀 수 있습니다.
- 메뉴에서 멈추는 게임은 `startAfter="ScenePlaying"` 을 기다리다 시간 초과로 끝납니다(로그에 `[Scenario]` 시간 초과가 보임). 그 게임은 인자를 바꿔 플레이 씬까지 가는지 먼저 확인합니다.
- 읽는 법: K 가 수십이면 8.4 표의 K 50 줄(버킷 훑기 몫이 작다), 수백이면 K 200 · 500 줄이며 B 안(조회 타입마다 버킷 목록 캐시)을 따질 근거가 됩니다.

**끝난 뒤.** `py -3 -m Scripts shared-lock release gpu --owner <이름>`(독점), 임시 시나리오와 `py -3 -m Scripts worktree-remove tcbefore` 로 전 워크트리 지우기, 로그(`%TEMP%\tcbench_*.log`) 지우기.
