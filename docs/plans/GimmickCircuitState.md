# 기믹 회로 상태 구조 계획 (2026-10-17)

> 상태: 구현중 · 선행: ReflectionFormats · 영역: engine · 백로그: 없음 · 진행: 3/5

[리플렉션 형식 계획](ReflectionFormats.md) 9절 단계 2(`GimmickCircuit` 을 B 로)의 설계입니다. 그 단계는 "상태 바이트가 씬 · 프리팹 · 레벨 세이브에 남는다"는 이유로 보류됐고, 이 문서는 그 보류를 푸는 구조를 정합니다.

**정한 것(사용자 확정).**

- 씬 · 프리팹 · 레벨 세이브에 남는 컴포넌트 데이터는 A(자기 기술형, `BinarySerializer` 와 XML 텍스트)로 씁니다.
- 체크포인트 · 롤백 · 넷 스냅샷은 B(`FixedLayout`)로 씁니다.
- 두 형식은 같은 `REFLECT` 선언 하나에서 나옵니다. 손 코덱은 남기지 않습니다.
- 구조체가 바뀌어도 세이브를 읽습니다. 필드 추가는 선언한 기본값, 이름 변경은 `PROPERTY( Alias = "old" )`, 값 계산이 필요한 구조 변환은 `SW_SCHEMA_MIGRATION` 입니다([이름 변경 리다이렉트 계획](RenameRedirects.md) 1절).
- 지금 변환할 출시 데이터가 없으므로 옛 `_stateBytes` 를 읽는 변환 코드는 두지 않습니다.

## 1. 지금 구조

근거: `Source/GameFramework/Base/Gameplay/Gimmick/GimmickCircuit.cpp:15-46, 301-319, 461-530`, `GimmickCircuitComponent.h:182-190`, `GimmickCircuitComponent.cpp:188-218, 516-585`, `GimmickStateBundle.h`, `Kits/Feature/Network/NetClientServer/GimmickSnapshotUtil.h`.

### 1.1 상태 바이트 레이아웃

`GimmickCircuit::saveState` 가 `memcpy` 로 호스트 엔디언 그대로 씁니다. 태그와 크기는 없습니다.

| 순서 | 필드 | 크기 | 출처 |
|---|---|---|---|
| 1 | `kStateTag` = `"GMKC"` | 4 | `GimmickCircuitInternal` |
| 2 | `kStateVersion` = 1 | 4 | 손 버전 |
| 3 | `_layoutHash` | 8 | 회로 구조 해시(1.3) |
| 4 | `_stepIndex` | 4 | 지금 스텝 번호 |
| 5 | `_timer._accumulator` | 4 | 고정 스텝 누적 시간 |
| 6 | 노드마다 `_outputBits`, `_previousInputBits`, `_sensorValue`, `_sensorImpulse` | 노드 수 × 16 | `NodeRuntime` |
| 7 | `_listFloatState` 원소 | 개수 × 4 | 종류마다 `_floatStateCount` 개를 이어 붙인 목록 |
| 8 | `_listIntState` 원소 | 개수 × 4 | 종류마다 `_intStateCount` 개 |

`loadState` 는 위 크기를 손으로 다시 계산해(`expectedSize`, 줄 491-492) 길이가 정확히 같을 때만 읽습니다. 쓰는 쪽과 읽는 쪽의 크기 계산이 두 곳에 따로 있습니다. 이 손 계산을 없애는 것이 단계 2 의 목적입니다.

### 1.2 노드 수와 상한

- 노드 수 상한은 없습니다. `populate` 는 노드 수를 검사하지 않습니다.
- 포트는 종류마다 입력 · 출력 각각 32 개 이하입니다(`GimmickNodeRegistry.cpp:21`, `kMaxPortCount`). 그래서 `_outputBits` 와 `_previousInputBits` 는 `uint32` 비트 마스크 하나에 들어갑니다.
- 종류마다 실수 상태 수는 `uint16`(`GimmickNodeKind::_floatStateCount`)이고, 가장 큰 기본 종류는 Elevator 의 9 개(1 + `kMaxElevatorFloor`)입니다.
- 넷 스냅샷은 회로 하나를 `kMaxCircuitBytes`(16 조각 × 245 바이트 = 3,920 바이트)까지 싣고, 회로 수는 `kMaxCircuitCount`(4,096)까지입니다. 번들은 항목 하나를 `kMaxEntryBytes`(1 MiB), 항목 수를 `kMaxEntryCount`(65,536)로 제한합니다.

### 1.3 회로 구조 해시의 역할

`computeLayoutHash`(줄 306-319)는 노드마다 종류 이름 해시와 id 해시를 섞고, 끝에 노드 수 · 실수 상태 수 · 정수 상태 수를 섞습니다.
이 해시는 **이 상태가 이 회로의 것인가**를 판단합니다. 디자이너가 회로를 고친 뒤(노드 추가, 종류 변경) 옛 레벨 세이브나 다른 기계의 스냅샷을 읽으면 해시가 달라 `loadState` 가 거절하고, 컴포넌트는 경고를 남기고 처음 상태로 시작합니다(`GimmickCircuitComponent.cpp:193-197`).

B 의 스키마 해시는 이 일을 대신하지 못합니다. B 해시는 **타입 선언**(필드 이름, 순서, 타입, 범위 키)에서 나오므로 회로 데이터가 바뀌어도 같습니다. 그래서 구조 해시는 새 구조에서도 데이터 필드로 남습니다.

### 1.4 상태를 쓰는 위치

| 사용처 | 지금 | 새 형식 |
|---|---|---|
| `GimmickCircuitComponent::_stateBytes`(`PROPERTY( HideInInspector )`) | 스텝을 실행한 틱마다 씀. 씬 · 프리팹 XML, 레벨 세이브(`ObjectStateSerializer`), 핫 리로드, 에디터 복사 · 붙여넣기가 실음 | A |
| `GimmickCircuit::_initialStateBytes` | `populate` 직후 상태, `resetToInitial` 이 읽음 | 프로세스 안 값(직렬화 없음) |
| `GimmickCircuitComponent::_checkpointBytes` | `captureCheckpoint` · `restoreCheckpoint` | 프로세스 안 값(2.4) |
| `GimmickStateBundle` | 씬의 모든 회로를 소유 오브젝트 키와 묶는 손 코덱(`"GMSB"`, 버전 1). 롤백(`IRollbackGame::saveState`)과 넷 스냅샷이 씀 | B |
| `GimmickSnapshotUtil` | 번들 항목 바이트를 245 바이트 조각으로 나눠 `NetSnapshot` 엔티티에 실음 | B 바이트를 같은 방식으로 나눔 |
| `GimmickCircuit::computeStateHash` | `saveState` 바이트의 FNV-1a 64 | B 바이트의 해시 |

같은 이름이지만 다른 필드가 있습니다. `ObjectCopyBuffer::_stateBytes`, `EditorSceneCommands.cpp:647, 690`, `FractureNetworkSync`, `FractureComponentBase`, `SceneCooker`, `TestTerrain`, `TestFractureNetworkSync` 의 `_stateBytes` 는 기믹과 관계없으므로 건드리지 않습니다.

### 1.5 XML 데이터

`_stateBytes` 를 가진 파일은 **14 개, 요소는 18 개**입니다(지시서의 15 개와 다릅니다). 모두 빈 요소 `<_stateBytes />` 입니다.

- 프리팹 13 개: `Resource/common/prefabs/gimmicks/` 아래 `adventure/torch`, `adventure/lockeddoor`, `adventure/lockeddoor3d`, `shooter/keycarddoor`, `shooter/keycarddoor3d`, `common/rotatingplatform`, `common/hazardzone`, `common/hazardzone3d`, `common/elevator`, `common/elevator3d`, `common/barrelspawner`, `platformer/movingplatform`, `platformer/movingplatform3d`(각 `.prefab.xml`).
- 씬 1 개: `Resource/game/empty/maps/gimmickshowcase.scene.xml`(요소 5 개, 줄 113, 152, 249, 318, 521).

## 2. 새 구조

### 2.1 `REFLECT` 구조체

새 헤더 `Source/GameFramework/Base/Gameplay/Gimmick/GimmickCircuitState.h` 에 둡니다. `GimmickCircuit.h` 는 리플렉션을 쓰지 않는 실행 코드이고, 컴포넌트와 번들이 둘 다 이 타입을 쓰므로 따로 둡니다.

```cpp
/** @brief 노드 하나의 실행 상태입니다. */
REFLECT()
struct SW_GF_API GimmickNodeState
{
    REFLECT_BODY();

    PROPERTY()
    uint32 _outputBits{ 0 };        // 포트 32 개 이하(kMaxPortCount)라 비트 마스크 하나
    PROPERTY()
    uint32 _previousInputBits{ 0 };
    PROPERTY()
    float32 _sensorValue{ 0.0f };
    PROPERTY()
    float32 _sensorImpulse{ 0.0f };
};

/** @brief 회로 하나의 상태입니다. 노드 목록이 비어 있으면 저장된 상태가 없다는 뜻입니다. */
REFLECT()
struct SW_GF_API GimmickCircuitState
{
    REFLECT_BODY();

    PROPERTY()
    uint64 _layoutHash{ 0 };        // 이 상태가 어느 회로 구조의 것인지(1.3)
    PROPERTY()
    uint32 _stepIndex{ 0 };
    PROPERTY( Min = 0 )
    float32 _accumulator{ 0.0f };
    PROPERTY( MaxCount = 1024 )
    vector<GimmickNodeState> _listNode{};
    PROPERTY( MaxCount = 8192 )
    vector<float32> _listFloatState{};
    PROPERTY( MaxCount = 8192 )
    vector<int32> _listIntState{};

    bool isEmpty() const { return _listNode.empty(); }
};
```

필드마다의 결정은 아래와 같습니다.

| 필드 | 범위 키 | B 인코딩(ReflectionFormats 3.4) | 근거 |
|---|---|---|---|
| `_layoutHash` | 없음 | `writeVarUint` | 해시는 모든 값이 가능합니다 |
| `_stepIndex` | 없음 | `writeVarUint` | 시작 직후에는 작은 값이라 짧습니다 |
| `_accumulator` | `Min = 0` | 32 비트 원본 | `Step` 을 붙이면 손실 인코딩이 되어 롤백 결정성이 깨집니다. `Max` 는 회로의 `_stepTime` 이라 이 타입 밖의 값이므로 적지 않습니다 |
| `_outputBits`, `_previousInputBits` | 없음 | `writeVarUint` | 비트 마스크라 범위가 의미 없습니다. `Max = 4294967295` 는 `float32` 로 정확히 나타나지 않아 생성 오류입니다(3.4) |
| `_sensorValue`, `_sensorImpulse` | 없음 | 32 비트 원본 | 거리 · 무게 · 피해량은 데이터가 정합니다. 손실 없이 그대로 씁니다 |
| `_listNode` | `MaxCount = 1024` | 개수 + 원소 | 2.5 |
| `_listFloatState`, `_listIntState` | `MaxCount = 8192` | 개수 + 원소 | 노드 1,024 개 × 평균 8 슬롯. 2.5 |

`GimmickNodeState` 는 같은 헤더의 공개 필드만 가지므로 생성기가 부모 코덱 안에 펼칩니다(3.6). 둘 다 적격 타입이라 B 코덱이 자동으로 생깁니다.

**"상태 없음"의 표현.** 지금은 `_stateBytes.empty()` 가 "저장된 상태 없음, 처음부터 시작"입니다. 새 구조에서는 `_listNode.empty()` 입니다.
만들어진 회로는 노드가 하나 이상이므로(`isBuilt` 가 `_listNode.empty() == false`) 빈 목록은 실제 상태와 겹치지 않습니다. 따로 깃발 필드나 0 해시 같은 표시 값을 두지 않습니다.

**지우는 것.** `kStateTag`, `kStateVersion`, `writeValue` · `readValue`, `expectedSize` 계산, `GimmickStateBundle::kMagic` · `kVersion` · `kMaxEntryBytes` 와 손 `write` · `read` 입니다. A 는 타입 태그와 버전 머리가, B 는 스키마 해시가 이 일을 맡습니다.

### 2.2 `GimmickCircuit` API

| 지금 | 새 API |
|---|---|
| `void saveState( vector<uint8>& )` | `void saveState( GimmickCircuitState& outState ) const` |
| `[[nodiscard]] bool loadState( const vector<uint8>& )` | `[[nodiscard]] bool loadState( const GimmickCircuitState& state )` |
| `vector<uint8> _initialStateBytes` | `GimmickCircuitState _initialState` |
| `computeStateHash` 가 손 바이트를 해시 | `FixedLayout::write` 한 바이트를 해시 |

`loadState` 는 `_layoutHash` 가 같고 세 목록의 개수가 이 회로의 개수와 같을 때만 옮깁니다. 하나라도 다르면 아무것도 바꾸지 않고 `false` 입니다. 지금의 `expectedSize` 비교가 개수 비교 셋으로 바뀝니다.
바이트 길이를 손으로 계산하는 곳은 남지 않습니다. 잘린 바이트와 범위 밖 개수는 B 읽기(`Truncated` · `OutOfRange`)와 A 읽기가 먼저 거절합니다.

### 2.3 A 와 B 가 같은 선언에서 나오는 경로

```
GimmickCircuitState (REFLECT, PROPERTY)
 ├─ A: GimmickCircuitComponent::_state (PROPERTY( HideInInspector ))
 │     ├─ 씬 · 프리팹 XML         (텍스트 리플렉션 읽기 · 쓰기)
 │     ├─ 레벨 세이브 · 핫 리로드 (ObjectStateSerializer → BinarySerializer)
 │     └─ 에디터 복사 · 되돌리기  (ObjectCopyBuffer)
 └─ B: GimmickStateEntry::_state → GimmickStateBundle (REFLECT)
       ├─ 롤백 저장               (FixedLayout::write · read)
       └─ 넷 스냅샷               (GimmickSnapshotUtil 이 B 바이트를 조각으로 나눔)
```

- **A.** 컴포넌트의 `vector<uint8> _stateBytes` 를 `GimmickCircuitState _state` 로 바꿉니다. 이 필드가 `PROPERTY` 이므로 A 는 아무 코드 없이 따라옵니다. 필드 추가 · 이름 변경 · 구조 변환 정책이 그대로 적용됩니다(2.6).
- **B.** 번들을 `REFLECT` 타입으로 바꿉니다.

  ```cpp
  REFLECT()
  struct SW_GF_API GimmickStateEntry
  {
      REFLECT_BODY();
      PROPERTY()
      uint64 _ownerKey{ 0 };
      PROPERTY()
      GimmickCircuitState _state{};
  };

  REFLECT()
  struct SW_GF_API GimmickStateBundle
  {
      REFLECT_BODY();
      PROPERTY( MaxCount = 65536 )
      vector<GimmickStateEntry> _listEntry{};
      // collect, apply, findEntry, makeOwnerKey 는 그대로
  };
  ```

  번들의 `write` · `read` 는 `FixedLayout::write` · `read` 를 부르는 얇은 함수가 되고, 읽기 결과(`FixedReadResult`)를 버리지 못하게 `[[nodiscard]]` 로 둡니다. `computeHash` 는 B 바이트의 해시입니다.
  게임이 나중에 번들을 디스크에 저장하려면 같은 타입을 `BinarySerializer` 로 쓰면 됩니다. 형식을 하나 더 만들지 않습니다.
- **넷 스냅샷.** `GimmickSnapshotUtil` 은 지금처럼 회로마다 조각을 만들지만 조각에 싣는 바이트는 그 항목의 `GimmickCircuitState` 를 `FixedLayout::write` 한 것입니다(스키마 해시 64 비트 머리 포함).
  받는 쪽은 조각을 합친 뒤 `FixedLayout::read` 하고, `SchemaMismatch` 를 포함한 실패는 그 회로만 건너뛰고 셉니다(지금 의미와 같습니다).
  회로마다 해시 8 바이트가 붙지만, 넷 채널이 핸드셰이크에서 해시를 섞는 경로(백로그 `D7-7`)가 생기기 전까지는 이 머리가 다른 빌드의 바이트를 거절하는 유일한 장치이므로 남깁니다.
- **컴포넌트 API.** `getStateBytes` 는 `const GimmickCircuitState& getState() const`, `restoreStateBytes( const vector<uint8>& )` 는 `[[nodiscard]] bool restoreState( const GimmickCircuitState& )` 가 됩니다. 바이트를 해석하는 일은 번들 하나만 맡습니다.

### 2.4 체크포인트

`_checkpointBytes` 는 프로세스 안에서만 쓰고 디스크와 와이어로 나가지 않습니다. `GimmickCircuitState _checkpoint` 값으로 보관하면 직렬화가 아예 필요 없습니다.
이 값을 바이트로 보내는 경로(롤백, 넷)는 모두 번들을 지나므로 B 이고, 체크포인트를 A 에 쓰는 경로는 생기지 않습니다. 결정 "체크포인트는 B" 와 충돌하지 않는다고 보고 이 방법을 권합니다.
체크포인트를 바이트로 남겨야 할 이유(메모리 비교, 외부 전달)가 있으면 `vector<uint8>` 에 `FixedLayout::write` 하도록 바꾸는 것도 같은 선언으로 됩니다. 확인이 필요한 결정입니다(6.3).

### 2.5 `MaxCount` 와 회로 검증

B 는 개수 상한이 정적이어야 합니다(3.5). 지금 `populate` 에는 노드 수 상한이 없으므로, 상한을 넘는 회로는 **데이터 오류**로 만듭니다.
`populate` 가 노드 수, 실수 상태 수, 정수 상태 수를 세어 상한을 넘으면 다른 검증과 같이 한 줄 오류를 모으고 `false` 를 돌려줍니다. 조용히 자르지 않습니다.

상한 값의 원본은 `PROPERTY( MaxCount = … )` 선언 하나로 둡니다. 3.8 의 한계(키는 C++ 상수를 받지 않습니다) 때문에 C++ 상수를 따로 적으면 같은 수가 두 곳에 생깁니다.
그래서 `populate` 는 상수를 쓰지 않고 `GimmickCircuitState` 의 `PropertyInfo` 에서 `MaxCount` 메타데이터를 읽어 비교합니다(기동 뒤 한 번 읽어 정적 지역 변수에 보관). 메타데이터를 읽는 API 가 없으면 구현 전에 알리고, 그때는 상수와 선언 값이 같은지 대조하는 테스트를 최후의 수단으로 둡니다.

넷 조각 상한(`kMaxCircuitBytes` = 3,920 바이트)과는 따로입니다. B 는 varuint 를 쓰므로 노드 하나가 대략 10~18 바이트이고, 노드 200 개 안팎에서 조각 상한에 닿습니다.
조각 상한을 넘는 회로는 지금처럼 `appendEntities` 가 "싣지 못한 회로" 로 셉니다. 기존 회로의 크기는 검증에서 측정합니다(5절).

### 2.6 구조체가 바뀔 때

| 변경 | A(씬 · 프리팹 · 레벨 세이브) | B(롤백 · 넷) |
|---|---|---|
| 필드 추가 | 옛 데이터는 선언한 기본값으로 읽힙니다. `_layoutHash` 는 회로 데이터에서 나오므로 그대로 맞고 상태가 이어집니다 | 스키마 해시가 바뀌어 다른 빌드의 바이트는 `SchemaMismatch` 입니다. 같은 빌드끼리만 오가므로 문제가 없습니다 |
| 필드 이름 변경 | `PROPERTY( Alias = "old" )` | 해시가 바뀝니다(리다이렉트는 해시에 넣지 않습니다, RenameRedirects 규칙 5) |
| 값 계산이 필요한 변환 | 소유 타입(`sw::GimmickCircuitComponent`)의 기록 버전을 올리고 `SW_SCHEMA_MIGRATION( id, sw::GimmickCircuitComponent, 옛 버전, fn )` 을 등록합니다. 등록이 없으면 오류로 멈춥니다 | 해당 없음 |
| 회로 데이터 변경(노드 추가 등) | 구조 해시가 달라 `loadState` 가 거절하고, 경고 뒤 처음 상태로 시작합니다(지금과 같음) | 같음 |

`GimmickNodeState` 의 의미가 바뀌는 변경(예: 비트 마스크를 두 필드로 나눔)은 기본값으로 메울 수 없으므로 변환 함수가 필요합니다. 지금은 출시 데이터가 없어 변환을 등록하지 않습니다.

## 3. XML 다시 쓰기

14 개 파일의 18 요소가 모두 빈 `<_stateBytes />` 이므로 바이트 변환이 없습니다. **변환 스크립트는 필요 없습니다.**

- 각 파일에서 `<_stateBytes />` 줄을 지웁니다. 새 필드 `_state` 가 없는 데이터는 선언한 기본값(빈 노드 목록 = 상태 없음)으로 읽히므로, 빈 요소를 새 이름으로 바꿔 적을 필요가 없습니다.
- `PROPERTY( Alias = "_stateBytes" )` 는 **두지 않습니다.** 옛 필드는 `vector<uint8>` 이고 새 필드는 구조체라 같은 이름으로 읽으면 기록 타입이 맞지 않습니다. 옛 값은 비어 있어 옮길 내용도 없습니다.
- 작업자는 `Edit` 도구로 파일마다 한 줄을 지웁니다. 일괄 치환이 필요하면 스크래치패드의 일회용 Python 파일로 하고 저장소에 넣지 않습니다(heredoc 금지, AGENTS.md `Editing traps`).
- 지운 뒤 `Resource` 전체에서 `_stateBytes` 가 0 건인지 `Grep` 으로 확인합니다.
- 다음에 에디터가 이 파일을 저장하면 `<_state>` 요소가 기본값으로 다시 생깁니다. 지금 손으로 넣지 않는 이유는 텍스트 리플렉션이 빈 구조체를 어떤 요소로 쓰는지(빈 요소인지 하위 요소를 모두 쓰는지)를 손으로 흉내 내지 않기 위해서입니다.

모르는 요소를 만난 XML 읽기의 동작(건너뛰고 알림, 또는 오류)은 확인하지 못했습니다. 지우지 않고 남겨 두는 경로는 쓰지 않으므로 결과에 영향은 없지만, 쿠킹된 씬이나 다른 게임 폴더에 `_stateBytes` 가 남아 있으면 그 동작이 드러납니다. 6.2 에서 확인합니다.

## 4. 단계

한 작업자가 한 워크트리에서 순서대로 진행합니다. 1~3 은 API 가 이어져 있어 중간 커밋은 빌드되지 않을 수 있고, 나누면 같은 헤더를 여러 손이 고치게 됩니다.

| 단계 | 내용 | 건드릴 파일 | 겹침 주의 | 에이전트(모델) |
|---|---|---|---|---|
| 2-a | 상태 구조체와 회로 API | 새 `Gimmick/GimmickCircuitState.h`, `GimmickCircuit.h` · `.cpp`, `Test/EngineTest/GameFramework/Gimmick/TestGimmick.cpp` | 없음 | `worker`(opus) |
| 2-b | 컴포넌트 필드 · API, 체크포인트 값, XML 18 요소 삭제 | `GimmickCircuitComponent.h` · `.cpp`, XML 14 개, `TestGimmickScene.cpp`(주석 줄 91 포함) | 같은 컴포넌트를 고치는 다른 기믹 작업 | 2-a 와 같은 `worker` |
| 2-c | 번들을 `REFLECT` + `FixedLayout` 로, 넷 조각 바이트를 B 로 | `GimmickStateBundle.h` · `.cpp`, `Kits/Feature/Network/NetClientServer/GimmickSnapshotUtil.h` · `.cpp`, `TestGimmickState.cpp` | 넷 채널 등록(백로그 `D7-7`)을 하는 작업과 `GimmickSnapshotUtil` 이 겹침 | 2-a 와 같은 `worker` |
| 2-d | GPU 확인: 쇼케이스 씬 실행 · 캡처 | 없음(실행만) | GPU 잠금 | `gpu-verifier` |
| 2-e | 문서: `ReflectionFormats.md` 9절 단계 2 행과 보류 문단 갱신, `09_Decisions.md` 에 결정 기록, 헤더 주석(`GimmickCircuit.h`, `GimmickCircuitComponent.h:14-15`, `GimmickStateBundle.h:3-9`, `GimmickSnapshotUtil.h:4-6`) | 문서 2 개, 위 헤더 | 다른 단계 행을 고치는 작업과 `ReflectionFormats.md` 가 겹침 | 헤더 주석은 2-a~2-c 의 `worker`, 문서는 병합하는 쪽 |

- 생성기 입력에 새 헤더가 들어가는지(모듈의 리플렉션 헤더 목록)를 2-a 첫 빌드에서 확인합니다. `.gen.cpp` 주석과 `--dump` 출력에서 두 타입이 적격인지 봅니다.
- 리플렉션 생성기(`Tools/ReflectionParser/`)와 직렬화 런타임은 고치지 않습니다. 고쳐야 할 것이 나오면 범위 밖으로 보고합니다.

## 5. 검증

빌드는 `cmake --build --preset Ninja-Debug --target all AllTests` 입니다. 테스트는 CTest 로 돌립니다(직접 실행만으로 끝내지 않습니다).

| 대상 | 테스트 | 확인하는 것 |
|---|---|---|
| 회로 | `TestGimmick.cpp` 의 저장 · 복원 테스트(지금 줄 426-453)를 구조체로 바꿈 | 값 왕복, 다른 회로의 상태 거절, 개수 하나만 다른 상태 거절, 거절 뒤 회로가 그대로인지 |
| 회로 | 기존 결정성 테스트(줄 509-510, `runA` · `runB`) | 같은 입력이면 같은 `computeStateHash` |
| 회로 검증 | 새 테스트 | 노드 1,025 개 회로가 `populate` 에서 오류 한 줄로 멈춤. 상한 값이 `PropertyInfo` 의 `MaxCount` 와 같음 |
| A | 새 테스트: 플레이 중 컴포넌트를 `ObjectStateSerializer` 로 저장 · 로드 | 로드 뒤 N 스텝을 더 돌린 상태 해시가 끊지 않고 돌린 쪽과 같음 |
| A | 새 테스트: 같은 일을 씬 XML 저장 · 로드로 | 실수 필드(`_accumulator`, `_sensorValue`)가 텍스트를 지나도 비트까지 같은지. 6.1 위험 1 |
| A | 기존 `TestGimmickScene` | 14 개 XML 이 오류 없이 로드되고 상태 없이 처음부터 시작 |
| B | `TestGimmickState.cpp` 의 번들 테스트를 B 로 | `FixedLayout::equals` 왕복, 바이트를 하나씩 줄인 모든 앞부분에서 `Truncated`, `MaxCount` 초과 쓰기 실패, 범위 밖 개수 `OutOfRange`, 실패 뒤 번들 그대로 |
| B | 기존 롤백 테스트(줄 114-124) | 되감아 다시 돌린 결과가 같음 |
| 넷 | 기존 조각 테스트(줄 192-254) | 큰 회로가 여러 조각으로 나뉘고, 조각 하나가 빠지면 그 회로만 건너뜀. `huge._bytes` 를 쓰던 크기 초과 경우는 노드 수가 많은 회로로 바꿈 |
| 측정 | 쇼케이스 씬과 13 프리팹 회로의 B 바이트 크기 | 모두 `kMaxCircuitBytes` 안인지. 보고에 가장 큰 값을 적음 |
| 이전 · 이후 | 같은 입력으로 옛 코드와 새 코드의 스텝마다 출력 비트 · 액추에이터 값 | 같음(바이트가 달라 해시 비교는 쓰지 않음, ReflectionFormats 6절) |
| GPU | `gpu-verifier` 가 쇼케이스 씬을 열고 캡처 | 문 · 엘리베이터 · 무버가 움직이고 오류 로그가 없음 |

이전 · 이후 비교는 2-a 전에 옛 코드로 스텝마다 값을 기록해 두는 테스트 하나를 먼저 커밋하고, 같은 기록을 새 코드에서 다시 비교하는 방식으로 합니다. 비교가 끝나면 기록 파일은 지우고 테스트는 값 왕복 테스트로 남깁니다.

## 6. 위험과 확인할 것

### 6.1 위험

1. **텍스트 실수 정밀도.** A 의 XML 쓰기가 `float32` 를 왕복 가능한 자릿수(9 자리)로 쓰지 않으면, 플레이 중 저장한 씬을 다시 읽은 회로가 몇 스텝 뒤 다른 결과를 냅니다. 지금은 바이트 배열이라 이 문제가 없었습니다. 5절의 XML 테스트가 확인합니다. 정밀도가 모자라면 직렬화 쪽 결함이므로 범위 밖으로 보고합니다.
2. **넷 조각 크기.** varuint 인코딩은 값에 따라 길이가 달라 상태가 바뀔 때 조각 수가 바뀔 수 있습니다. 조각 수가 바뀌면 그 회로의 엔티티가 모두 다시 보내집니다. 기능은 맞고 대역폭만 늘어나므로 측정 결과를 보고 판단합니다.
3. **상한 값이 두 곳.** `PropertyInfo` 에서 `MaxCount` 를 읽지 못하면 C++ 상수와 선언이 갈라질 수 있습니다(2.5).
4. **이름 충돌.** `_stateBytes` 를 `Grep` 하면 기믹과 무관한 파일이 함께 나옵니다(1.4). 작업자가 그 파일을 고치지 않게 지시문에 목록을 적습니다.
5. **회로 데이터 변경 뒤의 조용한 초기화.** 구조 해시가 다르면 경고 뒤 처음 상태로 시작하는 지금 동작을 그대로 둡니다. 레벨 세이브에서 진행이 사라지는 일이 경고 한 줄로만 보입니다. 6.3 의 결정 대상입니다.

### 6.2 구현 중 확인할 것

- `PropertyInfo` 에서 `MaxCount` 메타데이터를 읽는 API 가 있는가(2.5).
- 소유 타입의 기록 버전을 어디에 선언하는가(`SW_SCHEMA_MIGRATION` 이 받는 버전의 출처). 지금 변환을 등록하지 않으므로 이 단계에서는 위치만 헤더 주석에 적습니다.
- XML 읽기가 모르는 요소를 만나면 어떻게 하는가. 쿠킹 산출물이나 다른 게임 폴더에 `_stateBytes` 가 남아 있는가.
- 빈 `vector<GimmickNodeState>` 를 가진 구조체를 텍스트 리플렉션이 어떤 요소로 쓰는가(에디터가 다음에 저장할 때의 차이).

### 6.3 결정이 필요한 것

- 체크포인트를 바이트 없이 값으로 보관하는가(2.4, 권장), B 바이트로 보관하는가.
- 구조 해시가 맞지 않는 저장 상태를 지금처럼 경고 뒤 처음 상태로 시작하는가, 오류로 멈추는가. 레벨 세이브는 디자이너가 회로를 고친 뒤에도 열려야 하므로 지금 동작을 권합니다. 다만 "폴백을 두지 않는다" 원칙과 맞는지 확인이 필요합니다.

## 7. 같은 결함이 다른 곳에 있을 수 있는가

손으로 계산한 바이트 크기를 쓰는 쪽과 읽는 쪽이 따로 가진 구조가 근본 원인입니다. 기믹 안에서는 `ElementGrid` 의 상태 크기 계산(`ElementGrid.cpp:380`, `cellBytes`)이 같은 구조로 보이며, 이 단계 범위 밖이라 따로 조사합니다.

## 8. 구현에서 바꾼 것(2026-10-18, `design-reviewer` 검토 반영)

위 2 절과 다르게 구현한 것만 적습니다. 커밋은 `wt/feat10` 의 2-a · 2-b · 2-c 입니다. 빌드와 테스트는 통합 때 한 번에 돌립니다.

- **노드 id 로 짝짓는 상태.** `GimmickNodeState` 가 `_id`(id 해시) · `_kind`(종류 이름 해시) · `_stateVersion` · 노드별 실수 · 정수 슬롯 목록을 갖고, 회로 전체의 `_layoutHash` 와 평평한 슬롯 목록은 없앴습니다.
  회로 해시 하나에 읽기를 걸면 노드 하나만 고쳐도 그 회로(와 그 프리팹을 쓰는 모든 씬)의 옛 세이브가 실패하기 때문입니다. `GimmickCircuit::loadState` 는 `GimmickStateMatch`(`Exact` · `NodeSetChanged` · `Mismatch`)와 받아들이는 한계를 받습니다.
  `Mismatch` 는 같은 id 인데 종류 · 상태 버전 · 슬롯 수가 다르거나 같은 id 가 둘일 때뿐입니다. 새 노드는 처음 상태, 저장에만 있는 노드는 버립니다.
- **종류의 상태 버전.** `GimmickNodeKind::_stateVersion`(기본 1). 슬롯 수만 대조하면 같은 수 안에서 뜻이 바뀐 변경을 잡지 못합니다.
- **상한.** 노드 수 1,024(`GimmickCircuitState::kMaxNodeCount`, `populate` 가 오류), 종류마다 슬롯 32(`GimmickNodeState::kMaxSlotCount`, `registerKind` 가 거절). 2.1 의 회로 전체 8,192 슬롯 상한은 Delay 하나가 정수 20 개라 근거가 없어 버렸습니다.
  `PropertyInfo` 에 `MaxCount` 메타데이터가 없어(6.2 확인) C++ 상수를 함께 두고, 상한에서 B 쓰기가 성공하고 하나 넘으면 실패하는지를 `GimmickTest.StateLimitsMatchDeclaredMaxCount` 가 대조합니다.
- **저장 상태 적용은 한 함수.** `GimmickCircuitComponent::applySavedState` 를 `onPostLoad` · `onBeginPlay` · `restoreCheckpoint` 가 지납니다. `NodeSetChanged` 는 경고 뒤 이어 가고, `Mismatch` 는 오류를 남기고 회로를 비워 돌지 않습니다(6.3 의 두 번째 결정: 조용한 처음 상태 시작을 없앰).
  롤백 · 넷은 `restoreState`(`Exact` 만, 아니면 바꾸지 않고 false)이고 `GimmickStateBundle::apply` 는 건너뛴 수를 돌려 의미를 부르는 쪽이 정합니다. 체크포인트는 값입니다(6.3 의 첫 결정).
- **저작 저장 문맥(검토 ⓕ)은 백로그 `C-53`.** 애노테이션 · 파서 · `SerializeContext` · 씬 저장 경로를 함께 고쳐야 하고 리플렉션 작업과 파일이 겹쳐 이 단계에서 하지 않았습니다.
- **측정 방법(통합 때, Release).** 쇼케이스 씬 회로 5 개와 프리팹 13 개를 0 · 600 · 36,000 스텝 돌린 뒤 `FixedLayout::write( state )` 바이트 수와 조각 수(`kChunkPayloadBytes` 로 나눔)를 기록하고 가장 큰 값이 `kMaxCircuitBytes` 안인지 봅니다
  (Delay 의 절대 스텝 번호가 varuint 길이를 늘립니다). `storeState` 는 스텝을 돈 틱마다 부르므로 같은 판에서 `saveState` 의 시간 · 할당을 3~5 회 중앙값으로 잽니다(목록 용량을 이어 쓰므로 첫 틱 뒤 할당 0 이 기대값).
