# 5-4. 데이터, 직렬화, 설정

> 질문: 5-4. 데이터, 직렬화, 설정 영역에서 이미 정한 것은 무엇이고 근거는 무엇인가?
> 독자: 같은 제안을 다시 하려는 사람, 정한 이유를 찾는 사람.
> 결론: 결정 기록 5 절의 한 영역이다. 번호 체계와 쓰는 법은 [결정 기록](../09_Decisions.md) 5절에 있다.

- **엔진 데이터에는 별칭을 두지 않습니다**(2026-10-03). 실제로 출시한 게임 데이터가 없으므로, 이름을 바꾸면 데이터를 다시 씁니다. 옛 `SaveGame` 파일도 읽지 않습니다. 계약은 Serialization README 에 있습니다.
  - 이 결정은 지금 이관할 데이터가 없다는 사실에 기댄 것이지 영구 금지가 아닙니다(사용자 확인, 2026-10-17). 출시한 데이터가 생겼을 때 바로 쓰도록 별칭(`Alias` · `ValueAlias` · `Removed`)은 미리 갖춥니다(이관 등록은 아래 2026-10-23 결정으로 지웠습니다 — 이름 변경 리다이렉트 R5).
   - 기능은 갖추되 출시 데이터가 이관할 것이 생기기 전에는 사용하지 않습니다 — 제품 데이터 · 게임 코드에 별칭을 두지 않습니다(2026-10-18 사용자 결정).
- **첫 출시 전에는 이관 없이 데이터를 다시 쓰고, 판은 정확히 같을 때만 읽습니다**(2026-10-23 사용자 결정). 출시된 빌드가 없어 지금 코드와 데이터 전부가 현재 판입니다. 형식이 바뀌면 저장소의 데이터를 다시 쓰고, 옛 판 리더 · 이관 함수 · 판 범위 읽기를 두지 않습니다. 판이 다르면 읽지 않고 오류로 멈춥니다.
  - 지운 것: `SW_SCHEMA_MIGRATION` · `SchemaMigrationRegistry`(2026-10-17 에 둔 정적 등록 — 프로덕션 등록이 0 이었다), 세 형식의 `deserializeVersioned` · `Archive` 의 옛 TypeInfo 인자(`legacyTypeInfo`), `AssetFormatRegistry` 의 XML 판 이관 등록부(대신 `AssetXMLFormat::isCurrentXMLVersion` 이 정확 일치만 본다), 판 범위로 읽는 `StateArchiveUtil::readHeaderInRange`와 FarmVillage 상태의 1 판 리더, 외형 공유 코드의 "높은 판도 받는" 범위 검사. 근본 원인은 판 번호를 범위로 받는 API 라서, 정확 일치 형식만 남겼다.
  - 남긴 것은 호환 분기가 아니라 계약이거나 출시 뒤에도 필요한 것이다: `BinaryWireVersion`, orphan 정책(`SchemaOrphanPolicy` — 텍스트 Ignore · 바이너리 Reject), 네트워크 `combine` 버전, 서버 레코드 판 거절, SQL 마이그레이션 번호, 팩 형식 거절, 쿠킹 기록의 "옛 형식은 읽지 않고 다시 굽는다", 사용자 설정 `<Upgrade>` 체인. 호환 의무는 출시를 자르는 순간부터이고, 그때 판 번호를 기준값으로 굳힌다.
  - 같은 버전에서 지운 필드를 버려도 되는지는 이관이 아니라 orphan 정책이라 호출부가 컨텍스트로 정합니다(`setAllowUnknownProperties`, 오브젝트 상태만 켬).
- **설정 파일에는 기본값과 다른 값만 적습니다**(2026-10-06). 언리얼의 `Default*.ini` 와 같은 규칙이고, `ConfigFileSchemaTest` 가 기본값을 다시 적은 키를 막습니다.
- **설정 값의 출처는 하나씩입니다**(2026-10-06). 시작 씬은 팩의 `gamesettings.xml`, 창 제목은 게임 프리셋의 `GameConfig::_windowTitle`(언리얼 ProjectName), 고정 스텝은 `EngineConfig::_fixedDeltaTime` 입니다.
  물리는 고정 스텝 위에 서브스텝 수(`_subStepCount`)만 둡니다(유니티 `Time.fixedDeltaTime` 과 같음). 기본 RHI 도 한 곳에만 적습니다.
- **앱이 다시 쓰는 에디터 상태는 `Saved/Editor/` 에 둡니다**(2026-10-06). `Config/Editor/` 에는 사람이 쓰는 설정만 남깁니다.
- **텍스처와 모델 임포트 설정은 키 테이블입니다**(2026-10-06). 리플렉션 구조체로 만들지 않았고, `PROPERTY( Config )` 메타도 지웠습니다. 엔진 타입에 키트 설정 필드를 두지 않고, 키트 설정은 팩 데이터에 둡니다.
- **자체 JSON 로더를 리플렉션 구조체로 옮기는 기준**(2026-10-12). 모듈 매니페스트, `AnimationLODSettings`, `MemoryBudgetFile`, `FacialRig`, `LipSync` 는 옮겼습니다. 다음은 옮기지 않습니다.
  `TextureImportConfig` · `ModelImportConfig` 는 적지 않은 값이 부모 프리셋을 따르는 "적지 않음" 의미가 필요한데 리플렉션은 "없음" 과 "0" 을 가르지 못합니다.
  `CrashReportService` 는 동적 JSON 문서이고, JWT · Chrome 트레이스 · VRM 은 외부 형식입니다.
  `CultureInfo` 는 적지 않은 필드가 부모 문화권을 따르고(`parent`), `LocalizationDocuments` 는 키가 데이터인 문자열 표이며, `AnimationGraphAsset` 은 노드 종류마다 필드가 다른 그래프라
  고정 구조체 하나로 읽을 수 없습니다(2026-10-14 R4 판정).
- **자체 포맷 에셋 로더 넷도 리플렉션으로 읽고 씁니다**(2026-10-17, C-19 R4). `SpriteClipAsset` · `SequenceAsset` · `DialogueGraphAsset` · `Skeleton` 은 키가 PROPERTY 이름이고,
  저장소 데이터는 새 키로 다시 썼습니다(옛 키 별칭 없음, 임포트 결과 스켈레톤은 `--import-models` 로 다시 만듦). "적지 않음" 을 초기값으로 읽으면 안 되는 칸(스켈레톤 전부,
  대화 노드 · 링크 id 와 핀)은 `PROPERTY( Required )` 로 두어 빠지면 오류입니다 — `ConfigManager::readConfigJSON` 이 구조체 칸과 구조체 목록 원소 안까지 봅니다.
  종류(시퀀스 항목 · 트랙, 대화 노드)는 정수 대신 열거자 이름이고, 모르는 종류를 읽어 두고 적용만 않던 규칙과 모르는 노드 종류를 Dialogue 로 읽던 폴백은 없앴습니다.
  범위 밖 프레임 번호도 자르지 않고 오류입니다. 스프라이트 구간이 프레임 밖이면 자르고 알리는 것만 남겼습니다(에디터가 프레임을 지운 뒤 저장할 수 있다).
- **`sw::vector<bool>` 은 쓰지 못하고, 불 목록은 `DynamicBitset` 입니다**(2026-10-17, A-18). 기본 구성의 `sw::vector<bool>` 은 원소마다 바이트였고 `SW_ENABLE_STL_CONTAINER` 구성에서는
  비트로 묶는 `std::vector<bool>` 이라, 같은 이름이 구성마다 다른 의미였습니다(STL 구성에서 리플렉션 래퍼가 원소 주소를 잡지 못해 컴파일이 멈춤). `VectorElementCheck` 가 두 구성에서
  같은 `static_assert` 로 막습니다. 언리얼 `TArray<bool>` 은 바이트 배열이고 비트 묶음은 `TBitArray` 로 따로 둡니다(기억에 의존). 리플렉션은 `DynamicBitset` 을 값 하나로 다룹니다.
  자기 기술형은 '0' · '1' 글(텍스트 처리기), 고정 배치형은 `MaxCount` 가 필요한 `bits`(개수 + 비트마다 1 비트)이고, 원소 주소를 쓰는 컨테이너 래퍼를 거치지 않습니다.
- **`sw::unordered_map` · `sw::unordered_set` 은 두 구성 모두 밀집 배열 구현입니다**(2026-10-18, A-20). STL 구성에서 `std::` 해시 컨테이너로 바꾸면 순회 순서(밀집 구현은 넣은 순서,
  `erase` 는 마지막 원소를 빈 자리로 옮김)가 구성마다 달라져, 순서에 기대는 코드가 기본 구성에서만 맞았습니다. 이제 STL 구성에서 바뀌는 것은 안쪽 `vector` 의 할당자뿐입니다(`deque` · `list` 와 같은 모양).
  `map` · `set` 은 그대로 나눕니다 — 두 구성 모두 키 순서이고 주소 · 반복자 보장은 기본 구성(정렬된 벡터)이 더 약해, 기본 구성에서 맞는 코드는 `std::map` 에서도 맞습니다.
  함수 객체의 EBO 는 구성과 상관없는 `CompressedPair` 가 맡습니다. 언리얼 `TMap` 도 밀집 배열(`TSet` 의 `TSparseArray`)이고 표준 컨테이너로 바꾸는 구성이 없습니다(기억에 의존).
- **`SW_ENABLE_STL_CONTAINER` 가 구성마다 바꾸는 것은 컨테이너 기본 할당자 하나입니다**(2026-10-24, EngineCleanups `StlContainerConfig`; 위 `map` · `set` 줄을 대신합니다).
  구성의 목적은 옵션 설명 그대로 "엔진 할당자 대신 `std::allocator`"(메모리 추적을 빼는 것)로 읽었습니다. `vector` · `array` · `pair` · `map` · `set` · `span` 은 두 구성 모두 자체 구현이고,
  `string` · `list` · `deque` 는 두 구성 모두 `std::*` 를 상속해 경합 탐지를 얹은 클래스입니다. 바뀌는 기본 할당자는 `ContainerAllocator<T>`(`Memory.h`)이며 컨테이너 헤더의 기본 템플릿 인자는 이것을 씁니다.
  `InlineAllocator` 도 두 구성에서 같은 SBO 구현입니다(힙 쪽만 `ContainerAllocator`). 이유: 같은 이름이 구성마다 다른 형식이면 기본 구성에서 맞는 코드가 STL 구성에서 달라질 수 있고(`vector<bool>` · 해시 순서),
  `map` · `set` 을 나눠 둔 근거(약한 쪽이 기본 구성이라 기본 구성에서 맞는 코드는 `std::map` 에서도 맞음)는 구현을 같게 하면 필요 없어집니다. `string` · `list` · `deque` 를 `std::*<T, 할당자>` 별칭으로 줄이지 않은 것은
  기본 구성의 경합 탐지(디버그)를 잃기 때문입니다. 표준 컨테이너와의 비교나 디버그 STL 검사가 목적이었다면 이 결정을 되돌립니다 — 사용자 확인을 기다립니다.
  STL 구성에서만 건너뛰는 테스트는 할당자에 기대는 것(`MemoryTagTest` 둘, `PrimitiveMeshIsTaggedMesh`)만 남기고 나머지(`TestVector` · `TestDataStructure` · `TestMemory`)는 두 구성이 같이 돕니다.
- **메모리 태그 통계의 해제는 센 블록만 뺍니다**(2026-10-18, A-24). `IAllocationTracker::recordAllocation` 이 `AllocationRecord{ 해시, _bCounted }` 를 돌려주고, 할당기는 센 블록의 헤더에
  기록기 번호(`getTrackerID`, 인스턴스마다 다름)를 적어 그 기록기에만 해제를 넘깁니다. 추적을 켜기 전에 잡은 블록을 켠 뒤에 풀면 태그 바이트가 음수 쪽으로, 켠 동안 잡은 블록을 끈 뒤에 풀면 양수 쪽으로 어긋났습니다.
  헤더 크기(48 B)는 그대로이고(쓰지 않던 칸을 씀) 해제마다 비교 하나가 늡니다. 언리얼 LLM 도 할당 때 추적한 포인터만 해제 때 뺍니다(기억에 의존).
- **누수는 "창 안에서 태어나 닫힐 때 살아 있는 블록" 으로 잽니다**(2026-10-24, [LeakDetection](../plans/LeakDetection.md)). 기준선 뒤의 살아 있는 양 차이로 재면 창 밖의 오래된 블록을 풀 때 새 누수가 상쇄됩니다
  (`Scene` 에서 오래된 100 B 를 풀고 새 50 B 를 흘리면 차이가 음수). `AllocationRecord` 에 `_windowStamp`(uint32)를 두고 블록 헤더의 패딩 6 B 를 `uint8[2] + uint32` 로 나눠 헤더는 48 B 로 둡니다.
  `Memory` 는 스탬프의 뜻을 모르고 해제 때 돌려주기만 하며(`recordFree` 가 `AllocationRecord` 를 받는다), 창은 `MemoryProfiler::beginLeakWindow` · `endLeakWindow` 가 맡습니다.
  기존 태그별 기준선 배열은 이 창의 한 인스턴스(`TagTotals`, 스택 없음)로 합쳤고, 짧은 창은 모든 할당의 스택을 잡는 `FullStacks` 입니다. 전수 캡처의 비용과 한 씬 로드의 할당 수는 측정으로 정합니다(계획 문서 L2).
  자기 기술형은 '0' · '1' 글(텍스트 처리기), 고정 배치형은 `MaxCount` 가 필요한 `bits`(개수 + 비트마다 1 비트)이고, 원소 주소를 쓰는 컨테이너 래퍼를 거치지 않습니다.
  설정 JSON · 데이터 XML · 세이브가 같은 `Min` · `Max` 를 다르게 지키던 것을 한 계약으로 합쳤고, 프리셋 표가 원소마다 따로 부르던 검사도 지웠습니다.
- **범위는 인스펙터와 읽기가 한 함수로 풀고, 소프트 읽기는 실패를 둘로 가릅니다**(2026-10-18, C-56 · C-60 · C-62).
  `PropertyMetadata::computeAllowedRange` 가 숫자 경계와 필드 경계(`Min = "-_halfSize"`)를 함께 풀어 인스펙터와 읽기 공통 끝이 같은 규칙을 지킵니다(필드 경계 읽기 함수는 그래서 Shipping 에도 있습니다).
  게임 데이터 XML(`GameDataXML::readFields`)의 자체 범위 검사는 지우고 공통 끝에 맡겼습니다. `deserializeSoft` 는 `bool` 대신 `SoftReadResult`(`NotParsed` · `Rejected` · `Read`)를 돌려줍니다.
  `bool` 하나일 때는 범위 밖 값에서 바로 돌아가 같은 파일의 모르는 키를 알리지 않았고, 범위 밖 값을 "JSON 이 아니다"로 알렸습니다. 이제 `NotParsed` 만 바로 돌아가고 `Rejected` 는 orphan 을 모두 알린 뒤 실패합니다.
  float32 로 보관하는 숫자 경계를 float64 칸과 견주는 어긋남(`Min = 0.1`)은 조사 시점에 그런 칸이 없어(float64 칸의 경계는 `MemoryBudgetMonitor::_megabytes` 의 `Min = 0.0` 하나) 형식을 바꾸지 않았습니다.
- **이름 읽기(`XMLReadUtil::readOptionalName`)는 글 `none` 을 오류로 알립니다**(2026-10-18, C-59). `hashed_string` 은 언리얼 `FName` 처럼 `none` 을 빈 이름과 같게 보고 빈 이름의 `c_str()` 은 "None" 이라,
  이름으로 받은 열거형 글(`align="none"`)이 "적지 않음"이 되고 적지 않은 경로가 "None" 이 됐습니다. 이름 형식을 바꾸면 리플렉션이 빈 이름을 "None" 으로 쓴 씬 · 프리팹(100 곳 넘음)과 열거자 `None` 이 함께 바뀌므로,
  이름 읽기의 한 창구가 그 글을 오류로 막고 그런 속성은 글로 읽습니다. 경로로 쓰는 이름은 비었는지 먼저 봅니다(`CharacterAppearanceComponent::applyBodyPart`).
- **열거자 이름의 정본은 리플렉션 `ENUM()` 입니다**(2026-10-15, 같은 해 10-12 의 "enum 이름 테이블 45 곳 유지" 를 뒤집음). 철자가 열거자 이름과 같은 손 이름 표 · `toString( Enum )` 은 지우고
  `getEnumName( value )`(`Engine/Reflection/EnumName.h`)로 읽습니다. 언리얼 `UENUM` + `UEnum::GetValueAsString`, Godot ClassDB 열거 상수와 같은 방향입니다(기억에 의존).
  손 표는 `static_assert` 가 개수만 지키고 순서 · 철자의 어긋남은 못 막았고, 쓰는 곳이 거의 로그였으므로 "배열 인덱스가 빠르다" 는 이득이 없습니다.
  남기는 것: 철자가 다른 와이어 · 데이터 · 지표 문자열(`AdminRole` 의 `"viewer"`, `HealthState` 의 소문자, 서비스 지표 라벨, `EnumNameTable` 로 읽는 데이터 이름)과 Core 의 열거형(Core 는 리플렉션 아래 티어)입니다.
  철자가 데이터에 박힌 이름 표를 리플렉션으로 옮기려면 데이터를 열거자 철자로 다시 씁니다(별칭 없음).
- **같은 빌드끼리만 읽는 바이트는 고정 배치형(B) 코덱이고, 생성기가 모든 적격 `REFLECT` 타입에 만듭니다**(2026-10-17, [고정 배치형 코덱 계약](../../Source/Engine/Serialization/Fixed/README.md) 단계 1).
  사용자는 B 를 선언하지 않고, 등록 API 가 `FixedLayout::findCodec` 으로 확인합니다. 세이브와 에셋은 자기 기술형(A) 그대로입니다. 언리얼 `NetSerialize` · Iris 디스크립터처럼
  선언(`UPROPERTY`)에서 와이어 형식을 도출하고, 손 코덱의 손 버전 상수 대신 정규 문자열 해시가 형식 변경을 잡습니다. 단계 1 에서 정한 세부:
  - 버전 상수 이름은 docs/agents/Constants.md "Constants" 를 따라 `FixedLayout::kVersion` 입니다(계획 초안의 `kFixedFormatVersion`).
  - 읽기 결과에 `TrailingData` 를 더했습니다. 남은 비트를 `OutOfRange` 로 부르면 원인이 섞이기 때문입니다.
  - `float64` 는 64 비트 원본으로 씁니다. `Step` 은 float32 에만 붙습니다.
  - 수학 타입은 `ParserDefines.h` `fixedlayout::kArrMathType` 목록으로 정합니다. 구조에서 추측하면 핸들처럼 모양만 같은 타입까지 적격이 됩니다.
  - `vector<string>` 처럼 가변 길이가 두 겹이면 `MaxCount` 하나로 길이를 정할 수 없으므로 적격이 아닙니다.
  - 다른 헤더의 중첩 타입과 기반 타입은 실행 시간에 코덱을 찾아 의존을 한 번 확인합니다(계획 8.1 위험 2).
  - 코덱이 없는 타입을 진입점에 넘기면 이유를 로그에 남기고 프로세스를 멈춥니다(모든 구성). 0 해시 같은 대체 값을 돌려주면 단계 4 의 핸드셰이크 해시에 그 값이 섞입니다.
  - 정수 경계는 float32 로 정확히 나타나야 합니다(`Min` · `Max` 를 float32 로 보관). 크기가 아니라 정확성이 기준이라 `OnlineServerConfig::_serverID` 의 `Max = 2000000000` 은 그대로 씁니다.
  - `MaxCount` 는 새 애노테이션 종류 `count` 로 1 이상의 정수만 받습니다. 실수로 읽어 반올림하지 않습니다.
  - 양자화 읽기는 칸 번호를 직접 읽어 칸 수를 넘으면 `OutOfRange` 입니다. 손 코덱이 쓰는 `BitReader::readQuantizedFloat` 도 같은 규칙으로 바꿨습니다(2026-10-17). 범위 밖 칸은 실패이고, `BitWriter::writeInt` · `writeQuantizedFloat` 는 범위 밖이면 쓰지 않고 `false` 입니다.

  실행 파일 크기(Debug, 단계 전후)는 `Engine.dll` +153,088 B(+0.7 %), `GameFramework.dll` +81,408 B(+1.0 %)입니다. 이때 `REFLECT` 타입 454 개 가운데 적격은 173 개,
  적격이 아닌 것은 281 개였습니다(대부분 `MaxCount` 없는 가변 길이와 표 밖 타입). 기존 `.gen.cpp` · `.gen.h` 1069 개는 덧붙은 줄만 있고, 바뀐 줄은 ABI 스탬프 하나였습니다.
  대상 타입을 사용 자리에서 모으는 길(`.cpp` 를 생성기 입력에 넣기, 등록 목록 파일)은 `.cpp` 수정마다 생성기가 다시 돌고 키트 `.gen.cpp` 가 게임 소스에 의존해 계층이 거꾸로 되며 타입 목록의 원본이 둘이 되어 기각했습니다.
  필드 속성(`Replicated` 등)에서 도출하는 길도 기각했습니다. 그 속성은 "어느 필드를 보내는가"만 말하고 롤백은 모든 필드가 필요합니다. `.gen.cpp` 도우미는 이름 없는 네임스페이스에 타입 식별자를 붙여 두므로 유니티 제외가 풀려도 충돌하지 않습니다.
- **이름 변경 리다이렉트는 선언 옆에 적고, 리다이렉트 표는 그 이름의 주인이 듭니다**(2026-10-17, 이름 변경 리다이렉트 R1~R4). 선언은 기존 키를 그대로 씁니다.
  타입 · 열거형의 옛 FQN 은 `REFLECT( Alias )` · `ENUM( Alias )`, 열거자의 옛 이름은 `ENUM( ValueAlias = "Old:New" )`, 지운 열거자의 대체는 새 키 `ENUM( Removed = "Old:Replacement" )`, 필드의 옛 이름은 `PROPERTY( Alias )` 입니다.
  유니티 `FormerlySerializedAs` 처럼 이력이 선언 옆에 있고, 언리얼 `CoreRedirects` 처럼 호출부는 모릅니다. 다만 표를 전역 하나로 모으지 않고 `EnumInfo::_mapRedirectNameToValue`, `PropertyInfo::_listAlias`,
  `TypeRegistry` 별칭 표에 둡니다. 언리얼이 전역 표인 것은 리다이렉트가 ini 에서 오기 때문이고, 여기서는 선언이 등록과 함께 오므로 주인에 붙여야 상속 필드 복사 · 핫 리로드 재등록 · 모듈 해제 묘비를 그대로 따릅니다.
  읽기는 지금 이름을 모두 본 뒤에만 옛 이름을 봅니다(`EnumInfo::findValueByNameHash` · `findValueByText`, `SerializerUtil::findPropertyIndexByNameHash` · `matchProperty`). 텍스트 읽기도 같은 표를 씁니다.
  옛 이름은 `_mapNameToValue` 에서 빠져 인스펙터 목록과 쓰기에 나오지 않습니다. 대상이 없거나, 옛 이름이 지금 열거자 · 필드이거나, 같은 옛 이름을 두 번 적으면 생성 오류입니다(예전에는 경고만 하고 버렸습니다).
  선언 없이 지우거나 바꾼 이름은 엄격한 읽기가 실패합니다. 리다이렉트는 바이트 배치와 무관하므로 고정 배치형 정규 문자열에 넣지 않습니다(`FixedLayoutTest.RenameRedirectsDoNotChangeTheSchemaHash`).
  `.gen` 1108 개를 전후 생성기로 비교해 바뀐 파일은 `ValueAlias` 를 쓰는 `TestSampleActor.gen.cpp` 하나였습니다.
  삭제는 대체를 명시해야 하고(선언이 없으면 오류로 멈춤), 필드 삭제에는 `Removed` 같은 선언이 없습니다. 모르는 칸을 받는 문맥(오브젝트 상태)만 선언 없이 건너뜁니다. 지운 필드를 선언으로 받게 할지는 받아야 하는 출시 데이터가 생길 때 정합니다.
  리다이렉트 검사는 같은 타입 안만 봅니다. 기반 클래스의 지금 필드 이름과 겹치는 옛 이름은 읽기가 지금 이름을 먼저 보는 순서로만 막습니다(`ReflectionSerializationTest.CurrentFieldNameWinsOverAnOldName`).
- **`REFLECT` 타입은 비가상 베이스를 하나까지만 둡니다**(2026-10-17, [고정 배치형 코덱 계약](../../Source/Engine/Serialization/Fixed/README.md) 단계 7-b). 둘째 베이스와 가상 베이스는 베이스가 `REFLECT()` 인지와 상관없이
  헤더 · 줄 · 타입 · 베이스 이름과 함께 생성 오류입니다(`AstVisitor.cpp` `collectBase`). `castTo` 는 리플렉션 부모 사슬로 파생을 판단하고 C++ 상속이 보이지 않으면 `reinterpret_cast` 로 내려가므로,
  부모가 offset 0 에 있지 않으면 포인터 보정 없이 잘못된 주소가 됩니다. 제품 코드에는 `REFLECT()` 없는 둘째 베이스가 없었고, 그 모양을 지키던 테스트 픽스처만 지웠습니다.
- **열거형 이름 조회는 값이 빈틈없으면 생성된 배열 인덱스이고, `getEnumName<E>` 는 `EnumInfo` 를 함수 지역 캐시에 둡니다**(2026-10-17, 고정 배치형 코덱 계약 3.13 · 단계 7-a). API 는 그대로입니다.
  생성기가 표식(`Invalid`, `Count`)을 뺀 값이 `0..N-1` 인 열거형에 intern 된 이름 배열을 만들고 열거자마다 `static_assert` 로 libclang 의 값과 컴파일러의 값을 대조합니다(생성기는 `SW_DEBUG` 없이 헤더를 읽습니다).
  캐시는 타입 테이블 세대가 같을 때만 씁니다. 모듈 해제는 묘비에서 배열 포인터를 비우고, 레지스트리 파괴도 세대를 올립니다. 이름에서 값으로 가는 경로와 리다이렉트 표는 바꾸지 않았습니다.
  배열을 받은 열거형은 198 개이고, 기존 `.gen` 은 덧붙은 줄만 있었습니다(바뀐 다른 줄은 지운 7-b 픽스처뿐). 실행 파일 크기(Release, 7-a · 7-b 전후)는 `Engine.dll` +12,800 B(+0.08 %),
  `GameFramework.dll` +5,120 B(+0.10 %), `App.exe` -512 B 입니다. 시간은 재지 않았습니다.
- **`GameObject::getTypeInfo` 는 `StaticType()` 결과를 캐시하지 않습니다**(2026-10-17, 단계 7-a 에서 넣었다가 병합에서 뺐습니다). 측정한 이득이 없고(`StaticType()` 이 이미 `TypeLookupCache` 를 거침), 함수 지역 정적 포인터는 엔진을 다시 만들면 해제된 `TypeInfo` 를 가리킵니다.
- **테스트만 쓰는 공개 타입은 상용 엔진 대응으로 가릅니다**(2026-10-15). 조사에서 제품 코드 참조 0 인 공개 타입이 654 개였습니다. "사용처 0 이어도 상용 엔진에 대응이 있는 공개 API 는 남긴다" 를 적용합니다.
  대응이 있는 것(언리얼 · 유니티 · Godot 에 같은 기능이 있는 키트 규칙 · 엔진 API)은 남기고 테스트를 붙여 둡니다. 대응이 없고 테스트를 돕기만 하는 타입(테스트용 목 · 고정값 · 한 테스트만 부르는 `Util`)은
  `Test/` 로 옮기거나 그 테스트 파일 안으로 넣습니다. 일괄 삭제는 하지 않고 영역을 고칠 때 그 영역의 목록을 정리합니다.
- **한 타입만 다루는 `Util` 은 그 타입의 메서드로, 한 파일만 쓰는 `Util` 은 그 `.cpp` 안으로 옮깁니다**(2026-10-15, C-26 ②). 언리얼 `FBuoyancyData` · Godot 의 값 타입 메서드처럼 데이터가 자기 계산을 듭니다(기억에 의존).
  옮긴 곳: 부력 → `BuoyancyPontoon`, 찢김 → `SurfaceMask`, 체형 모프 · 스키닝 → `AppearanceGeometry`, 팟 → `PokerPot`, JWT 서명 → `JSONWebToken`, JWK 쓰기 → `JwksKeyCache`,
  인벤토리 → 거래 항목 → `TradeClient`, 에디터 서브시스템 등록 함수 → `EditorSubsystemRegistration`, 복수형 키워드 → `CultureInfo`(규칙 표는 `CultureInfo.cpp`), RHI 환경 판정 → `App.cpp`, 프레임 단계 실행 → `ImGuiEditor.cpp`.
  테스트만 부르던 일곱(JWT 서명 · JWK · 팟 · 거래 항목 등)은 상용 엔진 · 라이브러리에 대응 기능이 있어 제품 코드에 남겼습니다.
- **확장자 → 에디터 종류의 정본은 종류 표(`EditorAssetType.cpp`) 하나입니다**(2026-10-15, C-26 ④). 엔진이 읽는 포맷은 그 타입이 `kExtension` 을 들고(`Material` · `AnimationGraphAsset` · `SpriteClipAsset` · `SequenceAsset` · `TileMapXMLData` · 오디오 데이터 둘을 더함)
  표는 그 상수를 가리키며, 씬 · 프리팹은 쿠킹 규칙(`AssetCookPath`)을 따릅니다. `EveryEngineFormatMapsToItsKind` · `EveryResourceDataKindIsData` 가 엔진 포맷과 데이터 파일 종류를 이 표에 대어 봅니다.
  언리얼 팩토리 · 에셋 타입 액션, Godot `ResourceFormatLoader::get_recognized_extensions` 처럼 포맷마다 확장자를 듭니다(기억에 의존). 파일이 하나도 없던 맨 접미사(`.seq` · `.sprite` · `.dialogue` · `.tilemap`)는 별칭이라 지웠습니다.
- **`UIStyleField` 서술자 표는 필드마다 플래그 한 칸이고, 필드 이름은 `WidgetStyle` 리플렉션 PROPERTY 이름입니다**(2026-10-15). 플래그를 PROPERTY `Meta` 로 옮기는 안은 기각했습니다 — 커스텀 메타는 Shipping 에 없습니다.
- **이벤트(`MulticastDelegate` 필드)가 받는 애노테이션은 필드 표의 Emit 열이 `Display` 인 줄입니다**(2026-10-15). 표시 메타(Category · DisplayName · Tooltip · Meta · HideInInspector · HideInMenu)를 `Editor` 에서 갈라 `Display` 로 적고, 파서의 손 목록을 지웠습니다. 생성 코드는 같습니다.
- **게임 · 키트 튜닝 값의 데이터화**(2026-10-15, C-26 ④): 작물 색 → `CropDef::_color`(`crops.xml` 의 `color`), StarSkirmish 유닛 색과 판 규칙(사냥 주기 · 도착 반경 · 시작 광물) → 디렉터 PROPERTY,
  MeadowVillage 칸 색 → 디렉터 PROPERTY, `ActionRoom` 플레이어 조절 값 → `ActionRoomSettings`, 오버월드 걸음 시간 → `OverworldTileMoverSettings::_stepSeconds`. 와이어 양자화 범위(`MechArenaSnapshot` 등)는 형식이라 데이터로 옮기지 않습니다. 2026-10-17 부터 손 상수가 아니라 스냅샷 구조체의 `PROPERTY( Step, Min, Max, MaxCount )` 가 그 범위입니다.
  손 이름 표 중 철자가 열거자와 같은 `TelemetryUploadResult` · `CrashReportUploadResult` · `TelemetryRecordResult` · `AIDirectorEventKind` 는 `ENUM()` 으로 옮겼고, 소문자 데이터 철자인 `CrashReportConsent` · `CrashReportState` · `AIDirector` 신호 · 풀은 `EnumNameTable` 로 남깁니다.
  부하 봇은 동작 이름 · 받는 칸 · 받는 값을 동작 표 한 줄(`LoadBotScenario.cpp`)로 모았습니다.
- **유지보수성을 위한 데이터화**(2026-10-15, 사용자 기준). 코드에는 불변식(수학 상수, ABI · 프로토콜 필드 번호 · 상한, 세이브 태그 · 버전, 비트 마스크, enum 1:1 순서 표, 로그 · assert 문자열, PROPERTY 메타 리터럴)만 남기고
  튜닝값 · 이름 표 · 기본값 · 색 · 크기 · 목록 · 표는 데이터(PROPERTY, 리플렉션 데이터, `Config/*.json`, 리소스 XML)로 둡니다. 정한 모양 —
  ① **코드 폴백 정의를 두지 않는다**: 액션 룸의 내장 몬스터 · 배치와 `MonsterCatalog` 의 `default_monster` 를 지우고 카탈로그 · `ActionRoomLayout` 이 없으면 오류(언리얼 DataTable 이 행을 못 찾으면 null).
  ② **엔진 기동 설정은 바탕 층 파일**: `Config/Engine/EngineConfig.json` 이 값의 정본이고 헤더는 형식만이다(언리얼 `BaseEngine.ini`). 바탕 층은 모든 키를 적고(`ConfigFileSchemaTest`), 프로젝트 설정 창은 `REFLECT( Meta = "ConfigBaseLayer" )` 를 보고 모든 키로 저장한다.
  파일 · 구운 사본이 모두 없으면 형식(0)이 범위 검사에서 져 기동이 멈춘다. 기동 전 리소스 우선순위는 구운 사본에서 읽는다. `_window._defaultRHI` 만 쿠킹 표가 정본이다. `FixedTimestep` 의 "0 이하면 기본값" 폴백도 지웠다.
  ③ **표는 엔진 데이터 하나**: 회선 흉내 프리셋은 `engine/settings/netemulation.presets.xml`(`NetEmulationPresetTable`, 유니티 Network Simulator 프리셋 자산 자리) — Core 는 리플렉션 아래라 표를 읽지 않고 조건만 갖는다. 에디터 단추 이름도 그 표의 `_label`.
  UI 미리보기 해상도는 `editor/settings/uipreview.resolutions.xml`. 그림자 품질 단계의 한 변은 설정 스키마 선택지의 `targetValue`(`gv_shadowMapResolution`, 언리얼 Scalability ini 의 `r.Shadow.MaxResolution` 자리).
  ④ **데이터 속성은 스키마가 기본값 · 필수 · 범위를 선언한다**: `GameDataXML::readFields` 가 리플렉션 Def 를 읽고 모르는 속성 · 빠진 `PROPERTY( Required )` · `Min`/`Max` 밖 값을 이름과 함께 오류로 낸다. 읽는 코드의 리터럴 기본값과 `MathUtil::max` 로 조용히 자르기는 쓰지 않는다
  (첫 적용 `ParkLayout`, 데이터는 다시 썼다). 리플렉션 XML 파일(`ReflectedXMLFile::loadDesc`)도 모르는 이름 · 범위 밖 값을 이름과 함께 알린다.
  ⑤ 부하 봇 시나리오의 `_count` · `_timeoutMs` · `_minMs` 는 필수다(읽는 코드의 숨은 기본값 제거). ⑥ 새로 들어오는 것은 `CheckDataDrivenLiterals`(추가한 줄 검사, 전체는 기준선)가 막는다.
  에디터 색 · 아이콘 표는 디자인 토큰 작업(editorux 묶음)이 맡는다.
- **설정 · 기본 에셋의 경로 칸은 `DataPath` 형식입니다**(2026-10-16, 데이터 주도 W0). `Engine/Serialization/Base/DataPath.h` — 문자열 · 리터럴로 만들 수 없고(생성자 삭제)
  리플렉션 데이터 읽기("DataPath" 텍스트 처리기)만 값을 넣는다. 칸은 `PROPERTY( Required )` 로 두고 읽기가 빠진 칸을 오류로 알린다. 첫 적용은 `EngineDefaultAssets` 의 경로 42 칸 —
  값은 `enginedefaultassets.xml` 에만 있고, 표는 `EngineDefaultAssets::load` · 서브시스템만 만든다(`ConstructKey`, `REFLECT( Abstract )`). 못 읽으면 기동 · 쿠킹이 멈춘다. 테스트는
  `test::getEngineDefaultAssets()`(하네스가 읽은 표)로 경로를 얻는다. 언리얼 `FSoftObjectPath` 가 ini 에서만 채워지는 설정 칸과 같은 자리이고, 게이트가 아니라 형식으로 막는다.
- **읽기 API 는 폴백 인자를 받지 않습니다**(2026-10-18 설계, 구현 전). `XMLNode` · `JSONValue` · `KeyValueFile` 의 값 읽기는 스키마 읽기(`readFields`), 필수 읽기(`std::optional` 반환), 선택 읽기(`std::optional` 출력) 셋만 남기고 폴백 인자 함수를 지웁니다. 기본값의 원본은 Def 초기값 하나입니다([데이터 주도 구조 정리 계획](../plans/DataDriven.md) 7절). 새 읽기 함수와 키트 카탈로그의 스키마 읽기(D1 ~ D5)는 들어갔고, 폴백 인자 함수를 지우는 일은 D9 입니다.
- **게임플레이 튜닝 값은 컴포넌트 `PROPERTY` 나 키트 설정 구조체 필드로 둡니다**(2026-10-06). 언리얼 UPROPERTY 와 데이터 에셋 방식입니다. 성능 임계값은 측정으로 정한 이름 붙은 상수로 두고, 언리얼처럼 실제로 조절할 것만 전역 변수로 만듭니다.
- **중력의 출처는 물리 설정 하나입니다**(2026-10-06). 코드는 `PhysicsSystem::getConfiguredGravity` 로 읽고, 셰이더(파도)는 머티리얼 상수로 같은 값을 받습니다(언리얼 `UWorld::GetGravityZ`).
- **4 글자 표식은 리틀 엔디언 FourCC 하나로 통일했습니다**(2026-10-06). 세이브와 쿠킹 바이너리를 다시 만들었습니다.
- **엔진의 각도 필드는 라디안입니다**(2026-10-06). 도 단위 필드를 두지 않고, `PropertyUnitsTest` 가 이 규칙에 예외를 두지 않습니다.
- **이름으로 다른 표를 가리키는 데이터는 저장소 전체를 정적으로 훑어 검사합니다**(2026-10-15, 참조 감사). 런타임이 모르는 이름을 기본값으로 대신하고 로그만 남기는 곳(물리 레이어 · 재질, 정렬 레이어, 내비 영역,
  입력 액션 · 레이어, 카탈로그 id)은 그 장면을 돌리기 전엔 드러나지 않습니다. 언리얼 Data Validation · 유니티 missing reference 검사는 에디터에서 연 에셋만 보지만, 여기서는 에셋 검증 규칙
  `component_reference`(기반 타입의 파생까지 · 팩별 표)로 일곱 팩 전부를 커밋 훅 · CI(nogpu)에서 봅니다. 규칙은 데이터 한 줄이고 검사기는 타입 · 칸 · 표만 압니다.

- **이름은 상용 엔진과 견주어 정한다(2026-10-10, 이름 점검).** 이름 점검이 모호하다고 한 것을 모두 바꾸지 않는다. 그대로 두는 것: `GameObjectManager`(한 씬의 `GameObject` 를 만들고 소유하고 틱한다),
  `GPUScene`(언리얼 `GPUScene` 과 같은 용어), `PhysicsOverlapWorld`(겹침 질의 전용 월드 — 강체 씬 `IPhysicsScene` 과 헷갈리지 않게 2026-10-13 `PhysicsWorld` 에서 바꿨다), `GameConfig`(프리셋 JSON) 와 `GameSettings`(팩의 `gamesettings.xml`) — 언리얼도 프로젝트
  설정과 게임 설정이 따로 있다, `GameplayAbilityDef`(카탈로그 정의: id · 클래스 이름 · `GameplayAbilityConfig`)와 `GameplayAbilityConfig`(어빌리티 하나의 설정) — 정의가 설정을 품는 층이라 둘 다 필요하다,
  `AbilitySpec`(언리얼 `FGameplayAbilitySpec` — 부여된 인스턴스), `TypeInfo` · `Info` 값 묶음, `InputManager` · `AssetManager`. 나눈다: `ResourceUtil` 은 경로 쪽 `ResourcePaths` 와 읽기 쪽 `ResourceIO` 로(언리얼 `FPaths` / `FFileHelper` 가 같은 선 — 끝났다),
  `EditorUtil` 은 경로를 `EditorPaths` 로 뺀다. `FrameRendererUtil` 은 그대로(언리얼 `RenderUtils` 도 같은 묶음). 파일 이름과 타입이 어긋난 곳은 `EditorColor.h`(색 타입 + 색 상수 묶음) · `EventType.h`(이벤트 ID 와 `IEvent`) · `Crafting.h`(제작 모듈 헤더)를 그대로 둔다 — 파일이 한 모듈의 묶음이다. 기각한 제안의 목록과 이유는 아래 표. `MathUtil::getRandom` · `getRandomRange` 는 스레드마다 씨앗이 다른 비결정 난수라 그대로 둔다(언리얼 `FMath::Rand` 와 같은 자리) — 결정적 난수는 `Core/Math/XorShift32.h` · `HashUtil::splitMix64Next`.

이름 점검(2026-10-10)의 제안 가운데 **기각한 것**(이름이 이미 맞다):

| 제안 | 이유 |
|---|---|
| `GameObjectManager` → `SceneObjects` | 하는 일을 정확히 말한다. `SceneObjects` 가 더 흐리다. 헤더 주석("씬 하나의 얼굴")만 관계를 정확히 고친다 |
| `GPUScene` → `GpuFrameData` | 언리얼의 `GPUScene`(인스턴스 데이터를 GPU 에 올려 둔 장면)와 같은 용어다 |
| `InputManager` → `InputHub` | 싱글턴 허브라는 결정(이 문서)과 `Manager` 접미 관례 그대로 |
| `AssetManager` → `AssetFacade` | 캐시 묶음이라도 `Manager` 로 일관(`GameObjectManager` · `ConfigManager` 와 같은 층)(그 안의 `getMaterialManager` 는 `getMaterialCache` 로 바꿨다) |
| `TypeInfo` 세 뜻 → `*KindRow` | `RenderPassTypeInfo` 는 "패스 종류의 정보"를 이름 그대로 담는다 |
| `OverlapInfo` · `CollisionInfo` · `HitInfo` → `*Event`/`*Contact` | 접촉 결과 값 묶음이고 `Info` 가 맞다(언리얼 `FHitResult` 와 같은 자리) |
| `ComponentRegistry` → `*Index` 등 | `Registry` 는 이 저장소에서 "등록된 것의 목록"으로 일관 |
| `WeakInternTable` → `WeakSharedTable` | `Intern` 은 `hashed_string` 과 같은 쓰임 |
| 열거형 `Auto` · `Normal` · `Custom` · `Count` | 열거형 안에서만 뜻이 서고 각자 문맥이 분명하다. `Misc = 0`(`HorrorCatalog.h`) 하나만 `None` 으로 바꿨다 |
| 통지 동사(`notify` · `broadcast` · `publish` …) 규칙화 | 추정에 그친다 |

약어 철자 · 어휘표 · 접미사는 docs/agents/NamingCpp.md "Function names" 와 [코딩 규칙 예시](../04_CodingGuidelines.md) 2 절에 있다.

- **저장 목적은 문맥의 필수 값이다(2026-10-18, C-53).** `SerializeContext` 는 `SerializePurpose`(Authoring · Snapshot · SaveGame)를 생성자로만 받고, 기본 문맥도 `getDefault( purpose )` 로 목적을 말해야 얻는다. 필드 규칙(`Transient` · `RuntimeState` · `SaveGame`)은 `SerializerUtil::shouldSerializeProperty` 한 곳이 세 형식(XML · JSON · 바이너리)에 같게 정한다.
  리플렉션 직렬화기(`XMLSerializer` · `JSONSerializer` · `BinarySerializer`)의 문맥 기본 인자는 `getDefault( SerializePurpose::Authoring )` 이다 — 그 함수들은 파일 입출력 도우미라 저작 형식이 계약이고, 기본 인자가 시그니처에 목적을 적는다. 오브젝트 상태(`ObjectStateSerializer`)는 기본 인자 없이 목적을 받는 `ObjectSaveOptions` · `ObjectLoadContext` 로만 쓰고 읽는다. 아카이브(`Archive::serializeObject`)도 문맥 없는 겹지정을 지웠다(언리얼도 평범한 아카이브가 영속 저장이고 PIE 복제 · 세이브는 플래그로 따로 고른다).
  에디터 되돌리기 · 복사 · 복제 · "시뮬레이션 변경 남기기" 는 Authoring 이다(사용자가 고치는 대상은 저작 데이터 — 언리얼 복사도 `TextExportTransient` 를 뺀다). 게임 상태 봉투는 핫 리로드(Snapshot)와 세이브 파일(SaveGame)이 함께 쓰므로 부르는 쪽이 목적을 넘긴다.
  컴포넌트 필드에는 `RuntimeState` 만 붙이고 `SaveGame` 을 함께 붙이지 않는다 — 붙이면 그 타입이 옵트인 세이브가 되어, 씬 오브젝트 전체를 싣는 세이브에서 나머지 필드가 빠진다. 런타임 값이 저작 값에서 시작하면 "남은 수" 가 아니라 "쓴 수" 를 둔다(`GatheringNodeComponent::_usesTaken`) — 저작 값이 바뀌어도 첫 플레이가 따른다.
- **녹화 시나리오는 `saved` 도메인의 id 로 다시 돌린다(2026-10-22, DiskPathVsResourceId).** 시나리오 실행은 작업 폴더의 `Saved` 를 디스크 도메인 `saved` 로 붙이고(`ScenarioRecorder::mountSavedDomain`), 녹화는 디스크 경로로 쓰고 `makeReplayID` 의 id 로 읽는다. 시나리오 목록에 디스크 입구를 두는 안은 입구가 다시 둘로 갈라져 기각했다 — Godot 의 `res://` · `user://` 가르기와 같은 자리다.
- **에셋 파일 형식은 XML 이나 바이너리이고, JSON 은 도구 입력 · 설정 · 사용자 상태다**(2026-10-22, 구조 단순화 TX5~TX7). 스켈레톤 · 리그 · 애니메이션 그래프 · 본 마스크 · 스프라이트 클립 · 얼굴 · 비즘 · 대화 · 시퀀스 · 지역화 문서(`.strings` · `.translation` · `.locproject` · `.cultures` · `.tm`) · VRM 아바타 · SQL 마이그레이션 목록 · 애니메이션 조정 표 세 개를 XML 로 옮겼다.
  로더는 리플렉션 구조체(`ConfigManager::readConfigXMLElement` — 모르는 속성 · 자식 · 필수 칸을 중첩까지 검사)이거나 `XMLNode` 로 손으로 읽는 것(`XMLReadUtil::hasOnlyKnownNames` · `readFloatList`)이고, 옛 JSON 리더는 두지 않았다. 임포터는 XML 을 쓰며 커밋된 결과를 `App --import-models` 로 다시 만들었다.
  형식은 `AssetTypeRegistry::isSuffixFormatAllowed` 로 굳혔다 — `.json` 접미사는 쿠킹 입력으로만 등록된다. **변환한 데이터는 엔진이 다시 쓴 결과와 바이트가 같아야 한다**: 속성값의 `>` 는 이스케이프하지 않고(본문 글은 한다), 120 바이트(글자 수가 아니다)를 넘는 여는 태그는 속성별로 접는다. 이 규칙을 놓치면 `--gather-text` · 재임포트가 파일을 다시 쓰고 `--check-text` 가 낡았다고 한다.
  재임포트는 `skeleton_minion` 의 메시 · 클립도 다시 만들었다(저장소의 옛 산출물이 지금 임포터와 부동소수 끝자리가 달랐다 — 사원수 두 값의 마지막 한 자리). 다른 모델은 스켈레톤 글만 바뀌었다.
- **셰이더 도메인 목록은 설정 데이터에서 발견한다 — 코드는 include 루트 종류(engine · common)만 안다**(2026-10-23, 머티리얼 셰이더 훅 D1). 정본은 `Config/Engine/EngineConfig.json` 의 `_listResourcePriority` 이고, `game` 토큰은 `game/` 아래 폴더를 훑어 `game/<이름>` 으로 펼친다(엔진은 게임 이름을 모른다). 게임 셰이더의 include 루트는 자기 도메인 + engine + common 이고 다른 게임은 넣지 않는다. 목록이 비었거나 종류가 빠지면 기본값 없이 오류로 멈추고, 게임 폴더의 같은 이름 헤더가 include 루트의 헤더를 가리면 컴파일 · 스탬프 쓰기 · 판정이 오류로 멈춘다(`ShaderCooker::collectShaderDomains` · `collectIncludeRootDomains` · `findShadowedHeaders`). 쿠킹 스탬프 값은 바뀌지 않는다.

- **기믹 회로 상태는 씬 · 프리팹 · 세이브에는 A, 체크포인트 · 롤백 · 넷 스냅샷에는 B 로 쓰고, 둘은 같은 `REFLECT` 선언 하나에서 나옵니다**(2026-10-17~20, 기믹 회로 상태 구조). 근본 원인은 상태 바이트의 크기를 쓰는 쪽과 읽는 쪽이 따로 손으로 계산했고 회로 해시 하나가 상태 전체의 수용 여부를 정한 것이었습니다.
  `GimmickNodeState` 는 `_id`(id 해시) · `_kind`(종류 이름 해시) · `_stateVersion` · 노드별 실수 · 정수 슬롯 목록을 갖고, `GimmickCircuit::loadState` 는 `GimmickStateMatch`(`Exact` · `NodeSetChanged` · `Mismatch`)를 돌려줍니다. `Mismatch` 는 같은 id 인데 종류 · 종류 상태 버전(`GimmickNodeKind::_stateVersion`, 기본 1) · 슬롯 수가 다르거나 같은 id 가 둘일 때뿐이며, 새 노드는 처음 상태로 두고 저장에만 있는 노드는 버립니다.
  저장 상태 적용은 `GimmickCircuitComponent::applySavedState` 한 함수입니다(`NodeSetChanged` 는 경고 뒤 이어 가고 `Mismatch` 는 오류를 남기고 회로를 비워 돌지 않음). 롤백 · 넷은 `restoreState` 로 `Exact` 만 받습니다. 상한은 노드 1,024 · 종류마다 슬롯 32(`registerKind` 가 거절)이고 정본은 C++ 상수 + `PROPERTY( MaxCount = kMaxSlotCount )` 입니다.
  `GimmickNodeState` 의 뜻이 바뀌는 변경(비트 마스크를 두 필드로 나누는 것 등)은 기본값으로 메울 수 없어 종류의 상태 버전을 올리고 저장소의 데이터를 다시 씁니다. 모르는 XML 요소(옛 `_stateBytes`)는 오류가 아니라 건너뛰고 로드마다 경고합니다(`SchemaOrphanPolicy::Ignore`). 측정(Release)은 [검증과 측정](../08_Verification.md) "기믹 회로 상태"이고, 저장 · 복원이 스텝 결과를 바꾸지 않음은 결정성 테스트(`SameInputsSameStateHash`)로 확인했습니다. 옛 코드 대비 바이트 동일성은 보증하지 않습니다.
- **`isDerivedFrom` 은 조상 디스플레이 테이블을 그대로 두고 DFS 구간 번호는 채택하지 않습니다**(2026-10-17, 리플렉션 캐스트 비용 조사). 디스플레이도 O(1)(원자 로드 네 번과 비교 한 번, 깊이 8 `kAncestorDisplayDepth` 초과나 테이블이 없으면 `isDerivedFromSlow`)이라 구간 번호가 줄이는 것은 원자 로드 두어 번뿐입니다. 구간 번호는 전체 타입 트리를 깊이 우선으로 번호 매겨야 하는데 타입이 추가되면(DLL 로드 · 핫 리로드 · 플러그인) 다른 타입의 번호가 바뀌고, 코드 생성기는 모듈 하나만 보므로 전역 번호를 빌드 시간에 정할 수 없습니다. 디스플레이는 이름 인덱스 기반이라 타입 추가가 다른 타입의 테이블을 바꾸지 않습니다.
  남은 캐스트 비용의 근본 원인은 캐스트가 아니라 타입 없는 핸들과 씬 전체 순회였고, 타입별 컴포넌트 목록과 `TComponentHandle<T>` 로 풀었습니다(5-6). `GameObject` 의 가상 `getTypeInfo()` 는 결과 캐시에 측정한 이득이 없고 엔진 재생성에서 낡아 캐시하지 않습니다.
- **엔진 설정 · 게임 프리셋 · 2D 렌더 설정은 정적 활성본(`getActive`)이 아니라 소유 서브시스템이 들고 서비스 표로 읽습니다**(2026-10-25, ArchitectureSeams 3 단계). 언리얼 `GEngine->…Settings`(전역 CDO) · 유니티 `ProjectSettings`(에디터 전역)는 전역이지만, 여기서는 핫 리로드 · 다중 월드 · 테스트 격리가 목적이라 소유자를 둔다. `EngineConfig` · `GameConfig` 는 `ConfigManager` 가 생성자에서 자리를 만들어 두고 읽은 값을 그 자리에 담으며(다시 읽기도 제자리 - 서비스 표 칸의 주소가 같다), `Render2DSettings` 는 `Render2DSettingsSubsystem` 이 기동에서 읽어 소유한다. 읽는 쪽은 `engine::getSubsystem<T>()`, 테스트는 `ScopedEngineServiceOverride::bindObject` 로 다른 값을 건다. 서비스 표에 못 닿는 에디터 · 게임 프레임워크 호출처는 `GameConfig::getActive()` 전달 창구(사본이 아닌 서비스 표의 값)를 쓰며 `editor::` · `game::` 창구로 옮기면 지운다. `CheckServiceAccess` 는 정적 사본의 참조를 돌려주는 `static const T& getActive()` 도 활성 창구로 센다(예외 표의 `PendingScopeMove` 는 영구 예외가 아니라 이전 대기다).
