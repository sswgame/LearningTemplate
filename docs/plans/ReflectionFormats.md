# 리플렉션 직렬화 형식 계획 (2026-10-17)

같은 `PROPERTY` 선언 하나에서 쓰임새가 다른 두 바이너리 형식을 만듭니다. 오래 남는 데이터는 지금의 **자기 기술형**(A)으로, 같은 빌드 안에서만 오가는 데이터는 새 **고정 배치형**(B)으로 씁니다.
B 의 읽기 · 쓰기 · 비교 함수는 실행 시간에 프로퍼티 목록을 훑지 않고, 빌드 시간에 코드 생성기가 타입마다 만듭니다. 손으로 쓴 코덱(롤백, 장르 스냅샷, 온라인 프로토콜)은 단계적으로 B 로 옮깁니다.

**원칙: 사용자는 B 를 선언하지 않습니다.** 게임 · 엔진 코드 작성자는 지금처럼 `REFLECT()` 와 `PROPERTY()` 만 씁니다. B 전용 매크로나 등록 목록은 없습니다.
생성기는 적격인 모든 `REFLECT` 타입에 B 코덱을 만들고, B 가 필요한지는 그 타입을 받는 등록 API(롤백, 넷 채널, 온라인 메서드)가 정합니다(3.1). 범위 · 길이 같은 B 메타데이터는 기존 `PROPERTY` 키 `Min` · `Max` 의 확장과 키 둘(`Step`, `MaxCount`)로 적습니다(3.4).

**원칙: 마이그레이션은 언제나 가능해야 합니다.** 세이브 · 에셋(A)의 스키마 진화는 그대로 두고, B 에도 변환을 넣을 확장 지점을 남깁니다(3.14, 5절). 지금 변환할 옛 데이터가 없으므로 현 단계에서 변환 코드는 만들지 않습니다.

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
| Mech 스냅샷(옛 `MechArenaSnapshot.cpp`, 단계 4-a 에서 지움) | `BitWriter`, 양자화 | `kMagic` 만 | 개수를 255 로 잘라 조용히 버림 |
| Horror 스냅샷(옛 `HorrorSnapshot.cpp`, 단계 4-a 에서 지움) | `BitWriter`, 양자화 | `kMagic = "DBD1"` | 같은 개수 자르기 |
| 온라인 `*Protocol.cpp` 12 파일, 2,046 줄 | `BitWriter` | 키트마다 손 `kVersion`(`ChatProtocol.h:69` 등) | 와이어를 바꾸면 버전을 높이라는 주석(`ChatProtocol.h:23`)이 유일한 장치 |

`BitWriter::writeInt` 는 범위 밖 정수를 경고 없이 잘라 씁니다(`Source/Core/Network/BitStream.cpp:70-74`). 범위를 잘못 정하면 값이 조용히 바뀝니다.
`FighterRuntime`(`FightingMatch.h:117`)에는 `REFLECT` · `PROPERTY` 가 없습니다. Mech · Horror 스냅샷 구조체는 단계 4-a 에서 `REFLECT` · `PROPERTY` 를 받았습니다.

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
- 조용히 버리는 값: `writeInt` 의 범위 자르기. Mech · Horror 의 개수 255 자르기와 Mech 의 `max( 1.0f, halfSize )` 는 단계 4-a 에서 쓰기 실패로 바뀌었습니다.
- 스키마 확인 없는 순서 기반 형식: 컴팩트 모드(1.1).
- 모르는 타입 폴백: `deserializeValueBinary` 는 어느 핸들러도 없으면 1 바이트를 건너뛰고 성공을 반환합니다(`SerializerUtil.cpp:629-633`). 이 문서의 범위 밖이지만 B 에서는 인코딩 테이블 밖의 타입을 가진 타입에 코덱이 생기지 않고, 등록 API 가 이유와 함께 멈추는 구조로 막습니다(3.1, 3.4). A 쪽 수정은 백로그 후보입니다(8.2).

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
| 온라인 서비스 프로토콜 | B | 서버 · 클라이언트 같은 빌드 배포(5절) |

진행 중인 대전을 세이브하는 경우(`FightingMatch::writeState`)는 세이브이므로 A 입니다. 지금은 롤백 바이트를 그대로 저장해 같은 상태를 한 형식으로만 씁니다(`FightingMatch.cpp:1214`).
B 로 옮기면 롤백은 B, 세이브는 A 로 나눕니다. 두 형식이 같은 선언에서 생성되므로 "형식을 둘로 따로 관리한다"는 예전 걱정은 없어집니다.
세이브 섹션이 A 이므로 버전 머리와 등록된 변환으로 마이그레이션하는 경로가 열려 있습니다. 지금 변환할 옛 세이브가 없으므로 옛 롤백 바이트 섹션을 읽는 변환 코드는 현 단계에서 두지 않습니다.

## 3. 고정 배치형(B) 명세

### 3.1 어느 타입이 B 코덱을 받는가: 모든 적격 `REFLECT` 타입

**규칙.** 코드 생성기는 `REFLECT()` 가 붙은 모든 타입 가운데 **적격**인 타입에 B 코덱을 만듭니다. 사용자는 아무것도 더 선언하지 않습니다.
적격이란 B 에 들어가는 필드(`Transient` 가 아닌 모든 `PROPERTY`)의 타입이 3.4 의 인코딩 테이블 안에 있다는 뜻입니다. 기반 클래스가 있으면 기반도 적격이어야 합니다.
적격이 아닌 타입은 코덱 없이 등록되고, 생성기는 그 이유(필드 이름과 타입)를 `.gen.cpp` 주석과 `--dump` 출력에 남깁니다. 오류로 멈추지 않습니다. 포인터나 길이 상한 없는 `vector` 를 가진 `REFLECT` 타입이 많아서, 적격이 아니라는 사실 자체는 결함이 아니기 때문입니다.

**B 가 필요한 곳은 사용하는 자리가 정합니다.** B 를 쓰는 진입점(3.10 의 롤백 저장, 넷 채널 등록, 온라인 메서드 등록)이 처음 그 타입을 볼 때 코덱이 있는지 확인합니다.
코덱이 없으면 그 자리에서 이유를 담은 오류로 멈춥니다. 진입점은 모두 기동 때 한 번 실행하는 등록 코드이므로, 실행 파일을 켜는 순간 드러납니다(8.1 위험 1).

후보 셋을 비교한 결과는 아래와 같습니다.

| 후보 | 판단 |
|---|---|
| ① 모든 `REFLECT` 타입(적격만) | 채택 |
| ② 사용 자리 스캔 · 등록 목록 파일 | 기각 |
| ③ 기존 필드 속성(`Replicated` 등)에서 도출 | 기각, 나중의 부분 집합 용도로만 |

**① 의 비용.** `Source` 에 `REFLECT(` 줄이 378 개, `PROPERTY(` 가 2,097 개, 반사 헤더가 317 개입니다(grep).
생성 코드는 필드당 쓰기 · 읽기 · 비교 · 해시 문자열을 합쳐 6 ~ 10 줄이므로, 모두 적격이라 해도 약 1 만 3 천 ~ 2 만 1 천 줄이 317 개 `.gen.cpp` 에 나뉘어 붙습니다. 파일당 평균 40 ~ 70 줄입니다.
빌드 시간 영향은 구조로 보면 작습니다. 근거는 아래 넷입니다. 측정은 하지 않았습니다.

- 새 번역 단위가 없습니다. 이미 있는 `.gen.cpp` 에 덧붙습니다.
- `.gen.cpp` 의 컴파일 비용 대부분은 PCH 와 원본 헤더 include 이고, 덧붙는 것은 템플릿 없는 직선 코드입니다.
- 팬아웃이 늘지 않습니다. `.gen.cpp` 만 `FixedLayout.h` · `BitStream.h`(Core 의 작은 헤더)를 include 합니다. 모든 TU 에 강제 include 되는 우산 헤더 `FlagOps.gen.h` 는 건드리지 않습니다.
- `ReflectionTypes.h` 는 한 번만 바뀝니다. `TypeInfo` 에 코덱 테이블 포인터 하나를 더하고, 테이블 구조체는 전방 선언만 합니다. [빌드 속도 계획](BuildSpeed.md) 12 절의 문제는 이 헤더가 **자주** 바뀌는 것이고, 한 번의 변경은 그 문제를 키우지 않습니다.

남는 비용은 실행 파일 크기입니다. 코덱 함수는 등록 테이블이 참조하므로 링커가 지우지 않습니다. 적격 타입만 생성하는 것으로 줄이고, 단계 1 끝에 늘어난 크기를 기록합니다.

**② 를 기각한 이유.** 생성기의 입력은 헤더뿐입니다(`ReflectionCodeGen.cmake:41-58`, 파서 `--input`). 사용 자리는 `.cpp` 에 있습니다.

- `.cpp` 를 생성기 입력에 넣으면 `.cpp` 를 고칠 때마다 그 대상의 생성기가 다시 돕니다(depfile 의존).
- 사용 자리가 다른 대상에 있으면 방향이 거꾸로 됩니다. 게임 `.cpp` 가 키트 타입을 B 로 쓰면 키트의 `.gen.cpp` 가 게임 소스에 의존하게 되어 계층 순서를 깹니다.
- 별도 등록 목록 파일은 사용자가 쓰는 선언이 다른 곳으로 옮겨 간 것일 뿐이고, 타입 목록의 원본이 둘이 됩니다.

**③ 을 기각한 이유.** 지금 필드 속성은 `Replicated`, `RepNotify`, `SaveGame`, `Transient` 가 있습니다(`Source/Core/Predefined/AnnotationMeta.txt`).

- `Replicated` 는 선언만 있고 소비자가 없습니다. `collectReplicatedProperties` 를 부르는 곳이 정의 파일(`PropertyRoleUtil.cpp`) 밖에 없습니다(grep).
- 필드 속성은 "어느 필드를 보내는가"를 말하고 "어느 타입이 B 인가"를 말하지 않습니다. 롤백은 모든 필드가 필요하므로 필드 속성으로는 정할 수 없습니다.
- 나중에 변경분 복제를 만들면 `Replicated` 를 B 필드의 부분 집합으로 쓰는 것은 자연스럽습니다. 이 계획에서는 하지 않습니다.

### 3.2 생성 코드의 위치와 호출 경로

- 코덱 함수는 이미 있는 `sw::generated::Registrar<T>` 특수화의 정적 멤버로 `.gen.cpp` 안에 만듭니다. `REFLECT_BODY()` 가 이 등록기를 friend 로 선언하므로(`ReflectionMacros.h:57-79`) private 필드에도 접근합니다.
  `REFLECT_BODY()` 가 없는 타입은 `offsetof` 를 쓰는 지금의 등록 코드가 이미 공개 필드만 다루므로 같은 조건으로 접근합니다.
- 등록기 `RegisterType` 가 `info._pFixedCodec` 에 정적 코덱 테이블(쓰기, 읽기, 비교, 스키마 해시 함수 포인터) 주소를 넣습니다. 함수 템플릿 특수화를 다른 TU 에서 부르는 구조(진단 없는 위반)나 DLL 데이터 export 가 필요 없습니다.
- 진입점은 타입 정보를 `typeFqn<T>()`(`TypeRegistry.h:74-79`, 컴파일러 시그니처에서 얻은 FQN)와 함수 지역 `TypeLookupCache` 로 한 번 찾고 캐시합니다. 이후 호출은 원자 로드 하나와 간접 호출 하나입니다.
- 중첩 타입이 같은 헤더에 있고 필드가 공개이면 생성기가 부모 코덱 안에 그 필드 코드를 **펼쳐** 씁니다. 다른 헤더나 private 필드이면 중첩 타입의 코덱 테이블을 FQN 캐시로 찾아 간접 호출합니다.

### 3.3 쓰는 쪽 모양

사용자는 평범한 호출만 하고 B 인지 모릅니다.

```cpp
// 롤백: 링 버퍼가 상태 타입을 받는다. 상태 구조체는 REFLECT + PROPERTY 만 가진다.
_rollback.saveFrame( frame, _matchState );                 // 안에서 FixedLayout::write
if ( _rollback.loadFrame( frame, _matchState ) == false )  // 스키마 불일치 · 잘림이면 false 와 이유 로그
    return false;

// 게임 넷: 스냅샷 타입을 채널에 등록하면 그 타입의 스키마 해시가 핸드셰이크 해시에 들어간다.
_netChannel.registerMessage<MechArenaSnapshot>( kSnapshotMessage );
_netChannel.send( kSnapshotMessage, snapshot );

// 온라인: 공유 프로토콜 코드가 메서드와 요청 · 응답 타입을 한 번 등록한다(서버 · 클라이언트가 같은 함수를 부른다).
ChatProtocol::describe( registry );                        // registry.method<ChatSendRequest, ChatSendReply>( ChatMethod::kSend )
_client.call( ChatMethod::kSend, request, onReply );
```

`FixedLayout::write` · `read` 는 저수준 진입점입니다. 게임 · 키트 코드는 위의 등록 API 를 쓰고 직접 부르지 않습니다.
등록 API 가 B 진입점이라 코덱 확인(3.1)이 등록 시점에 일어납니다. 진입점이 등록 API 몇 개로 모이면 확인하는 위치도 그만큼으로 줄어듭니다.

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
`unordered_*` 를 빼는 이유는 순회 순서가 결정적이지 않아 같은 값이 다른 바이트가 되기 때문입니다. 롤백 해시 비교가 깨집니다.
핸들(`ComponentHandle` 등)이 필요해지면 그 형식에 B 코덱을 하나 등록하는 일을 별도 단계로 둡니다. 지금 후보에는 핸들 필드가 없습니다.

**기존 속성의 확장.** B 전용 메타데이터는 새 선언 형식 없이 기존 `PROPERTY()` 키로 표현합니다.

| 키 | 지금 | 확장 |
|---|---|---|
| `Min` · `Max` | 실수 리터럴, 허용 범위(`float.MinRange`) | 정수 비트 폭과 양자화 범위로도 씀. 필드 이름과 쉼표 목록도 받음 |
| `Step` | 없음(새 키) | 양자화 간격. 값의 의미 있는 최소 단위 |
| `MaxCount` | 없음(새 키, `count` 종류) | 문자열 · 컨테이너 최대 길이. 1 이상의 정수로 읽음 |

- `Min` · `Max` 를 재사용하는 근거는 이 키가 이미 "허용 범위"라는 점입니다. 인스펙터 슬라이더 범위는 `UIMin` · `UIMax` 로 따로 있습니다(`AnnotationMeta.txt` 의 `float.UIMinRange` 주석).
  허용 범위 밖의 값은 이미 잘못된 값이므로 와이어 범위로 써도 뜻이 바뀌지 않습니다.
- `Min` · `Max` 는 지금 `float32` 로 보관합니다. 정수 경계는 `float32` 로 정확히 나타나는 값이어야 하고, 아니면 생성 오류입니다(2e9 는 정확하고 16777217 은 아닙니다).
  숫자가 아닌 필드(bool, 열거형, 문자열, 구조체)의 `Min` · `Max` 도 생성 오류입니다. 정수 필드는 실수 필드 이름을 범위로 쓸 수 없습니다(소수부가 잘립니다).
- 범위 밖 정수는 **쓰기가 실패**합니다. `writeInt` 처럼 자르지 않습니다(`BitStream.cpp:70-74`). 실수 양자화는 `Step` 를 적는 순간 손실 인코딩이므로 경계로 자릅니다. `Step` 이 붙은 필드만 손실이 있습니다.
- 쓰기 실패는 결함이므로 `FixedLayout::write` 는 `[[nodiscard]] bool` 을 반환하고, 등록 API 는 단언하거나 오류로 멈춥니다.

### 3.5 가변 길이

- 문자열과 `vector` 는 `MaxCount` 가 있어야 적격입니다. 넷에서 받은 길이로 메모리를 잡기 전에 상한이 있어야 하기 때문입니다.
- 개수는 `[0, MaxCount]` 범위 정수로 씁니다. 쓸 때 개수가 `MaxCount` 를 넘으면 쓰기가 실패합니다. Mech 스냅샷처럼 255 개로 자르지 않습니다.
- 읽을 때 개수가 `MaxCount` 를 넘으면 `FixedReadResult::OutOfRange`, 잘린 스트림은 `Truncated` 입니다(`BitReader::hasOverflowed`, `BitStream.h:136`).
- 상한은 정적 값입니다. 런타임 설정이 상한이면, 데이터가 정하는 최대값보다 크거나 같은 정적 상한을 적고 데이터 쪽 검증을 따로 둡니다.

### 3.6 중첩 형식과 필드 순서

- 필드는 `PROPERTY()` 가 붙은 멤버를 **선언 순서대로** 씁니다. 기반 클래스의 필드가 먼저입니다. 크기 접두는 없습니다. 같은 빌드가 읽으므로 끝을 알 필요가 없습니다.
- 다중 상속 타입은 적격이 아닙니다(7.4 와 같은 이유).
- 다른 헤더 · 다른 대상의 중첩 타입이 적격인지는 생성기가 libclang AST 에서 그 타입의 `PROPERTY` 를 읽어 판정합니다. AST 순회가 입력 헤더 밖의 선언도 보는지는 미확인이며 단계 1 의 첫 확인입니다(8.1 위험 2).

### 3.7 열거형

- 값만 씁니다. 이름 해시는 쓰지 않습니다. 열거자를 바꾸면 스키마 해시가 바뀌므로 이름이 필요 없습니다.
- `ENUM( Count = … )` 가 있으면 `[최소 열거자, Count - 1]` 범위 비트 수로 씁니다. 없으면 기반 타입 전체 폭입니다.
- 읽을 때 등록된 열거자 값이 아니면 `OutOfRange` 입니다. 생성기가 열거자 값 목록을 `switch` 로 만들어 실행 시간 조회가 없습니다. `FightingMatch.cpp:1174-1175` 의 `bBadEnum` 손 검사가 여기에 해당합니다.
- `ENUM( Flags )` 는 기반 타입 폭의 비트로 쓰고, 등록된 비트 밖의 비트가 켜져 있으면 쓰기와 읽기가 모두 실패합니다.

### 3.8 런타임 범위 양자화

`MechArenaSnapshot` 은 위치 범위를 같은 스냅샷의 `_arenaHalfSize` · `_ceiling` 로 정하고 그 값도 바이트에 싣습니다(`MechArenaSnapshot.h`).
`Min` · `Max` 가 **필드 이름**을 받게 해서 표현합니다. 벡터 타입은 원소마다 쉼표로 나눠 적습니다.

```cpp
struct MechPilotSnapshot
{
    PROPERTY( Step = 0.01, Min = "-_arenaHalfSize, 0, -_arenaHalfSize", Max = "_arenaHalfSize, _ceiling, _arenaHalfSize" )
    float3 _position{};
    // ...
};

struct MechArenaSnapshot
{
    PROPERTY( Min = 1, Max = 100000 ) float32 _arenaHalfSize{ 200.0f };  // 범위 필드를 앞에 둔다
    PROPERTY( Min = 1, Max = 100000 ) float32 _ceiling{ 60.0f };
    PROPERTY( MaxCount = 255 ) vector<MechPilotSnapshot> _listPilot{};
    // ...
};
```

- 이름은 선언 위치에서 바깥으로 찾습니다. 자기 타입의 앞선 필드를 먼저 보고, 없으면 그 타입을 필드로 가진 바깥 타입의 앞선 필드를 봅니다.
  읽기는 선언 순서이므로 범위 필드가 **앞에** 있어야 읽는 쪽이 범위를 압니다. 뒤에 있으면 생성 오류입니다. `MechArenaSnapshot` 은 단계 4-a 에서 `_arenaHalfSize` · `_ceiling` 을 목록 앞으로 옮겼습니다.
- 바깥 타입으로 이름을 찾는 경우는 생성기가 중첩 타입을 부모 코덱 안에 펼칠 수 있을 때(3.2, 같은 헤더 · 공개 필드)만 됩니다. 펼칠 수 없거나 같은 중첩 타입을 이름이 다른 두 부모가 쓰면 생성 오류입니다.
- 범위 필드는 자기 `Min` 으로 하한을 강제합니다. Mech 의 `>= 1.0f` 검사가 그것이고, 자르지 않고 읽기 실패입니다. 옛 손 코덱이 쓰던 `max( 1.0f, … )` 는 쓰기 실패로 바뀌었습니다.
- 에디터는 지금 리터럴 `Min` · `Max` 만 압니다. 필드 이름 형태의 범위를 인스펙터가 쓰는 일은 별도 단계(9절 4-b)이고, 그 전까지 그 필드는 인스펙터에서 범위를 강제하지 않습니다. 에디터 메타데이터 생성은 이름 형태를 만나면 범위를 쓰지 않습니다.

**한계.** 키 값은 리터럴이나 필드 이름만 받고 C++ 상수(`kMaxCount`)는 받지 않습니다. 파서가 상수 식을 계산하지 않기 때문입니다. 같은 값이 여러 필드에 반복될 수 있습니다(8.1 위험 4).
데이터(카탈로그 크기 등)로 정해지는 범위는 키로 표현하지 않습니다. `FightingMatch` 의 `_moveIndex < moveCount`(`:1176-1177`)가 그 예이며 3.10 의 검증 훅에 둡니다.

### 3.9 엔디언과 정렬

- `BitWriter` 는 낮은 비트부터 채우고 정수는 시프트로 씁니다(`BitStream.h:1-6` 머리 주석). 실수는 `memcpy` 로 `uint32` 비트를 얻은 뒤 같은 경로로 씁니다(`BitStream.cpp:76-80`). 그래서 호스트 엔디언과 무관합니다.
- 비트 단위이므로 정렬이 없고 구조체의 패딩 바이트를 쓰지 않습니다. `writeBytes` · `writeBlob` 의 원본 바이트는 B 에서 쓰지 않습니다.
- `GimmickCircuit` 의 `memcpy` 방식은 호스트 엔디언에 의존했지만 같은 프로세스 안이라 문제가 없었습니다. B 로 옮기면 그 의존도 사라집니다.

### 3.10 스트림 구성과 읽기 의미

```
[스키마 해시 64 비트] [최상위 타입의 필드들, 선언 순서, 태그 · 크기 없음]
```

- 해시는 최상위 한 번만 씁니다. 중첩 타입과 컨테이너 원소는 해시 없이 바로 필드입니다.
- 넷 채널과 온라인 연결은 핸드셰이크가 해시를 이미 확인했으므로 메시지마다 해시를 싣지 않습니다(5절). 본문만 쓰는 경로는 확인을 마친 채널 · 연결 객체 안에만 있고 공개 API 로 내보내지 않습니다.
- **원자성**: 읽기는 대상 객체를 복사한 임시 객체에 읽고, 끝까지 성공했을 때만 옮깁니다. 실패하면 대상은 그대로입니다.
  복사본에서 시작하는 이유는 B 에 없는 필드(`FighterRuntime::_pDef` 같은 정의 포인터)를 보존하기 위해서입니다. `FightingMatch::loadState` 의 `arrLoaded` 복사(`:1147`)를 생성 코드가 대신합니다.
- **결과 형식**: `FixedReadResult` 는 `[[nodiscard]]` 열거형입니다. 값은 `Ok`, `SchemaMismatch`, `Truncated`, `OutOfRange`, `ValidateFailed`, `TrailingData` 입니다. 호출부가 이유를 로그에 남길 수 있습니다.
- **코덱이 없는 타입**: `write` · `read` · `equals` · `getSchemaHash` 에 넘기면 이유를 로그에 남기고 프로세스를 멈춥니다(모든 구성). 0 해시 같은 대체 값을 돌려주지 않습니다. 등록 API 는 기동 때 `findCodec` 으로 이유를 미리 보입니다.
- **검증 훅**: 타입에 기존 `REFLECT( Validate = 함수 )`(`CodeGenerator.cpp:572-576`)가 있으면 읽기 끝에 부르고, 오류가 있으면 `ValidateFailed` 입니다. 새 훅을 만들지 않습니다.
  데이터 범위 검사(`_moveIndex` 등)와 "같은 캐릭터인가" 대조 값(`FightingMatch.cpp:1100`)이 이 훅으로 갑니다.
- 남은 비트가 있으면 실패입니다. 바이트 경계까지의 패딩 비트만 허용합니다.

### 3.11 스키마 해시

생성기가 적격 타입마다 **정규 문자열**을 만들고 FNV-1a 64 로 해시합니다. 정규 문자열에 들어가는 것은 아래와 같습니다.

1. B 인코딩 규칙 버전 상수 `FixedLayout::kVersion`. 인코더 규칙(varint 형태, 양자화 반올림)을 바꾸면 이 값 하나만 높여 모든 해시를 바꿉니다.
2. 타입의 FQN.
3. 필드마다 선언 순서로 이름, `TypeNameMap::normalize` 로 정규화한 타입 이름, B 에 영향을 주는 키(`Min`, `Max`, `Step`, `MaxCount`)와 값, 비트필드 여부.
4. 필드 타입이 열거형이면 FQN, 기반 타입, 모든 열거자의 `이름=값`.
5. 필드 타입이 다른 `REFLECT` 타입이면 그 타입의 해시. 다른 헤더의 타입일 수 있으므로 실행 시간에 그 타입의 코덱 테이블에서 얻어 섞고, 함수 지역 `static` 으로 한 번만 계산합니다.

필드 이름을 넣는 이유는 같은 타입 필드 둘의 순서를 바꾸는 변경(`_x` 와 `_z`)을 잡기 위해서입니다. 이름만 바꿔도 해시가 바뀌지만, 같은 빌드끼리만 읽으므로 비용이 없습니다.
멤버 초기값, `DisplayName` · `Tooltip` 같은 표시 키는 넣지 않습니다. 바이트 배치와 무관하기 때문입니다.

생성기는 대상마다 정규 문자열 목록을 `FixedSchemas.gen.txt` 로 생성 폴더에 남깁니다. 해시가 왜 바뀌었는지 사람이 비교해 볼 수 있게 하는 진단용 파일이며, 빌드는 이 파일을 읽지 않습니다.

### 3.12 생성 비교

- 비교 함수는 B 와 같은 필드 집합을 선언 순서로 비교합니다. 실수는 **비트 패턴**으로 비교합니다. 그래서 `NaN == NaN` 은 참, `-0.0f == +0.0f` 는 거짓입니다.
  "같은 바이트를 만드는가"와 뜻이 같아야 롤백 불일치 검사에 쓸 수 있기 때문입니다.
- 선언을 더하지 않으므로 `operator==` 를 생성할 수는 없습니다. 비교는 `FixedLayout::equals( a, b )` 로 부릅니다. 손 `operator==` 가 필드를 하나씩 비교하는 타입이면 본문을 이 호출 한 줄로 바꿀 수 있습니다.
- 손 `operator==` 75 개 가운데 리플렉션 타입은 거의 없어서(1.2) 대신할 수 있는 범위는 B 로 옮기는 타입뿐입니다. 비교를 대신하려고 타입에 `REFLECT` 를 붙이지는 않습니다.

### 3.13 열거형 이름 조회

B 는 열거자 이름을 쓰지 않습니다. 로그와 진단용 `getEnumName` 은 FQN 해시로 `TypeRegistry` 를 찾고, 없으면 `"Unknown"` 을 반환합니다(`Source/Engine/Reflection/EnumName.h:16-25`). API 는 그대로 두고 내부만 바꿉니다.

- 생성기가 열거자 값이 `0..Count-1` 로 빈틈없는 열거형에 이름 정적 배열을 만들고 `EnumInfo` 에 그 포인터를 등록합니다. 조회는 배열 인덱스입니다.
- 템플릿 `getEnumName<E>` 는 `EnumInfo` 포인터를 함수 지역 캐시에 둡니다. 호출마다 해시 맵을 찾지 않습니다.
- 이름에서 값을 찾는 경로(`_mapNameToValue`)는 에디터와 데이터 파서만 씁니다. 그대로 둡니다.
- `"Unknown"` 반환은 로그용 표시라 폴백 금지 원칙의 대상이 아닙니다. 데이터 읽기는 이 함수를 쓰지 않습니다(`EnumName.h:6` 주석).

### 3.14 마이그레이션 확장 지점

B 의 기본 동작은 스키마 해시가 다르면 거절입니다. 다만 구조가 변환을 막지 않게 확장 지점을 남깁니다. **지금은 구현하지 않습니다.**

- 읽기가 `SchemaMismatch` 를 내기 직전에 변환 레지스트리를 봅니다. 키는 (기록된 해시, 대상 타입)이고 값은 "옛 바이트 → 새 객체" 변환 함수입니다. 레지스트리가 비어 있으면 거절합니다.
- 옛 바이트를 해석하려면 옛 정규 문자열이 있어야 합니다. `FixedSchemas.gen.txt`(3.11)를 배포본과 함께 보관하면 범용 디코더를 만들 수 있습니다. 이것도 필요해질 때 만듭니다.
- 지금 변환할 옛 B 데이터가 없으므로, 현 단계에서는 레지스트리의 조회 위치(읽기 함수 안 한 곳)만 비워 두고 변환 코드는 두지 않습니다.

## 4. 코드 생성 변경 범위

| 파일 | 변경 |
|---|---|
| `Tools/ReflectionParser/AstVisitor.cpp` | 필드 순서 보존 확인, 기반 · 중첩 타입의 필드 읽기 |
| `Tools/ReflectionParser/ParsedReflection.h` | 적격 판정 결과와 이유, 범위 식(필드 이름 형태) |
| `Tools/ReflectionParser/AnnotationFields.cpp` | `Min` · `Max` 가 필드 이름 · 쉼표 목록을 받게 |
| `PredefinedAnnotationField.xxx`, `Source/Core/Predefined/AnnotationMeta.txt` | `Step`, `MaxCount` 두 키 |
| `Tools/ReflectionParser/CodeGenerator.cpp` · `.h` | `emitFixedCodec`, 정규 문자열, 적격 판정, 펼치기 |
| `Tools/ReflectionParser/Templates/FixedCodec*.tpl` | 코덱 테이블 머리 · 꼬리 |
| `Tools/ReflectionParser/ParserDefines.h` | 템플릿 파일 · 키 상수(미확인: 상수 위치) |
| `Source/Engine/Reflection/ReflectionTypes.h` | `TypeInfo::_pFixedCodec` 포인터 하나, 테이블 전방 선언 |
| `Source/Engine/Serialization/Fixed/FixedLayout.h` · `.cpp` | `write` · `read` · `equals` · `FixedReadResult` · 변환 레지스트리 조회 위치 |
| `Source/Engine/Reflection/EnumName.cpp` · 생성기 열거형 등록 | 3.13 |

`ReflectionMacros.h` 는 바뀌지 않습니다. 사용자가 쓰는 매크로가 늘지 않습니다.

- 함수 본문은 템플릿보다 `CodeEmit` 로 쓰는 편이 맞습니다. 필드마다 분기가 많아서입니다. 머리와 꼬리만 템플릿으로 두어 `TypeRegistrarBegin.tpl` · `End.tpl` 과 맞춥니다.
- 리플렉션 타입의 인코딩은 Serialization 이 가진다는 기존 원칙(`SerializeReflectionRPC.cpp` 머리 주석)을 따라 런타임은 `Engine/Serialization/Fixed` 에 둡니다.
- 생성 오류(범위 필드가 뒤에 있음, `float32` 로 정확하지 않은 정수 경계, 이름을 찾지 못한 범위, 숫자가 아닌 필드의 `Min` · `Max`)는 헤더 경로, 줄, 필드 이름을 담고 생성기를 실패로 끝냅니다. 적격이 아닌 것은 오류가 아닙니다(3.1).

**유니티 빌드 ODR.** `.gen.cpp` 는 유니티 배치에서 빠집니다(`ReflectionCodeGen.cmake:183`). 코덱 함수는 `Registrar<T>` 특수화의 멤버이고 정의가 하나뿐입니다.
`.gen.cpp` 안 도우미는 이름 없는 네임스페이스에 두고 이름에 타입 식별자(`sanitizeIdentifier`)를 붙입니다. 나중에 유니티 제외가 풀려도 충돌하지 않습니다.

빌드 시간 · 크기 영향은 3.1 의 "① 의 비용"에 있습니다.

## 5. 호환 정책

**A 의 스키마 진화는 그대로 둡니다.** 세이브와 에셋은 지금처럼 태그, 기록 타입 변환(`tryCoerceBinaryPayload`), 버전 머리와 `SchemaMigrateFn`(`BinarySerializer.h` 3절 `deserializeVersioned`)으로 옛 데이터를 읽습니다.
B 를 더한다고 A 의 이 경로를 줄이거나 B 와 공유하게 바꾸지 않습니다. 마이그레이션이 언제나 가능해야 한다는 것은 구조 요구이고, A 가 그 요구를 맡습니다.

- **롤백 · 결정성 스냅샷 · 프로세스 안 복제**: 같은 프로세스, 같은 빌드입니다. 해시 불일치는 결함이며 `SW_ASSERT` 와 오류 로그로 멈춥니다.
- **`FightingMatch`**: 롤백은 B, 세이브 섹션은 A 로 나눕니다. 세이브 섹션은 버전 머리와 등록된 변환(`SchemaMigrateFn`)을 쓰는 A 경로이므로 앞으로 필드가 바뀌어도 마이그레이션 경로가 열려 있습니다.
  지금 세이브 섹션은 롤백 바이트이고 변환할 옛 세이브가 없으므로, 현 단계에서는 옛 롤백 바이트 섹션을 읽는 변환 코드를 두지 않습니다.
- **게임 넷 연결**: 지금도 정확히 같은 버전만 받습니다(`NetTypes.h:35-57`). 채널에 등록된 메시지 타입의 스키마 해시를 모두 `NetWireVersion::combine` 에 섞습니다.
  손 버전을 높이지 않아도 바이트 배치가 바뀌면 핸드셰이크가 `VersionMismatch` 로 거절합니다. 손 버전은 의미만 바뀌고 배치는 그대로인 경우에만 높입니다.
- **온라인 서비스(사용자 결정: B 로 옮김)**: 서버와 클라이언트는 같은 빌드로 배포합니다. 지금도 키트마다 `getProtocolVersion()` 을 정확히 비교합니다(`OnlineServiceHost.cpp:408-425`).
  - 접속 핸드셰이크의 **첫 필드**를 "온라인 스키마 해시"로 바꿉니다. 클라이언트와 서버가 같은 공유 등록 함수(`*Protocol::describe`)로 만든 메서드 · 타입 목록의 해시를 모두 섞은 값입니다. 서버는 다른 어떤 바이트보다 먼저 이것을 비교합니다.
  - 다르면 연결을 `kVersionMismatch` 로 거절하고 응답에 서버 해시를 실어 보냅니다. 클라이언트는 이 오류를 "업데이트 필요" 화면으로 보이고 다시 접속하지 않습니다. 메시지를 하나라도 B 로 해석하기 전에 멈추므로 조용히 잘못 읽는 경로가 없습니다.
    클라이언트가 이 오류를 어느 UI 로 보이는지는 읽지 않았습니다(미확인, 단계 5-a 에서 확인).
  - 이 첫 필드와 거절 응답은 B 가 아니라 고정된 작은 형식(해시 64 비트 + 오류 코드)입니다. B 스키마가 바뀌어도 이 비교 자체는 항상 읽힐 수 있어야 하기 때문입니다.
  - 나중에 옛 클라이언트를 받아야 하면 로그인 · 버전 확인 같은 소수 메시지만 A 로 남기는 탈출구가 있습니다. 지금은 열지 않습니다(8.3).
- 타입 ID(`TypeInfo::_typeID`)는 실행 순서에 따라 정해지는 로컬 인덱스이므로(`ReflectionCore.cpp:657`) 와이어에 싣지 않습니다. B 의 해시는 이름과 구조에서만 나오므로 빌드가 같으면 프로세스가 달라도 같습니다.

## 6. 옮기는 순서와 검증

순서는 바이트가 프로세스 안에만 머무는 것부터입니다. 롤백과 회로 상태가 먼저이고, 와이어를 바꾸는 스냅샷과 프로토콜이 나중입니다. 단계 번호는 9절과 같습니다.

1. 생성기와 런타임(단계 1). 손 코덱은 건드리지 않습니다.
2. `GimmickCircuit` 상태(단계 2). 손 크기 계산을 없애는 효과가 가장 분명합니다. `_layoutHash` 대조는 검증 훅으로 둡니다.
3. `FightingMatch` 롤백 B, 세이브 A(단계 3).
4. Mech · Horror 스냅샷과 게임 넷 채널 등록(단계 4). 핸드셰이크에 해시를 섞습니다.
5. 온라인: 핸드셰이크 첫 필드 해시와 "업데이트 필요" 거절을 먼저(단계 5-a), 그다음 프로토콜 12 파일(단계 5-b).
6. 컴팩트 모드 삭제, `cloneObject` 를 B 로(단계 6).

단계마다 아래 테스트를 둡니다. 성능 수치는 요구하지 않습니다.

- **왕복**: 값을 채운 객체를 쓰고 읽어 `FixedLayout::equals` 로 비교합니다. 경계값(범위 최소 · 최대, 빈 컨테이너, `MaxCount` 개)을 포함합니다.
- **스키마 변경 거부**: 테스트 전용 헤더에 필드 순서만 다른 두 타입을 두고 정규 문자열 해시를 비교합니다. 해시가 달라야 하고, 한쪽이 쓴 바이트를 다른 쪽이 읽으면 `SchemaMismatch` 여야 합니다.
  같은 방법으로 필드 이름 변경, 열거자 추가, `Step` · `Min` 변경, `FixedLayout::kVersion` 변경을 각각 확인합니다.
- **적격 판정**: 포인터 필드, `MaxCount` 없는 `vector` 를 가진 타입에 코덱이 없고, 그 타입을 등록 API 에 넘기면 이유를 담은 오류로 멈추는지 확인합니다.
- **잘림 · 범위**: 바이트를 한 개씩 줄인 모든 접두에 대해 `Truncated` 가 나오는지, 범위 밖 개수와 열거자가 `OutOfRange` 인지 확인합니다. 실패 뒤 대상 객체가 그대로인지도 확인합니다.
- **쓰기 실패**: 범위 밖 정수, `MaxCount` 초과에서 `FixedLayout::write` 가 `false` 인지 확인합니다.
- **이전 · 이후 동작 비교**: 손 코덱을 지우기 전에, 같은 시뮬레이션을 손 코덱과 B 로 각각 저장 · 복원해 이후 프레임의 상태 해시가 같은지 비교합니다.
  롤백은 기존 롤백 테스트(되감아 다시 실행한 결과가 같다)를 그대로 통과해야 합니다. 해시 머리와 인코딩이 바뀌므로 바이트가 아니라 동작을 비교합니다.
- **넷 · 온라인 거절**: 메시지 타입 하나의 필드를 바꾼 빌드가 핸드셰이크에서 거절되는지, 온라인은 그 거절이 다른 메시지보다 먼저 오고 클라이언트가 "업데이트 필요"로 끝나는지 확인합니다.

**손 코드와 같은 수준이라는 구조상 근거.** 생성 코드는 손 코덱과 같은 `BitWriter` 호출을 같은 순서로 부르는 직선 코드입니다.
프로퍼티 목록 순회, 이름 해시 비교, 기록 타입 변환이 없고, 필드 주소는 멤버 접근이라 컴파일러가 상수 오프셋으로 만듭니다.
손 코드보다 많은 일은 셋입니다. 최상위 해시 64 비트 한 번, 최상위 간접 호출 한 번(3.2), 읽기의 임시 복사 한 번입니다. 임시 복사는 `FightingMatch` 손 코드도 하고 있습니다(`:1147`).
펼칠 수 없는 중첩 타입(다른 헤더 · private 필드)은 원소마다 간접 호출이 하나 더 있습니다. 지금 후보(Mech · Horror · Fighting)는 중첩 타입이 같은 헤더에 있어 펼칠 수 있습니다.

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

`getComponent<` 413 건 가운데 틱 안에서 부르는 비율은 세지 않았습니다(미확인, 단계 0). `forEachComponentOfType<` 는 `HealthListenerComponent.cpp:15`, `FirstPersonCameraComponent.cpp:160` 처럼 게임플레이 이벤트 처리에서 부릅니다.
결론적으로 캐스트 자체는 이미 핫패스에 쓸 수 있는 구조입니다(잠금 · 해시 · 문자열 없음, O(1)). 남은 비용의 뿌리는 캐스트가 아니라 **타입 없는 핸들**과 **씬 전체 순회**입니다.

### 7.4 위험

- **다중 상속 · 인터페이스**: 디스플레이는 부모 하나(`_parentFQN`)의 사슬만 표현합니다. 리플렉션이 파생이라고 답했지만 C++ 상속 관계가 아니면 `reinterpret_cast` 로 내려갑니다(`ReflectionCast.h:167`).
  두 번째 기반을 거치는 경우 포인터 보정이 없어 잘못된 주소가 됩니다. 지금 그런 타입이 있는지는 미확인입니다(단계 0). 구조로 막으려면 생성기가 `REFLECT` 타입의 다중 상속을 생성 오류로 처리합니다(단계 7-b).
- **모듈 경계 · 핫 리로드**: 디스플레이와 `TypeLookupCache` 는 이미 묘비(`isAlive`)와 테이블 비우기로 처리합니다. 타입 목록(7.2)을 더하면 모듈 해제 때 그 모듈 타입의 목록을 비워야 합니다.
- **유니티 빌드**: 캐스트 경로는 헤더 인라인과 `.gen.cpp` 정의뿐이라 영향이 없습니다.
- **ID 결정성**: `_typeID` 는 등록 순서 카운터입니다(`ReflectionCore.cpp:657`). 네트워크 · 디스크에 싣지 않습니다. 타입을 와이어에 실어야 하면 FQN 해시를 씁니다.

## 8. 위험과 구현 중 확인할 것

### 8.1 위험과 완화

1. **적격이 아닌 타입을 B 로 쓰려는 경우가 빌드 시간이 아니라 기동 시간에 드러납니다.** 사용자가 선언하지 않으므로 사용 자리의 컴파일러는 코덱 유무를 모릅니다(3.1).
   완화: B 진입점을 등록 API 몇 개로 모아 기동 때 한 번 확인하고, 오류 메시지에 적격이 아닌 필드와 이유를 담습니다. 게임마다 기동하는 기존 자동화 시나리오가 이 확인을 지나갑니다.
2. **AST 범위**: 다른 헤더의 중첩 타입 필드를 생성기가 읽을 수 있는지 확인하지 않았습니다(3.6). 읽지 못하면 그런 중첩은 간접 호출로만 처리하고, 적격 판정은 그 타입의 코덱 테이블 유무를 실행 시간에 확인합니다.
3. **의도하지 않은 와이어 변경**: 선언이 없으므로 `REFLECT` 타입의 필드를 정리하거나 순서를 바꾸면 B 형식이 바뀝니다.
   롤백 · 스냅샷은 같은 빌드끼리라 영향이 없습니다. 넷 · 온라인은 해시가 달라져 옛 빌드와의 연결이 거절됩니다. 조용히 잘못 읽지는 않습니다.
   사용자가 알 수 있는 수단은 셋입니다. ① 거절 로그와 "업데이트 필요" 화면이 양쪽 해시를 보입니다. ② `FixedSchemas.gen.txt` 를 비교하면 무엇이 바뀌었는지 보입니다. ③ 필드 순서가 와이어에 영향을 준다는 사실을 `PROPERTY` 헤더 주석과 Reflection README 에 한 줄로 적습니다.
   온라인 배포 전 확인용으로 온라인 스키마 해시를 커밋된 기준 파일과 비교하는 테스트를 두는 안은 정확성 장치가 아니라 알림이므로 이 계획에 넣지 않습니다. 해시 거절이 이미 정확성을 보장합니다.
4. **리터럴 반복**: 3.8 의 키 값이 C++ 상수를 참조하지 못해 같은 값이 여러 필드에 반복됩니다. 값이 어긋나도 해시가 바뀌므로 잘못 읽지는 않지만, 의도와 다른 범위가 됩니다.
5. **`Min` · `Max` 의 뜻이 강해집니다.** 허용 범위가 와이어 범위가 되므로, 게임 코드가 허용 범위 밖 값을 잠깐 쓰던 곳은 B 쓰기 실패로 드러납니다. 결함이 드러나는 것이므로 의도한 결과입니다. 단계 2 ~ 4 에서 해당 필드의 범위를 실제 값으로 다시 정합니다.
6. **해시에 들어가지 않는 의미 변경**: 단위를 미터에서 센티미터로 바꾸는 변경은 해시를 바꾸지 않습니다. 이 경우를 위해 넷 손 버전 상수는 지우지 않습니다.
7. **`REFLECT` 를 처음 붙이는 타입**: 스냅샷 구조체에 `REFLECT` 를 붙이면 `TypeRegistry` 에 등록되고 인스펙터 · A 직렬화 대상이 됩니다. 의도한 결과이지만 등록 수가 늘어납니다.

### 8.2 백로그로 넘긴 것

- `SerializerUtil::deserializeValueBinary` 의 "모르는 타입이면 1 바이트 건너뛰고 성공"은 [engine.md](../backlog/engine.md) `A-13` 입니다. A 의 결함이며 이 계획과 독립입니다.
- `BitWriter::writeInt` 의 조용한 범위 자르기와 `BitReader::readInt` · `readQuantizedFloat` 의 읽기 자르기는 [network.md](../backlog/network.md) `A-14` 입니다. 손 코덱이 남아 있는 동안의 안전망입니다.

### 8.3 정한 것

사용자 결정은 모두 끝났습니다. 결정 기록에는 단계 1 을 병합할 때 함께 옮깁니다.

- 사용자는 B 를 선언하지 않습니다. 적격 `REFLECT` 타입 전부에 생성하고 사용 자리의 등록 API 가 확인합니다(3.1).
- 온라인 서비스 프로토콜도 B 로 옮기고 서버 · 클라이언트를 같은 빌드로 배포합니다. 핸드셰이크 첫 필드에서 스키마 해시를 비교하고 다르면 "업데이트 필요"로 거절합니다(5절).
  나중에 옛 클라이언트를 받아야 하면 로그인 · 버전 확인 같은 소수 메시지만 A 로 남기는 탈출구를 엽니다. 지금은 열지 않습니다.
- `FightingMatch` 는 롤백 B, 세이브 A 입니다. 지금 변환할 옛 세이브가 없으므로 현 단계에서 변환 코드를 두지 않고, A 의 마이그레이션 경로는 그대로 열려 있습니다(5절).
- B 의 스키마 불일치는 거절이 기본이고, 변환 레지스트리 조회 위치만 남겨 둡니다(3.14).
- 타입 핸들(7-d)과 매니저의 타입별 컴포넌트 목록(7-c)을 도입합니다.

### 8.4 구현 중 확인할 기술 질문

- 단계 1 에서 답한 것(2026-10-17): `_listProperty` 는 `clang_visitChildren` 순서, 곧 선언 순서입니다. 다른 헤더의 선언도 같은 번역 단위에 있어
  열거자 · `ENUM( Count, Flags )` · `REFLECT()` 표시를 읽습니다(`AstVisitor.cpp` `collectFixedEnum` · `isReflectedRecord`). 다만 다른 헤더 타입의 `PROPERTY` 를
  다시 모으지는 않고 실행 시간에 그 코덱을 찾아 의존을 확인합니다(`FixedLayout::findCodec`). 템플릿 · 키 상수는 `ParserDefines.h` 의 `templatekey` · `fixedlayout` 입니다.
  `sw::vector` 는 자체 클래스라 정규 이름이 `sw::vector<…>` 입니다. 명세와 다르게 정한 것은 [결정 기록](../09_Decisions.md) 5-4 에 있습니다.
- 클라이언트가 `kVersionMismatch` 를 어느 UI 로 보이는가(단계 5-a).
- 지금 `ComponentHandle` 필드가 A 에서 어떤 바이트로 직렬화되는가(단계 7-d).
- `GameObject::getTypeInfo` 에 타입 캐시가 있는가, `REFLECT` 타입 가운데 다중 상속이 있는가(단계 0).

## 9. 단계

| 단계 | 내용 | 건드릴 파일 | 겹침 주의 | 에이전트(모델) |
|---|---|---|---|---|
| 0 | 조사: `operator==` 분류, 틱 안 `getComponent`, 다중 상속 `REFLECT`, `GameObject` 타입 캐시, `ComponentHandle` 필드 목록 | 없음(읽기) | 없음 | `scout`(haiku) |
| 2 | `GimmickCircuit` 을 B 로 | `GimmickCircuit.*` | 없음 | `worker`(sonnet) |
| 3 | `FightingMatch` 롤백 B, 세이브 A | `FightingMatch.*` | 없음 | `worker`(opus) |
| 4-a | Mech · Horror 스냅샷을 B 로(끝남). 넷 채널 등록 · 해시 섞기는 백로그 `D7-7` | `MechArenaSnapshot.*`, `HorrorSnapshot.*` | 두 스냅샷 한 손 | `worker`(opus) |
| 4-b | 인스펙터가 필드 이름 형태 `Min` · `Max` 를 쓰게 | 인스펙터 범위 코드(미확인 위치) | 단계 1 뒤 | `worker`(sonnet) |
| 5-a | 온라인 핸드셰이크 첫 필드 해시, "업데이트 필요" 거절 | `OnlineServiceHost.*`, `OnlineServiceClient.*`, 클라이언트 오류 UI | 5-b 보다 먼저 | `integrator`(opus) |
| 5-b | 온라인 프로토콜 12 파일을 B 로, `*Protocol::describe` 등록 | `*Protocol.*` | 12 파일 한 손 | `worker`(opus) |
| 6 | 컴팩트 모드 삭제, `cloneObject` B | `BinarySerializer.*`, `Archive.*` | 다른 직렬화 작업과 겹침 | `worker`(sonnet) |
| 7-a | `GameObject` 타입 캐시, 열거형 이름 배열 | `GameObject.*`, `EnumName.cpp`, 생성기 열거형 등록 | 생성기는 단계 1 뒤 | `worker`(sonnet) |
| 7-b | `REFLECT` 다중 상속 생성 오류 | `AstVisitor.cpp` | 단계 1 과 같은 파일, 순서대로 | `worker`(sonnet) |
| 7-c | 매니저의 타입별 컴포넌트 목록 | `GameObjectManager.*`, `GameObjectStore.*` | 7-d 와 같은 파일, 7-c 먼저 | `integrator`(opus) |
| 7-d | 타입 핸들 `TComponentHandle<T>` | `ComponentHandle.h`, `GameObjectManager.*`, 호출부, 인스펙터 | 7-c 뒤 같은 손 | `integrator`(opus) |

단계 1(생성기 · `TypeInfo::_pFixedCodec` · `FixedLayout` 런타임 · 테스트)은 끝났습니다(2026-10-17). 런타임은 `Source/Engine/Serialization/Fixed/FixedLayout.h`,
생성기는 `Tools/ReflectionParser/FixedCodecGenerator.*`, 테스트는 `Test/ReflectionTest/TestFixedLayout.cpp` · `TestFixedLayoutTypes.h` 와
`ReflectionParserTest.FixedLayoutAnnotationMistakesStopTheBuild` 입니다. 단계 2 ~ 5 는 서로 파일이 겹치지 않아 도우미 하나와 나눠 진행할 수 있습니다.

단계 4-a 가운데 스냅샷을 B 로 옮기는 일은 끝났습니다(2026-10-17). Mech · Horror 스냅샷은 `PROPERTY` 범위 키만 갖고 손 코덱 · `kMagic` 이 없습니다.
두 스냅샷은 디스크에 남지 않습니다(세이브는 `Archive` 의 `writeState` 가 따로 씁니다). Horror 발전기의 진행 · 표시 두 목록은 원소 하나(`HorrorGeneratorSnapshot`)로 합쳤고, 위치는 높이까지 싣습니다.
메시지는 스트림 머리 해시를 그대로 실어, 배치가 다른 빌드의 바이트는 읽기가 `SchemaMismatch` 로 거절합니다. 테스트는 `MechArenaTest` · `AsymmetricHorrorTest` 의 `SnapshotRoundTripsAndRefusesOutOfRangeValues` 입니다.
채널 등록 API(`registerMessage<T>` 로 등록하면 해시가 핸드셰이크 버전에 섞이는 구조)와 3.10 의 본문만 쓰는 경로는 이 스냅샷을 싣는 게임이 아직 없어 백로그 `D7-7` 로 넘겼습니다.

**단계 7-c · 7-d 상세.**

- 7-c: 매니저가 타입별 컴포넌트 목록을 보관하고 컴포넌트 추가 · 제거 때 그 타입과 조상 타입 목록을 갱신합니다(7.2). `forEachComponentOfType` 의 API 는 그대로이고 내부만 바뀝니다.
  검증은 기존 `forEachComponentOfType` 호출 28 곳의 테스트와 함께, 추가 · 제거 · 모듈 해제(묘비) 뒤 목록이 맞는지 보는 테스트입니다.
- 7-d: `TComponentHandle<T>` 는 `ComponentHandle` 에 타입을 더한 형식입니다. `T*` 에서 만들면 타입이 확정되고, 타입 없는 핸들에서 만들 때만 `isDerivedFrom` 확인을 한 번 하며 실패하면 빈 핸들이 아니라 오류입니다.
  찾기는 `static_cast` 와 Debug 단언(`GameObjectManager.h:279-281` 과 같은 단언)입니다. 7.2 의 "찾고 바로 캐스트" 호출부를 바꿉니다.
  - `PROPERTY` 필드인 핸들은 A 직렬화와 인스펙터가 함께 바뀝니다. A 의 바이트 형태는 가능하면 지금 `ComponentHandle` 과 같게 두고, 읽을 때 대상 컴포넌트 타입이 `T` 가 아니면 읽기 오류입니다. 인스펙터의 컴포넌트 선택기는 `T` 로 거릅니다.
  - 바이트 형태를 바꿔야 하면 씬 데이터는 새로 저장합니다. 지금 변환할 옛 데이터가 없으므로 변환 코드는 두지 않고, A 의 마이그레이션 경로(5절)는 그대로 둡니다.
  - 검증은 바뀐 호출부의 기존 테스트, 씬 저장 · 로드 왕복, 잘못된 타입의 핸들을 담은 씬이 로드 오류로 멈추는 테스트, 네 백엔드 × 대표 셋입니다.
