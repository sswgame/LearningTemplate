# 리플렉션 직렬화 형식 계획 (2026-10-17)

> 상태: 측정대기 · 선행: 없음 · 영역: engine · 옛번호: 없음 · 진행: 표

같은 `PROPERTY` 선언 하나에서 쓰임새가 다른 두 바이너리 형식을 만듭니다. 오래 남는 데이터는 지금의 **자기 기술형**(A)으로, 같은 빌드 안에서만 오가는 데이터는 새 **고정 배치형**(B)으로 씁니다.
B 의 읽기 · 쓰기 · 비교 함수는 실행 시간에 프로퍼티 목록을 훑지 않고, 빌드 시간에 코드 생성기가 타입마다 만듭니다. 손으로 쓴 코덱(롤백, 장르 스냅샷, 온라인 프로토콜)은 단계적으로 B 로 옮깁니다.

**원칙: 사용자는 B 를 선언하지 않습니다.** 게임 · 엔진 코드 작성자는 지금처럼 `REFLECT()` 와 `PROPERTY()` 만 씁니다. B 전용 매크로나 등록 목록은 없습니다.
생성기는 적격인 모든 `REFLECT` 타입에 B 코덱을 만들고, B 가 필요한지는 그 타입을 받는 등록 API(롤백, 넷 채널, 온라인 메서드)가 정합니다(3.1). 범위 · 길이 같은 B 메타데이터는 기존 `PROPERTY` 키 `Min` · `Max` 의 확장과 키 둘(`Step`, `MaxCount`)로 적습니다(3.4).

**원칙: 마이그레이션은 언제나 가능해야 합니다.** 세이브 · 에셋(A)의 스키마 진화는 그대로 두고, B 에도 변환을 넣을 확장 지점을 남깁니다(3.14, 5절). 지금 변환할 옛 데이터가 없으므로 현 단계에서 변환 코드는 만들지 않습니다.

7절은 리플렉션 캐스트(`castTo`)의 비용을 같은 기준(측정이 아니라 구조와 원리)으로 따집니다.
이 문서는 B 형식의 계약(2 ~ 6절)과 캐스트 비용(7절), 남은 단계(9절)를 담습니다. 끝난 단계의 설계 서술은 지우고 구현 위치만 1절 표와 9절에 남겼습니다. 줄 번호는 구현이 옮겨 가서 적지 않습니다.
결정은 [결정 기록](../09_Decisions.md) 5-4 · 5-9 에 있습니다.

## 1. 구현 위치

단계 1 ~ 6 · 7-a · 7-b 는 들어갔습니다(9절). 왜 이 구조인지의 근거는 [결정 기록](../09_Decisions.md) 5-4 · 5-9 와 각 코드 헤더 주석에 있고, 아래 2 ~ 6절은 구현이 따르는 계약입니다.

| 무엇 | 어디에 |
|---|---|
| 런타임(`write` · `read` · `equals` · `getSchemaHash` · `findCodec`) | `Source/Engine/Serialization/Fixed/FixedLayout.h` · `.cpp` |
| 코덱 생성기 | `Tools/ReflectionParser/FixedCodecGenerator.*`, 필드 분류는 `AstVisitor.cpp`, 키는 `AnnotationFields.cpp` |
| 코덱 테이블 포인터 | `TypeInfo::_pFixedCodec`(`ReflectionTypes.h`) |
| 롤백(B) · 세이브(A) 나눔 | `FightingMatch`, 상태 필드는 `FrameData.h` · `InputCommandBuffer.h` · `RoundSeries.h` |
| 장르 스냅샷 | `MechArenaSnapshot.h`, `HorrorSnapshot.h` |
| 기믹 회로 상태 | `GimmickCircuitState.h`, `GimmickStateBundle.*` |
| 온라인 스키마 해시 · 거절 | `OnlineProtocolRegistry.*`, 공통 처리 `OnlineReplyUtil::markError`, 키트 `XxxProtocol::MessageTypes` |
| 영속 값(A) | `ServiceRecordUtil.h` |
| 범위 읽기 공통 끝 | `SerializerUtil::isWithinDeclaredRanges`, 인스펙터는 `InspectorPropertyLayout.*` |
| 열거형 이름 | `EnumName.*`, `EnumInfo::_listDenseName` |
| 테스트 | `Test/ReflectionTest/TestFixedLayout.cpp` · `TestFixedLayoutTypes.h`, `ReflectionParserTest.FixedLayoutAnnotationMistakesStopTheBuild` |

`PROPERTY` 키 하나는 `PredefinedAnnotationField.xxx` 한 줄, `ParsedReflection.h` 멤버, `AnnotationMeta.txt` 의 철자로 정하고, 셋이 맞지 않으면 파서가 시작할 때 멈춥니다.
`.gen.h` 는 비트 플래그 트레이트만 담고, `.gen.cpp` 는 유니티 배치에서 빠집니다(ODR). C++17 이라 `operator== = default` 는 쓸 수 없습니다.

**근본 원인(끝남).** 바이트 배치의 원본이 선언과 따로 손 코드에 있어서, 필드를 더하고 손 버전 상수를 안 높이면 조용히 잘못 읽었습니다(롤백 · 스냅샷 · 회로 상태 · 온라인 12 파일).
지금은 선언에서 생성한 코덱의 스키마 해시가 형식 변경을 잡습니다. 컴팩트 모드(스키마 확인 없는 순서 기반)와 호출부 없는 `cloneObject` 는 단계 6 에서 지웠고, `writeInt` 의 범위 자르기는 쓰기 실패(`[[nodiscard]] bool`)로, 모르는 타입 1 바이트 건너뛰기는 읽기 실패로 바뀌었습니다(옛 `A-13` · `A-14`).
자기 기술형(A)의 읽기 방식(이름 해시 대조, 기록 타입 변환, 누락 필드 기본값)은 그대로입니다.

손으로 쓴 `operator==` 75 개 중 리플렉션 타입은 4 파일뿐이고 나머지는 값 형식입니다. 생성 비교는 B 로 옮긴 타입의 비교로 한정하며, 비교를 대신하려고 타입에 `REFLECT` 를 붙이지 않습니다(생성 비교는 `FixedLayout::equals`, 나머지 `operator==` 는 값 형식이라 손대지 않습니다).

## 2. 두 형식의 역할

기준은 하나입니다. **쓴 빌드와 다른 빌드가 그 바이트를 읽을 수 있는가.** 읽을 수 있으면 A, 같은 빌드만 읽으면 B 입니다.

| 데이터 | 형식 | 이유 |
|---|---|---|
| 디스크 세이브 | A | 패치 뒤에도 읽어야 함 |
| 에셋 · 쿠킹 씬 | A | 스키마보다 오래 남음 |
| 핫 리로드 상태 보존 | A | 리로드 뒤 타입 배치가 바뀜 |
| 에디터 복사 · 붙여넣기 · 되돌리기 | A | 리로드를 넘어 남음 |
| 설정(XML) | 텍스트 | 사람이 고침, 이 계획 밖 |
| 롤백 상태 | B | 같은 프로세스, 프레임마다 씀 |
| 결정성 해시용 스냅샷 | B | 같은 빌드의 바이트 비교 |
| 장르 스냅샷 · 넷 복제 | B | 핸드셰이크가 같은 빌드를 보장 |
| 온라인 서비스 프로토콜 | B | 서버 · 클라이언트 같은 빌드 배포(5절) |

진행 중인 대전을 세이브하는 경우(`FightingMatch::writeState`)는 세이브이므로 A, 롤백은 B 입니다. 두 형식이 같은 선언에서 생성되므로 형식을 둘로 따로 관리하는 걱정은 없습니다.
세이브 섹션이 A 이므로 버전 머리와 등록된 변환으로 마이그레이션하는 경로가 열려 있습니다. 변환할 옛 세이브가 없으므로 변환 코드는 두지 않습니다.

## 3. 고정 배치형(B) 명세

### 3.1 어느 타입이 B 코덱을 받는가: 모든 적격 `REFLECT` 타입

**규칙.** 코드 생성기는 `REFLECT()` 가 붙은 모든 타입 가운데 **적격**인 타입에 B 코덱을 만듭니다. 사용자는 아무것도 더 선언하지 않습니다.
적격이란 B 에 들어가는 필드(`Transient` 가 아닌 모든 `PROPERTY`)의 타입이 3.4 의 인코딩 테이블 안에 있다는 뜻입니다. 기반 클래스가 있으면 기반도 적격이어야 합니다.
적격이 아닌 타입은 코덱 없이 등록되고, 생성기는 그 이유(필드 이름과 타입)를 `.gen.cpp` 주석과 `--dump` 출력에 남깁니다. 오류로 멈추지 않습니다. 포인터나 길이 상한 없는 `vector` 를 가진 `REFLECT` 타입이 많아서, 적격이 아니라는 사실 자체는 결함이 아니기 때문입니다. 한 가지 예외가 압축 타입(`ReflectBuiltins.xxx` 의 `packed` 줄)을 `Transient` 가 아닌 `PROPERTY` 로 둔 경우입니다. 저장용으로 만든 타입이 조용히 코덱 없이 등록되지 않도록 생성 오류로 멈춥니다([PackedTypes](PackedTypes.md) 1절).

**B 가 필요한 곳은 사용하는 자리가 정합니다.** B 를 쓰는 진입점(3.10 의 롤백 저장, 넷 채널 등록, 온라인 메서드 등록)이 처음 그 타입을 볼 때 코덱이 있는지 확인하고, 없으면 그 자리에서 이유를 담은 오류로 멈춥니다.
진입점은 모두 기동 때 한 번 실행하는 등록 코드이므로 실행 파일을 켜는 순간 드러납니다(8.1 위험 1). 사용 자리 스캔 · 등록 목록 · 필드 속성 도출을 기각한 이유는 [결정 기록](../09_Decisions.md) 5-4 입니다.

**비용.** 새 번역 단위가 없고(기존 `.gen.cpp` 에 덧붙음), 덧붙는 것은 템플릿 없는 직선 코드이며, `.gen.cpp` 만 `FixedLayout.h` · `BitStream.h` 를 include 합니다(`FlagOps.gen.h` 는 건드리지 않음).
`ReflectionTypes.h` 는 `TypeInfo` 에 포인터 하나를 더할 때 한 번 바뀌었습니다. 실행 파일 크기 증가는 이렇습니다(결정 기록 5-4 에서 옮김).

| 시점 | 구성 | 증가 |
|---|---|---|
| 단계 1 전후 | Debug | `Engine.dll` +153,088 B(+0.7 %), `GameFramework.dll` +81,408 B(+1.0 %). `REFLECT` 타입 454 개 중 적격 173 개 |
| 7-a · 7-b 전후 | Release | `Engine.dll` +12,800 B(+0.08 %), `GameFramework.dll` +5,120 B(+0.10 %), `App.exe` -512 B |

단계 1 의 Release `App.exe` · 모듈 DLL 전후 값은 재지 않았습니다. 재려면 단계 1 직전 커밋과 지금을 같은 Release 구성으로 지어 `Bin` 의 파일 크기를 견줍니다.

### 3.2 생성 코드의 위치와 호출 경로

- 코덱 함수는 이미 있는 `sw::generated::Registrar<T>` 특수화의 정적 멤버로 `.gen.cpp` 안에 만듭니다. `REFLECT_BODY()` 가 이 등록기를 friend 로 선언하므로 private 필드에도 접근합니다.
- 등록기 `RegisterType` 가 `info._pFixedCodec` 에 정적 코덱 테이블(쓰기, 읽기, 비교, 스키마 해시 함수 포인터) 주소를 넣습니다. 함수 템플릿 특수화를 다른 TU 에서 부르는 구조나 DLL 데이터 export 가 필요 없습니다.
- 진입점은 타입 정보를 `typeFqn<T>()` 와 함수 지역 `TypeLookupCache` 로 한 번 찾고 캐시합니다. 이후 호출은 원자 로드 하나와 간접 호출 하나입니다.
- 중첩 타입이 같은 헤더에 있고 필드가 공개이면 생성기가 부모 코덱 안에 그 필드 코드를 **펼쳐** 씁니다. 다른 헤더나 private 필드이면 중첩 타입의 코덱 테이블을 FQN 캐시로 찾아 간접 호출합니다.

### 3.3 쓰는 쪽 모양

사용자는 평범한 호출만 하고 B 인지 모릅니다.

```cpp
// 롤백: 링 버퍼가 상태 타입을 받는다. 상태 구조체는 REFLECT + PROPERTY 만 가진다.
_rollback.saveFrame( frame, _matchState );                 // 안에서 FixedLayout::write
if ( _rollback.loadFrame( frame, _matchState ) == false )  // 스키마 불일치 · 잘림이면 false 와 이유 로그
    return false;

// 게임 넷: 스냅샷 타입을 채널에 등록하면 그 타입의 스키마 해시가 핸드셰이크 해시에 들어간다(후보 NetChannelSchemaHashHandshake).
_netChannel.registerMessage<MechArenaSnapshot>( kSnapshotMessage );

// 온라인: 키트의 MessageTypes 목록으로만 쓰고 읽는다(5절).
```

`FixedLayout::write` · `read` 는 저수준 진입점입니다. 게임 · 키트 코드는 등록 API 를 쓰고 직접 부르지 않습니다. 진입점이 등록 API 몇 개로 모이므로 코덱 확인(3.1)도 그만큼의 자리에서만 일어납니다.

### 3.4 필드 타입별 인코딩

| 필드 타입 | 인코딩 |
|---|---|
| `bool`, `uint8` 플래그 | 1 비트 |
| 정수, `Min` · `Max` 있음 | 범위 비트 수(`BitMath::computeBitsRequired`) |
| 정수, 범위 없음 | 부호 있으면 `writeVarInt`, 없으면 `writeVarUint` |
| `float32`, `Step` 있음 | 칸 번호를 범위 비트 수로 씀(`FixedCodecUtil::writeQuantizedFloat32`, `Min` · `Max` 필수). 읽을 때 칸 수를 넘는 번호는 `OutOfRange` |
| `float32`, `Step` 없음 | 32 비트 원본 비트. `Min` · `Max` 가 있으면 쓰기 · 읽기 모두 범위를 확인 |
| `float64` | 64 비트 원본 비트(`Step` 은 받지 않음). `Min` · `Max` 는 `float32` 와 같음 |
| `float3` 등 수학 타입 | 원소마다 위 규칙 |
| 열거형 | 3.7 |
| `string`, `vector<T>` | `MaxCount` 필수, 개수 + 원소(3.5) |
| `T[N]`, `std::array<T, N>` | 원소 N 개, 개수 없음 |
| 적격 `REFLECT` 타입 | 그 타입의 필드(3.6) |

위에 없는 타입이 하나라도 있으면 그 타입은 적격이 아닙니다. 포인터, 참조, 핸들, 접근자 프로퍼티, `unordered_*` 컨테이너, `MaxCount` 없는 가변 길이, 적격이 아닌 구조체가 해당합니다.
`unordered_*` 를 빼는 이유는 순회 순서가 결정적이지 않아 같은 값이 다른 바이트가 되기 때문입니다(롤백 해시 비교가 깨집니다).
핸들(`ComponentHandle` 등)이 필요해지면 그 형식에 B 코덱을 하나 등록하는 일을 별도 단계로 둡니다.

**기존 속성의 확장.** B 전용 메타데이터는 새 선언 형식 없이 기존 `PROPERTY()` 키로 표현합니다.

| 키 | 지금 | 확장 |
|---|---|---|
| `Min` · `Max` | 실수 리터럴, 허용 범위(`float.MinRange`) | 정수 비트 폭과 양자화 범위로도 씀. 필드 이름과 쉼표 목록도 받음 |
| `Step` | 없음(새 키) | 양자화 간격. 값의 의미 있는 최소 단위 |
| `MaxCount` | 없음(새 키, `count` 종류) | 문자열 · 컨테이너 최대 길이. 1 이상의 정수로 읽음 |

- `Min` · `Max` 는 이미 "허용 범위"라는 뜻이라 와이어 범위로 써도 뜻이 바뀌지 않습니다. 인스펙터 슬라이더 범위는 `UIMin` · `UIMax` 로 따로 있습니다.
- `Min` · `Max` 는 `float32` 로 보관하므로 정수 경계는 `float32` 로 정확히 나타나는 값이어야 하고, 아니면 생성 오류입니다(2e9 는 정확하고 16777217 은 아닙니다).
  숫자가 아닌 필드(bool, 열거형, 문자열, 구조체)의 `Min` · `Max` 도 생성 오류입니다. 정수 필드는 실수 필드 이름을 범위로 쓸 수 없습니다(소수부가 잘립니다).
- 범위 밖 정수는 **쓰기가 실패**합니다. 자르지 않습니다. 실수 양자화는 `Step` 을 적는 순간 손실 인코딩이므로 경계로 자릅니다. `Step` 이 붙은 필드만 손실이 있습니다.
- 쓰기 실패는 결함이므로 `FixedLayout::write` 는 `[[nodiscard]] bool` 을 반환하고, 등록 API 는 단언하거나 오류로 멈춥니다.

### 3.5 가변 길이

- 문자열과 `vector` 는 `MaxCount` 가 있어야 적격입니다. 넷에서 받은 길이로 메모리를 잡기 전에 상한이 있어야 하기 때문입니다.
- 개수는 `[0, MaxCount]` 범위 정수로 씁니다. 쓸 때 개수가 `MaxCount` 를 넘으면 쓰기가 실패합니다(조용히 자르지 않습니다).
- 읽을 때 개수가 `MaxCount` 를 넘으면 `FixedReadResult::OutOfRange`, 잘린 스트림은 `Truncated` 입니다.
- 상한은 정적 값입니다. 런타임 설정이 상한이면, 데이터가 정하는 최대값보다 크거나 같은 정적 상한을 적고 데이터 쪽 검증을 따로 둡니다.
- `MaxCount` 는 1 이상의 정수 리터럴이나 C++ 정수 상수 이름(`MaxCount = kMaxSlotCount`, `MaxCount = LedgerConstant::kMaxAssetIDSize`)을 받습니다. 같은 상한을 코드도 쓰면 상수 이름으로 적습니다(정본은 상수 하나).
  생성기(`AstVisitor` 의 `resolveMaxCountName`)가 필드가 든 타입과 바깥 타입을 안쪽부터, 그다음 바깥 네임스페이스를 안쪽부터 보며 그 상수를 찾아 값을 계산하고, 그 값이 코덱 · 정규 문자열(스키마 해시) · `PropertyMetadata::_maxCount` 에 들어갑니다.
  찾지 못한 이름, 정수가 아닌 상수, 1 보다 작은 값은 생성 오류입니다. 상수가 다른 헤더에 있으면 그 헤더를 include 해야 합니다(파서도 같은 번역 단위만 봅니다).

### 3.6 중첩 형식과 필드 순서

- 필드는 `PROPERTY()` 가 붙은 멤버를 **선언 순서대로** 씁니다. 기반 클래스의 필드가 먼저입니다. 크기 접두는 없습니다. 같은 빌드가 읽으므로 끝을 알 필요가 없습니다.
- 다중 상속 타입은 생성 오류입니다(7.4 와 같은 이유, 생성기 `collectBase`).
- 다른 헤더의 중첩 타입은 같은 번역 단위의 선언에서 열거자 · `ENUM` · `REFLECT()` 표시만 읽고, `PROPERTY` 는 다시 모으지 않습니다. 실행 시간에 그 코덱을 찾아 의존을 확인합니다(`FixedLayout::findCodec`, 8.1 위험 2).

### 3.7 열거형

- 값만 씁니다. 이름 해시는 쓰지 않습니다. 열거자를 바꾸면 스키마 해시가 바뀌므로 이름이 필요 없습니다.
- `ENUM( Count = … )` 가 있으면 `[최소 열거자, Count - 1]`, 없으면 `[최소 열거자, 최대 열거자]` 범위 비트 수로 씁니다. 기반 타입 전체 폭으로 쓰지 않습니다. 범위가 32 비트를 넘는 열거형은 생성 오류입니다.
- 읽을 때 등록된 열거자 값이 아니면 `OutOfRange` 입니다. 생성기가 열거자 값 목록을 `switch` 로 만들어 실행 시간 조회가 없습니다.
- `ENUM( Flags )` 는 기반 타입 폭의 비트로 쓰고, 등록된 비트 밖의 비트가 켜져 있으면 쓰기와 읽기가 모두 실패합니다.

### 3.8 런타임 범위 양자화

`MechArenaSnapshot` 은 위치 범위를 같은 스냅샷의 `_arenaHalfSize` · `_ceiling` 로 정하고 그 값도 바이트에 싣습니다. `Min` · `Max` 가 **필드 이름**을 받게 해서 표현합니다. 벡터 타입은 원소마다 쉼표로 나눠 적습니다.

```cpp
struct MechPilotSnapshot
{
    PROPERTY( Step = 0.01, Min = "-_arenaHalfSize, 0, -_arenaHalfSize", Max = "_arenaHalfSize, _ceiling, _arenaHalfSize" )
    float3 _position{};
};

struct MechArenaSnapshot
{
    PROPERTY( Min = 1, Max = 100000 ) float32 _arenaHalfSize{ 200.0f };  // 범위 필드를 앞에 둔다
    PROPERTY( Min = 1, Max = 100000 ) float32 _ceiling{ 60.0f };
    PROPERTY( MaxCount = 255 ) vector<MechPilotSnapshot> _listPilot{};
};
```

- 이름은 선언 위치에서 바깥으로 찾습니다. 자기 타입의 앞선 필드를 먼저 보고, 없으면 그 타입을 필드로 가진 바깥 타입의 앞선 필드를 봅니다.
  읽기는 선언 순서이므로 범위 필드가 **앞에** 있어야 읽는 쪽이 범위를 압니다. 뒤에 있으면 생성 오류입니다.
- 바깥 타입으로 이름을 찾는 경우는 생성기가 중첩 타입을 부모 코덱 안에 펼칠 수 있을 때(3.2, 같은 헤더 · 공개 필드)만 됩니다. 펼칠 수 없거나 같은 중첩 타입을 이름이 다른 두 부모가 쓰면 생성 오류입니다.
- 범위 필드는 자기 `Min` 으로 하한을 강제합니다(자르지 않고 읽기 실패).
- 인스펙터는 같은 타입의 필드 하나를 가리키는 경계(`Min = "-_halfSize"`)를 그 객체의 지금 값으로 막습니다(단계 4-b). 생성기가 그 필드를 읽는 함수(`PropertyMetadata::_pReadMinRange` · `_pReadMaxRange`)를 내므로 이름이 틀리면 생성된 코드가 컴파일되지 않습니다.
  인스펙터와 읽기 공통 끝(`SerializerUtil::isWithinDeclaredRanges`)이 `PropertyMetadata::computeAllowedRange` 하나로 범위를 풀어서 이 함수는 Shipping 에도 있습니다. 바깥 타입의 필드를 가리키는 경계와 성분 목록은 읽기 함수가 없습니다(인스펙터는 바깥 객체를 모르고 벡터 성분에는 범위를 두지 않음).

**상수 이름.** `Min` · `Max` 는 리터럴과 필드 이름 말고 C++ 숫자 상수 이름도 받습니다(`Max = LockstepSession::kMaxInputDelay`, `Min = -Limit::kMaxRatio`). 생성기가 `MaxCount` 와 같은 함수로(`AstVisitor::findNamedConstant`) 값을 찾아 풀고, 풀린 값이 코덱 · 정규 문자열 · 인스펙터 범위에 들어갑니다.
`_` 로 시작하지 않는 이름이 상수이고 `_` 로 시작하는 이름은 필드입니다. 못 찾았거나 숫자가 아니면 생성 오류입니다. 같은 값을 숫자로 적던 때와 정규 문자열이 같으므로 리터럴을 상수로 바꿔도 스키마 해시는 바뀌지 않습니다.
**한계.** `Step` 은 상수를 받지 않습니다. 25 곳 남짓의 양자화 간격은 한 필드의 정밀도 선택이라 다른 상수와 짝인 곳이 없었습니다(8.2). 데이터(카탈로그 크기 등)로 정해지는 범위는 3.10 의 검증 훅에 둡니다.
데이터(카탈로그 크기 등)로 정해지는 범위는 키로 표현하지 않고 3.10 의 검증 훅에 둡니다.

### 3.9 엔디언과 정렬

`BitWriter` 는 낮은 비트부터 채우고 정수는 시프트로, 실수는 `memcpy` 로 얻은 `uint32` 비트를 같은 경로로 씁니다. 그래서 호스트 엔디언과 무관하고, 비트 단위라 정렬과 패딩이 없습니다. `writeBytes` · `writeBlob` 의 원본 바이트는 B 에서 쓰지 않습니다.

### 3.10 스트림 구성과 읽기 의미

```
[스키마 해시 64 비트] [최상위 타입의 필드들, 선언 순서, 태그 · 크기 없음]
```

- 해시는 최상위 한 번만 씁니다. 중첩 타입과 컨테이너 원소는 해시 없이 바로 필드입니다.
- 넷 채널은 핸드셰이크가 해시를 이미 확인했으므로 메시지마다 해시를 싣지 않습니다(5절). 본문만 쓰는 경로는 확인을 마친 채널 객체 안에만 있고 공개 API 로 내보내지 않습니다.
  온라인 연결은 메시지마다 타입 해시를 그대로 싣습니다. 메서드 표(`OnlineMethodTable`)는 메서드마다 받는 타입을 담지 않으므로, 한 메서드가 받는 타입이 바뀐 빌드끼리의 어긋남은 이 머리 해시가 `SchemaMismatch` 로 거절합니다.
- **원자성**: 읽기는 대상 객체를 복사한 임시 객체에 읽고, 끝까지 성공했을 때만 옮깁니다. 실패하면 대상은 그대로입니다. 복사본에서 시작하는 이유는 B 에 없는 필드(`FighterRuntime::_pDef` 같은 정의 포인터)를 보존하기 위해서입니다.
- **결과 형식**: `FixedReadResult` 는 `[[nodiscard]]` 열거형입니다. 값은 `Ok`, `SchemaMismatch`, `Truncated`, `OutOfRange`, `ValidateFailed`, `TrailingData` 입니다.
- **코덱이 없는 타입**: `write` · `read` · `equals` · `getSchemaHash` 에 넘기면 이유를 로그에 남기고 프로세스를 멈춥니다(모든 구성). 0 해시 같은 대체 값을 돌려주지 않습니다.
- **검증 훅**: 타입에 기존 `REFLECT( Validate = 함수 )` 가 있으면 읽기 끝에 부르고, 오류가 있으면 `ValidateFailed` 입니다. 새 훅을 만들지 않습니다. 데이터 범위 검사(`_moveIndex` 등)와 "같은 캐릭터인가" 대조 값이 이 훅으로 갑니다.
- 남은 비트가 있으면 실패입니다. 바이트 경계까지의 패딩 비트만 허용합니다.

### 3.11 스키마 해시

생성기가 적격 타입마다 **정규 문자열**을 만들고 FNV-1a 64 로 해시합니다. 정규 문자열에 들어가는 것은 아래와 같습니다.

1. B 인코딩 규칙 버전 상수 `FixedLayout::kVersion`. 인코더 규칙(varint 형태, 양자화 반올림)을 바꾸면 이 값 하나만 높여 모든 해시를 바꿉니다.
2. 타입의 FQN.
3. 필드마다 선언 순서로 이름, `TypeNameMap::normalize` 로 정규화한 타입 이름, B 에 영향을 주는 키(`Min`, `Max`, `Step`, `MaxCount`)와 값, 비트필드 여부.
4. 필드 타입이 열거형이면 FQN, 기반 타입, 모든 열거자의 `이름=값`.
5. 필드 타입이 다른 `REFLECT` 타입이면 그 타입의 해시. 다른 헤더의 타입일 수 있으므로 실행 시간에 그 타입의 코덱 테이블에서 얻어 섞고, 함수 지역 `static` 으로 한 번만 계산합니다.

필드 이름을 넣는 이유는 같은 타입 필드 둘의 순서를 바꾸는 변경(`_x` 와 `_z`)을 잡기 위해서입니다. 멤버 초기값, `DisplayName` · `Tooltip` 같은 표시 키, 이름 변경 리다이렉트는 바이트 배치와 무관하므로 넣지 않습니다.
생성기는 대상마다 정규 문자열 목록을 `FixedSchemas.gen.txt` 로 남깁니다. 해시가 왜 바뀌었는지 비교하는 진단용이며 빌드는 읽지 않습니다.

### 3.12 생성 비교

- 비교 함수는 B 와 같은 필드 집합을 선언 순서로 비교합니다. 실수는 **비트 패턴**으로 비교하므로 `NaN == NaN` 은 참, `-0.0f == +0.0f` 는 거짓입니다. "같은 바이트를 만드는가"와 뜻이 같아야 롤백 불일치 검사에 쓸 수 있기 때문입니다.
- 선언을 더하지 않으므로 `operator==` 를 생성할 수는 없습니다. 비교는 `FixedLayout::equals( a, b )` 로 부릅니다.

### 3.13 열거형 이름 조회

B 는 열거자 이름을 쓰지 않습니다. 로그와 진단용 `getEnumName` 은 FQN 해시로 `TypeRegistry` 를 찾고 없으면 `"Unknown"` 을 반환합니다(`EnumName.h`, 단계 7-a). 이 값은 로그용 표시라 폴백 금지의 대상이 아니고 데이터 읽기는 쓰지 않습니다.
빈틈 없는 열거형의 이름은 `EnumInfo::_listDenseName`(값으로 보관, 모듈 해제와 겹친 스레드 조회를 막기 위함)에, 템플릿 `getEnumName<E>` 의 포인터는 세대와 한 쌍으로 `TypeCacheSlot` 한 칸에 둡니다(따로 저장하면 세대 경계에서 새 세대에 옛 포인터가 붙음). 근거는 결정 기록 5-4 입니다.

### 3.14 마이그레이션 확장 지점

B 의 기본 동작은 스키마 해시가 다르면 거절입니다. 다만 구조가 변환을 막지 않게 확장 지점을 남깁니다. **지금은 구현하지 않습니다.**

- 읽기가 `SchemaMismatch` 를 내기 직전에 변환 레지스트리를 봅니다. 키는 (기록된 해시, 대상 타입)이고 값은 "옛 바이트 → 새 객체" 변환 함수입니다. 레지스트리가 비어 있으면 거절합니다.
- 옛 바이트를 해석하려면 옛 정규 문자열이 있어야 합니다. `FixedSchemas.gen.txt`(3.11)를 배포본과 함께 보관하면 범용 디코더를 만들 수 있습니다. 필요해질 때 만듭니다.
- 지금 변환할 옛 B 데이터가 없으므로 레지스트리의 조회 위치(읽기 함수 안 한 곳)만 비워 두고 변환 코드는 두지 않습니다.

## 4. 코드 생성 변경 범위

생성기 쪽 변경은 `AstVisitor.cpp`(필드 순서 · 중첩 타입 읽기), `ParsedReflection.h`(적격 판정과 이유, 범위 식), `AnnotationFields.cpp`(`Min` · `Max` 가 필드 이름 · 쉼표 목록을 받음), `Step` · `MaxCount` 두 키, `FixedCodecGenerator.*`(`CodeEmit` 로 함수 본문, 머리와 꼬리만 `Templates/FixedCodec*.tpl`)입니다.
런타임은 Serialization 이 리플렉션 타입의 인코딩을 가진다는 기존 원칙에 따라 `Engine/Serialization/Fixed` 에 둡니다. `ReflectionMacros.h` 는 바뀌지 않아 사용자가 쓰는 매크로가 늘지 않았습니다.
생성 오류(범위 필드가 뒤에 있음, `float32` 로 정확하지 않은 정수 경계, 이름을 찾지 못한 범위, 숫자가 아닌 필드의 `Min` · `Max`)는 헤더 경로, 줄, 필드 이름을 담고 생성기를 실패로 끝냅니다. 적격이 아닌 것은 오류가 아닙니다(3.1).

## 5. 호환 정책

**A 의 스키마 진화는 그대로 둡니다.** 세이브와 에셋은 태그, 기록 타입 변환(`tryCoerceBinaryPayload`), 버전 머리(판이 정확히 같을 때만 읽는다)로 읽습니다. 이관 함수와 옛 판 리더는 첫 출시 전이라 없다(2026-10-23, 결정 기록 5-4).
B 를 더한다고 A 의 이 경로를 줄이거나 B 와 공유하게 바꾸지 않습니다. 마이그레이션이 언제나 가능해야 한다는 요구는 A 가 맡습니다.

- **롤백 · 결정성 스냅샷 · 프로세스 안 복제**: 같은 프로세스, 같은 빌드입니다. 해시 불일치는 결함이며 `SW_ASSERT` 와 오류 로그로 멈춥니다.
- **`FightingMatch`**: 롤백은 B, 세이브 섹션은 A(버전 머리와 등록된 변환)입니다. 변환할 옛 세이브가 없으므로 옛 롤백 바이트 섹션을 읽는 변환 코드는 두지 않습니다.
- **게임 넷 연결**: 정확히 같은 버전만 받습니다(`NetTypes.h`). 채널에 등록된 메시지 타입의 스키마 해시를 모두 `NetWireVersion::combine` 에 섞으면 손 버전을 높이지 않아도 바이트 배치가 바뀔 때 핸드셰이크가 `VersionMismatch` 로 거절합니다(후보 `NetChannelSchemaHashHandshake`).
  손 버전은 의미만 바뀌고 배치는 그대로인 경우에만 높입니다.
- **온라인 서비스(B, 서버 · 클라이언트 같은 빌드 배포)**: 접속 핸드셰이크의 **첫 필드**가 "온라인 스키마 해시"이고 서버는 다른 어떤 바이트보다 먼저 이것을 비교합니다(단계 5-a).
  - 해시는 빌드에 올라온 키트 프로토콜 항목(`OnlineProtocolEntry`) 전부의 영역과 메시지 스키마 해시, `kBaseVersion` 을 섞습니다(`OnlineProtocolRegistry::computeSchemaHash`). 클라이언트가 올린 키트만 섞으면 키트 일부만 쓰는 클라이언트가 어긋나므로 빌드 전체를 섞습니다.
    메시지 스키마 해시는 키트의 `XxxProtocol::MessageTypes` 가 타입마다의 `FixedLayout::getSchemaHash` 를 섞은 값입니다. 키트는 이 목록으로만 메시지를 쓰고 읽으므로(목록 밖 타입은 컴파일이 멈춥니다) 해시용 목록을 따로 적는 곳이 없습니다.
    의미만 바뀌고 배치가 그대로인 변경(8.1 위험 6)은 `OnlineProtocolConstant::kBaseVersion` 하나를 올립니다.
  - 다르면 연결을 `kVersionMismatch` 로 거절하고 응답에 서버 해시를 실어 보냅니다. 클라이언트는 "업데이트 필요"로 끝나고 다시 접속하지 않으며, 서버는 거절한 뒤 연결을 닫습니다. 메시지를 하나라도 B 로 해석하기 전에 멈춥니다.
    이 오류를 화면으로 보이는 게임 UI 는 아직 없습니다. 신호는 `OnlineServiceClient::getState() == VersionMismatch` 와 응답의 `_errorCode` 이고, 계정 키트는 `LoginResult::UpdateRequired` 로 바꿉니다.
  - 이 첫 필드와 거절 응답은 B 가 아니라 고정된 작은 형식(해시 64 비트 + 오류 코드)입니다. B 스키마가 바뀌어도 이 비교 자체는 항상 읽혀야 하기 때문입니다.
  - 나중에 옛 클라이언트를 받아야 하면 로그인 · 버전 확인 같은 소수 메시지만 A 로 남기는 탈출구가 있습니다. 지금은 열지 않습니다.
- 타입 ID(`TypeInfo::_typeID`)는 실행 순서에 따라 정해지는 로컬 인덱스이므로 와이어에 싣지 않습니다. B 의 해시는 이름과 구조에서만 나오므로 빌드가 같으면 프로세스가 달라도 같습니다.

## 6. 테스트

단계마다 아래 테스트를 둡니다(`TestFixedLayout.cpp` 가 첫 구현). 성능 수치는 요구하지 않습니다.

- **왕복**: 값을 채운 객체를 쓰고 읽어 `FixedLayout::equals` 로 비교합니다. 경계값(범위 최소 · 최대, 빈 컨테이너, `MaxCount` 개)을 포함합니다.
- **스키마 변경 거부**: 필드 순서만 다른 두 타입의 정규 문자열 해시가 달라야 하고, 한쪽이 쓴 바이트를 다른 쪽이 읽으면 `SchemaMismatch` 여야 합니다. 필드 이름 변경, 열거자 추가, `Step` · `Min` 변경, `FixedLayout::kVersion` 변경도 각각 확인합니다.
- **적격 판정**: 포인터 필드, `MaxCount` 없는 `vector` 를 가진 타입에 코덱이 없고, 그 타입을 등록 API 에 넘기면 이유를 담은 오류로 멈추는지 확인합니다.
- **잘림 · 범위**: 바이트를 한 개씩 줄인 모든 접두에 대해 `Truncated` 가 나오는지, 범위 밖 개수와 열거자가 `OutOfRange` 인지, 실패 뒤 대상 객체가 그대로인지 확인합니다.
- **쓰기 실패**: 범위 밖 정수, `MaxCount` 초과에서 `FixedLayout::write` 가 `false` 인지 확인합니다.
- **이전 · 이후 동작 비교**: 손 코덱을 지우기 전에 같은 시뮬레이션을 손 코덱과 B 로 저장 · 복원해 이후 프레임의 상태 해시가 같은지 비교합니다. 바이트가 아니라 동작을 비교합니다.
- **넷 · 온라인 거절**: 메시지 타입 하나의 필드를 바꾼 빌드가 핸드셰이크에서 거절되는지, 온라인은 그 거절이 다른 메시지보다 먼저 오고 클라이언트가 "업데이트 필요"로 끝나는지 확인합니다.

**손 코드와 같은 수준이라는 구조상 근거.** 생성 코드는 손 코덱과 같은 `BitWriter` 호출을 같은 순서로 부르는 직선 코드입니다. 프로퍼티 목록 순회, 이름 해시 비교, 기록 타입 변환이 없고 필드 주소는 컴파일러가 상수 오프셋으로 만듭니다.
손 코드보다 많은 일은 최상위 해시 64 비트 한 번, 최상위 간접 호출 한 번(3.2), 읽기의 임시 복사 한 번입니다. 펼칠 수 없는 중첩 타입(다른 헤더 · private 필드)은 원소마다 간접 호출이 하나 더 있습니다.

## 7. 리플렉션 캐스트 비용

### 7.1 지금 구조

- `castTo<To>( From* )` 는 업캐스트와 같은 타입이면 컴파일 시간에 끝납니다(`Source/Engine/Reflection/ReflectionCast.h:135,175`). `castTo<Component>( Component*, 타입 )` 은 이 분기에 걸려 타입 인자를 보지 않고 항상 성공합니다.
  종류로 거를 때는 `getTypeInfo()->isDerivedFrom( 타입 )` 을 씁니다. `NavGeometryCollector.cpp:133` 주석이 같은 함정을 적고 있습니다.
- 다운캐스트는 세 단계입니다(`ReflectionCast.h:133-169`).
  1. 대상 타입: `findStaticType<To>()` 가 `To::StaticType()` 을 부릅니다. 이 함수는 `.gen.cpp` 에 정의되어 있어 인라인되지 않고, 대상 타입이 다른 모듈이면 DLL 경계 호출입니다. 내부는 `TypeLookupCache::find` 의 칸 읽기(순서 번호 · 포인터 · 세대 원자 로드와 지금 세대 로드)와 `isAlive` 확인입니다(`TypeRegistry.h`).
     조회 루프(`getComponent`)는 이것을 루프 밖에서 한 번 구합니다(`GameObject.h:288`).
  2. 동적 타입: `Component` 는 만들 때 받은 `_pTypeInfo` 포인터를 보관하고 `findCachedTypeInfo()` 가 그것을 반환합니다. 가상 호출은 캐시가 없을 때만 합니다(`Component.h:500-505`).
     객체에 타입 필드를 두는 개선은 `Component` 에 이미 되어 있습니다. `GameObject` 는 가상 `getTypeInfo()` 를 쓰고 결과를 캐시하지 않습니다(단계 7-a, [결정 기록](../09_Decisions.md) 5-4).
  3. 상속 검사: `TypeInfo::isDerivedFrom( const TypeInfo* )` 는 깊이마다 조상 이름 인덱스를 적은 테이블(디스플레이)을 봅니다. 원자 로드 네 번과 비교 한 번이며 잠금 · 해시 맵 · 문자열 비교 · 사슬 순회가 없습니다(`ReflectionTypes.h:1051-1065`).
     깊이가 8(`kAncestorDisplayDepth`, `ReflectionConstants.h:53`)을 넘거나 테이블이 없으면 `isDerivedFromSlow` 가 부모 포인터를 따라갑니다.
- `castToExact` 는 `Source` 에 없습니다.
- `Component.h:520-523` 주석이 `castTo<SceneComponent>` 를 약 8 ns 로 적고, 핫패스에서는 생성자가 세우는 비트(`isSceneComponent`)로 대신했다고 적습니다. 이 문서가 잰 값은 아닙니다.

### 7.2 개선 후보와 판단

**DFS 구간 번호(`[first, last]`)로 `isDerivedFrom` 을 정수 비교 두 번으로.** 채택하지 않습니다.

- 지금 디스플레이도 O(1) 입니다. 구간 번호가 줄이는 것은 원자 로드 두어 번입니다.
- 구간 번호는 전체 타입 트리를 깊이 우선으로 번호 매겨야 합니다. 타입이 하나 추가되면(DLL 로드, 핫 리로드, 플러그인) 다른 타입의 번호가 바뀝니다. 코드 생성기는 모듈 하나만 보므로 전역 번호를 빌드 시간에 정할 수 없고, 실행 시간에 다시 매기면 그 사이 캐스트가 틀린 번호를 봅니다.
- 디스플레이는 이름 인덱스 기반이라 타입 추가가 다른 타입의 테이블을 바꾸지 않습니다. 등록 · 해제 때 영향받는 테이블만 비웁니다(`ReflectionTypes.h:929-940`). 지금 구조가 동적 로드에 맞습니다.

**`getTypeInfo` 가상 호출을 타입 필드로.** `Component` 는 이미 했습니다. `GameObject` 는 가상 호출을 그대로 둡니다. 결과 캐시는 측정한 이득이 없고 엔진 재생성에서 낡아 넣지 않았습니다([결정 기록](../09_Decisions.md) 5-4).

**정적 타입이 확정된 호출부는 `static_cast`.** 가장 흔한 모양은 "핸들로 컴포넌트를 찾고 바로 캐스트"입니다.
`FacialAnimationComponent.cpp:455`, `PoseRetargetComponent.cpp:119`, `SkeletalMeshComponent.cpp:375`, `MotionWarpingComponent.cpp:91`, `ObjectStateSerializer.cpp:79` 가 그렇습니다.
`ComponentHandle`(`Source/Core/Container/ComponentHandle.h:17`)은 타입이 없는 id 라서 캐스트가 필요합니다. 타입이 있는 핸들 `TComponentHandle<T>` 를 만들면 핸들을 만들 때 타입이 정해지므로 찾은 뒤 `static_cast` 로 충분합니다.
id 슬롯이 다른 타입으로 재사용되는 경우는 지금도 Debug 단언으로만 확인합니다(`GameObjectManager.h:279-281`). 타입 핸들도 같은 단언을 유지합니다.
**도입하기로 정했습니다(사용자 결정).** 호출부를 바꾸는 변경이고, 필드 타입이 바뀌므로 `PROPERTY` 필드면 A 직렬화와 인스펙터도 함께 바뀝니다. 순서와 검증은 9절 7-d 에 있습니다.

**`getComponent<T>` 를 타입 인덱스 배열로.** `getComponent<T>` 는 오브젝트의 컴포넌트 목록을 선형으로 훑습니다(`GameObject.h:281-297`). 오브젝트당 컴포넌트는 적으므로 캐스트 비용보다 훑기 비용이 작습니다.
문제는 씬 전체 순회입니다. `GameObjectManager::forEachComponentOfType` 은 모든 오브젝트의 모든 컴포넌트를 훑습니다(`GameObjectManager.h:132-141`). 비용이 씬 크기에 비례하고 캐스트는 그 안의 작은 부분입니다.
구조 개선은 매니저가 타입별 컴포넌트 목록을 보관하는 것입니다. 등록 · 해제 때 그 타입과 조상 타입의 목록에 넣고 뺍니다. 조상 목록은 디스플레이 테이블로 얻습니다. 호출부 API 는 그대로입니다.
비용은 컴포넌트 추가 · 제거마다 깊이만큼의 목록 갱신과 메모리입니다. 언리얼의 `TObjectIterator` 는 전역 순회, 유니티의 `FindObjectsOfType` 도 전역 순회이므로 타입 목록은 그보다 나은 쪽입니다.

**스토리지 분리(컴포넌트 종류별 배열).** 렌더 · 물리 같은 엔진 핫루프는 이미 캐스트를 거의 쓰지 않습니다. `castTo<` 는 `Engine/Renderer` 에 1 곳(캡처 경로), `Engine/Physics` · `Engine/Graphics` · `Core/Network` 에 0 곳입니다.
엔진 핫루프가 자기 목록을 보관하는 구조라고 추정하지만, 그 구조 자체는 읽지 않았습니다(미확인). 컴포넌트 저장소를 종류별 배열로 바꾸는 일은 이 계획의 범위를 넘으므로 후보로만 적습니다.

### 7.3 지금 호출부

| 호출 | 건수 | 주로 있는 곳 |
|---|---|---|
| `getComponent<` | 413 | `GameFramework` 159, `Games` 158, `Engine/Object` 38 |
| `castTo<` | 83 | `Engine/Object` 11, `Editor` 10, `Games` 9 |
| `forEachComponentOfType<` | 28 | 게임플레이 · 에디터 · 내비게이션 쿠킹 |
| `isDerivedFrom(` | 16 | 에디터 명령, 내비게이션, UI 문서 로더 |
| `isA<` | 4 |  |

`getComponent<` 413 건 가운데 틱 안에서 부르는 비율은 세지 않았습니다. 이 수와 상관없이 `TypedComponents`(끝나서 문서를 지웠다 — git log) 가 타입별 목록과 `TComponentHandle<T>` 로 풀었습니다. `forEachComponentOfType<` 는 `HealthListenerComponent.cpp:15`, `FirstPersonCameraComponent.cpp:160` 처럼 게임플레이 이벤트 처리에서 부릅니다.
결론적으로 캐스트 자체는 이미 핫패스에 쓸 수 있는 구조입니다(잠금 · 해시 · 문자열 없음, O(1)). 남은 비용의 근본 원인은 캐스트가 아니라 **타입 없는 핸들**과 **씬 전체 순회**입니다.

### 7.4 위험

- **다중 상속 · 인터페이스**: 디스플레이는 부모 하나(`_parentFQN`)의 사슬만 표현합니다. 리플렉션이 파생이라고 답했지만 C++ 상속 관계가 아니면 `reinterpret_cast` 로 내려갑니다(`ReflectionCast.h:167`).
  두 번째 기반을 거치는 경우 포인터 보정이 없어 잘못된 주소가 됩니다. 생성기가 `REFLECT` 타입의 둘째 베이스와 가상 베이스를 헤더 · 줄 · 타입 이름과 함께 생성 오류로 막습니다(단계 7-b, `AstVisitor.cpp` `collectBase`).
- **모듈 경계 · 핫 리로드**: 디스플레이와 `TypeLookupCache` 는 이미 묘비(`isAlive`)와 테이블 비우기로 처리합니다. 타입 목록(7.2)을 더하면 모듈 해제 때 그 모듈 타입의 목록을 비워야 합니다.
- **유니티 빌드**: 캐스트 경로는 헤더 인라인과 `.gen.cpp` 정의뿐이라 영향이 없습니다.
- **ID 결정성**: `_typeID` 는 등록 순서 카운터입니다(`ReflectionCore.cpp:657`). 네트워크 · 디스크에 싣지 않습니다. 타입을 와이어에 실어야 하면 FQN 해시를 씁니다.

## 8. 위험과 남은 확인

### 8.1 위험과 완화

1. **적격이 아닌 타입을 B 로 쓰려는 경우가 빌드 시간이 아니라 기동 시간에 드러납니다.** 사용자가 선언하지 않으므로 사용 자리의 컴파일러는 코덱 유무를 모릅니다(3.1). 완화: B 진입점을 등록 API 몇 개로 모아 기동 때 한 번 확인하고, 오류 메시지에 적격이 아닌 필드와 이유를 담습니다.
2. **다른 헤더의 중첩 타입**: 생성기는 그 타입의 `PROPERTY` 를 다시 모으지 않고 실행 시간에 코덱 테이블 유무를 확인합니다(3.6).
3. **의도하지 않은 와이어 변경**: 선언이 없으므로 `REFLECT` 타입의 필드를 정리하거나 순서를 바꾸면 B 형식이 바뀝니다. 롤백 · 스냅샷은 같은 빌드끼리라 영향이 없고, 넷 · 온라인은 해시가 달라져 옛 빌드와의 연결이 거절될 뿐 조용히 잘못 읽지는 않습니다.
   알 수 있는 수단은 거절 로그의 양쪽 해시, `FixedSchemas.gen.txt` 비교, `PROPERTY` 헤더 주석과 Reflection README 의 "선언 순서는 와이어 형식" 문단입니다. 온라인 스키마 해시를 커밋된 기준 파일과 비교하는 테스트는 알림이지 정확성 장치가 아니라 두지 않습니다.
4. **리터럴 반복**: 3.8 의 `Min` · `Max` 는 상수 이름을 받으므로 코드의 상수와 짝인 경계는 상수로 적습니다(8.2). 숫자로 남은 경계는 값이 우연히 같은 단순 범위입니다. 어긋나도 해시가 바뀌므로 잘못 읽지는 않지만, 의도와 다른 범위가 됩니다.
   `MaxCount` 는 상수 이름을 받으므로(3.5) 상한을 코드와 두 곳에 적지 않습니다. 남은 숫자 `MaxCount` 중 같은 값의 상수가 있는데 뜻이 같은지 이름만으로 정할 수 없는 것(2026-10-21 기준 약 20 곳, 주로 온라인 키트의 커서 · 문구 길이)은 그 키트를 고칠 때 상수 이름으로 바꿉니다.
5. **`Min` · `Max` 의 뜻이 강해집니다.** 허용 범위가 와이어 범위가 되므로, 게임 코드가 허용 범위 밖 값을 잠깐 쓰던 곳은 B 쓰기 실패로 드러납니다. 결함이 드러나는 것이므로 의도한 결과입니다.
6. **해시에 들어가지 않는 의미 변경**: 단위를 미터에서 센티미터로 바꾸는 변경은 해시를 바꾸지 않습니다. 넷은 손 버전 상수를, 온라인은 `kBaseVersion` 을 남겨 이 경우에 올립니다.
7. **`REFLECT` 를 처음 붙이는 타입**: 스냅샷 구조체에 `REFLECT` 를 붙이면 `TypeRegistry` 에 등록되고 인스펙터 · A 직렬화 대상이 됩니다. 의도한 결과이지만 등록 수가 늘어납니다.

### 8.2 남은 확인

- 없음. `getComponent<` 413 건의 틱 안 비율(옛 단계 0)은 세지 않고 닫았습니다(7.3).
- 온라인 숫자 `MaxCount` 는 `wt/onlinemax` 에서 모두 상수 이름으로 바꿨습니다(8.1 위험 4). 같은 값이 여러 곳이면 상수 하나로 모았고(`AdminProtocol`, `EconomyProtocol`, `LoginConstant`, `MatchmakingLimit` 등), 서로 다른 키트의 같은 값(관리자 `kMaxMessageKeySize` · 서버 목록 `ServerDirectoryLimit::kMaxMessageKeySize`)은 키트끼리 포함할 수 없어 각자 두었습니다.
  같은 헤더에 같은 값의 상수가 있으면 알리는 게이트는 두지 않았습니다. 값이 우연히 같은 상수(해시 32 바이트와 이름 32 바이트 등)가 많아 오탐이 진짜 후보보다 많았기 때문입니다. `Min` · `Max` 의 반복 리터럴은 아래 `wt/minmaxconst` 항목에서 다룹니다.
- 기믹 · 장르 키트와 서버 버스 테스트 타입의 숫자 `MaxCount` 도 `wt/maxkits` 에서 상수 이름으로 바꿨습니다(`MechArenaSnapshot::kMaxListSize`, `HorrorSnapshot::kMaxListSize`, `InputCommandBuffer::kMaxFrameCount`, `RoundSeries::kMaxParticipantCount`, `BusTestLimit`). 채팅의 `ChatLimit::kMaxTextSize` · `kMaxReasonSize` 는 같은 값인 `ServiceReportConstant` 와 `static_assert` 로 맞춥니다(Base 는 키트를 포함할 수 없어 키트 쪽에 둠).
- `Min` · `Max` 가 상수 이름을 받게 넓혔습니다(`wt/minmaxconst`, 3.8). 소스와 테스트의 숫자 경계는 `Min` 1752 · `Max` 364 · `Step` 27 곳(직접 센 값)이고, 대부분은 `Min = 0` · `Min = 1` · `Max = 1.0` · `Max = 100` 같은 단순 범위로 다른 값과 짝이 없습니다.
  짝이 있는 곳은 필드 설명이나 클래스 문서가 같은 상수를 가리키는 `Max`(`Max` 는 리터럴만 받으므로 같은 값을 적는다고 쓴 주석 포함) 21 곳이었고 상수 이름으로 바꿨습니다: `NetConnection::kMaxSingleMessageSize`(복제 · MMO 예산), `LockstepSession::kMaxInputDelay`, `RollbackSession::kMaxInputDelay` · `kMaxPrediction`, `PhysicsSettings::kMaxWorkerCount2D`, `LedgerItemInstance::kMaxEnhanceLevel`, 온라인 목록 쪽수(`AdminProtocol`, `MailboxProtocol`, `EconomyProtocol`, `ChatLimit`, `LeaderboardLimit`), `MatchmakingLimit`, `PartyLobbyLimit`, `ServerDirectoryLimit::kMaxSeatCount`, `MonsterCollectorCatalog::kMaxLevel`, `FightingMatch::kDraw`.
  `Step` 은 짝이 없어 상수를 받게 하지 않았습니다. 값이 우연히 같은 것(`PhysicsSettings::_subStepCount` 의 16 과 `kMaxWorkerCount2D`, `MechArenaSnapshot` · `HorrorSnapshot` 의 인덱스 `Max = 255` 와 목록 길이 `kMaxListSize` 는 인덱스가 1 바이트라는 뜻이고 목록 길이와 별개, `RestaurantCatalog` 의 `Max = 24` 시각 등)은 숫자로 두었습니다.
  같은 값의 상수가 있는지를 알리는 게이트는 `MaxCount` 때와 같은 이유로 두지 않았습니다. 상수와 짝인 `Max` 가 더 있어도 생성 오류가 아니라 그 값이 해시에 들어가 어긋남이 드러납니다.
- 숫자 리터럴 `MaxCount` 를 생성 오류로 거절하는 것은 하지 않았습니다. 리터럴은 문서화된 입력 형식(3.5)이고, 고정 배치형 테스트 타입(`TestFixedLayoutTypes.h`)과 파서 테스트 문자열(`TestReflectionParser.cpp`, 잘못된 값 `2.5` 거절 포함)이 일부러 리터럴 경로를 확인하므로, 거절하면 그 테스트의 모양을 다시 짜야 해 작은 변경이 아닙니다.

## 9. 단계

완료한 단계는 한 줄(무엇이 어디에)만 남깁니다. 옛 단계 0(조사)은 다섯 항목이 각각 단계 1 · 3 · 7-b · 7-c · 7-d 로 풀려 쓸모가 없어 지웠습니다(번호는 비워 둡니다). 설계 근거는 결정 기록과 코드 헤더 주석, 위치는 1절 표입니다.

| 단계 | 내용 | 상태 |
|---|---|---|
| 1 | 생성기 · `TypeInfo::_pFixedCodec` · `FixedLayout` 런타임 · 테스트 | 끝남(2026-10-17, `a01e7a79a`) |
| 2 | `GimmickCircuitState` 하나에서 컴포넌트는 A, 묶음 · 넷 조각은 B. 설계는 `GimmickCircuitState`(끝나서 문서를 지웠다 — git log) 8절 | 끝남(`e6ae53938`, 2026-10-20 Debug `full-build` 경고 0 · 기믹 45 케이스 · `ReflectionTest` · `-L nogpu` 엔진 5 샤드 통과) |
| 3 | `FightingMatch` 롤백 B · 세이브 A, 두 읽기가 같은 `validateState`. 손 코덱 · 손 버전 상수 · `RoundSeries` · `InputCommandBuffer` 의 손 `writeState` · `readState` 삭제 | 끝남(`88add323a`, `5a14ccb39`) |
| 4-a | Mech · Horror 스냅샷을 B 로(`PROPERTY` 범위 키만, 손 코덱 · `kMagic` 없음, 스냅샷은 디스크에 남지 않음). 넷 채널 등록 · 해시 섞기는 후보 `NetChannelSchemaHashHandshake` | 끝남(`7fa9a9c70`, `016656bc2`) |
| 4-b | 인스펙터가 필드 이름 형태 `Min` · `Max` 를 씀(3.8). `ReflectionTypes.h`, `CodeGenerator.cpp`, `InspectorPropertyLayout.*` | 끝남(`7ce9eafe2`) |
| 5-a | Hello 첫 필드 스키마 해시, `OnlineProtocolRegistry`, "업데이트 필요" 거절 뒤 재접속 없음(5절). 키트 결과 열거형은 모두 `UpdateRequired` 를 가지며 `OnlineReplyUtil::markError` 가 키트 표보다 먼저 `kVersionMismatch` 를 그 값으로 바꿉니다 | 끝남(`5a1305dc3`) |
| 5-b | 온라인 11 키트를 B 로(`XxxProtocol::MessageTypes`, 손 코덱 · 손 `kVersion` 삭제, 영속 값은 `ServiceRecordUtil` 의 A). 결정 기록 5-9 | 끝남(`78b5affa3`) |
| 6 | 컴팩트 모드와 호출부 없는 `cloneObject` 삭제, `A-13` · `A-14` 해결 | 끝남(`626210330`) |
| 7-a | 열거형 이름 조회를 `EnumInfo::_listDenseName` · `EnumNameCache` 로(3.13) | 끝남(`58a651537`) |
| 7-b | `REFLECT` 둘째 · 가상 베이스를 생성 오류로(7.4, 결정 기록 5-4) | 끝남(`58a651537`) |
| 7-c | 매니저의 타입별 컴포넌트 목록 | 끝남(2026-10-19, 계획은 `TypedComponents`(끝나서 문서를 지웠다 — git log)) |
| 7-d | 타입 핸들 `TComponentHandle<T>` | 끝남(2026-10-19, 계획은 `TypedComponents`(끝나서 문서를 지웠다 — git log)) |
| 8 | `MaxCount` 가 C++ 정수 상수 이름을 받고 `PropertyMetadata::_maxCount` 에 상한을 둠(3.5). 기믹 · 온라인 키트의 숫자 중복 117 곳을 상수 이름으로, 대조 테스트(`StateLimitsMatchDeclaredMaxCount`, 키트 11 개의 `DeclaredLimitsMatchTheConstants`) 삭제 | 구현 끝(2026-10-21, `wt/maxcount`, 빌드 · 테스트 미확인) |

단계 3 에서 바뀐 기반 헤더 셋은 `FrameData.h`(`MoveTimeline` 의 `REFLECT`, `GuardStance` 의 `ENUM`), `InputCommandBuffer.h`(`InputFrame` · `InputCommandBuffer` 의 `REFLECT`), `RoundSeries.h`(`REFLECT`, `RoundSeriesPhase` 의 `ENUM`)입니다. 이 헤더를 건드리는 다른 작업과 병합할 때 겹칠 수 있습니다.
세이브 읽기의 `Min` · `Max` 는 자기 기술형 세 포맷의 읽기 공통 끝(`SerializerUtil::isWithinDeclaredRanges`)이 봅니다. 시퀀스 원소까지 늘 보고, 범위를 어디까지 볼지 고르는 인자를 두지 않습니다(설정 JSON · 데이터 XML · 세이브가 같은 계약).
단계 4-a: Horror 발전기의 진행 · 표시 두 목록은 원소 하나(`HorrorGeneratorSnapshot`)로 합쳤고, 메시지는 스트림 머리 해시를 그대로 실어 배치가 다른 빌드의 바이트를 `SchemaMismatch` 로 거절합니다.
