# 리플렉션 직렬화 형식 계획 (2026-10-17)

같은 `PROPERTY` 선언 하나에서 쓰임새가 다른 두 바이너리 형식을 만듭니다. 오래 남는 데이터는 지금의 **자기 기술형**(A)으로, 같은 빌드 안에서만 오가는 데이터는 새 **고정 배치형**(B)으로 씁니다.
B 의 읽기 · 쓰기 · 비교 함수는 실행 시간에 타입 정보를 찾지 않고, 빌드 시간에 코드 생성기가 타입마다 만듭니다. 손으로 쓴 코덱(롤백, 장르 스냅샷, 온라인 프로토콜)은 단계적으로 B 로 옮깁니다.
7절은 리플렉션 캐스트(`castTo`)의 비용을 같은 기준(측정이 아니라 구조와 원리)으로 따집니다.

이 문서는 다음 세션 작업자가 이것만 읽고 단계 1 을 시작할 수 있게 쓴 명세입니다. 근거는 실제로 읽은 코드만 `파일:줄` 로 적고, 읽지 않은 것은 "미확인"으로 적습니다.
줄 번호는 main `920db2ac7` 기준입니다. 단계가 끝나면 그 단계를 지우고, 결정은 [결정 기록](../09_Decisions.md)으로 옮깁니다.

## 1. 지금 구조

### 1.1 자기 기술형 바이너리(`BinarySerializer`)

- 스트림 머리는 `uint32` 하나입니다. 위 8 비트가 값 인코딩 버전, 아래 24 비트가 프로퍼티 수입니다(`Source/Engine/Serialization/Format/BinarySerializer.cpp:30-36`). 버전은 현재 버전 하나만 읽습니다(`:45-48`).
- 프로퍼티마다 이름 해시, 기록 타입 해시, 크기를 앞에 붙입니다. 읽는 쪽은 프로퍼티마다 타입의 프로퍼티 목록을 선형으로 훑어 이름 해시로 대상을 찾습니다(`:176-200`).
- 모르는 필드는 엄격 읽기에서 실패하거나 건너뛰고, 소프트 읽기에서는 orphan 으로 보관합니다(`:202-212`). 기록 타입이 다르면 `tryCoerceBinaryPayload` 로 값을 변환합니다(`:226-252`).
- 스트림에 없던 프로퍼티는 기본값으로 채웁니다(`:271-283`). 중복 확인은 프로퍼티가 64 개 이하면 비트마스크, 넘으면 `unordered_set` 입니다(`:27`, `:168-171`).
- 값 하나를 읽을 때마다 `SerializerUtil::deserializeValueBinary` 가 타입 이름으로 핸들러를 찾고, 없으면 열거형 레지스트리, 그다음 타입 레지스트리를 찾습니다(`Source/Engine/Serialization/Base/SerializerUtil.cpp:596-616`).
  열거형은 열거자 이름 해시와 값을 같이 씁니다(`SerializerUtil.cpp:117-123`).

이 비용은 스키마가 바뀐 뒤에도 예전 데이터를 읽기 위한 것입니다. 디스크 세이브, 에셋, 핫 리로드, 에디터 복사 · 붙여넣기에는 꼭 필요합니다.
같은 빌드의 같은 프로세스 안에서 쓰고 읽는 데이터에는 필요 없는 비용입니다.

같은 파일에 **적응형 컴팩트 모드**(`serializeCompact`)가 이미 있습니다. 이 모드는 이름 대신 프로퍼티 순서로 짝을 맞추고, 헤더 주석이 "중간에 넣거나 순서를 바꾼 스키마로 읽으면 엉뚱한 필드에 들어갑니다"라고 적고 있습니다(`BinarySerializer.h` 5절 주석).
스키마를 확인하는 장치가 없어서 잘못 읽어도 알 수 없습니다. `Archive::serializeCompactObject` 외에 호출하는 곳은 없습니다(`Source` 전체 grep). B 가 이 모드를 대신하고, 단계 6 에서 컴팩트 모드를 지웁니다.

`cloneObject` 는 POD 가 아니면 자기 기술형으로 쓰고 다시 읽어 복제합니다(`BinarySerializer.cpp:538-558`). `Source` 안에서 이 함수를 부르는 곳은 없습니다.
`BinarySerializer::` 를 직접 부르는 곳은 Serialization 밖에 세 파일뿐입니다. 에디터 컴포넌트 복사(`EditorWorkspace.cpp:275`, `EditorTransformCommands.cpp:103,150`)와 세이브의 커스텀 상태(`GameInstanceBase.cpp:454,537`)이며 모두 A 에 남습니다.

### 1.2 손으로 쓴 코덱(옮길 후보)

| 위치 | 형식 | 버전 관리 | 구조 결함 |
|---|---|---|---|
| `FightingMatch.cpp:1089-1203` 롤백 | `BitWriter`, 필드 나열 | 손 상수 `kStateVersion = 2`(`:18`) | 필드를 더하고 버전을 안 높이면 조용히 잘못 읽음 |
| `FightingMatch.cpp:1214-1229` 세이브 | 롤백 바이트를 그대로 섹션에 저장 | 위와 같음 | 빌드가 바뀌면 진행 중 대전 세이브를 읽지 못함 |
| `GimmickCircuit.cpp:461-515` 상태 | `memcpy` 바이트 | `kStateTag`, `kStateVersion`, 회로 구조 해시 | 기대 크기를 손으로 계산(노드당 `16`, `:492`) |
| `MechArenaSnapshot.cpp:42-155` | `BitWriter`, 양자화 | `kMagic` 만 | 개수를 255 로 잘라 조용히 버림(`:55,62,84`) |
| `HorrorSnapshot.cpp` | `BitWriter`, 양자화 | `kMagic = "DBD1"`(`:15`) | 같은 개수 자르기(`:44`) |
| 온라인 `*Protocol.cpp` 12 파일, 2,046 줄 | `BitWriter` | 키트마다 손 `kVersion`(`ChatProtocol.h:69` 등) | 와이어를 바꾸면 버전을 높이라는 주석(`ChatProtocol.h:23`)이 유일한 장치 |

`BitWriter::writeInt` 는 범위 밖 정수를 경고 없이 잘라 씁니다(`Source/Core/Network/BitStream.cpp:70-74`). 범위를 잘못 정하면 값이 조용히 바뀝니다.
Mech 스냅샷의 구조체(`MechArenaSnapshot.h:21-66`)와 `FighterRuntime`(`FightingMatch.h:117`)에는 `REFLECT` · `PROPERTY` 가 없습니다.

손으로 쓴 `operator==` 는 `Source` 에 75 개입니다. 이 가운데 같은 파일에 `PROPERTY` 가 있는 것은 4 파일(`atomic.h`, `Component.h`, `DataPath.h`, `WidgetTypes.h`)뿐입니다.
나머지는 `Core/Container`(12), `Core/Math`(8), `Engine/Graphics`(7), `Core/String`(6) 같은 값 형식이 대부분이고, 필드를 하나씩 비교하는 데이터 레코드인지는 분류하지 않았습니다(미확인, 단계 0).
그래서 생성 비교가 대신할 수 있는 범위는 "B 로 옮기는 타입의 비교"로 한정합니다. 나머지 `operator==` 를 바꾸려고 타입에 리플렉션을 붙이지 않습니다.

### 1.3 코드 생성 파이프라인

- 생성기는 `Tools/ReflectionParser` 의 C++ 콘솔 도구이며 libclang 으로 헤더를 파싱합니다(`Tools/ReflectionParser/README.md`). 파이썬 생성기는 없습니다.
- CMake 단계는 `cmake/Engine/ReflectionCodeGen.cmake` 의 `sw_addReflectionStep` 입니다. `REFLECT(` · `ENUM(` · `REFLECT_CONTAINER(` 가 있는 헤더를 모아(`:41-58`) 헤더마다 `<이름>.gen.cpp` 와 `<이름>.gen.h` 를 만듭니다(`:127-128`).
  템플릿은 `Tools/ReflectionParser/Templates/*.tpl` 이고 의존성으로 등록되어 있습니다(`:82`).
- `.gen.cpp` 는 유니티 배치에서 빠집니다(`:183`, 주석 `:25` "ODR"). `.gen.cpp` 는 원본 헤더를 include 합니다(`Templates/FileHeader.tpl`).
- 프로퍼티 하나는 `CodeGenerator::emitPropertyInfoEntry`(`CodeGenerator.cpp:328-430`)가 `PropertyInfo` 생성 식으로 씁니다. 이름 해시, 정규화한 타입 이름 해시, `offsetof`, 컨테이너 래퍼입니다.
  읽기 · 쓰기는 생성하지 않고, 실행 시간에 이 정보로 값을 찾아 씁니다.
- `REFLECT_BODY()` 는 `template <typename> friend struct ::sw::generated::Registrar;` 를 선언합니다(`Source/Engine/Reflection/ReflectionMacros.h:57-79`). 생성 코드는 이 friend 로 private 필드에 접근합니다.
  파서 빌드에서는 `annotate( "REFLECT_BODY" )` 가 붙은 멤버 함수로 바뀌어 파서가 표시를 읽습니다(`:57-61`).
- `.gen.h` 는 지금 비트 플래그 열거형의 트레이트 특수화만 담고, 원본 헤더를 include 하지 않습니다. 대상의 모든 TU 에 `/FI` 로 들어가는 우산 헤더(`FlagOps.gen.h`)가 모으기 때문입니다(`CodeGenerator.cpp:733-745`).
- 어노테이션 키 하나는 `PredefinedAnnotationField.xxx` 한 줄, `ParsedReflection.h` 멤버, `Source/Core/Predefined/AnnotationMeta.txt` 의 철자로 정합니다. 셋이 맞지 않으면 파서가 시작할 때 멈춥니다(`PredefinedAnnotationField.xxx:25-27`).
- 언어 표준은 C++17 이상입니다(`cmake/Modules/Options/CppStandard.cmake:6-7`). 그래서 `operator== = default` 를 쓸 수 없습니다.

### 1.4 같은 결함이 있는 곳

모두 "형식이 바뀌었는지를 사람이 기억해야 하는" 구조입니다. 뿌리는 **바이트 배치의 원본이 선언과 따로 손 코드에 있다**는 점입니다.

- 손 버전 상수: `FightingMatch` `kStateVersion`, `GimmickCircuit` `kStateVersion`, 온라인 키트 `kVersion` 12 개, `NetWireVersion::kCore`(`Source/Core/Network/NetTypes.h:42`).
- 손으로 계산한 크기: `GimmickCircuit.cpp:465,492` 의 `_listNode.size() * 16`.
- 조용히 버리는 값: 개수 255 자르기(Mech · Horror), `writeInt` 의 범위 자르기, Mech 의 `max( 1.0f, halfSize )`(`MechArenaSnapshot.cpp:45-46`).
- 스키마 확인 없는 순서 기반 형식: 컴팩트 모드(1.1).
- 모르는 타입 폴백: `deserializeValueBinary` 는 어느 핸들러도 없으면 1 바이트를 건너뛰고 성공을 반환합니다(`SerializerUtil.cpp:629-633`). 이 문서의 범위 밖이지만 B 에서는 컴파일 오류가 되는 구조로 막습니다(3.4). A 쪽 수정은 백로그 후보입니다(8.2).

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
| 프로세스 안 복제(`cloneObject`) | B | 같은 프로세스 |
| 온라인 서비스 프로토콜 | B 후보 | 배포 정책에 따름, 8.3 질문 1 |

진행 중인 대전을 세이브하는 경우(`FightingMatch::writeState`)는 세이브이므로 A 입니다. 지금은 롤백 바이트를 그대로 저장해 같은 상태를 한 형식으로만 씁니다(`FightingMatch.cpp:1214`).
B 로 옮기면 롤백은 B, 세이브는 A 로 나눕니다. 두 형식이 같은 선언에서 생성되므로 "형식을 둘로 따로 관리한다"는 예전 걱정은 없어집니다.

## 3. 고정 배치형(B) 명세

### 3.1 선언

```cpp
struct MechPilotSnapshot
{
    REFLECT_FIXED( MechPilotSnapshot );

    PROPERTY( FixedStep = 0.1, FixedMin = 0, FixedMax = 100000 )
    float32 _health{ 0.0f };

    PROPERTY( FixedMin = 0, FixedMax = 4095 )
    int32 _magazineAmmo{ 0 };
    // ...
};
```

- 타입은 `REFLECT()` 와 함께 클래스 본문에 `REFLECT_FIXED( 타입 이름 )` 를 둡니다. 매크로는 아래 멤버를 **선언만** 하고, 정의는 `.gen.cpp` 가 생성합니다.
  `void swWriteFixed( ::sw::BitWriter& ) const;`, `[[nodiscard]] ::sw::FixedReadResult swReadFixed( ::sw::BitReader& );`, `bool swFieldsEqual( const 타입& ) const;`, `static uint64 swFixedSchemaHash();`, `bool operator==( const 타입& ) const;`, `bool operator!=( const 타입& ) const;`.
- 멤버 함수로 두는 이유입니다. `.gen.cpp` 에만 정의된 함수 템플릿 특수화를 다른 TU 가 부르려면 그 TU 에 특수화 선언이 보여야 합니다. 보이지 않으면 진단 없는 위반(ill-formed NDR)입니다.
  `.gen.h` 로 선언을 내보내면 중첩 타입은 전방 선언을 할 수 없고, 다른 대상에서 그 `.gen.h` 가 보이는지도 확인하지 못했습니다(미확인). 언리얼의 `GENERATED_BODY()` 처럼 본문 매크로가 선언하면 두 문제가 모두 없습니다.
- 매크로가 타입 이름을 받는 이유는 비교 함수의 인자 타입 때문입니다. `REFLECT_BODY()` 의 `decltype( this )` 꼴은 반환 타입에만 쓸 수 있습니다.
- 파서 빌드에서 매크로는 `annotate( "REFLECT_FIXED" )` 가 붙은 멤버 함수 선언이 됩니다. `REFLECT_BODY()` 와 같은 방법입니다.
- 필드는 `PROPERTY()` 가 붙은 멤버만, **선언 순서대로** 씁니다. 기반 클래스가 있으면 기반이 `REFLECT_FIXED` 여야 하고 기반의 필드가 먼저입니다. `Transient` 필드는 빠집니다.
- 새 어노테이션 키(전부 `Emit = Manual`, B 에만 영향)는 아래와 같습니다. 편집기 범위 `Min` · `Max` 와 섞지 않습니다. 인스펙터 슬라이더를 바꿨는데 와이어가 바뀌면 안 되기 때문입니다.

| 키 | 값 | 뜻 |
|---|---|---|
| `FixedMin` · `FixedMax` | 정수 또는 실수 | 정수는 범위 비트 수, 실수는 양자화 범위 |
| `FixedStep` | 실수 | 실수 양자화 간격, 없으면 32 비트 그대로 |
| `FixedRangeFrom` | 같은 타입의 앞선 필드 이름 | 범위를 그 필드의 값으로 정함(3.8) |
| `MaxCount` | 정수 | 문자열 · 컨테이너 최대 길이(필수) |
| `FixedCodec` | 함수 이름 둘 | 손 코덱 탈출구(3.8) |

### 3.2 스트림 구성

```
[스키마 해시 64 비트] [최상위 타입의 필드들, 선언 순서, 태그 · 크기 없음]
```

- 해시는 최상위 한 번만 씁니다. 중첩 타입과 컨테이너 원소는 해시 없이 바로 필드입니다.
- 넷 채널처럼 핸드셰이크가 해시를 이미 확인한 곳은 해시 없이 본문만 씁니다(5절). 본문만 쓰는 API 는 확인을 마친 채널 객체만 부를 수 있게 합니다(단계 5).
- 진입점은 `Source/Engine/Serialization/Fixed/FixedLayout.h` 의 `writeFixed( const T&, BitWriter& )` 와 `[[nodiscard]] FixedReadResult readFixed( BitReader&, T& )` 입니다.
  리플렉션 타입의 인코딩은 Serialization 이 가진다는 기존 원칙(`SerializeReflectionRPC.cpp` 머리 주석)을 따릅니다.

### 3.3 스키마 해시

생성기가 타입마다 **정규 문자열**을 만들고 FNV-1a 64 로 해시합니다. 정규 문자열에 들어가는 것은 아래와 같습니다.

1. B 인코딩 규칙 버전 상수 `kFixedFormatVersion`. 인코더 규칙(varint 모양, 양자화 반올림)을 바꾸면 이 값 하나만 높이면 모든 해시가 바뀝니다.
2. 타입의 FQN.
3. 필드마다 선언 순서로 이름, `TypeNameMap::normalize` 로 정규화한 타입 이름, B 어노테이션 키와 값(정해진 순서), 비트필드 여부.
4. 필드 타입이 열거형이면 FQN, 기반 타입, 모든 열거자의 `이름=값` 입니다. 열거자 순서나 값을 바꾸면 해시가 바뀝니다.
5. 필드 타입이 다른 B 타입이면 그 타입의 해시입니다. 다른 헤더나 대상의 타입일 수 있으므로 생성기가 문자열로 넣지 않고, 실행 시간에 `Nested::swFixedSchemaHash()` 를 섞습니다.
   결과는 함수 지역 `static` 으로 한 번만 계산합니다. 순환 중첩(A 가 B 를, B 가 A 를 값으로 가짐)은 C++ 에서 불가능하므로 무한 재귀는 없습니다.

필드 이름을 넣는 이유는 같은 타입 필드 둘의 순서를 바꾸는 변경(`_x` 와 `_z`)을 잡기 위해서입니다. 이름만 바꾼 경우도 거부되지만, 같은 빌드끼리만 읽으므로 비용이 없습니다.
멤버 초기값은 넣지 않습니다. 바이트 배치와 무관하기 때문입니다.

### 3.4 필드 타입별 인코딩

| 필드 타입 | 인코딩 |
|---|---|
| `bool`, `uint8` 플래그 | 1 비트 |
| 정수, `FixedMin` · `FixedMax` 있음 | 범위 비트 수(`BitMath::computeBitsRequired`) |
| 정수, 범위 없음 | 부호 있으면 `writeVarInt`, 없으면 `writeVarUint` |
| `float32`, `FixedStep` 있음 | `writeQuantizedFloat` |
| `float32`, `FixedStep` 없음 | 32 비트 원본 비트 |
| `float3` 등 수학 타입 | 원소마다 위 규칙(벡터용 키는 원소에 공통) |
| 열거형 | 3.7 |
| `string` | 길이 + 바이트(3.5) |
| `vector<T>` | 개수 + 원소(3.5) |
| `T[N]`, `std::array<T, N>` | 원소 N 개, 개수 없음 |
| `REFLECT_FIXED` 타입 | 그 타입의 필드(3.6) |

위에 없는 타입은 **생성 오류**입니다. 포인터, 참조, 핸들, 접근자 프로퍼티, `unordered_*` 컨테이너, `REFLECT_FIXED` 가 아닌 구조체가 해당합니다.
`unordered_*` 를 막는 이유는 순회 순서가 결정적이지 않아 같은 값이 다른 바이트가 되기 때문입니다. 롤백 해시 비교가 깨집니다.
핸들(`ComponentHandle` 등)이 필요하면 그 형식에 B 코덱을 하나 등록하는 일을 별도 단계로 둡니다. 지금 후보에는 핸들 필드가 없습니다.

범위가 있는 정수가 범위 밖이면 **쓰기가 실패**합니다. `writeInt` 처럼 자르지 않습니다. 생성 코드는 범위를 확인한 뒤 `writeBits` 를 부릅니다.
실수 양자화는 선언 자체가 손실 인코딩이므로 범위 밖 값을 경계로 자릅니다. 이 차이는 키 이름에서 드러납니다. `FixedStep` 가 붙은 필드만 손실이 있습니다.
쓰기 실패는 결함이므로 `writeFixed` 는 `[[nodiscard]] bool` 을 반환하고, 호출부는 단언하거나 오류로 멈춥니다.

### 3.5 가변 길이

- 문자열과 `vector` 는 `MaxCount` 가 **필수**입니다. 없으면 생성 오류입니다. 넷에서 받은 길이로 메모리를 잡기 전에 상한이 있어야 하기 때문입니다.
- 개수는 `[0, MaxCount]` 범위 정수로 씁니다. 쓸 때 개수가 `MaxCount` 를 넘으면 쓰기가 실패합니다. Mech 스냅샷처럼 255 개로 자르지 않습니다.
- 읽을 때 개수가 `MaxCount` 를 넘으면 `FixedReadResult::OutOfRange` 입니다. 잘린 스트림은 `Truncated` 입니다(`BitReader::hasOverflowed`, `BitStream.h:136`).
- 상한은 정적 값입니다. 판의 탄 수처럼 런타임 설정이 상한이면, 데이터가 정하는 최대값보다 크거나 같은 정적 상한을 적고 데이터 쪽 검증을 따로 둡니다.

### 3.6 중첩 형식

- 필드가 `REFLECT_FIXED` 타입이면 그 타입의 `swWriteFixed` 를 부릅니다. 크기 접두는 없습니다. 같은 빌드가 읽으므로 끝을 알 필요가 없습니다.
- 생성기는 같은 헤더 밖의 타입이 `REFLECT_FIXED` 인지 libclang AST 에서 매크로 표시로 확인합니다. 확인하지 못하면 생성 오류입니다.
- 기반 클래스의 필드는 기반의 함수를 먼저 불러 씁니다. 다중 상속 타입은 생성 오류입니다(7.4 와 같은 이유).

### 3.7 열거형

- 값만 씁니다. 이름 해시는 쓰지 않습니다. 열거자를 바꾸면 해시가 바뀌므로 이름이 필요 없습니다.
- `ENUM( Count = … )` 가 있으면 `[최소 열거자, Count - 1]` 범위 비트 수로 씁니다. 없으면 기반 타입 전체 폭입니다.
- 읽을 때 등록된 열거자 값이 아니면 `OutOfRange` 입니다. 생성기가 열거자 값 목록을 `switch` 로 만들기 때문에 실행 시간 조회가 없습니다.
  `FightingMatch.cpp:1174-1175` 의 `bBadEnum` 손 검사가 여기에 해당합니다.
- `ENUM( Flags )` 는 기반 타입 폭의 비트로 쓰고, 등록된 비트 밖의 비트가 켜져 있으면 쓰기와 읽기가 모두 실패합니다.

### 3.8 런타임 범위 양자화와 한계

`MechArenaSnapshot` 은 위치 범위를 같은 스냅샷의 `_arenaHalfSize` · `_ceiling` 로 정하고 그 값도 바이트에 싣습니다(`MechArenaSnapshot.cpp:27-35,49-50`). 표현 방법은 두 가지입니다.

1. **같은 타입의 앞선 필드**: `PROPERTY( FixedStep = 0.01, FixedRangeFrom = "_arenaHalfSize" )`. 범위는 `[-값, +값]` 입니다. 생성기는 그 필드가 **앞에 선언된** 스칼라인지 확인합니다.
   읽을 때는 이미 읽은 값을 쓰므로 순서가 맞습니다. 범위 필드는 `FixedMin` 로 하한을 강제합니다(Mech 의 `>= 1.0f` 검사). 자르지 않고 읽기 실패입니다.
2. **다른 계층의 값**: Mech 는 조종사 위치(`MechPilotSnapshot`)가 부모(`MechArenaSnapshot`)의 값을 씁니다. 키로 계층을 넘는 참조를 표현하면 생성기가 코덱 문맥을 아래로 넘겨야 하고, 그 설계가 이 계획에서 가장 복잡해집니다.
   그래서 이 경우는 `FixedCodec = "writePosition, readPosition"` 탈출구를 씁니다. 손 함수 두 개(쓰기, 읽기)를 필드 하나에 붙이고, 함수 시그니처는 생성 코드가 컴파일 시간에 확인합니다.
   탈출구 함수의 이름과 시그니처 문자열은 해시에 들어가지만 함수 본문은 들어가지 않습니다. 본문을 바꾸면 해시가 그대로입니다. 이것이 이 방법의 한계이며, 탈출구 함수는 `kFixedCodecVersion` 상수를 같은 줄에 적어 해시에 넣게 합니다.
   탈출구 개수는 단계 3 끝에 세어 결정 기록에 남기고, 늘어나면 문맥 전달 설계를 다시 검토합니다.

비트 폭과 개수 상한을 속성으로 표현할 때의 한계는 아래와 같습니다.

- 키 값은 리터럴만 받습니다. `kMaxCount` 같은 C++ 상수를 이름으로 참조하려면 파서가 상수 식을 계산해야 하는데, 이 단계에서는 하지 않습니다. 같은 상수를 여러 필드가 쓰면 리터럴이 반복됩니다(8.1 위험 3).
- 하한 · 상한이 데이터(카탈로그 크기 등)로 정해지는 값은 정적 키로 표현할 수 없습니다. `FightingMatch` 의 `_moveIndex < moveCount` 검사(`:1176-1177`)가 그 예이며, 3.10 의 검증 훅에 둡니다.

### 3.9 엔디언과 정렬

- `BitWriter` 는 낮은 비트부터 채우고, 정수는 시프트로 씁니다(`BitStream.h:1-6` 머리 주석). 실수는 `memcpy` 로 `uint32` 비트를 얻은 뒤 같은 경로로 씁니다(`BitStream.cpp:76-80`). 그래서 호스트 엔디언과 무관합니다.
- 비트 단위이므로 정렬이 없습니다. 구조체의 패딩 바이트는 쓰지 않습니다. `GimmickCircuit` 의 `memcpy` 방식은 호스트 엔디언 그대로였지만 같은 프로세스 안이라 문제가 없었습니다. B 로 옮기면 그 의존도 사라집니다.
- `writeBytes` · `writeBlob` 의 원본 바이트는 B 에서 쓰지 않습니다.

### 3.10 읽기 의미

- **원자성**: `readFixed( reader, inoutValue )` 는 `inoutValue` 를 복사한 임시 객체에 읽고, 끝까지 성공했을 때만 옮깁니다. 실패하면 `inoutValue` 는 그대로입니다.
  복사본에서 시작하는 이유는 B 에 없는 필드(`FighterRuntime::_pDef` 같은 정의 포인터)를 보존하기 위해서입니다. `FightingMatch::loadState` 의 `arrLoaded` 복사(`:1147`)를 생성 코드가 대신합니다.
- **결과 형식**: `FixedReadResult` 는 `[[nodiscard]]` 열거형입니다. 값은 `Ok`, `SchemaMismatch`, `Truncated`, `OutOfRange`, `ValidateFailed` 입니다. `bool` 이 아니라서 호출부가 이유를 로그에 남길 수 있습니다.
- **검증 훅**: 타입에 기존 `REFLECT( Validate = 함수 )`(`CodeGenerator.cpp:572-576`)가 있으면 읽기 끝에 부르고, 오류가 있으면 `ValidateFailed` 입니다. 새 훅을 만들지 않습니다.
  데이터 범위 검사(`_moveIndex` 등)와 "같은 캐릭터인가" 대조 값(`FightingMatch.cpp:1100`)이 이 훅으로 갑니다.
- 남은 비트가 있으면 실패입니다. 최상위 읽기 끝에서 확인합니다. 바이트 경계까지의 패딩 비트만 허용합니다.

### 3.11 생성 비교

- `swFieldsEqual` 은 B 와 같은 필드 집합을 선언 순서로 비교합니다. 실수는 **비트 패턴**으로 비교합니다. 그래서 `NaN == NaN` 은 참, `-0.0f == +0.0f` 는 거짓입니다.
  "같은 바이트를 만드는가"와 뜻이 같아야 롤백 불일치 검사에 쓸 수 있기 때문입니다. 수학적 비교가 필요한 곳은 손 비교를 그대로 둡니다.
- `operator==` 는 `swFieldsEqual` 을 부릅니다. 매크로가 선언하므로 같은 타입에 손 `operator==` 를 또 두면 컴파일 오류가 납니다. 둘 중 하나만 남습니다.

### 3.12 열거형 이름 조회

B 는 열거자 이름을 쓰지 않습니다. 로그와 진단용 이름 조회 `getEnumName` 은 FQN 해시로 `TypeRegistry` 를 찾고, 없으면 `"Unknown"` 을 반환합니다(`Source/Engine/Reflection/EnumName.h:16-25`).
API 는 그대로 두고 내부만 바꿉니다.

- 생성기가 열거자 값이 `0..Count-1` 로 빈틈없는 열거형에 이름 정적 배열을 만들고 `EnumInfo` 에 그 포인터를 등록합니다. 조회는 배열 인덱스입니다.
- 템플릿 `getEnumName<E>` 는 `EnumInfo` 포인터를 `TypeLookupCache` 와 같은 함수 지역 캐시에 둡니다. 호출마다 해시 맵을 찾지 않습니다.
- 이름 문자열에서 값을 찾는 경로(`_mapNameToValue`)는 에디터와 데이터 파서만 씁니다. 그대로 둡니다.
- `"Unknown"` 반환은 로그용 표시라 폴백 금지 원칙의 대상이 아닙니다. 데이터 읽기는 이 함수를 쓰지 않습니다(`EnumName.h:6` 주석).

## 4. 코드 생성 변경 범위

| 파일 | 변경 |
|---|---|
| `Source/Engine/Reflection/ReflectionMacros.h` | `REFLECT_FIXED( T )` 두 분기 추가 |
| `Tools/ReflectionParser/AstVisitor.cpp` | 표시 감지, 필드 순서 보존 확인, 기반 · 중첩 타입 표시 확인 |
| `Tools/ReflectionParser/ParsedReflection.h` | `_bFixedLayout`, 필드별 B 키 멤버 |
| `PredefinedAnnotationField.xxx`, `Source/Core/Predefined/AnnotationMeta.txt` | 3.1 의 키 다섯 개 |
| `Tools/ReflectionParser/CodeGenerator.cpp` · `.h` | `emitFixedLayout`, 정규 문자열, 생성 오류 |
| `Tools/ReflectionParser/Templates/FixedLayout*.tpl` | 함수 머리 · 꼬리 템플릿 |
| `Tools/ReflectionParser/ParserDefines.h` | 템플릿 파일 · 키 상수(미확인: 상수 위치) |
| `Source/Engine/Serialization/Fixed/FixedLayout.h` · `.cpp` | `writeFixed` · `readFixed` · `FixedReadResult` |
| `Source/Engine/Reflection/EnumName.cpp` · 생성기 열거형 등록 | 3.12 |

- 함수 본문은 템플릿보다 `CodeEmit` 로 쓰는 편이 맞습니다. 필드마다 분기가 많아서입니다. 머리와 꼬리만 템플릿으로 두어 지금의 `TypeRegistrarBegin.tpl` · `End.tpl` 과 맞춥니다.
- 생성 오류는 헤더 경로, 줄, 필드 이름을 담고 생성기를 실패로 끝냅니다. 지금 `AnnotationMeta.txt` 에 없는 토큰을 실패로 처리하는 것과 같은 방식입니다(`Tools/ReflectionParser/README.md` "철자 목록").

**`.gen.cpp` 크기.** 필드 하나에 쓰기 · 읽기 · 비교 각 한두 줄과 해시 문자열 한 줄이 늘어납니다. 대략 필드당 6 ~ 10 줄입니다.
후보 타입(롤백 · 스냅샷 · 회로 · 온라인 메시지)은 수십 개, 필드는 수백 개라 늘어나는 양은 수천 줄입니다. `REFLECT_FIXED` 가 없는 타입은 한 줄도 늘지 않습니다.
새 번역 단위는 없습니다. 이미 있는 `.gen.cpp` 에 덧붙고, `REFLECT` 를 처음 붙이는 헤더만 `.gen.cpp` 하나가 생깁니다.

**[빌드 속도 계획](BuildSpeed.md) 12 절과의 관계.** 그 절의 문제는 `ReflectionTypes.h` 를 TU 1,150 ~ 1,250 개가 읽는다는 것입니다.

- `ReflectionMacros.h` 에는 매크로와 `BitWriter` · `BitReader` · `FixedReadResult` 전방 선언만 더합니다. 이 헤더를 읽는 TU 수는 늘지 않고 내용만 몇 줄 늡니다.
- `FixedLayout.h` 와 `BitStream.h` 는 `REFLECT_FIXED` 타입이 있는 헤더의 `.gen.cpp` 와 실제로 B 를 쓰는 `.cpp` 만 include 합니다. 생성기는 그 헤더에 B 타입이 있을 때만 include 줄을 씁니다.
- `ReflectionTypes.h` 는 건드리지 않습니다. B 는 `PropertyInfo` · `TypeInfo` 를 쓰지 않기 때문입니다. 12 절의 팬아웃을 늘리지 않습니다.

**유니티 빌드 ODR.** `.gen.cpp` 는 유니티 배치에서 빠집니다(`ReflectionCodeGen.cmake:183`). 생성하는 함수는 모두 그 타입의 멤버 정의라 정의가 하나뿐입니다.
`.gen.cpp` 안 도우미는 이름 없는 네임스페이스에 두고, 이름에 타입 식별자(`sanitizeIdentifier`)를 붙입니다. 나중에 유니티 제외가 풀려도 충돌하지 않습니다.

## 5. 호환 정책

- **롤백 · 결정성 스냅샷 · 프로세스 안 복제**: 같은 프로세스, 같은 빌드입니다. 해시 불일치는 결함이며 `SW_ASSERT` 와 오류 로그로 멈춥니다. 다른 경로로 읽지 않습니다.
- **게임 넷 연결**: 지금도 정확히 같은 버전만 받습니다. `NetWireVersion` 주석이 "옛 형식을 읽는 경로는 두지 않는다"고 적고 있고, 게임 · 키트 버전을 `combine` 으로 섞습니다(`NetTypes.h:35-57`). 협상은 하지 않습니다.
  B 로 옮긴 메시지 타입의 스키마 해시를 모두 이 `combine` 에 넣습니다. 손 버전을 높이지 않아도 바이트 배치가 바뀌면 핸드셰이크가 `VersionMismatch` 로 거절합니다. 손 버전은 의미만 바뀌고 배치는 그대로인 경우에만 높입니다.
  핸드셰이크를 통과한 연결은 메시지마다 해시를 싣지 않습니다(3.2). 1,200 바이트 패킷에 8 바이트씩 붙이지 않기 위해서입니다.
- **온라인 서비스**: 키트마다 `getProtocolVersion()` 을 서버가 정확히 비교하고, 다르면 `kVersionMismatch` 입니다(`OnlineServiceHost.cpp:408-425`). 지금도 협상은 없습니다.
  B 로 옮기면 `getProtocolVersion()` 이 `combine( kVersion, 그 키트 메시지 해시들 )` 을 반환합니다. 결과적으로 서버와 클라이언트가 같은 빌드로 함께 배포되어야 합니다. 이 결정은 사용자에게 묻습니다(8.3 질문 1).
- 타입 ID(`TypeInfo::_typeID`)는 실행 순서에 따라 정해지는 로컬 인덱스이므로(`ReflectionCore.cpp:657`) 와이어에 싣지 않습니다. B 의 해시는 이름과 구조에서만 나오므로 빌드가 같으면 프로세스가 달라도 같습니다.

## 6. 옮기는 순서와 검증

순서는 위험이 낮고 사용처가 하나인 것부터입니다. 바이트가 같은 프로세스 안에만 머무는 롤백과 회로 상태가 먼저이고, 와이어를 바꾸는 스냅샷과 프로토콜이 나중입니다.

1. **생성기와 런타임** (단계 1): 손 코덱은 건드리지 않습니다.
2. **`GimmickCircuit` 상태** (단계 2): 손 크기 계산을 없애는 효과가 가장 분명합니다. `_layoutHash` 대조는 검증 훅으로 둡니다.
3. **`FightingMatch` 롤백과 세이브 분리** (단계 3).
4. **Mech · Horror 스냅샷** (단계 4): 3.8 의 탈출구 개수를 이 단계에서 확정합니다.
5. **넷 핸드셰이크에 스키마 해시 넣기, 온라인 프로토콜** (단계 5): 사용자 결정 뒤에 합니다.
6. **컴팩트 모드 삭제, `cloneObject` 를 B 로** (단계 6).

단계마다 아래 테스트를 둡니다. 성능 수치는 요구하지 않습니다.

- **왕복**: 값을 채운 객체를 쓰고 읽어 `swFieldsEqual` 로 비교합니다. 경계값(범위 최소 · 최대, 빈 컨테이너, `MaxCount` 개)을 포함합니다.
- **스키마 변경 거부**: 테스트 전용 헤더에 필드 순서만 다른 두 타입을 두고 같은 FQN 처럼 해시를 비교합니다. 해시가 달라야 하고, 한쪽이 쓴 바이트를 다른 쪽이 읽으면 `SchemaMismatch` 여야 합니다.
  같은 방법으로 필드 이름 변경, 열거자 추가, `FixedStep` 변경, `kFixedFormatVersion` 변경을 각각 확인합니다.
- **잘림 · 범위**: 바이트를 한 개씩 줄인 모든 접두에 대해 `Truncated` 가 나오는지, 범위 밖 개수와 열거자가 `OutOfRange` 인지 확인합니다. 실패 뒤 `inoutValue` 가 그대로인지도 확인합니다.
- **쓰기 실패**: 범위 밖 정수, `MaxCount` 초과에서 `writeFixed` 가 `false` 인지 확인합니다.
- **이전 · 이후 동작 비교**: 옮기는 단계에서는 손 코덱을 지우기 전에, 같은 시뮬레이션을 손 코덱과 B 로 각각 저장 · 복원해 이후 프레임의 상태 해시가 같은지 비교합니다.
  롤백은 기존 롤백 테스트(되감아 다시 실행한 결과가 같다)를 그대로 통과해야 합니다. 바이트 자체는 다릅니다. 해시 머리와 인코딩이 바뀌므로 바이트가 아니라 동작을 비교합니다.
- **넷 해시**: 단계 5 에서 메시지 타입 하나의 필드를 바꾼 빌드가 핸드셰이크에서 `VersionMismatch` 로 거절되는지 확인합니다.

**손 코드와 같은 비용이라는 구조상 근거.** 생성 코드는 손 코덱과 같은 `BitWriter` 호출을 같은 순서로 부르는 직선 코드입니다.
실행 시간의 타입 조회, 프로퍼티 목록 순회, 이름 해시 비교, 기록 타입 변환이 없고, 필드 주소는 멤버 접근이라 컴파일러가 상수 오프셋으로 만듭니다.
손 코드보다 많은 일은 최상위 해시 64 비트 한 번, 읽기의 임시 복사 한 번입니다. 임시 복사는 `FightingMatch` 손 코드도 하고 있습니다(`:1147`).

## 7. 리플렉션 캐스트 비용

### 7.1 지금 구조

- `castTo<To>( From* )` 는 업캐스트와 같은 타입이면 컴파일 시간에 끝납니다(`Source/Engine/Reflection/ReflectionCast.h:135,175`). `castTo<Component>( Component*, 타입 )` 은 이 분기에 걸려 타입 인자를 보지 않고 항상 성공합니다.
  종류로 거를 때는 `getTypeInfo()->isDerivedFrom( 타입 )` 을 씁니다. `NavGeometryCollector.cpp:133` 주석이 같은 함정을 적고 있습니다.
- 다운캐스트는 세 단계입니다(`ReflectionCast.h:133-169`).
  1. 대상 타입: `findStaticType<To>()` 가 `To::StaticType()` 을 부릅니다. 이 함수는 `.gen.cpp` 에 정의되어 있어 인라인되지 않고, 대상 타입이 다른 모듈이면 DLL 경계 호출입니다. 내부는 `TypeLookupCache::find` 의 원자 로드 한 번과 `isAlive` 확인입니다(`TypeRegistry.h:94-110`).
     조회 루프(`getComponent`)는 이것을 루프 밖에서 한 번 구합니다(`GameObject.h:288`).
  2. 동적 타입: `Component` 는 만들 때 받은 `_pTypeInfo` 포인터를 보관하고 `findCachedTypeInfo()` 가 그것을 반환합니다. 가상 호출은 캐시가 없을 때만 합니다(`Component.h:500-505`).
     객체에 타입 필드를 두는 개선은 `Component` 에 이미 되어 있습니다. `GameObject` 는 가상 `getTypeInfo()` 를 씁니다(`GameObject.h:75`). 캐시 여부는 미확인입니다.
  3. 상속 검사: `TypeInfo::isDerivedFrom( const TypeInfo* )` 는 깊이마다 조상 이름 인덱스를 적은 테이블(디스플레이)을 봅니다. 원자 로드 네 번과 비교 한 번이며 잠금 · 해시 맵 · 문자열 비교 · 사슬 순회가 없습니다(`ReflectionTypes.h:1051-1065`).
     깊이가 8(`kAncestorDisplayDepth`, `ReflectionConstants.h:53`)을 넘거나 테이블이 없으면 `isDerivedFromSlow` 가 부모 포인터를 따라갑니다.
- `castToExact` 는 `Source` 에 없습니다.
- `Component.h:520-523` 주석이 `castTo<SceneComponent>` 를 약 8 ns 로 적고, 핫패스에서는 생성자가 세우는 비트(`isSceneComponent`)로 대신했다고 적습니다. 이 문서가 잰 값은 아닙니다.

### 7.2 개선 후보와 판단

**DFS 구간 번호(`[first, last]`)로 `isDerivedFrom` 을 정수 비교 두 번으로.** 채택하지 않습니다.

- 지금 디스플레이도 O(1) 입니다. 구간 번호가 줄이는 것은 원자 로드 두어 번입니다.
- 구간 번호는 전체 타입 트리를 깊이 우선으로 번호 매겨야 합니다. 타입이 하나 추가되면(DLL 로드, 핫 리로드, 플러그인) 다른 타입의 번호가 바뀝니다. 코드 생성기는 모듈 하나만 보므로 전역 번호를 빌드 시간에 정할 수 없고, 실행 시간에 다시 매기면 그 사이 캐스트가 틀린 번호를 봅니다.
- 디스플레이는 이름 인덱스 기반이라 타입 추가가 다른 타입의 테이블을 바꾸지 않습니다. 등록 · 해제 때 영향받는 테이블만 비웁니다(`ReflectionTypes.h:929-940`). 지금 구조가 동적 로드에 맞습니다.

**`getTypeInfo` 가상 호출을 타입 필드로.** `Component` 는 이미 했습니다. `GameObject` 는 미확인이며 단계 7-a 에서 확인합니다.

**정적 타입이 확정된 호출부는 `static_cast`.** 가장 흔한 모양은 "핸들로 컴포넌트를 찾고 바로 캐스트"입니다.
`FacialAnimationComponent.cpp:455`, `PoseRetargetComponent.cpp:119`, `SkeletalMeshComponent.cpp:375`, `MotionWarpingComponent.cpp:91`, `ObjectStateSerializer.cpp:79` 가 그렇습니다.
`ComponentHandle`(`Source/Core/Container/ComponentHandle.h:17`)은 타입이 없는 id 라서 캐스트가 필요합니다. 타입이 있는 핸들 `TComponentHandle<T>` 를 만들면 핸들을 만들 때 타입이 정해지므로 찾은 뒤 `static_cast` 로 충분합니다.
id 슬롯이 다른 타입으로 재사용되는 경우는 지금도 Debug 단언으로만 확인합니다(`GameObjectManager.h:279-281`). 타입 핸들도 같은 단언을 유지합니다. 호출부를 바꿔야 하는 변경이며, 필드 타입이 바뀌므로 그 필드가 `PROPERTY` 면 A 직렬화와 인스펙터도 함께 봐야 합니다.

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

`getComponent<` 413 건 가운데 틱 안에서 부르는 비율은 세지 않았습니다(미확인, 단계 0). `forEachComponentOfType<` 는 `HealthListenerComponent.cpp:15`, `FirstPersonCameraComponent.cpp:160` 처럼 게임플레이 이벤트 처리에서 부릅니다.
결론적으로 캐스트 자체는 이미 핫패스에 쓸 수 있는 구조입니다(잠금 · 해시 · 문자열 없음, O(1)). 남은 비용의 뿌리는 캐스트가 아니라 **타입 없는 핸들**과 **씬 전체 순회**입니다.

### 7.4 위험

- **다중 상속 · 인터페이스**: 디스플레이는 부모 하나(`_parentFQN`)의 사슬만 표현합니다. 리플렉션이 파생이라고 답했지만 C++ 상속 관계가 아니면 `reinterpret_cast` 로 내려갑니다(`ReflectionCast.h:167`).
  두 번째 기반을 거치는 경우 포인터 보정이 없어 잘못된 주소가 됩니다. 지금 그런 타입이 있는지는 미확인입니다(단계 0). 구조로 막으려면 생성기가 `REFLECT` 타입의 다중 상속을 생성 오류로 처리합니다(단계 7-b).
- **모듈 경계 · 핫 리로드**: 디스플레이와 `TypeLookupCache` 는 이미 묘비(`isAlive`)와 테이블 비우기로 처리합니다. 타입 목록(7.2)을 더하면 모듈 해제 때 그 모듈 타입의 목록을 비워야 합니다.
- **유니티 빌드**: 캐스트 경로는 헤더 인라인과 `.gen.cpp` 정의뿐이라 영향이 없습니다.
- **ID 결정성**: `_typeID` 는 등록 순서 카운터입니다(`ReflectionCore.cpp:657`). 네트워크 · 디스크에 싣지 않습니다. 타입을 와이어에 실어야 하면 FQN 해시를 씁니다.

## 8. 위험과 열린 질문

### 8.1 위험

1. **필드 순서 보존**: B 는 선언 순서에 의존합니다. `AstVisitor` 가 `_listProperty` 를 선언 순서로 채우는지 읽지 않았습니다(미확인). 단계 1 의 첫 확인입니다.
2. **해시에 들어가지 않는 의미 변경**: 단위를 미터에서 센티미터로 바꾸는 변경은 해시를 바꾸지 않습니다. 손 버전 상수의 역할이 그 경우에만 남습니다. 넷 쪽 손 버전은 지우지 않습니다.
3. **리터럴 반복**: 3.8 의 키 값이 상수를 참조하지 못해 같은 값이 여러 필드에 반복됩니다. 값이 어긋나도 해시가 바뀌므로 잘못 읽지는 않지만, 의도와 다른 범위가 됩니다.
4. **`REFLECT` 를 처음 붙이는 타입**: 스냅샷 구조체에 `REFLECT` 를 붙이면 `TypeRegistry` 에 등록되고 인스펙터 · A 직렬화 대상이 됩니다. 의도한 결과이지만 등록 수가 늘어납니다.
5. **`FixedCodec` 탈출구 남용**: 늘어나면 손 코덱이 다시 생깁니다. 3.8 의 개수 기록으로 감시합니다.
6. **세이브 형식 변경**: `FightingMatch` 세이브를 A 로 바꾸면 기존 세이브 섹션은 읽지 못합니다. 진행 중 대전 세이브가 실제로 배포된 적이 없다면 문제가 없습니다(미확인).

### 8.2 백로그로 넘길 것

- `SerializerUtil::deserializeValueBinary` 의 "모르는 타입이면 1 바이트 건너뛰고 성공"(`SerializerUtil.cpp:629-633`)을 오류로 바꿉니다. A 의 결함이며 이 계획과 독립입니다.
- `BitWriter::writeInt` 의 조용한 범위 자르기를 단언으로 바꿉니다. 손 코덱이 남아 있는 동안의 안전망입니다.

### 8.3 사용자 결정이 필요한 것

1. **온라인 서비스 프로토콜을 B 로 옮길지**: 옮기면 서버와 클라이언트를 같은 빌드로 함께 배포해야 합니다. 지금도 정확 비교라 사실상 같지만, 손 `kVersion` 을 높이지 않은 와이어 변경까지 거절하게 됩니다.
   운영 중인 서비스에서 예전 클라이언트를 받아야 한다면 온라인은 A 계열(태그 + 협상)로 따로 설계해야 합니다. 권장안은 "같은 빌드 배포, B 로 옮김"입니다.
2. **`FightingMatch` 세이브를 A 로 나누는 것**: 기존 세이브 섹션의 호환을 버려도 되는지(8.1 위험 6).
3. **타입 핸들 도입(7.2)**: `ComponentHandle` 필드 타입이 바뀌어 씬 데이터 · 인스펙터에 영향이 있습니다. 진행 여부를 묻습니다.

## 9. 단계

| 단계 | 내용 | 건드릴 파일 | 겹침 주의 | 에이전트(모델) |
|---|---|---|---|---|
| 0 | 조사: `operator==` 분류, 틱 안 `getComponent`, 다중 상속 `REFLECT` 타입, `GameObject` 타입 캐시 | 없음(읽기) | 없음 | `scout`(haiku) |
| 1 | 생성기 · 매크로 · `FixedLayout` 런타임 · 테스트 | 4절 목록, `Source/Tests` 새 파일 | `ReflectionMacros.h` 는 전체 재빌드 | `integrator`(opus) |
| 2 | `GimmickCircuit` 을 B 로 | `GimmickCircuit.*` | 없음 | `worker`(sonnet) |
| 3 | `FightingMatch` 롤백 B, 세이브 A | `FightingMatch.*` | 질문 2 뒤 | `worker`(opus) |
| 4 | Mech · Horror 스냅샷 | `MechArenaSnapshot.*`, `HorrorSnapshot.*` | 두 파일 한 손 | `worker`(sonnet) |
| 5 | 넷 핸드셰이크 해시, 온라인 프로토콜 12 개 | `NetTypes.h`, `OnlineServiceHost.cpp`, `*Protocol.*` | 질문 1 뒤, 프로토콜은 한 손 | `integrator`(opus) |
| 6 | 컴팩트 모드 삭제, `cloneObject` B | `BinarySerializer.*`, `Archive.*` | 다른 직렬화 작업과 겹침 | `worker`(sonnet) |
| 7-a | `GameObject` 타입 캐시, 열거형 이름 배열 | `GameObject.*`, `EnumName.cpp`, 생성기 열거형 등록 | 단계 1 생성기와 같은 파일 | `worker`(sonnet) |
| 7-b | `REFLECT` 다중 상속 생성 오류 | `AstVisitor.cpp` | 단계 1 과 같은 파일, 순서대로 | `worker`(sonnet) |
| 7-c | 매니저의 타입별 컴포넌트 목록 | `GameObjectManager.*`, `GameObjectStore.*` | 오브젝트 수명 작업과 겹침 | `integrator`(opus) |
| 7-d | 타입 핸들 | `ComponentHandle.h`, 호출부 | 질문 3 뒤 | `integrator`(opus) |

단계 1 의 끝 조건은 아래와 같습니다.

- 테스트 전용 헤더의 B 타입 세 개(스칼라만, 컨테이너 · 열거형 포함, 중첩)가 6 절의 왕복 · 거부 · 잘림 · 쓰기 실패 테스트를 통과합니다.
- 지원하지 않는 필드 타입과 `MaxCount` 없는 컨테이너가 생성 오류로 멈추는지 확인합니다. 생성기를 직접 실행하는 테스트로 확인합니다(`ReflectionParser.exe --dump`, `Tools/ReflectionParser/README.md`).
- `REFLECT_FIXED` 가 없는 기존 헤더의 `.gen.cpp` 내용이 바뀌지 않습니다. 생성 폴더를 단계 전후로 비교합니다.
