# 저작 저장과 런타임 상태 분리 계획 (2026-10-18, 백로그 C-53)

씬과 프리팹을 저작 파일로 저장할 때 플레이 중에 생긴 상태가 섞여 들어가지 않게 합니다. 저장 문맥이 "무엇을 위해 쓰는가" 를 형식으로 갖고, 런타임 상태 필드는 저작 저장에서 빠집니다.

## 1. 지금 구조와 뿌리

- 오브젝트 상태를 쓰는 길은 하나(`ObjectStateSerializer::saveToXMLString` · `saveToJSONString` · `saveToBinaryBuffer`)이고, 빠지는 필드는 `Transient` 뿐입니다.
  씬 저장, 씬 쿠킹, 프리팹 저장, 프리팹 오버라이드 계산, 에디터 복사와 되돌리기, 플레이 세션 복원, 씬 넘김, 게임 상태 스냅샷이 모두 같은 규칙을 씁니다.
- `ObjectSaveOptions` 는 기본 생성할 수 있고, `saveTo*` 는 `options` 를 기본 인자로 받습니다. 그래서 호출부는 "이 저장이 저작 파일인지 실행 중 스냅샷인지" 를 말하지 않아도 컴파일됩니다.
- 반대쪽 축은 이미 있습니다. `PROPERTY( SaveGame )` 과 `SerializeContext::setSaveGameOnly( bool )` 는 세이브 파일에 옵트인 필드만 쓰게 합니다(`SerializerUtil::shouldSerializeProperty`).
  하지만 `bool` 하나라서 "저작" 과 "스냅샷" 을 가를 값이 없습니다.
- 결과: `GimmickCircuitComponent::_state` 같은 플레이 상태가 에디터가 다음에 저장하는 씬 XML 에 들어갈 수 있습니다. 지금 빈 요소로 저장되는 것은 우연입니다.

뿌리는 저장 문맥에 목적이 형식으로 없다는 것입니다. 필드 플래그만 더해도, 목적을 말하지 않는 호출부가 남으면 같은 결함이 다시 생깁니다.

## 2. 해법

### 2.1 저장 목적은 필수 값입니다

`Engine/Serialization/Base` 에 `enum class SerializePurpose : uint8 { Authoring, Snapshot, SaveGame }` 을 둡니다.

- **Authoring**: 저작 파일입니다. 씬 저장, 씬과 프리팹 쿠킹, 프리팹 저장과 오버라이드 계산, 에디터의 복사, 복제, 되돌리기가 씁니다.
- **Snapshot**: 같은 실행 안에서 상태를 옮기는 것입니다. 플레이 세션 복원, 핫 리로드, 씬 넘김(`SceneManager.cpp:782`), 게임 상태 봉투(`GameInstanceBase.cpp:411`)가 씁니다.
- **SaveGame**: 세이브 파일입니다(`SaveGame.h:35`).

`SerializeContext` 는 목적을 생성자 인자로 받고 기본값이 없습니다. `setSaveGameOnly` 와 `isSaveGameOnly` 는 지우고 `getPurpose()` 하나로 바꿉니다.
`ObjectSaveOptions` 와 `ObjectLoadContext` 도 기본 생성자를 지우고 목적을 받는 생성자(`ObjectSaveOptions{ SerializePurpose::Authoring }`)로만 만듭니다. `saveTo*` 의 `options` 와 `loadFrom*` 의 `context` 기본 인자를 지웁니다.
그러면 목적을 말하지 않은 저장은 컴파일되지 않습니다.

### 2.2 필드 플래그

리플렉션 애노테이션에 `RuntimeState` 를 더합니다. 플레이 중에만 의미가 있는 필드에 붙입니다.

| 필드 플래그 | Authoring | Snapshot | SaveGame |
|---|---|---|---|
| 없음 | 씁니다 | 씁니다 | 타입에 `SaveGame` 필드가 없으면 씁니다 |
| `Transient` | 쓰지 않습니다 | 쓰지 않습니다 | 쓰지 않습니다 |
| `RuntimeState` | 쓰지 않습니다 | 씁니다 | 타입에 `SaveGame` 필드가 없으면 씁니다 |
| `SaveGame` | 씁니다 | 씁니다 | 씁니다 |

- `RuntimeState` 와 `SaveGame` 을 함께 붙일 수 있습니다. 회로 상태처럼 세이브에는 남기고 저작 파일에는 넣지 않는 필드입니다.
- 파서는 `RuntimeState` 와 `Transient` 를 함께 붙이면 생성 오류로 멈춥니다(뜻이 모순입니다). `RuntimeState` 와 `Required` 도 생성 오류입니다. 저작 파일이 쓰지 않는 필드를 필수로 요구할 수 없습니다.
- 판정은 `SerializerUtil::shouldSerializeProperty( type, prop, context )` 한 곳입니다. 텍스트, JSON, 바이너리가 모두 이 함수를 지나므로 세 형식의 규칙이 갈라지지 않습니다.

### 2.3 저작 파일 읽기

Authoring 목적으로 읽을 때 파일에 `RuntimeState` 필드가 있으면 필드 이름과 함께 로드 오류입니다. 옛 형식 리더를 두지 않는 방침(결정 기록 5-4)에 따라 데이터를 다시 씁니다.
조용히 버리면 저작 파일에 섞인 런타임 상태가 다음 저장까지 계속 남아 있는 것처럼 보입니다.

## 3. 상용 엔진 비교와 대안

- **언리얼**: 프로퍼티 플래그에 목적별 제외(`Transient`, `DuplicateTransient`, `NonPIEDuplicateTransient`, `TextExportTransient`)와 세이브 옵트인(`SaveGame`)이 있고, 아카이브가 `ArIsSaveGame` 과 포트 플래그로 목적을 갖습니다 [기억]. 이 계획은 목적을 아카이브(문맥)에, 제외를 필드 플래그에 두는 같은 계열입니다.
- **Godot**: 프로퍼티 사용 플래그(`PROPERTY_USAGE_STORAGE`, `PROPERTY_USAGE_EDITOR`)로 저장 여부와 에디터 노출을 가릅니다 [기억].
- **유니티**: 저작과 런타임을 필드로 가르지 않고, 플레이 모드가 끝나면 씬을 다시 로드해 런타임 변경을 버립니다 [기억]. 이 저장소의 에디터는 플레이 세션을 스냅샷으로 복원하므로 이 방식만으로는 막지 못합니다.

기각한 대안:
- **런타임 상태를 직렬화하지 않는 별도 객체로 분리.** 기각합니다. 기믹마다 상태 객체를 나눠야 하고, 핫 리로드와 플레이 세션 복원도 따로 길을 만들어야 합니다. 큰 시뮬레이션 상태는 이미 `ComponentStateStore` 가 이 방식으로 다룹니다.
- **런타임 필드를 `Transient` 로 두고 `ComponentStateStore` 로 옮기는 방법.** 기각합니다. 리플렉션 필드인데 손으로 쓰는 직렬화가 하나 더 생기고, 핫 리로드 스냅샷에서도 빠집니다.
- **필드 이름 규칙을 보는 린트.** 최후의 수단이라 두지 않습니다. 형식(필수 목적, 필드 플래그)이 막습니다.
- **에디터 되돌리기와 복사를 Snapshot 으로 두는 방법.** 기각합니다. 에디터에서 사용자가 고치는 대상은 저작 데이터입니다. 언리얼의 복사도 `TextExportTransient` 를 빼고 씁니다 [기억].

## 4. 단위

A1 ~ A3(애노테이션, 필수 목적 문맥과 판정 표, 오브젝트 상태 옵션의 목적 필수화)은 들어갔습니다. 게임 상태 봉투는 핫 리로드(`serializeState`, Snapshot)와 세이브 파일(`saveStateToFile`, SaveGame)이 함께 쓰므로 부르는 쪽이 목적을 넘깁니다(`captureSnapshot( purpose, … )`).

| 단위 | 내용 | 건드릴 폴더 | 선행 | 검증 | 측정 |
|---|---|---|---|---|---|
| A4 | 런타임 필드에 `RuntimeState` 를 붙이고 데이터를 다시 씁니다. `GimmickCircuitComponent::_state` 부터 하고, 디렉터와 기믹 컴포넌트의 플레이 중 필드를 찾아 함께 붙입니다 | `GameFramework/Base/Gameplay/Gimmick/`, 찾은 컴포넌트, `Resource/` 의 씬과 프리팹 | A3 | 기믹 회로 세이브 테스트(결정 기록 5-12 리플렉션 행), 전 팩 쿠킹, 대표 셋 실행 | 없음 |

A4 의 필드 찾기는 읽기만 하는 일이라 `scout` 에 맡깁니다. `RuntimeState` 와 `SaveGame` 을 함께 붙이면 그 타입이 옵트인 세이브가 되어 나머지 필드가 세이브 파일에서 빠집니다.
씬 오브젝트 전체를 세이브에 싣는 지금 구조에서는 컴포넌트 필드에 `SaveGame` 을 붙이지 않고 `RuntimeState` 만 붙입니다(세이브는 옵트인이 아닌 타입의 `RuntimeState` 를 씁니다).

## 5. 사용자 결정

없습니다. 되돌리기와 복사를 Authoring 으로 두는 것은 3절의 이유로 정했습니다.
