# 설계 백로그(2026-11-07 코드 대조 감사)

> 질문: 지금 코드에서 확인한 결함 · 구조 경계 · 확장 후보와 그 판단은 무엇인가?
> 독자: 이 저장소에서 일하는 에이전트와 사용자.
> 결론: 결함은 바로 고치고, 측정 후보는 재서 정하며, 기능 후보는 수직 슬라이스로 만든다. 판단과 작업 배정은 맨 아래 "통합 담당 판단" 절이 정본이다.

## 테스트가 비어 있는 길

인계(`docs/NEXT.md`)에 적힌 Linux 에디터 체인 실패는 이번엔 재현하지 않았습니다. 초점 없는 창에서 `audiomixer`, `graphaddnode`, `viewportselect`, `assetmanage` 등이 입력을 못 받는다는 기록입니다. lavapipe의 `R16G16_SNORM` 블렌드 거부는 코드가 이미 블렌드 비트가 없으면 덮어쓰기로 바꾸므로, 그 기록은 옛 증상일 수 있습니다.

쿠킹된 팩이 코드가 경로를 이어 붙여 읽는 에셋을 빼먹는지는 `-cooked-content`로 확인하지 않았습니다. 서비스 생성 순서가 의존·이름 순이라 생성자 안에서 다른 서비스를 찾으면 깨진다는 위험도, 그런 생성자는 이번 검색에서 찾지 못했습니다.

그래프(`graphify-out/graph.json`)도 조회했다. `ModuleSharedAssetCache.h`는 여러 온라인·장르 카탈로그에서 직접 가져오고, `RHIModuleABI.h`는 RHI 백엔드 등록·진입점에서 가져온다. 그래프의 가져오기 간선은 의존 방향을 찾는 단서이지 런타임 결합이나 결함의 증거는 아니다. 빌드·시나리오는 돌리지 않았다.

## 구조·시스템·이름 감사

이번 확인은 그래프와 일회성 구조 감사 스크립트(지움), `naming-words`, `cpp-shape --all` 결과를 교차 확인한 1차 감사다. 이름 후보 수는 자동 검사 결과이지 모두 고쳐야 할 오류 수가 아니다. 특히 직렬화·리플렉션·에셋 키에 노출된 이름은 사용처와 호환성을 확인한 뒤 바꾼다.

### 구조와 시스템

| 우선순위 | 영역·근거 | 개선 방향 |
|---|---|---|
| 높음 | 엔진 층은 컴파일 의존 규칙으로 막지만, 링크 산출물 경계와는 별개다. 서버 빌드에서만 드러나는 층 위반도 인계에 기록돼 있다. | 층별 include·링크 의존을 계속 검사한다. 실제 순환과 독립 배포 요구를 확인하고, 이득이 확인된 층만 별도 타깃으로 나눈다. |
| 중간 | `ModuleSharedAssetCache.h`는 그래프에서 여러 장르·온라인 카탈로그의 직접 의존점으로 보인다. 이는 포함 관계만 확인한 것이며, 그 자체로 결함은 아니다. | 사용처가 실제 공유 캐시 계약에 의존하는지 확인하고, 불필요한 광역 헤더 의존은 좁힌다. 모듈 언로드·재로드 시 캐시 소유권과 무효화 동작을 테스트한다. |
| 높음 | 쿠킹 의존성은 경로를 코드에서 조립하는 에셋을 놓칠 수 있다. | `-cooked-content` 실행으로 경로 조립 에셋 누락을 잡는다. `AssetReferenceFields` 변경의 재쿠킹도 확인한다. |
| 중간 | RHI 포맷·사용 조합의 지원 여부는 백엔드와 장치마다 다르며, Linux lavapipe 관련 기록은 이미 코드가 바뀌었을 가능성이 있다. | 포맷 기능을 실제 리소스·렌더 상태 조합으로 질의하고, 미지원 경로의 동작을 백엔드별 시나리오로 검증한다. 기존 lavapipe 기록은 재현 후 갱신한다. |
| 중간 | 온라인 `AccountTicketConnect` 의 단위 테스트(`TestAccountTicketConnect.cpp`)는 거절 · 종료 경로만 본다. 로그인 티켓 교환 · 암호화 세션 연결 성공은 검증하지 않는다. | 루프백 온라인 호스트에서 성공 · 만료 · 재시도 실패 경로를 테스트하고, 비밀 값이 로그나 오류 결과에 노출되지 않는지 확인한다. |

### 이름과 용어

| 범위 | 확인 결과 | 개선 방향 |
|---|---|---|
| 함수·타입의 용어 | `naming-words`는 통과(0건)했지만, 이는 저장소가 등록한 함수 첫 단어·타입 접미사·금지 이름만 검사한다. 멤버·지역 변수·매개변수의 형태와 업계 용어 전체를 보증하지 않는다. | 새 이름은 `docs/agents/NamingCpp.md`의 용어·동사·타입 역할 표를 기준으로 정한다. 반복되는 오용만 좁은 규칙으로 린트에 추가하고, 대량 일괄 개명은 하지 않는다. |
| 파일·폴더·타입 이름 | 일회성 구조 감사 스크립트(지움)가 모호한 파일 이름 후보 24개와 폴더 후보 5개를 냈다. 예: `GameObject`, `FrameData`, `TypeInfo`, `BuildInfo`, `RawImageData`, `Engine/Object`, `GameData`. 이는 이름과 내용이 어긋난다는 판정이 아니라 검토 후보이다. | 타입의 실제 책임·소유 시스템을 확인해 구체 이름으로 바꾼다. 특히 `TypeInfo` 같은 일반 접미사는 구체 역할을 이름에 드러내고, 파일 이동·이름 변경은 리플렉션·직렬화·에셋 경로 참조까지 함께 갱신한다. |
| 정적 함수 전용 타입 | 같은 검사에서 `Util`·`Internal` 접미사가 없는 정적 함수 전용 타입 후보 464개가 나왔다. `SceneCooker`, `EditorSceneCommands`, `ReflectionInvoke`처럼 역할이 이름에 드러나는 타입도 포함돼 있어 수치만으로 위반이라 할 수 없다. | 기능 소유자가 분명하면 소유 시스템 이름을 유지한다. 소유자가 없고 이름만으로 책임을 알기 어려운 타입만 `…Util` 또는 파일 내부 `…Internal`로 정리한다. |
| 구성원·지역·매개변수·컨테이너 | 규칙 문서에는 `_camelCase`, `camelCase`, 출력 매개변수 `out…`, 단수 컨테이너 이름 등이 있으나 현재 `naming-words` 검사 범위에는 포함되지 않는다. 이번 검사에서 전수 위반 목록은 만들지 않았다. | 기존 규칙을 자동 검사에 단계적으로 연결한다. 자동 수정 전에는 예외·생성 코드·외부 프로토콜 이름을 분리하고 위반 위치를 보고하도록 한다. |
| 파일·타입 대응 및 폴더 일관성 | 구조 감사는 14개 이상 소스 단위가 있는 폴더 29곳에서 이름 첫 단어 반복을 표시했다. 이는 책임 경계가 불명확할 수 있는 신호이지, 접두사를 금지하라는 뜻은 아니다. | 폴더가 여러 책임을 섞는 경우에만 도메인 하위 폴더로 나눈다. 파일의 주 타입·책임과 파일명을 일치시키되, 기존 단일형·생성 파일 관례는 유지한다. |

**범위 한계:** 전체 C++ 식별자를 수작업으로 전수 판정하거나 모든 시스템의 런타임 동작을 실행한 것은 아니다. 위 표는 확인된 구조 제약과 감사 도구가 드러낸 검토 대상을 분리한다. 같은 유형의 이름·수명·의존성 문제가 다른 시스템에도 있는지는 후속 리팩터링에서 각 소유 경계와 테스트를 따라 확인한다.

### 구조 리팩터링 코드 대조 (2026-11-07)

앞선 표는 서로 다른 커밋의 조사와 패턴 검색에서 나온 가설이었다. 현재 구현을 직접 따라가 보니 일부 주장은 이미 해결됐거나 근거가 부족해 그대로 할 일로 둘 수 없다. 그래프는 관계를 찾는 길잡이로만 쓰고, 아래 판단은 현재 소스 코드로 대조했다. 이 단계는 전체 코드의 완전한 감사를 마쳤다는 뜻은 아니다.

| 우선순위 | 현재 코드에서 확인한 내용 | 개선 방향과 완료 조건 |
|---|---|---|
| 검증 필요 | **엔진 프레임 API에는 뷰 입력이 남아 있지만 렌더 구현 경계는 이미 분리돼 있다.** [`Engine::update`](../Source/Engine/Engine.h)는 `FrameViewports`와 카메라 공급자를 받지만, 제출은 [`IEngineRenderDriver`](../Source/Engine/Presentation/IEngineRenderDriver.h)로 전달한다. 따라서 “Engine이 Renderer 구현을 직접 안다”는 이전 주장은 틀렸다. | 서버·쿠커 타깃에서 이 프레임 입력 형식이 실제로 불필요한 의존을 끌어오는지 빌드 타깃·include 그래프로 확인한다. 컴파일 경계 위반이 재현될 때만 뷰 데이터 계약을 분리한다. |
| 점검 보류 | **렌더 확장 두 경로는 확인했으나 중복 결함은 입증되지 않았다.** 기본 씬 수집은 엔진 컴포넌트를 구체적으로 처리하고, 모듈 확장은 [`RenderExtensionRegistry`](../Source/Engine/Renderer/Extension/RenderExtensionRegistry.h)와 프록시 수명 계약을 쓴다. 둘의 입력·수명·스레드 계약은 다르다. | 일괄 통합하지 않는다. 실제 플러그인 기능이 기본 수집 경로에 의존하거나 같은 기능을 두 번 구현하는 사례가 확인될 때만 공통 기여 API를 설계한다. |
| 점검 보류 | **온라인 서비스는 공통 프로토콜 기반을 이미 갖췄다.** [`OnlineProtocolService`](../Source/GameFramework/Kits/Feature/Online/OnlineCore/Server/Service/OnlineProtocolService.h)는 타입 있는 메서드 호출·응답과 공통 결과 지표·재시도 처리를 제공한다. “키트마다 손 디스패치·지표·오류 처리가 제각각”이라는 이전 요약은 현재 코드에 맞지 않는다. | 새 `OnlineSession`이나 두 번째 서버 추상화를 먼저 만들지 않는다. 게임의 실제 클라이언트 조립·수명 소유를 확인하고, 여러 게임에서 같은 통합 결함이 확인될 때만 세션 경계를 정한다. |

**아직 코드·측정으로 확인하지 않은 후보:** 헤더 전파와 번역 단위 비용, 시작 시간의 단계별 비중, UI/VFX 배열 복사, 프레임 경로 intern은 조사 문서의 기록만으로 결함이라 판정하지 않는다. 다음 점검에서는 Editor, 오디오·애니메이션·물리, 쿠킹·셰이더, 플랫폼별 빌드 경계를 소유 코드와 실제 호출 경로에서 확인하고, 성능 주장은 프로파일링·벤치마크로 검증한다.

**유지할 건강한 구조:** 타입 기반 온라인 프로토콜·InterestReplicationPolicy의 증분 가시 목록·프로브 revision 캐시·머티리얼 변경 감지·`IEngineRenderDriver` 경계는 현재 코드에서 이미 작동하는 구조다. 재작성 후보로 올리기 전에 실제 회귀나 측정된 병목이 있는지 먼저 확인한다.

### 데이터 기반 값 · 매직 리터럴 전수 점검 (요청 2026-11-07)

**남은 일(9 차 W5 뒤):** 늘지 않게 하는 구조(파일 단위 래칫 `Scripts/lint/MagicLiterals.py`, 기준선 `Config/Rules/Lint/MagicLiteralBaseline.json`, 규칙은 [Constants.md](agents/Constants.md) "The ratchet" · "Sentinels")는 섰다. 기준선이 큰 파일(데모 UI 화면 · 게임 모드 컴포넌트)의 튜닝 실수를 PROPERTY · 카탈로그로, 다른 작업자가 맡은 AI · 내비 · 에디터 패널의 `-1`, 반환형을 `sw::optional` 로 바꿀 `find*Index` 무리(약 92 곳 — `sw::expected` Core 밖 이전과 함께, [Errors](agents/Errors.md)).

| 우선순위 | 코드에서 확인한 내용 | 다음 작업과 완료 조건 |
|---|---|---|
| 높음 | **게임별 설정 일부는 이미 데이터로 편집할 수 있다.** [`WildWestGameModeComponent`](../Source/Games/WildWest/WildWestGameModeComponent.h)의 기온·하루 길이·목격 범위·경고 기준·시작 돈은 `PROPERTY`이고, [`MeadowFarmGameModeComponent`](../Source/Games/MeadowVillage/MeadowFarmGameModeComponent.h)의 시작 돈·시간 배율·색·간격도 `PROPERTY`다. 생성자 기본값만 보고 이를 코드 하드코딩 결함으로 세지 않는다. | 인스턴스/프리팹 데이터로 덮어쓸 수 있는지, 기본값이 실제 게임 데이터에서 설정되는지 확인한다. 조절할 필요가 있는 값은 `PROPERTY`·게임 설정/카탈로그·에셋 중 소유권에 맞는 하나로 둔다. |

상수 위치와 이름의 규칙은 [Constants.md](agents/Constants.md)가 정본이다. 특히 튜닝 값은 일반 상수가 아니며, sentinel은 `-1`이라는 표기 자체가 아니라 그 값의 도메인 의미와 저장/통신 호환성을 기준으로 정리한다.

### App 프로세스 종료 코드 체계 (요청 2026-11-07)

**판단: 현 체계는 제거하지 않는다.** [`ProcessExitCodes.json`](../Config/Engine/ProcessExitCodes.json)이 종료 코드 숫자의 단일 출처이며 configure 생성기([`ProcessExitCodes.py`](../Scripts/generate/ProcessExitCodes.py))가 C++ 열거형과 CMake 변수를 만든다. 실제 사용처는 자동화 결과 변환([`AutomationExitCode.h`](../Source/DevTools/Client/Automation/Runner/AutomationExitCode.h)), CTest 백엔드 건너뜀([`Test/Scenario/CMakeLists.txt`](../Test/Scenario/CMakeLists.txt)), RHI 환경 불가([`RHIInitResult.h`](../Source/Engine/Graphics/RHI/RHIInitResult.h)), 에디터 자체 테스트 종료다([`EditorSelfTest.cpp`](../Source/Editor/SelfTest/EditorSelfTest.cpp)). 이를 없애면 CTest `SKIP_RETURN_CODE`와 자동화의 결과 분류가 숫자 하드코딩으로 되돌아갈 위험이 있다.

| 우선순위 | 현재 경계 | 다음에 검토할 개선 |
|---|---|---|
| 낮음(생성기 보강 검토) | 생성기는 이름·숫자·라벨 중복을 검사하지만, 프로세스 종료 코드의 플랫폼 표현 범위나 자동화 결과 열거자와의 대응을 전부 검증하지 않는다. 현재 표 값은 POSIX 종료 상태에서 표현 가능한 범위 안이다. | 스키마 검증에 지원 플랫폼의 종료 코드 범위, 고정 예약 코드, 자동화 결과 매핑의 누락/중복 조건을 추가할지 검토하고 생성기 테스트를 둔다. CTest의 skip 코드 77 및 외부 프로토콜/도구가 기대하는 값을 변경할 때는 동작 호환성을 확인한다. |

별도인 [`Scripts/common/ExitCode.py`](../Scripts/common/ExitCode.py)는 스크립트 자체의 `Passed`/`Violation`/`CheckInvalid` 반환 계약으로, App 프로세스 종료 코드 표와 역할이 다르다. 이름이 비슷하다고 두 표를 합치거나 모든 도구에 App 코드를 강제하지 않는다.

### 추가 아키텍처 고도화 후보 (코드 대조 2026-11-07)

새로운 추상화를 늘리기보다, 현재 구조가 보장하는 실행 토폴로지와 의존성 계약을 명확히 하는 후보다. 둘 다 현재 요구가 확인되기 전에는 재설계하지 않는다.

| 우선순위 | 코드에서 확인한 구조 | 고도화 방향과 조건 |
|---|---|---|
| 조건부(다중 엔진 필요 시 높음) | **엔진 서비스 빠른 조회는 프로세스 전역 단일 바인딩이다.** [`ServiceSlotOf<T>::s_slot`](../Source/Core/Subsystem/ServiceSlot.h)은 타입마다 정적 슬롯 하나이고, [`EngineServices.cpp`](../Source/Engine/Foundation/EngineServices.cpp)는 전역 `s_pSubsystems` 하나를 바인딩한다. 두 번째 엔진의 바인딩은 `EngineServiceBindKind::AlreadyBound` 로 거절된다(앞 엔진을 덮지 않는다). | 에디터 다중 미리보기, 인프로세스 서버·클라이언트 등 동시 Engine이 실제 요구될 때만 호출 경로에 `EngineContext`/locator를 전달해 슬롯을 컨텍스트별로 분리하고, 두 인스턴스 격리 테스트를 만든다. |
| 중간(계약 일관성) | **서브시스템 의존성은 두 선언으로 나뉜다.** [`EngineSubsystemRegistration.h`](../Source/Engine/Foundation/EngineSubsystemRegistration.h)의 등록 `after` 목록은 공장이 다른 범위 항목을 찾기 전의 생성 순서를 정하고, 각 `ISubsystem::getSubsystemDependencies()`는 초기화·종료 순서를 정한다. 의미가 달라 둘을 단순 통합하면 안 되지만, 동일한 관계를 중복 선언할 때 누락·불일치가 가능하다. | 기존 테스트와 등록 표에서 두 그래프의 역할을 확인하고, 공장이 `scope.get<T>()`로 요구하는 타입이 `after`에 빠지면 시작 단계에서 이미 오류가 나는지 검증한다. 수명 의존성 누락도 독립 테스트/진단으로 잡는다. 선언을 하나로 합치는 것은 생성 순서와 초기화·역순 종료 의미를 모두 보존할 수 있을 때만 검토한다. |

### 셰이더 구조와 디퍼드/G버퍼 활용 고도화 (코드 대조 2026-11-07)

**현황:** 디퍼드 렌더링과 G-buffer는 이미 구현되어 있고 선택 파이프라인으로 쓸 수 있다. [`deferredpipeline.xml`](../Content/engine/pipeline/deferredpipeline.xml)은 알베도(`R8G8B8A8_UNORM`), 옥타헤드럴 노멀(`R16G16_SNORM`), 금속도·거칠기·그려짐 표시(`R8G8B8A8_UNORM`)의 세 색 첨부와 깊이를 쓰며, [`deferred_lighting.hlsl`](../Content/engine/shaders/deferred_lighting.hlsl)은 깊이에서 월드 위치를 복원하고 포워드와 공유하는 조명 식으로 라이트를 계산한다. 포워드 파이프라인도 계속 지원한다. 즉 “G-buffer가 없다”기보다 **G-buffer 계약이 현재 PBR 표면 두 속성 중심으로 고정되어 있고 실제 게임 장면에서 그 이점과 비용을 넓게 검증할 필요가 있다.**

- **게임 제작자가 기본 머티리얼을 만들 편집 도구는 있다.** Content Browser에는 `Content/game/<게임>/materials/` 아래 새 `.matgraph.xml` 머티리얼 그래프를 만드는 템플릿이 있고 [`MaterialGraphPanel`](../Source/Editor/Panels/MaterialGraph/MaterialGraphPanel.h)은 노드 연결, 파라미터, 실시간 미리보기, 컴파일 문제를 다룬다. 저장하면 컴파일된 HLSL과 `.gen.material`이 생성되어 런타임/Shipping은 생성물만 읽는다. 생성 머티리얼을 부모로 둔 게임별 `.material` 변형은 [`MaterialPanel`](../Source/Editor/Panels/Material/MaterialPanel.h)에서 색·숫자·텍스처 등 노출된 프로퍼티를 편집하고 구 미리보기/선택 오브젝트 적용을 할 수 있다. 기존 게임에도 이런 변형 에셋이 사용된다(예: `game/wildwest/materials/rider.material`).
- **다만 완전한 게임별 셰이더 저작 플랫폼은 아니다.** 그래프 노드 등록표는 엔진의 고정 등록이며 새 노드는 엔진 C++/HLSL 변경과 쿠킹이 필요하다. 게임 키트/게임 모듈이 자체 노드나 조명 모델을 에디터에 등록하는 확장점은 확인되지 않았다. 현재 노드 그래프는 제한된 표면 출력과 HLSL 조각을 조합하는 저작 도구이지 임의 셰이더를 안전하게 만들고 G-buffer 호환성을 보장하는 범용 도구는 아니다. Material 패널의 구 미리보기는 실제 게임 메시·조명·파이프라인 차이를 모두 검증하지 못한다. 따라서 **기존 노드로 PBR 표면을 조합하고 그 게임의 값/텍스처를 튜닝하는 작업은 가능하지만, 새로운 게임 고유 셰이딩 모델이나 G-buffer 데이터 계약을 아티스트가 단독으로 추가하는 것은 현재 토대 밖**이다.
- **작업자 중심의 다음 단계:** 게임 도메인별 `Material Graph` 노드 복제 대신, 공통 표면 속성/노드 라이브러리를 명확히 지원하고 `_base` 머티리얼 변형으로 게임별 값만 분리한다. 자주 쓰는 조합은 실제 게임 재료(피부·천·금속·젖음·식생 등)별 시작 그래프/재질 템플릿과 검증된 파라미터 그룹으로 제공한다. 미리보기에는 해당 게임의 대표 메시·텍스처·조명/HDR 환경과 현재 Forward/Deferred 경로를 선택할 수 있게 하고, 컴파일 성공뿐 아니라 누락 텍스처·지원 셰이딩 경로·G-buffer/ForwardOnly 분류·쿠킹 상태를 인라인으로 표시한다. 게임별 새 노드는 요구가 반복되고 공통 표면 계약으로 표현할 수 없을 때만 별도 모듈/확장 설계를 검토하며, 코드/HLSL 구현자와 콘텐츠 작업자의 책임을 분리한다. 먼저 기존 `MaterialGraphPanel`, `MaterialPanel`, 에셋 템플릿을 실제 게임 재료 제작 시나리오로 시험해 노드 발견성, 머티리얼 변형, 타깃 메시 미리보기와 쿠킹 확인 중 어느 단계가 제작 병목인지 정한다.
- **이미 있는 우회 경로:** 툰처럼 빛별 단계 셰이딩을 요구하거나 그래프의 Emissive처럼 현재 G-buffer 표면에 담기지 않는 값은 [`MATERIAL_FORWARD_ONLY`](../Source/Engine/Renderer/Pipeline/RenderPassTypeInfo.h)로 G-buffer에서 제외해 `ForwardOnlyOpaque`로 그린다. 이는 표현 불가능한 효과를 정확히 그리는 안전한 경로지만, 기본 PBR 계열 머티리얼까지 실수로 ForwardOnly로 분류하면 디퍼드 조명·타일 라이트 경로의 혜택을 받지 못한다. 반대로 모든 속성을 G-buffer에 넣는 것도 첨부 대역폭·메모리·타일 기반 GPU 비용을 불필요하게 키운다.
- **셰이더 구조 확장 원칙:** G-buffer 레이아웃은 판 번호 있는 명세 한 벌([`GBufferAttachmentSpec`](../Source/Engine/Renderer/Pipeline/GBufferLayout.h) v1 — PSO 포맷 · 파이프라인 XML 검증 · 렌더 타깃 창 풀이가 읽고, HLSL `SW_GBUFFER_LAYOUT_VERSION` 과 단위 테스트가 판을 맞춘다)이 됐다. 남은 것: 공통 `PixelContext`/표면 평가와 패스별 출력(Forward 색 또는 G-buffer 인코딩)을 분리하고, 표면 모델/필수 입력을 명시적으로 선언·검증한다. 알파의 Unlit/Lit 분류와 픽셀 유효 표시에 ID 를 덧붙이려면 판을 올린다. 정의되지 않은 필드나 미지원 조합은 쿠킹/파이프라인 검증에서 실패시킨다.
- **확장 우선순위:** 우선 기존 Material Graph/셰이더에서 Lit·Unlit·Emissive·툰·알파 마스크·노멀맵의 패스 분류를 목록화해 지원 계약을 만든다. 다음으로 PBR에서 실제 필요한 추가 정보(예: AO 또는 재질/셰이딩 모델 식별자)가 있는지 대표 게임의 장면과 조명 효과에서 확인한다. 공통 속성 하나가 추가되어야 한다면 기존 남는 비트/채널을 쓸 수 있는지 정밀도와 호환성을 검사하고, 셰이더 모델마다 다른 데이터가 필요하면 데이터 팩/별도 G-buffer 첨부 또는 ForwardOnly 중 측정과 명확한 계약을 근거로 고른다. Emissive는 단순히 모든 그래프를 ForwardOnly로 남기지 말고, 조명과 합성의 의미를 정해 G-buffer 이후의 emissive 합성 경로가 포워드 조명·Bloom·톤맵 순서와 일치하는지 평가한다.
- **측정과 플랫폼 선택:** Forward와 Deferred는 같은 카메라·콘텐츠·해상도·품질·플랫폼 조건으로 비교한다. 고정된 시나리오에서 GPU 프레임 시간, G-buffer 쓰기/읽기 대역폭과 메모리, 셰이더 변형/PSO 수, 라이트 수에 따른 증가, 투명·ForwardOnly 드로우를 기록한다. 데스크탑 다중 동적 라이트에서 이득이 확인되면 기본 선택 대상으로 삼고, 타일 메모리와 대역폭 제약이 큰 모바일/저사양 GPU에서는 품질별·플랫폼별 포워드 선택을 유지하거나 측정으로 결정한다. 프레임 시간은 반복 측정과 동일 기기 기준선을 사용하고, 단일 측정으로 승격하지 않는다.
- **검증:** writer와 lighting reader가 같은 패킹을 쓰는 왕복/레퍼런스 테스트, 빈 픽셀·역노멀·재질 경계·Unlit/Emissive 값 시나리오, 포워드/디퍼드 동일 장면 이미지 비교, 각 RHI 백엔드의 셰이더 쿠킹/PSO 생성(첨부 포맷 · 클리어 검증은 `GBufferLayout` 이 한다). G-buffer를 확장하면 저장 대역폭, 동시 컬러 첨부 한도와 Android/Web 등 백엔드의 포맷 지원을 확인하기 전에는 기본 렌더링 경로로 강제하지 않는다.

### Test Framework 고도화 후보 (코드 대조 2026-11-07)

현재 틀은 필터·태그·disabled 사유·픽스처·정적 매개변수 케이스·자식 역할·시한·반복·결정적 샤드·스냅숏·기대 로그·JSON 결과를 이미 제공한다. 에디터 자체 테스트도 같은 `TestRunner`를 쓴다. 이 기능을 중복 구현하거나 별도 프레임워크를 추가할 이유는 확인하지 못했다. Shipping에서는 테스트 구현이 이미 제외되므로 제거할 대상도 지금은 특정하지 않는다.

| 우선순위 | 코드에서 확인한 경계 | 고도화 방향 |
|---|---|---|
| 조건부(병렬화 필요 시) | 이름의 FNV 해시로 몫을 나누는 샤드는 재현 가능하지만 케이스 실행시간을 고려하지 않아 긴 테스트가 한 몫에 몰릴 수 있다. 동일 프로세스 내 병렬 실행은 전역 활성 상태와 단일 로그 질의 경로가 있어 안전하지 않다. | 병렬화 전에 먼저 JSON에서 지속적인 실행시간 자료를 모으고 현재 CTest 샤드의 병목이 실제인지 측정한다. 필요할 때 시간 균형형이면서 결정적인 몫 배정을 도입한다. 스레드 병렬 실행은 `TestExecutionContext`를 케이스별로 분리해 단언·로그·크래시 문맥을 격리하기 전에는 금지하고, 우선은 프로세스 샤딩을 유지한다. |

**실제 게임 루프를 검증하는 시나리오:** 프레임워크 단위 테스트나 한 시스템만 보는 시나리오만으로는 장르 키트가 플레이 가능한 게임 기능을 뒷받침하는지 판단하기 어렵다. 그렇다고 특정 상용 게임을 복제할 필요는 없다. 해당 장르에서 실제 플레이어가 경험할 목표·행동·결과·진행을 담은 작고 독창적인 수직 슬라이스를 만들고, 기존 테스트 게임과 자동화 시나리오를 그 흐름의 회귀 검증에 활용한다.

- **현재 기반을 먼저 재사용한다.** 게임별 자동화 시나리오는 이미 있다: [`Shooter3D autoplay`](../Content/game/shooter3d/automation/autoplay.scenario.xml)는 AI 조준·발사·적중과 플레이어 조종 복귀를, [`HarvestValley actions`](../Content/game/harvestvalley/automation/actions.scenario.xml)는 이동·도구 선택·밭 갈기를, [`StarSkirmish AI`](../Content/game/starskirmish/automation/ai.scenario.xml)는 자원 수집·일꾼 생산·보급 건설을, [`AbilityArena AI`](../Content/game/abilityarena/automation/ai.scenario.xml)는 적 추격·공격을 확인한다. 따라서 “게임 동작을 흉내 내는 테스트가 전혀 없다”는 진단은 맞지 않는다.
- **다만 이 사례들은 완결된 장르 플레이 루프를 모두 검증하지는 않는다.** 현재 확인한 시나리오들은 입력/AI와 개별 상태 전이를 검증하는 데 유용하지만, 예를 들어 전투의 목표 완료와 보상, 농사의 파종→성장→수확→판매와 날짜 진행, RTS의 생산→교전→승패 같은 시작부터 결과까지의 흐름이 각 게임에서 일관되게 자동 검증되는지는 별도 확인이 필요하다. 지원하지 않는 기능을 테스트가 있다고 가정해 빈칸으로 채우지 않는다.
- **고도화는 기존 게임 하나씩의 수직 슬라이스로 한다.** 각 선택 장르에 플레이 가능한 작은 맵, 실제 입력/조작 또는 동일한 게임 입력 추상화를 쓰는 자동 플레이, 유효한 카탈로그/에셋, 명시적인 완료·실패 조건을 갖춘다. 시나리오는 한두 개의 의미 있는 전체 루프를 수행하고 핵심 상태 변화, 저장·재시작이 범위에 포함될 때의 복원, 로그/골든을 판정한다. 자동 플레이는 테스트 전용 치트 경로로 게임 규칙을 우회하지 말고 플레이어와 같은 입력·게임 서비스를 사용하게 한다.
- **검증 층을 나눈다.** 순수 규칙은 단위 테스트, 시스템 간 결합은 게임 수직 슬라이스 시나리오, 렌더링·조작·에디터 UX는 별도 화면/입력 시나리오로 둔다. 한 거대한 시나리오가 모든 조합을 떠맡지 않도록 하며, 고정 델타·시드·초기 상태와 감시 값으로 반복성을 확보한다. 실제 프레임률이나 타이밍이 요구사항인 부분만 벽시계 기반 측정으로 분리한다.

**게임 검증 순서:** 현재 자동화 사례와 게임 기능을 먼저 대조하고, 장르의 완결 루프가 비어 있는 게임에만 수직 슬라이스 시나리오를 추가한다.

### 데이터 기반 카탈로그 참조 전수 점검 (코드·Graphify 대조 2026-11-07)

**범위:** 카탈로그 간 ID 참조의 검사 경로를 대조했다. 공통 규칙은 S2 계약(b2c3841ac4)으로 데이터 표 한 벌([`datacatalogreferences.xml`](../Content/engine/settings/datacatalogreferences.xml), [`DataCatalogReferences`](../Source/Engine/Serialization/Inherit/DataCatalogReferences.h))이 됐다 — 깨진 필수 참조는 로드 · 쿠킹 실패, 선택 참조는 경고다. 아래는 그 표 밖에 남은 경로다.

| 검증 경로 | 확인된 범위 | 현재 경계 |
|---|---|---|
| 별도 로더/자체 검사 | 다른 파일 카탈로그로의 연결(무기 → 탄 · 아이템, 격투 기술 → 프레임)은 아래 엄격도 계약대로 쥐는 쪽이 시작할 때 본다. | `FighterCatalog`(기술 → 프레임)는 이 줄에 맞으나 그 기술만 빼는 정책이다 — 계약대로 서지 않게 할지 본다. |
| 자체 카탈로그 내부 연결 | [`ActionPlatformerCatalog`](../Source/GameFramework/Kits/Genre/Action/ActionPlatformer/Catalog/ActionPlatformerCatalog.cpp)의 패턴 상태 전이는 로더가 직접 검사한다(Ability · Quest · SkillTree · Schedules · Metroidvania · RTS · Conquest 는 참조 표로 옮겼다). | ActionPlatformer는 잘못된 패턴을 제외한다 — 표로 옮길 수 있는지 본다. |

**개선 방향:** (1) 표 밖의 별도 로더/자체 검사에서 오류를 보고하는 규칙을 모아, 필수 참조·선택 참조·경고 허용·항목 제외·전체 로드 실패 중 의도된 정책을 각 연결별로 확정하고, 표로 옮길 수 있는 것은 `datacatalogreferences.xml` 로 옮긴다. (2) 의도적으로 허용된 경고/부분 복구는 유지하고, 필수 연결만 로드 결과에 반영한다. (3) 같은 루트 안의 연결과 `FighterCatalog`처럼 다른 리소스를 요구하는 연결을 구분해 검증 API를 설계한다. (4) 상속으로 물려받은 참조·다른 팩/파일 참조의 테스트를 추가한다.

### 데이터 기반 규칙의 소유 경계

| 우선순위 | 코드에서 확인한 경계 | 고도화 방향과 완료 조건 |
|---|---|---|
| 검토 후 제외(자동 생성 의도) | [`CropCatalog::fillItemCatalog`](../Source/GameFramework/Kits/Genre/Simulation/Farming/CropCatalog.cpp)는 작물의 씨앗/생산물 ID에 대응하는 아이템이 없으면 기본 아이템 정의를 만든다. 이를 일반적인 미검증 참조로 분류하면 안 된다. | 자동 생성이 콘텐츠 계약인지 확인하고 그 계약을 유지한다. 기존 ID가 있으면 재사용하는 동작을 검사하며, 데이터 소유권 변경 없이는 이 동작을 오류로 바꾸지 않는다. |

**엄격도 계약(2026-11-08 정함 — S2 · T6):**

| 오류 종류 | 정책 | 보는 곳 |
|---|---|---|
| 같은 파일의 id 연결(필수) | 로드 · 쿠킹 실패 | 참조 표 `Content/engine/settings/datacatalogreferences.xml`(런타임 `XMLCatalogLoader` · 쿠커 `PackCooker` 가 같은 표). Schedules(장소 · 약속 · 아키타입 · NPC 11 줄) · Metroidvania(줍는 것 → 능력 · 부적)까지 올렸다 |
| 다른 팩 · 코드가 채울 수 있는 연결 | 경고만(`optional="true"`) | 같은 표 |
| 코드 등록 이름(스케줄 활동 · 몸 설정 이름) | 오류, 로드 실패 | 그 카탈로그 코드(`ScheduleCatalog::validateActivityNames`) — 표에 적을 수 없다 |
| 다른 파일 카탈로그로의 연결(무기 → 탄 · 아이템, 격투 기술 → 프레임) | 둘을 함께 쥐는 쪽이 시작할 때 보고 깨지면 서지 않는다 | `WeaponCatalog::checkProjectiles` · `findUnknownItems` 를 Shooter3DGame · MechArenaWorld 가 부른다. 쿠커는 보지 못한다(텍스트로 읽는 카탈로그 · 다른 폴더 조합이 있어 "같은 폴더 형제 파일" 규칙을 두지 않았다) |
| 원소 자기 칸의 오류(모르는 속성 · 범위 밖 · 시각 · 겹침) | 오류 · 경고와 그 원소 제외 | 각 카탈로그 읽기(`XMLFields`) — `ResourceDataSchemaTest` 가 경고도 실패로 잡는다 |

**남은 일:** 전체 팩의 잘못된 참조가 실제 배포 콘텐츠에 있는지는 데이터 일괄 검증을 돌리지 않아 판정하지 않았다. 잘못된 참조가 쿠킹/게임 시작을 통과하는 회귀 테스트를 더한다.

### ReflectionParser · 런타임 Reflection 고도화 후보 (코드 대조 2026-11-07)

Graphify는 관계 탐색에만 사용하고 파이프라인·캐시·호출 경로는 구현과 대조했다. 이미 있는 최적화를 새 과제로 중복 등록하지 않는다. 아래 성능 항목은 병목이 입증된 결함이 아니라 측정 후보이며, 실측 전에는 구조를 바꾸지 않는다.

| 우선순위 | 코드에서 확인한 내용 | 개선 방향과 완료 조건 |
|---|---|---|
| 조건부(파서 프로파일링) | [`SourceTokens::tokenizeCursor`](../Source/Programs/ReflectionParser/SourceTokens.cpp)는 각 프로퍼티 이름의 매크로 치환 여부, 반영 함수 매개변수의 기본값, 이벤트 매개변수 이름을 위해 libclang 토큰을 만들고 토큰·문자열 벡터를 구성한다. 파서가 남기는 실행 시간은 [`ReflectionParser.cpp`](../Source/Programs/ReflectionParser/ReflectionParser.cpp)의 전체 경과 시간 하나라 이 비용의 비중은 확인되지 않았다. | 큰 헤더 집합에서 토큰화 횟수·토큰 수·할당량과 전체 파싱 시간을 측정한다. 이 단계가 유의미할 때에만 토큰 저장소 재사용 또는 같은 선언 내 중복 토큰화 제거를 검토하고, 기본 인자·매크로 생성 이름·별칭 이벤트의 fixture 결과를 보존한다. |
| 유지(기존 구조 계약) | [`TypeRegistry::populateLookupCaches`](../Source/Engine/Reflection/TypeRegistry.cpp)는 등록 배치가 끝난 단일 스레드에서 상속 프로퍼티 목록, 이름 조회, 세이브게임·검증 여부를 미리 채운다. `StaticType()` 조회는 [`TypeLookupCache`](../Source/Engine/Reflection/TypeLookupCache.h)의 세대 검증 슬롯, 캐스트는 조상 테이블을 사용한다. 그러므로 매 조회가 잠금·맵 조회 또는 상속 사슬 순회라는 전제는 맞지 않는다. | 이 사전 계산을 유지한다. 새 캐시를 추가하거나 등록·언로드 경로를 바꿀 때는 생성·무효화 순서를 갱신하고 모듈 재등록 테스트를 함께 둔다. 일반 `findType`과 열거형 해시 역조회처럼 별도 조회의 비용은 실제 소비자·호출 빈도에서 병목이 확인된 경우에만 다룬다. |
| 테스트 정확성 | 파서 테스트는 내부 함수를 직접 호출하는 단위 테스트는 아니지만, [`Test/CMakeLists.txt`](../Test/CMakeLists.txt)는 8개 헤더 fixture를 실제 파서에 넣어 생성 코드·목록·오류를 대조하고, 알 수 없는 인자와 필수 인자 누락도 검사한다([`RunParserFixture.cmake`](../Test/ReflectionParser/RunParserFixture.cmake)). 런타임 쪽에는 `TestReflectionValidation` 등 단위 테스트가 있다. 현재 파서 성능 기준선이나 단계별 프로파일은 확인되지 않았다. | 기존 fixture를 파서 테스트 부재로 분류하지 않는다. 성능 최적화 전에 변경 없음(incremental skip)·변경된 단일 헤더·다중 헤더 배치·실패 폴백을 같은 입력에서 반복 측정하는 기준선을 만든다. CI의 변동성 있는 시간 문턱은 안정적인 기준과 실행 환경이 마련되기 전에는 추가하지 않는다. |

#### FUNCTION 리플렉션 활용과 호출 경계

- **이미 쓰이는 곳:** 생성된 `FunctionInfo`는 함수명·인자/기본값·반환 타입·invoker와 메타데이터를 보유한다. Inspector는 반영된 메서드의 인자를 기존 프로퍼티 위젯으로 편집하고 호출 결과를 표시하며, `CallInEditor` 인자 없는 메서드는 빠른 실행 버튼으로 보여 준다([`EditorPropertyGrid`](../Source/Editor/Panels/Inspector/EditorPropertyGrid.cpp)). 애니메이터·시퀀서·VFX·대사 컴포넌트의 재생/시험 동작, 대사 라인의 뷰포트 프리뷰([`EditorViewportPreview`](../Source/Editor/Common/Commands/Scene/EditorViewportPreview.cpp)), 반영 생성자 기반 인스턴스 생성 및 함수 API 문서화가 실제 활용 사례다.
- **기반은 있으나 연결이 드문 곳:** [`ReflectionInvoke`](../Source/Engine/Reflection/ReflectionInvoke.h)는 타입 변환/기본 인자 처리, 이름으로 호출, 텍스트 인자 호출, 이벤트를 함수에 연결하는 API를 제공한다. `callWithText` 는 원격 호출([`RemoteCall`](../Source/GameFramework/Kits/Feature/Network/NetClientServer/RemoteCall.h) — 정책은 그 머리 주석)이 쓰지만 `bindEventToFunction` 의 소비자는 Reflection 테스트뿐이며, 대사 외 범용 `EditorPreview` 소비자도 발견하지 못했다. 따라서 콘솔·비주얼 스크립팅·기믹에 이미 범용 배선이 연결됐다고 간주하지 않는다.
- **권장 확장 순서:** (`CallInEditor` 계약은 섰다 — 인스펙터는 CallInEditor 메서드만 기본으로 보이고, 호출은 주인 오브젝트 편집 하나로 감싸 Undo 한 단계 · 씬 dirty 를 남긴다, a02f95478d.) (1) 인자 없는 버튼에 그치지 않고 기존 타입 위젯과 `ReflectCallResult` 오류 표시를 재사용해 안전한 파라미터 동작을 제공한다. (2) 실사용이 확인된 반복 작업부터 검색 가능한 액션 목록/컨텍스트 메뉴에 연결한다. (3) 콘솔이나 비주얼 배선은 실제 수요가 생길 때 `callWithText`/`bindEventToFunction`를 명시적 허용 목록, 수명 관리, 핫 리로드 검증과 함께 소비한다. 키트 전용 동작은 해당 컴포넌트/키트가 있을 때만 노출한다.
- **완료 기준:** 호출 가능한 메서드는 인자 변환·기본값·오류·반환값·dirty/Undo 결과가 예측 가능해야 한다. 이벤트 구독은 소스/타깃 파괴와 모듈 재로드 후 안전하게 해제·재탐색된다. 현재 테스트의 형식·인자 오류 검증은 유지하고 Inspector 시나리오로 노출 및 실행 가능 범위를 회귀 확인한다.

### 비동기 · 병렬 처리 확장 후보 (코드 대조 2026-11-07)

Graphify로 시스템 간 관계를 찾고 실제 호출 경로와 동기화 경계를 확인했다. 아래 후보는 모두 프로파일링으로 비용을 확인한 뒤 진행한다. 비동기는 대기 시간을 숨기고 병렬 처리는 처리량을 늘리는 서로 다른 도구다. 이미 있는 `TaskManager`와 `engine::runParallel(ByCost)`를 우선 사용하고, 항목마다 무제한 태스크를 만들거나 프레임에서 즉시 기다리는 식의 적용은 피한다.

| 우선순위 | 코드에서 확인한 현재 경로 | 확장 방향과 완료 조건 |
|---|---|---|
| 조건부 높음(에디터 저장 멈춤) | [`SceneManager::writeActiveScene`](../Source/Engine/Scene/SceneManager.cpp)는 호출 중 [`Scene::serializeToDocument`](../Source/Engine/Scene/Scene.cpp)를 실행하고, 활성 오브젝트를 차례로 돌며 상태를 XML로 직렬화한 뒤 XML 또는 상속 차이 파일을 동기 저장한다. 큰 씬 저장은 호출 스레드를 붙잡을 수 있다. | 큰 씬에서 직렬화·델타 계산·파일 쓰기 시간을 나눠 측정한다. 직렬화가 문제면 게임 스레드에서 일관된 `SceneDocument` 스냅숏을 만든 뒤, 그 독립 값의 XML 생성·파일 기록만 워커로 넘긴다. 저장 완료/실패를 비동기로 알리고, 저장 성공 전에 씬의 source path·dirty 상태를 완료 처리하지 않으며, 저장 중 편집이 이어질 때 오래된 스냅숏을 최신 상태로 오인하지 않는 버전 검사를 둔다. `Scene`·오브젝트 매니저를 워커에서 직접 읽게 하지 않는다. |
| 조건부 높음(대형 씬 최초 진입) | [`SceneManager::loadSceneAsyncJob`](../Source/Engine/Scene/SceneManager.cpp)은 파싱과 씬 인스턴스화를 이미 워커에서 수행하며 `Scene.load.parse` · `Scene.load.instantiate` 시간을 따로 잰다. 다만 [`Scene::instantiateNodes`](../Source/Engine/Scene/Scene.cpp)는 엔티티를 한 루프에서 만들고 상태를 적용한 뒤 핸들 연결·부착을 마친다. 등록과 로드 후 처리는 별도 게임 스레드 경로로 나뉘어 프레임당 개수 제한도 있다. | 로그에서 `instantiate`가 우세할 때만 독립적인 상태 디코드·검사 등 순수한 전처리를 병렬화하고, 오브젝트 생성/프리팹 캐시/파일 ID 연결/부착과 등록은 현재 소유권 경계를 유지한다. 완료 순서가 달라도 저장 ID와 계층이 동일한지, 취소·실패 때 부분 씬이 모두 정리되는지, 등록 프레임 예산이 지켜지는지를 대형 fixture로 검증한다. 통째 엔티티 생성의 병렬화는 `GameObjectManager`와 공유 프리팹 상태의 스레드 안전성이 확인되기 전에는 하지 않는다. |
| 조건부 높음(쿠킹 빌드 시간) | [`SceneCooker::cookAllScenes`](../Source/Engine/Scene/SceneCooker.cpp)은 씬 파일을 정렬한 뒤 씬 쿠킹과 해당 프리로드 목록 쿠킹을 순서대로 처리한다. 씬 단위 결과는 서로 독립적으로 보이지만 [`CookStore::cookFile`](../Source/Engine/Asset/Cook/CookStore.h)은 공유 통계·요청 목록·해시 시간을 변경하고, 매 작업의 시작에서 [`AssetManager::clearAssetCaches`](../Source/Engine/Asset/AssetManager.cpp)를 호출한다. `CookJob` 입력 범위는 thread-local 이지만 이 사실만으로 전체 쿠커가 병렬 안전해지지는 않는다. | 실제 씬 쿠킹 시간이 빌드를 지배하는지 먼저 잰다. 병렬화가 필요하면 먼저 에셋 캐시를 작업별로 격리하고 CookStore의 기록/DDC·통계·요청 집합 갱신을 안전하게 분리하거나 결과를 한 스레드에서 병합한다. 그 뒤 씬 단위로 제한된 동시성 작업을 주고, 씬 출력 성공 뒤 프리로드 목록을 처리하는 의존성·실패 집계·결정적 산출물·요청되지 않은 출력 삭제를 테스트한다. 공유 캐시를 그대로 둔 채 씬 루프만 태스크로 감싸지 않는다. |
| 조건부(게임 스레드 에셋 지연) | [`AssetLoadOperation::startLoad`](../Source/Engine/Asset/AssetHandle.cpp)은 안전한 일반 요청을 `AssetManager::launchLoadWork`로 워커에 넘기고, asset streaming은 경로 찾기와 파일 읽기를 비동기로 한다. 반면 `AssetLoadStart::OnCallingThread` 또는 쿠킹 경로는 호출 스레드에서 읽고 완료한다. `AssetLoadProfiler`는 동기/비동기 구분과 IO·decode·upload 단계별 시간을 이미 모은다. | 실제 프레임 스톨 상위 에셋과 그 단계(IO, decode, upload)를 프로파일러로 찾는다. 워커에서 안전하고 호출자가 지연 로드를 허용하는 경로만 비동기로 바꾸며, 즉시 사용할 에셋은 준비 완료 계약과 로딩 UI를 함께 둔다. 쿠킹의 입력 추적상 호출 스레드 읽기가 필요한 경로 및 RHI 스레드 제약은 유지하고, 임의로 모두 비동기화하지 않는다. |
| 조건부(렌더 패스 기록 대기) | [`RenderGraph::executeParallel`](../Source/Engine/Renderer/Graph/RenderGraph.cpp)은 리소스 의존 그래프의 각 레벨 안에서 이미 커맨드 기록을 병렬화하고 레벨마다 합류·제출한다. 같은 레벨의 폭이 좁거나 패스 기록이 짧으면 워커 기상·합류 비용이 이득을 지울 수 있고, 레벨 경계를 없애면 GPU 리소스 순서가 깨질 수 있다. | 기존 `RT.Graph.executeParallel` 및 패스별 비용과 레벨 폭·유휴 시간을 함께 측정한다. 병목이 큰 독립 패스의 CPU 전처리로 확인될 때만 그 준비를 기록과 별개인 작업으로 앞당기거나 합법적인 독립성을 그래프에 표현한다. 패스의 커맨드 리스트 기록과 배리어·레벨 제출 순서는 계속 그래프 의존성의 소유로 둔다. |
| 유지(이미 폭넓게 적용) | 컴포넌트 틱은 오브젝트/배치 단위 fork-join 및 선행 단계, 변환 계층은 더티 루트별 병렬 갱신, 애니메이션은 비용 기반 평가, 프레임 시스템은 접근권 기반 lane, GPU 업로드·수목 배치·환경 식생도 독립 작업을 나눈다. 렌더 그래프와 셰이더 쿠킹은 독립 레코드/포맷을 병렬 처리하고 결과 합치기는 직렬화한다. PackWriter도 항목별 압축·CRC를 병렬로 한다. 틱의 비용 기반 분배 실험은 주석상 8개 게임 중 1개만 이득이고 7개는 같거나 느렸다. | 기존 병렬 경로를 기준선으로 삼고 시스템마다 새 풀/스레드를 추가하지 않는다. 특히 틱은 기본 분배 정책을 무조건 바꾸지 말고 게임별 측정 결과를 따른다. 병렬화 후보에는 작업 수·청크 크기·큐 우선순위·합류 시간과 단일 스레드 결과의 결정성을 함께 기록한다. |

**우선순위 제안:** 사용자가 체감하는 저장 멈춤은 씬 스냅숏을 분리한 비동기 저장을 먼저 실측하고, 처리량 확장은 씬 쿠킹을 다음으로 검토한다. 런타임 씬 생성과 렌더 기록은 이미 일부 비동기/병렬이므로 각 단계별 타이머·프레임 프로파일에서 실제 병목이 드러날 때만 병렬 폭을 늘린다. 현재 문서에 쓴 쿠킹·저장 확장 후보는 정확한 실측 전의 구조 검토 항목이며, 아직 병목으로 확정한 것은 아니다.

### 렌더 스레드와 업로드 전용 스레드 검토 (2026-11-07)

**Unreal과의 용어 구분:** Epic 문서가 설명하는 렌더 스레드 밖의 대표 스레드는 전용 업로드 스레드가 아니라 **RHI Thread**다. Render Thread가 플랫폼 중립 RHI 명령을 만들면 RHI Thread가 이를 플랫폼 API 명령으로 변환·실행하며, 프레임 사이에 작업을 겹치되 GPU 제출 순서는 보존한다([Threaded Rendering](https://dev.epicgames.com/documentation/en-us/unreal-engine/threaded-rendering-in-unreal-engine), [Parallel Rendering Overview](https://dev.epicgames.com/documentation/en-us/unreal-engine/parallel-rendering-overview-for-unreal-engine)). Unreal Texture Streaming은 보이는 정도와 메모리 예산으로 mip 상주를 관리하는 별도 시스템이다([Texture Streaming](https://dev.epicgames.com/documentation/en-us/unreal-engine/texture-streaming-in-unreal-engine)). 따라서 Unreal의 이 구조를 “GPU 업로드만 처리하는 스레드”를 추가하는 근거로 그대로 옮기지는 않는다.

| 구분 | 저장소에서 확인한 사실 | 판단 |
|---|---|---|
| 이미 분리된 렌더 실행 | [`RenderThread`](../Source/Engine/Renderer/RenderThread.h)는 게임 스레드에서 받은 패킷을 전용 스레드에서 실행한다. 렌더 그래프는 독립 패스의 커맨드 기록을 이미 병렬화하지만, 레벨 경계에서 합류하고 GPU 제출을 순서대로 진행한다([`RenderGraph::executeParallel`](../Source/Engine/Renderer/Graph/RenderGraph.cpp)). 현재 Unreal RHI Thread와 같은 별도 RHI 명령 소비 스레드는 확인되지 않았다. | RT CPU의 드라이버 호출·커맨드 제출이 병목이면 전용 업로드 스레드보다, RHI 명령 수집과 실행을 분리하는 RHI Thread형 구조가 비교 대상이다. 이는 모든 프레임 명령의 소유권·순서·자원 수명을 바꾸는 큰 변경이므로 프로파일에서 RT CPU 병목과 GT/RT 겹침 부족이 입증되어야 한다. |
| 메시 GPU 생성 | [`GPUUploadQueue::flush`](../Source/Engine/Graphics/Upload/GPUUploadQueue.cpp)는 이번 스냅샷에 보이는 메시의 RHI 생성을 `TaskManager::runParallel`로 나누지만 포크-조인으로 호출 스레드가 완료를 기다린다. 이는 전용 스레드가 아니며, 대기 구간은 `GT.GPUUpload.flush`로 계측된다. 완료 후 스냅샷을 내보내므로 같은 프레임 렌더에서 메시가 빠지는 것을 막는다([`RenderPacketSceneCollector::buildSnapshot`](../Source/Engine/Renderer/Frame/Packet/RenderPacketSceneCollector.cpp)). | `GT.GPUUpload.flush`가 실제 GT 임계 경로라면 먼저 워커 풀의 점유·작업시간과 메시 수를 측정한다. 대기를 없애려면 보이는 즉시 사용 가능한 메시를 보장하는 현재 계약을 바꾸어야 한다(예: 이전 프레임 선요청 또는 준비 전 메시를 잠시 제외). 완료 전에 RT로 넘기거나 임의의 새 스레드에서 같은 Mesh를 만들면 팝인·중복 생성·RHI 수명 경쟁을 일으킬 수 있다. |
| 텍스처 에셋 로드 | 텍스처 파일 읽기·디코드는 로드 작업에서 수행한다. 이후 [`TextureCache::completePendingLoads`](../Source/Engine/Graphics/Texture/TextureCache.h)는 렌더 스레드에서 장치 자원을 만들고 업로드를 제출하며, 제출 수를 프레임 예산으로 제한하고 이미 제출한 업로드는 뒤 프레임에서 티켓으로 폴링한다([`FrameRenderer::executePacket`](../Source/Engine/Renderer/Frame/FrameRenderer.cpp), [`Texture2D::finishUpload`](../Source/Engine/Graphics/Texture/Texture2D.cpp)). 블로킹 acquire·결정적 테스트 경로는 별도로 끝날 때까지 기다린다. 로드 · 업로드 단계 계측(`AssetLoadScope` 의 `Texture` · `TextureUpload`, `RT.TextureUpload.pump` · 제출 · 폴링 · 대기 수)은 들어갔고 값은 S8 측정 표가 채운다. | 비동기 IO→디코드→제출→폴링 파이프라인은 이미 있어 “모든 텍스처를 새 스레드로 옮기기”는 중복이다. S8 값으로 RT의 staging 복사나 API 호출이 실제 병목인지 확인한다. OpenGL 컨텍스트 점유와 DX11 즉시 컨텍스트 잠금은 예외·직렬화 경로로 남겨야 한다. |
| GPU 전송 실행 | 업로드 API는 요청 스레드가 스테이징에 데이터를 복사해 전송을 제출하고, 기다리지 않는 경로는 티켓으로 완료를 확인한다. DX12는 전송 전용 펜스를 쓰지만 제출된 복사 리스트는 디바이스의 `_commandQueue`로 실행한다. Vulkan도 전송 커맨드 버퍼를 만들지만 `vkQueueSubmit` 대상은 `_graphicsQueue`다. 따라서 현재 코드에서 전용 CPU 업로드 스레드나 별도 GPU copy queue가 일반 경로로 동작한다고 볼 근거는 없다. | CPU 스테이징 준비와 GPU 전송은 다른 병목이다. CPU 제출을 다른 스레드로 옮겨도 GPU 복사 시간·대역폭은 줄지 않으며, 별도 GPU copy queue를 추가하려면 백엔드별 큐 선택·소유권 이전·동기화·자원 상태 전이와 fallback을 별도로 구현해야 한다. GPU copy/graphics 중첩이 필요한 측정 근거가 없으면 그 작업은 보류한다. |

**결론:** 지금 당장 전용 업로드 스레드를 추가할 근거는 없다. 이미 공유 워커 풀을 이용한 메시 생성과 비동기 텍스처 전송이 있고, 문제 후보는 메시 생성에서 GT가 기다리는 비용 또는 텍스처 제출 중 RT의 CPU 비용으로 서로 다르다. A/B에서는 `gv_gpuUploadQueue`를 켠/끈 경우의 GT/RT 프레임 시간, 메시 생성 수·대기 시간, 텍스처 `IO/Decode/Upload` 비용·대기 텍스처 수를 DX12·Vulkan과 OpenGL에서 비교한다. RT의 RHI 실행이 실제 병목이면 업로드 전용 스레드가 아니라 프레임 명령 순서와 CPU 소유권을 보존하는 RHI Thread형 설계의 작은 프로토타입을 별도 검토한다. 측정 전에는 성능 이득으로 확정하지 않는다.

### 에디터 UI 스레드 분리 검토 (2026-11-07)

Graphify로 호출 관계를 좁힌 뒤 앱 루프, 에디터 ABI, ImGui 플랫폼 백엔드, 뷰 렌더 타깃의 실제 소유권을 코드에서 확인했다. 렌더 스레드 분리는 이미 되어 있지만 UI 프레임 전체를 메인 스레드 밖으로 옮길 수 있는 구조는 아니다.

| 우선순위 | 코드에서 확인한 현재 경계 | 개선 방향과 완료 조건 |
|---|---|---|
| 유지(기존 UI·렌더 병렬화) | [`App::runFrame`](../Source/App/App.cpp)은 게임 모듈 갱신 뒤 메인 스레드에서 [`EditorModuleManager::updateEditorUI`](../Source/App/EditorModuleManager.cpp)를 호출하고, 그 결과로 씬 틱 여부·뷰 렌더 타깃을 확정한 뒤 `Engine::update`를 호출한다. [`IEditor`](../Source/Editor/IEditor.h)의 계약도 `updateUI`는 메인 스레드, `prepareRender`·`render`·`postPresent`는 렌더 스레드로 나눈다. UI가 복사한 [`EditorDrawDataSnapshot`](../Source/Editor/Common/Backend/EditorDrawDataSnapshot.h)은 [`DrawSnapshotHandoff`](../Source/Editor/Common/Backend/DrawSnapshotHandoff.h)를 통해 렌더 스레드가 이전 프레임을 그리는 동안 다음 UI 프레임을 만들게 한다. | 이 UI→렌더 스냅숏 경계는 유지한다. 렌더 스레드가 살아 있는 ImGui 상태를 읽지 않는 현재 보장을 깨지 않는다. |
| 차단(입력·플랫폼 소유권) | [`App::onWindowMessage`](../Source/App/App.cpp)는 OS 이벤트를 동기적으로 에디터에 먼저 전달하고, `processEvent`가 같은 ImGui IO에 입력을 넣은 뒤 반환한 포착 결과로 게임 입력 전달 여부를 정한다([`ImGuiEditor::processEvent`](../Source/Editor/ImGuiEditor.cpp)). `NewFrame`, `Render`, `UpdatePlatformWindows`도 그 컨텍스트와 플랫폼 백엔드를 사용한다. Win32 백엔드는 WndProc 이벤트를 직접 처리하고, X11 백엔드는 전달된 `XEvent`를 즉시 읽는다. | UI 스레드로 옮긴다면 ImGui 컨텍스트와 모든 플랫폼 호출을 한 소유 스레드에 고정하고, OS 이벤트는 순서·프레임 ID가 있는 큐로 전달한다. 입력 포착 응답이 비동기일 때 게임과 에디터 중 누가 키를 받는지, 자동화의 가상 입력과 동기 재생을 어떻게 보존할지 먼저 계약·테스트한다. |
| 차단(씬·엔진 상태 공유) | 프레임 단계는 패널 그리기뿐 아니라 명령·씬 세션·에셋 리로드·제목 갱신을 처리한다([`EditorFrameSteps.cpp`](../Source/Editor/EditorFrameSteps.cpp)). 예를 들어 [`HierarchyPanel`](../Source/Editor/Panels/Scene/HierarchyPanel.cpp)은 활성 `GameObjectManager`를 읽고 선택·재부모화 명령을 실행한다. 반면 [`EditorRenderViews`](../Source/Editor/Common/Workspace/EditorRenderViews.h)는 UI 프레임에서 뷰 상태를 기록하고, 크기 변경 시 `TextureCache`에 렌더 타깃을 요청·교체한다. 프레임 끝에 확정하는 씬 틱/뷰 출력과 원시 `CameraComponent*`도 엔진이 소비한다. | UI와 시뮬레이션을 겹치려면 라이브 `Scene`·오브젝트·컴포넌트·RHI 자원을 양쪽에서 읽고 쓰게 하지 않는다. 메인 스레드가 만든 불변 에디터 입력 스냅숏을 UI에 주고, 편집 명령·선택 변경·뷰 요청을 식별자 기반 출력으로 돌려받아 메인 스레드에서 검증·적용한다. 카메라와 엔진 자원은 원시 포인터를 넘기지 말고 소유 스레드에서 핸들로 다시 찾는다. 프레임 결과가 필요한 `Engine::update`와 Play/Pause/Step 동작은 결정적 순서를 유지한다. |
| 조건부(실측 뒤 UI 계산 분리) | [`ImGuiEditor::updateUI`](../Source/Editor/ImGuiEditor.cpp)에는 `GT.Editor.frameSteps`, 단계별 `GT.Editor.step.*`, 스냅숏 대기 `GT.Editor.waitDrawSnapshot`·게시 구간이 있고, 패널별 시간은 `-gv_editorPanelTimes`로 잴 수 있다. 파일 IO·색인 등은 이미 [`EditorBackgroundTask`](../Source/Editor/Common/Task/EditorBackgroundTask.h) 규약으로 TaskManager 워커에서 계산하고 UI 스레드가 결과를 가져온다. | 먼저 대표 대형 씬·패널에서 UI CPU 시간과 패널별 시간, 스냅숏 슬롯/게시 대기를 따로 측정한다. 대기가 크면 렌더 프레임 지연 문제로 분류하고 UI 스레드 이동 근거로 삼지 않는다. 패널 계산이 병목이면 불변 입력→세대가 붙은 결과를 돌려주는 기존 백그라운드 잡 패턴으로 순수 계산·파일 작업부터 분리하고, UI 위젯 생성과 상태 변경은 소유 스레드에 둔다. |
| 큰 구조 변경(전용 UI 스레드) | 현재 UI 업데이트는 게임 갱신 뒤, 엔진 업데이트 앞에서 동기적으로 한 번 완료되어야 뷰 타깃·틱 상태를 그 프레임에 사용한다. 모듈 리로드·RHI 교체·종료는 에디터와 확장 DLL의 콜백 및 렌더 스냅숏 수명을 함께 다룬다. | UI 전체를 옮기는 것은 가능하더라도 단순히 `updateUI()` 호출을 새 스레드로 감싸는 변경은 아니다. 프레임 입력/출력 큐, UI 컨텍스트·플랫폼 백엔드의 단일 소유권, 씬 명령 커밋, 출력 프레임 지연 계약, 핫리로드·종료 전 UI 작업 drain을 함께 설계해야 한다. 위 프로파일에서 UI 생성이 실제 메인 스레드 임계 경로임을 확인한 경우에만 작은 프로토타입을 만들고, 입력·Play/Pause/Step·뷰 크기 변경·창 닫기·확장 리로드·자동화 결정성 테스트를 통과시킨다. |

**결론과 진행 순서:** 에디터 UI를 별도 스레드로 분리하는 것은 이론상 가능하지만 현재 구조에서는 안전한 즉시 최적화가 아니다. 렌더와의 병렬화는 이미 draw-data 스냅숏으로 이루어지고, `updateUI`를 그대로 옮기면 동기 이벤트 포착·라이브 씬 편집·뷰 렌더 타깃 갱신·프레임 출력의 소유권이 충돌한다. 우선 기존 계측으로 UI 계산과 렌더 대기를 구분하고, 비용이 확인된 순수 계산/IO만 기존 백그라운드 작업 경로로 옮긴다. 전용 UI 스레드는 불변 입력/출력 경계와 명시적인 프레임 지연이 필요하다는 점을 감수할 만큼의 메인 스레드 병목이 입증될 때만 검토한다. 이 문제는 UI 함수 하나가 아니라 에디터가 라이브 엔진 상태를 직접 편집하는 구조에서 비롯되므로, 같은 공유 경계는 새 비동기 패널 작업에도 적용해 점검한다.

**갱신(사용자 2026-11-08 — 렌더가 아니라 게임(메인) 스레드와의 분리가 목표):** 게임이 멈추거나 긴 프레임에 걸려도 에디터가 반응하고, 에디터 UI 비용이 게임 프레임을 늦추지 않게 한다(언리얼 Slate · 유니티 · Godot 에디터는 게임 스레드에서 돈다 — 그보다 앞서는 방향). 측정을 기다리지 않고 선결 구조부터 넣는다:
1. **에디터 MVVM**(계약 정본은 [EditorUI.md](agents/EditorUI.md) "MVVM" 절, 남은 View 이전은 NEXT) — ViewModel 의 `gather`(메인 스레드, 동기화 지점)가 값만 담은 ViewState 를 만들고, View 는 그것만 읽으며, 사용자 의도는 명령으로 큐에 쌓여 메인 스레드가 프레임 시작에 실행한다(문서 데이터는 커맨드 스택). View 가 모델 · 씬 헤더를 include 하면 컴파일이 멈춘다(모델 층의 ImGui 금지 `#error` 를 반대 방향으로).
2. **입력 큐** — 창과 ImGui 는 UI 스레드가 소유하고, 게임 뷰 입력은 프레임 번호가 붙은 큐로 넘긴다(게임 입력 한 프레임 지연, 자동화 가상 입력도 같은 큐).
3. **출력 계약** — 뷰 크기 · Play/Pause/Step 도 명령으로 넘기고 한 프레임 지연을 계약으로 적는다. 그 뒤 ImGui 프레임만 UI 스레드로 옮긴다.

### 병렬 · 비동기 전수 조사 (2026-11-08)

**기준(사용자 2026-11-08):** 지금 게임은 완성된 내용이 적어 측정 숫자로 순서를 정하지 않는다. 기능의 연쇄를 보고 ① 대상 수 N 에 비례하고 대상끼리 독립인 일 ② 앞단이 뒤를 막는 사슬 ③ N² 모양을 미리 병렬 · 비동기 설계로 바꾼다. 확인은 목표 규모 부하 시나리오(`ctest -L load` — 에이전트 1 천 · 지역 수백)로 정확성 · 확장성을 보고, 숫자는 회귀 감시로 남긴다(언리얼 Mass 프로세서 · 유니티 DOTS 잡 + 의존과 같은 방향).

| 연쇄 | 지금 | 바꿀 모양 |
|---|---|---|
| AI 이동: 의도 → 경로 → 군중 → 컨트롤러 → 물리 | 비동기 경로 요청 줄(`PathRequestQueue<TSolver>`) · 군중 프레임 일(`SW_SCENE_SERVICE_WITH_FRAME_JOB`), 동기로 남긴 곳은 까닭을 주석에 | 남은 것: 한 종류의 군중 안을 나누기(Detour 군중은 한 덩어리 — 종류 하나에 에이전트 수천이면 언리얼 Mass 처럼 군중을 지역으로 쪼갠다), 측정은 `load/navcrowd` |
| 에디터 UI | 게임 스레드 | MVVM([EditorUI.md](agents/EditorUI.md)) → 입력 큐 → 출력 계약(위 "에디터 UI 스레드 분리 검토") |

**씬 서비스 · 자체 스레드 · 폴링 판정(2026-11-08).** 도구 고르는 기준은 [CppStyle](agents/CppStyle.md) "Parallel and async — which tool where" 다.

| 자리 | 판정 |
|---|---|
| `MeshLODSystem` · `TextureStreamingSystem` · `SceneAudio`(에미터 자리) | `runParallelByCost` — 칸마다 고르고 공유 쓰기(`setLODIndex` · 머티리얼 맵 · 오디오 엔진 명령)는 게임 스레드가 목록 순서로. LOD → 스트리밍 사슬은 데이터 의존이 아니다(둘 다 뷰만 읽는다) — 스테이지를 두지 않는다. 오디오 가림 레이캐스트는 물리 씬 한 스레드 계약이라 직렬(프레임에 16 개 상한) |
| `FoliageComponent` 레이어 · `FoliageTerrainFollower` | 레이어는 스테이지 + 태스크 대신 병렬 for. 따라가기는 바뀐 폴리지만 다시 짓고 그 안이 이미 병렬(레이어 · 후보 판정)이라 바깥은 그대로 |
| `SceneVFXProvider` | 그대로 — 렌더러 싱크에 모아 넣는 일이고 입자 진행은 컴포넌트 틱이다 |
| `NetObjectTable` · `SceneNetLink` | 프레임 일이 없다(찾기 표 · 연결 고리) |
| 시간을 재야 하는 잠자기(그대로) | 프레임 · 틱 맞춤(`FramePacer` · `App` 멈춤 틱 · `ServerApp` 둘 · `LoadBotMain`), GPU 펜스 · 장치(`RHITransferUtil` · `OpenGLRHIDevice` · `WebGPURHIDevice` · `RHIBackendSwitcher` TDR 흉내), 소켓 · 망(`DebugChannel` 둘 · `NetTransport` 둘 · `NetEndpoint` · `IocpStreamTransport` · `BlockingHTTPClient` · `RespEphemeralStore` · `AccountTicketConnect` · `DelayedStreamTransport`), 프로세스 · 파이프(`EditorNetPlaySession` 끝남 기다리기 · `updateUntil` · `TestRunner` 둘 · `AutomationPerBackend`), 감시 · 크래시(`AutomationHangWatch` 둘 · `CrashHandler` · `PosixCrashHandler` · `WindowsCrashHandler` · `FrameStall`), 재시도 · 물러서기(`FileUtil` · `ModuleShadowCopy` · `LockFreeObjectPool` · `PlatformFileUtil` 느린 IO 흉내 · `WindowsStandardInputReader`), 종료 한 번의 완료 거두기(`ServiceStoreUtil` · `LocalStoreChannel` — 완료를 부르는 쪽이 펌프해 넘기는 계약이다) |
| 스테이지 · 연쇄 자리 | `RenderGraph` 레벨 스테이지는 High 줄이 필요해 그대로(파일 주석). `AudioClip` 디코드는 에셋 로드 스테이지, `RenderThread` 스크린샷 쓰기는 완료를 우편함 콜백으로 알려 이미 연쇄다. `AsyncFileIO` 전달은 자기 스테이지 |

### 데이터 구동 구조 후보 판별 (2026-11-08)

**기준:** 숫자 리터럴(위 "데이터 기반 값" 절)이 아니라 **내용 · 정책을 코드가 정하는 모양**을, 앞으로 내용이 늘어날 연쇄 기준으로 고른다. 형식 · 프로토콜 · 수학 표(RHI 형식, DDS, 오디오 DSP 계수, 다운믹스 행렬, HTTP/2)는 코드에 둔다 — 바뀌는 까닭이 내용이 아니라 명세다. 이름으로 원소를 고르는 XML 읽기(카탈로그 리더)도 데이터를 읽는 쪽이라 그대로다.

| 우선 | 연쇄 · 자리 | 지금 | 데이터로 옮길 모양 |
|---|---|---|---|
| 5 | **게임 규칙 수치** — 게임 모드 컴포넌트의 상태 `switch`(`WildWestGameModeComponent` 등)와 키트의 `-1` 613 · 실수 상수 | 일부는 PROPERTY 로 이미 편집된다 | 규칙 카탈로그 · PROPERTY 로(위 "데이터 기반 값" 절의 순서) |

**판단(9 차, 옮긴 것 · 코드에 두는 것):**

- 1 게임 AI — `AIControllerComponent` 가 트리 칸(`_behaviorTree`)과 실행기를 쥐고 `think` = 감각(`updateBlackboard`) → 트리 한 틱이다. 잎 어휘는 `ai.*`(`AIControllerActions.h`) · `blackboard.add` · `blackboard.copy`, 조건 `aiHasDestination`. 옮긴 것: ArenaEnemyAI(grunt · caster) · ArenaAutoBattleAI · HorseFollowAI · ShooterEnemyAIController · FarmAutoFarmerAI · VoxelAutoPlayControllerComponent · ShooterAutoAimControllerComponent(`game/*/data/ai/*.bt.xml`, 자동 플레이 조종자는 `prefabs/autoplay_ai.prefab.xml`). `think` 는 덮어쓸 수 없다 — 파생은 감각(`updateBlackboard`)만 내고 판단은 트리 데이터다. 언리얼 `AAIController` + `RunBehaviorTree`, 감각은 BT 서비스 · AIPerception 자리.
  코드에 두는 것: `KartAI`(연속 조향 · 레이싱 라인 수학 — 언리얼도 차량 경로 추종은 코드), `SRPGCommanderAI`(효용 점수 탐색 — 가중치는 이미 설정 구조체, 갈래는 "칠 수 있으면 친다, 아니면 다가간다" 둘뿐이라 트리 이득이 없다), `ShooterEnemyComponent`(폰 몸의 행동 단계 · 복제되는 상태 — AI 판단이 아니다), `SkirmishUnitComponent` · `FarmVillagerComponent`(판의 상태를 따라 그리는 모습 — 마을 사람의 일과는 이미 스케줄 데이터).
- 2 프리셋 · 규칙 표 — 네 곳 모두 이미 데이터이거나 명세 표다. `FractureSettings` 의 switch 는 원본 해시에 섞는 철자 표(바꾸면 모든 파쇄 모델을 다시 임포트 — 형식 표), `TextureImportConfig` 규칙은 이미 JSON(남은 표는 DXGI 번호 — 명세), `Autosave` 정책은 이미 `AutosaveSettings` 자산(남은 switch 는 까닭의 합치기 우선순위 — 종료 · 직접 부름이 이겨야 하는 의미 규칙), `InputMap` 기본 바인딩은 이미 `*.input.xml`.
- 3 문화권 — 복수형 · 서수 규칙은 CLDR 규칙 글(`PluralRuleSet`, UTS #35 문법), 숫자 체계(`<numberingSystems>`)와 통화(`<currencies>`)는 문화권 표의 칸이다. C++ 규칙 함수 표 · `NumeralSystem` · 통화 표를 지웠다. 시간대는 이미 `timezones.xml`. 남은 것: 문화권별 통화 기호(CLDR 의 문화권별 currencies — 필요할 때 칸을 더한다), 날짜 패턴 기호는 형식이라 코드.
- 4 에디터 표시 — 뷰포트 입력 오버레이 색(`InputOverlay.*`)과 강조색 스와치(`AccentSwatch.*`)를 테마 토큰으로. 뷰포트 오버레이 바 표는 바마다 내용 코드가 붙은 구조라 코드, UIPreview 기기 목록은 이미 `uiresolutions.xml`.

### 에디터 기능 · UX 고도화 후보 (코드 · Graphify · 공식 문서 대조 2026-11-07)

코드와 최신 Graphify 그래프에서 실제 패널·서비스·탐색 경로를 확인하고, 상용 엔진 문서는 기능의 존재 여부가 아니라 반복 작업 UX의 비교 기준으로만 사용했다. 결론은 “기능이 거의 없다”가 아니다. `Source/Editor/Panels` 에는 씬·인스펙터·콘텐츠 브라우저 외에도 애니메이션·대화·오디오·머티리얼·UI·렌더·프로파일러·패키징·멀티플레이 도구가 있다. 사용자 단축키, 명령 팔레트, 에셋/오브젝트 Quick Launcher, 다중 선택 인스펙터, Undo/Redo 기록과 명명 레이아웃, Map Check, 에셋 참조 찾기와 파일 이동 시 경로 자동 수정, 비동기 임포트와 진행 상태, 자체 시나리오 테스트도 이미 구현되어 있다. 아래는 이 기반을 중복 구현하지 않고 작업 흐름의 완성도를 높일 후보만 남긴다.

| 우선순위 | 코드에서 확인한 현재 UX와 비교 근거 | 개선 방향과 완료 조건 |
|---|---|---|
| 중간(첫 사용·발견성) | 패널 등록과 메뉴/팔레트 진입점은 있지만 `Source/Editor` 패널 등록 목록에서 시작 안내·작업별 시작 화면이나 프로젝트 상태를 안내하는 Welcome 패널은 확인되지 않았다. Godot은 Project Manager에서 프로젝트 생성·가져오기·최근 프로젝트·환경 설정을 한곳에서 제공한다([Godot Project Manager](https://docs.godotengine.org/en/stable/tutorials/editor/project_manager.html)). 이 조사는 `Source/Editor`에 한정했으므로 App 바깥의 프로젝트 시작 흐름은 별도 확인 대상이다. | 먼저 새 프로젝트/빈 씬, 콘텐츠 없는 폴더, 첫 임포트, Map Check 오류, 첫 패키징처럼 현재 제품이 실제 지원하는 작업의 빈 상태에 다음 행동을 명확히 연결한다. 필요성이 확인되면 Welcome/Start 화면에 최근 프로젝트·최근 씬·복구 가능한 작업·최근 작업·문서 링크를 모은다. 기존 패널 검색·단축키·레이아웃을 대체하는 별도 허브를 만들지 말고, 처음 사용 및 재방문 시나리오로 발견 시간을 측정한다. |
| 높음(데이터의 시각적 저작 확장) | 공통 재료: [`EditorPreviewPanel`](../Source/Editor/Common/GUI/Panel/EditorPreviewPanel.h)의 격리 미리보기 씬/선택/Undo, [`EditorPreviewGizmo`](../Source/Editor/Viewport/EditorPreviewGizmo.h)·[`EditorViewportGizmo`](../Source/Editor/Viewport/EditorViewportGizmo.h), [`EditorTransaction`](../Source/Editor/Common/Workspace/EditorTransaction.h), [`EditorNodeGraph`](../Source/Editor/Common/Widgets/EditorNodeGraph.h). | 범용 시각 편집 프레임워크나 “모든 것을 노드 그래프로” 만들지 않는다. 적용 대상을 분류해 확장한다: 위치·부착은 뷰포트/기즈모(카메라 리그·장착점·스폰/상호작용 지점), 계층·범위는 트리와 오버레이(리그·LOD/본 영향 범위), 시간은 타임라인(시퀀서·애니메이션), 흐름은 노드 그래프(행동·대화·기믹), 조합·카탈로그는 검색 가능한 표/상세와 미리보기(외형·Ability·장비). 각 화면은 런타임이 소비하는 기존 에셋을 직접 편집하고, 유효 데이터와 미리보기 결과를 구별하며, 검증 오류를 해당 요소에 표시한다. 반복해서 같은 선택기·기즈모·검증/Undo 결합이 나타날 때만 작은 공용 위젯/헬퍼를 추출한다. 실제 제작 빈도·오류율이 낮은 데이터는 Inspector/표로 충분할 수 있다. |

**이미 있는 기능의 확장 우선순위:** Undo/Redo(`CommandStack` · History 패널), 명명 레이아웃, 인스펙터 다중 선택, 에셋 생성 템플릿 · 참조 찾기, 저장 검색 · 컬렉션, 자동 저장 · 복구, Map Check 고침, 단축키 재지정은 이미 있으니 새로 만들지 않는다. 개선은 Undo 트랜잭션 누락·취소 불가 편집을 실제 시나리오로 찾아 보강하는 쪽이다.

**타일맵 · 스프라이트 시트:** 자동 투명 영역 탐색, 회전/패킹된 아틀라스는 실제 콘텐츠가 요구할 때 런타임 포맷과 함께 설계한다 — UI 만 더해 런타임과 다른 UV 데이터를 만들지 않는다.

**시각 저작 적용 시 지켜야 할 경계와 검증**

- **표현은 데이터 의미를 따른다.** 공간 관계에는 뷰포트·기즈모, 계층 영향에는 트리·색 오버레이, 시간에는 타임라인, 분기/연결에는 그래프, 목록성 설정에는 검색 가능한 표·인스펙터를 쓴다. 한 화면에 모든 편집 패턴을 통합하지 않는다.
- **저작 데이터와 런타임 관측을 분리한다.** 편집 UI는 저장될 에셋을 변경하고, 프리뷰는 그 에셋을 격리된 씬/플레이어에서 소비한다. DevTools 런타임 디버거는 실제 실행 상태를 읽기 전용으로 보여 준다. 프리뷰에서만 성공하고 실제 런타임은 다른 규칙으로 해석하는 별도 사본을 만들지 않는다.
- **선택·변경·저장 수명주기를 통일한다.** 선택 대상과 활성 문서가 바뀔 때 이전 드래그를 종료하고, 빠른 드래그는 하나의 Undo 단계로 묶으며, 패널 닫기/재로드 전 dirty 확인을 보장한다. 본/소켓/에셋 참조가 사라지거나 스켈레톤이 바뀌면 기존 값을 조용히 폐기하지 않고 누락·재연결 상태를 표시한다.
- **기즈모를 데이터 편집과 혼동하지 않는다.** 기존 씬 기즈모는 씬 오브젝트를 고친다. 소켓·본 마스크 같은 에셋 값의 직접 조작은 데이터 로컬 공간 변환, 드래그 시작값/끝값, Undo coalescing, 문서 dirty/저장 경로를 해당 모델에 연결해야 한다. 프리뷰 오브젝트만 움직여 원본 에셋이 바뀐 것처럼 보이게 하거나, 원본을 건드리지 않는 preview-only 동작을 저장된 편집으로 표시하지 않는다.
- **검증은 보기 좋은 색칠로 대체되지 않는다.** 마스크는 실제 스켈레톤에 없는 본 이름, 상속과 개별 덮어쓰기의 최종 가중치, 0/1 경계 및 저장 후 재로드를 확인한다. 소켓은 부모 본/종류/변환의 허용 범위, 대칭·후보 연결, hot reload 뒤 같은 배치를 확인한다. 기즈모 조작 후 XML 재로드 결과와 런타임 적용 결과가 모델의 미리보기와 일치해야 한다.
- **첫 두 기능을 공통화의 실측 사례로 쓴다.** 애니메이션 마스크와 소켓 편집을 구현·캡처해 뼈 선택, 픽킹, 뷰포트 상호작용, Undo, 오류 표시에 실제로 공통 코드가 있는지 확인한다. 공통점이 충분하지 않으면 도메인별 UI를 유지한다. 완료는 XML 왕복/모델 테스트, 서로 다른 스켈레톤에서의 마스크 유효성, 소켓을 여러 포즈에서 조정한 뒤 재로드한 배치 일치, `editor/uiaudit.scenario.xml`과 `editor/uicapture.scenario.xml`의 화면 검토로 판정한다.

**볼륨 안개:** 남은 것: 주요 광원과 적용 범위 표시, 여러 해상도 · 품질에서 GPU 비용 측정(`RT.Fog.Volume`, S8 범위). `FogComponent` 의 씬 값이 정본이다(새 저작 형식을 만들지 않는다).

**비교 범위와 제외:** 상용 엔진의 모든 기능을 패리티 목록으로 복제하지 않는다. 코드 에디터/디버거, 마켓플레이스, 클라우드 협업, 원격 빌드, AI 검색은 제품의 주 사용자와 지원 비용이 확인되지 않아 이번 우선순위에서 제외한다. 특히 멀티 유저 편집·외부 서비스·유료 공급자 연결은 비용/운영 책임을 먼저 정한 뒤 별도 판단한다. 이 표는 UI 구현의 완전한 육안 감사가 아니라 코드와 등록된 기능 표면의 구조 검토다. 다음 UI 변경은 프로젝트 규칙 [`EditorUI`](agents/EditorUI.md)의 토큰·접근성·캡처 시나리오 기준으로 실제 화면을 확인한다.

### 키트별 무료 상업용 리소스 계획 (CC0 기준, 2026-11-07)

`Source/GameFramework/Kits/Genre`의 장르 키트와 `Feature` 키트에 필요한 데모·시나리오 리소스를 대조했다. 범용 에셋을 모든 키트에 무차별 복사하기보다, 표의 하위 키트가 실제로 보여 주는 대표 동작에 필요한 최소한의 샘플을 둔다. 이는 에셋 도입 제안이지, 특정 팩을 이미 내려받았거나 모든 하위 키트가 완성됐다는 뜻은 아니다.

| 키트 | 포함된 하위 키트 | 데모에 필요한 리소스 | 무료 출처 후보(CC0만) |
|---|---|---|---|
| Genre/Action | ActionAdventure, ActionCombat, ActionPlatformer, BattleRoyale, Fighting, MechArena, Metroidvania | 플레이어/적 캐릭터와 리그·기본 이동/공격 애니메이션, 무기·투사체·피격 효과, 플랫폼/전투 아레나 조각, HUD 아이콘·타격음 | 기존 확인분인 KayKit Adventurers/Skeletons, Kenney Blaster Kit·Starter Kit FPS·Impact Sounds, `common/credits.md`에 기록된 UAL·Sci-Fi 묶음부터 재사용. 새 캐릭터/클립은 묶음에 든 라이선스 원문을 확인한 CC0 팩만 추가 |
| Genre/Casual | CardGame, KartRacing, PartyArena, Rhythm | 카드 앞·뒷면과 토큰, 카트/트랙/장애물, 여러 플레이어를 구분하는 캐릭터와 아레나 표식, 박자에 맞는 2D 그래픽·UI·효과음/음악 | Kenney의 CC0 게임/오디오 팩과 기존 CC0 음악 목록(`common/audio.sources.json`); 트랙·소품은 CC0 모델을 쓰거나 데이터 기반 블록아웃 리소스로 구성 |
| Genre/Horror | AsymmetricHorror, CoopScavenger, GhostHunt, SurvivalHorror | 어두운 실내·시설·야외 환경 조각, 손전등/수집품/단서, 플레이어·위협 캐릭터와 애니메이션, 공간감 있는 환경음·경고음 | Poly Haven 및 ambientCG의 CC0 표면 재질/HDRI, 검증된 Kenney 효과음. 캐릭터·소품은 상표/실존 인물/유명 IP를 연상시키지 않는 CC0 원본만 사용 |
| Genre/RPG | ClassicJRPG, MonsterCollector, MonsterHuntRPG, OpenWorldWestern | 파티 캐릭터·몬스터·무기/장비·리그/클립, 마을/던전/야외 조각, 아이템·퀘스트 아이콘, 탐험/전투 음악·효과음 | KayKit Adventurers/Skeletons/Dungeon, Kenney RPG Audio·Nature Kit, 기존 CC0 OpenGameArt 음악. 몬스터/서부 테마는 실제 게임 식별성을 위해 조합·팔레트·이름을 새로 만들고 타 IP 표현을 복제하지 않음 |
| Genre/Simulation | CreatureLife, Farming, RestaurantSim, ThemePark | 작물 성장 단계·도구·동물/손님, 가구·음식·상점·놀이기구와 모듈식 환경, 상호작용 아이콘·상태 UI, 부드러운 환경/작업 효과음 | 이미 사용 중인 Kenney Nature Kit, Quaternius/KayKit의 해당 라이선스 확인 자산, Poly Haven·ambientCG 표면 재질, Kenney UI/Impact 오디오. 엔진에서 새로 만든 데이터·표식은 외부 원본과 구분 |
| Genre/Strategy | CityBuilder, RealTimeStrategy, SideScrollConquest, TacticsSRPG | 식별 가능한 보병/차량/건물 세트, 타일·지형·장애물, 팀/점령/사거리 표시, 전술 UI 아이콘 및 명확한 피드백 사운드 | Kenney CC0 2D/UI·게임 에셋, Poly Haven·ambientCG CC0 표면, 기존 CC0 효과음. 같은 세트 안에서 카메라 거리·실루엣·색 대비가 읽히는지 확인하고 저작권 있는 군장/국기·상표는 배제 |
| Feature/Network | NetClientServer, NetDestruction, NetLockstep, NetMMO, NetPairedAnimation, NetSimulation, NetTurnRelay | 프로토콜 결과를 눈으로 확인할 작은 테스트 맵, 식별 가능한 동기화 오브젝트/캐릭터, 파괴 전후 메시, 네트워크 상태용 UI 표시. 핵심은 이미지보다 재현 가능한 입력·시드·예상 상태 fixture | 장르 데모의 CC0 리소스를 재사용하고, 네트워크 자체에는 합성 fixture/기본 도형을 사용. 게임 로고/서비스 브랜드를 흉내 내지 않음 |
| Feature/Online | Account, Admin, Chat, Economy, Leaderboard, LiveOps, Mailbox, Matchmaking, ServerDirectory, Social, Trade | 가짜 사용자/프로필·아이템·상품·순위·이벤트 데이터와 상태별 UI 샘플. 기본 아바타/아이콘이 필요한 경우만 CC0 이미지 | 합성 데이터와 Kenney CC0 UI 아이콘/효과음. 실제 공급자 로고·실제 사용자 자료·결제/소셜 플랫폼 상표는 사용하지 않음 |
| Feature/Storage | CacheStore, SQLStore | 마이그레이션 전후 schema/seed fixture, 테스트 DB 데이터, 오류/복구 케이스. 제품 아트 리소스는 필수 아님 | 저장소 테스트용 자체 합성 데이터만 사용. 외부 DB 제품 로고/샘플 데이터셋을 도입하지 않음 |
| Feature/World | Overworld, Voxel | 오버월드 타일/바이옴/랜드마크 및 높이 필드, voxel 블록 팔레트·표면 텍스처, 경계·청크 시각화 리소스 | Kenney CC0 타일/자연 에셋, Poly Haven·ambientCG CC0 표면 텍스처. 지형 고도 자료는 CC0 여부와 원 데이터의 제3자 권리까지 별도로 검증 |

**허용 출처와 적용 규칙**

- **Kenney:** 공식 FAQ는 asset pages의 게임 에셋을 CC0이며 상업 프로젝트에 사용 가능하다고 명시한다([Kenney FAQ](https://kenney.nl/support)). 단, 각 다운로드 묶음에 동봉된 `License.txt`와 실제 사용 파일 목록을 확인하고 보관한다.
- **Poly Haven:** 모델·텍스처·HDRI가 CC0이며 상업 이용·재배포가 가능하다고 명시한다([Poly Haven License](https://polyhaven.com/license)). 에셋별 페이지와 출처도 크레딧에 기록한다.
- **ambientCG:** 다운로드 에셋이 CC0이며 상업 프로젝트에 원본 포함도 가능하다고 명시한다([ambientCG License](https://docs.ambientcg.com/license/)).
- **KayKit 및 현재 저장소의 기존 오디오 팩:** 이미 사용한 개별 팩에 한해 [`common/credits.md`](../Content/common/credits.md)와 [`common/audio.sources.json`](../Content/common/audio.sources.json)의 출처·라이선스 기록을 재사용한다. 새 팩은 같은 제작자라는 이유만으로 기존 팩의 라이선스를 추정하지 않는다.
- **OpenGameArt 등 사용자 업로드 사이트:** 사이트 전체 라이선스로 간주하지 않는다. 개별 항목이 CC0임을 원본 페이지와 내려받은 라이선스에서 확인한 경우만 허용한다. CC-BY는 상업 이용이 가능해도 현재의 “CC0만” 범위 밖이다([CC-BY 4.0 조건](https://creativecommons.org/licenses/by/4.0/)). NC·ND·불명확한 항목은 제외한다.
- **Quaternius:** 현재 공식 QAL은 무료 상업 이용을 허용하지만 원본 에셋 단독 재배포를 금한다([Quaternius QAL](https://quaternius.com/license.html)) — CC0-only 기준 밖이라 새 리소스로 들이지 않는다. 저장소에 이미 있는 묶음 여덟의 판단은 아래 "통합 담당 판단" 의 리소스 라이선스 결정(사용자 2026-11-08)과 `Content/common/credits.md` 머리가 정본이다.

**추가 전 체크리스트:** (1) 게임/키트에서 실제로 쓰는 파일과 키트 ID, (2) 원본 다운로드 페이지와 라이선스 원문/버전, (3) 원본 파일 SHA-256 및 받은 날짜, (4) 원본·변환본 경로와 변환 도구/옵션, (5) 재배포·수정 허용 여부를 `credits.md` 또는 형식이 정해진 출처 목록에 남긴다. CC0도 상표·특허·초상/프라이버시 등 제3자 권리를 자동으로 허가하지 않는다([CC0 안내](https://creativecommons.org/publicdomain/zero/1.0/)). 원본에 브랜드·실존 인물·유명 캐릭터가 들어가면 라이선스 표기와 별도로 제외 또는 권리 검토한다. 기존 폴더 규칙(`common` 공유 원본, 게임별 원본, 임포트된 리소스)과 [공용 리소스 구조](ResourceStructure.md)를 유지하고, 원본은 사용되는 파일만 Git LFS에 둔다. 단순히 “무료”라고 표시된 에셋을 자동 다운로드하거나 포함하는 도구는 만들지 않는다.

**실행 순서:** 먼저 이미 있는 각 게임의 `credits.md`와 공용 출처 기록을 키트별로 연결해 중복 구매/다운로드를 막는다. 그다음 기능 시연에 실제로 부족한 리소스만 위 후보에서 골라 팩 단위 CC0 증거와 해시를 남기고, 임포트·게임 화면·출하 패키지에 반영한다. 개별 자산을 확인하지 않고 모든 하위 키트의 리소스가 충족됐다고 간주하지 않는다.

### GameFramework 고도화 후보 (코드 대조 2026-11-07)

GameFramework의 모듈·수명주기·서비스·테스트 경계를 현재 코드와 Graphify로 확인했다. 키트 전반의 기능 구현을 줄별로 감사한 것은 아니며, 아래는 코드에서 확인된 구조와 그에 따른 다음 단계다.

| 우선순위 | 코드에서 확인한 내용 | 개선 방향과 완료 조건 |
|---|---|---|
| 중간 — GameInstance의 조정 책임 집중 | [`GameInstance.h`](../Source/GameFramework/Runtime/Foundation/Framework/Flow/GameInstance.h)와 구현은 팩 설정·초기화 순서·클라이언트 훅·네트워크 세션·저장/스냅샷·자동 저장·오디오·외형 데이터·프레임 갱신을 함께 조정한다. 기동 실패 되감기 · 서비스 충돌 실패 · 프로세스당 한 인스턴스는 S2 계약(e6933d4ab1, `GameInstanceTest`)으로 고정됐다. | 서로 다른 수명·호스트 대상·실패 정책을 가진 책임만 경계별로 분리한다. 단순히 파일이나 클래스를 쪼개지 말고 초기화/갱신/종료 순서, 소유권, 오류 전달이 독립적으로 검증되는지 완료 조건으로 삼는다. `GameInstance::update`의 순서는 명시적 계약으로 테스트해 키트 메시지·저장 완료·게임 훅 순서 회귀를 막는다. 여러 게임 인스턴스가 실제 요구가 되면 thread-local 폴백이 아니라 명시적 게임 컨텍스트를 호출 경계와 비동기 작업에 전달하도록 설계한다. |

**유지할 기반:** 키트 간 의존성과 모듈 종류는 각 `*.module.json` 매니페스트에 선언되고, [`ModuleManifest.cmake`](../cmake/Engine/ModuleManifest.cmake)는 순환·비활성 의존을 검사하고 의존 순서를 만든다. [`ModuleTargets.cmake`](../cmake/Engine/ModuleTargets.cmake)는 공유 키트, 서버/클라이언트 코드, 에디터 확장을 분리하고 매니페스트 의존이 없는 런타임 키트 링크를 거부한다. 이 경계를 다시 하드코딩하거나 별도 레지스트리로 복제하지 않는다.

**남은 범위:** 전체 장르/기능 키트의 API 일관성·데이터 버전 정책·확장성은 이번 범위에서 완전 감사하지 않았으므로 별도 코드 대조 후 추가한다.

### 오픈월드 액션 RPG 키트: MMO와 싱글플레이 분리 (코드 · 공식 기능 페이지 대조 2026-11-07)

**결론:** 검은사막과 붉은사막을 목표로 기능을 갖춘 게임을 만들려면 현재의 작은 테스트용 게임에 동작 몇 개를 더하는 것으로는 부족하다. 실제 장르 루프를 담은 플레이 가능한 수직 슬라이스와 이를 재사용할 키트 조합이 필요하다. 다만 검은사막은 지속 월드·경제·길드 경쟁이 있는 MMORPG이고 붉은사막은 캠페인·탐험 중심 싱글플레이 액션 어드벤처다. 두 게임을 하나의 “대형 오픈월드 키트”로 합치거나 게임 이름을 그대로 딴 키트로 만들지 않는다.

- **재사용할 기반은 있다.** [`SceneStreamingSubsystem`](../Source/Engine/Scene/SceneStreamingSubsystem.h)은 카메라 및 MMO 관찰자 원점 기반 셀 스트리밍을, [`GF_NetMMO`](../Source/GameFramework/Kits/Feature/Network/NetMMO/GF_NetMMO.module.json)은 관찰자별 관심 영역·우선도·복제를 제공한다. [`GF_ActionAdventure`](../Source/GameFramework/Kits/Genre/Action/ActionAdventure/GF_ActionAdventure.module.json)은 체력/스태미나, 월드 지도, 던전·장치, 요리·무기 내구도 등을, [`GF_OpenWorldWestern`](../Source/GameFramework/Kits/Genre/RPG/OpenWorldWestern/GF_OpenWorldWestern.module.json)은 말 유대·승마·사냥·생존·오픈월드 규칙 일부를 이미 제공한다. 온라인에는 소셜/길드, 거래, 경제 키트도 있다. 다만 이들은 서로 다른 게임 요구를 위한 기반 부품이며, 검은사막이나 붉은사막 수준의 통합 콘텐츠·밸런스·운영 기능이 완성됐다는 뜻은 아니다.
- **기능 범위가 서로 다르다.** 검은사막 공식 기능 소개는 전투/공성, 탐험, 거래, 낚시, 조련, 연금술, 요리, 채집, 사냥을 MMORPG 활동으로 제시한다([공식 기능 소개](https://www.naeu.playblackdesert.com/en-US/GameInfo/Feature)). 이에 맞는 키트 조합은 서버 권위 캐릭터/전투·직업 진행, 장기 성장·장비, 월드 보스/사냥터, 채집·생산·생활 숙련, 마을/작업자·거점, 거래소·경제, 길드·공성/거점전, 파티·대규모 복제와 관전 정책이다. 기존 `NetMMO`, 소셜/거래/경제는 일부 기반일 뿐, 완전한 MMO 서버·경제·안티치트나 이 모든 게임 규칙의 제공으로 간주하지 않는다.
- 붉은사막 쪽은 기존 `ActionAdventure`와 서부극 키트의 개별 부품을 재사용하되, 싱글플레이 캠페인·지역 탐험·보스 전투·캐릭터별 성장·동료/인물 관계·승마와 이동 수단·수집/제작·거점/주거를 연결하는 별도의 게임 키트 조합이 필요하다. 서부극의 법·현상금·사냥 규칙이나 Zelda형 사당/하트 규칙을 공통 오픈월드 규칙으로 일반화하지 않는다.
- **확장 콘텐츠를 위한 실제 요구:** 공식 붉은사막 DLC [Crimson Desert Enhanced: Charting the Unknown](https://crimsondesert.pearlabyss.com/en-US/Game/DLC/charting-the-unknown)은 새로운 육지/해상 위협, 선박으로 바다와 섬 탐험, 잠수·수중 보물/유적, 시설 임대·운영, 확장된 주거 건축, NPC와의 관계, 추가 이야기 흐름을 안내한다. 이 범위는 “DLC 지원”이 단순히 팩을 마운트하는 것보다 넓다. 기존 세이브와 이어지는 퀘스트/지역 상태, 해상·수중 이동/전투, 시설 수익·소유 상태, 건축 변경의 저장·되돌리기, 등장인물별 진행을 호환성 있게 추가해야 한다. 공식 2026-11-05 패치 노트에는 조타, 수중 동굴 입력/잠수, 자유 건축의 방 삭제 확인과 집 보존 관련 수정이 보여, 이 시스템들은 자동화 시나리오가 필요한 경계이기도 하다([공식 패치 노트 2.04.08](https://crimsondesert.pearlabyss.com/en-US/News/Notice/Detail?_boardNo=152)).
- 검은사막은 전통적인 단일 DLC 목록보다 운영 중인 콘텐츠 업데이트로 기능이 누적되는 게임으로 다뤄야 한다. 공식 업데이트 기록에서 확인한 2026-09-23 `Treasure Codex`는 보물·제작 재료·획득처·제작법, 진행도와 최초 완성일을 연결한다([공식 업데이트 기록](https://www.naeu.playblackdesert.com/en-US/Adventure/History)). 이는 수집품 하나를 추가하는 데 그치지 않고 카탈로그, 탐험/획득처 링크, 계정/캐릭터 진행도, UI와 저장 이력을 같은 업데이트로 연결해야 한다는 사례다. 업데이트 명칭과 기능은 계속 변하므로 특정 시점의 콘텐츠 목록을 키트에 고정하지 말고, 버전이 붙은 데이터 카탈로그와 선택 콘텐츠 팩으로 추가할 수 있게 한다.

**권장 구조와 범위**

1. `OpenWorldAction` 계열의 공통 기능 후보는 지역/관심 지점 진행, 퀘스트·발견 기록, 탐험 지도, 전투 조우/보스 계약, 탈것·탑승 인터랙션, 채집/제작/수집 진행도처럼 두 유형에서 실제로 공유될 규칙만 대상으로 한다. 장르 이름을 딴 새 키트는 기존 `ActionAdventure`, `OpenWorldWestern`, `NetMMO`, `Social`, `Trade`, `Economy`를 조합해본 뒤, 중복되는 소유권·저장·데이터 계약이 확인된 경우에만 나눈다.
2. MMO 전용은 `GF_ActionMMO` 같은 선택적 조합 후보로 두고 서버 권위·영속 프로필·구역/샤드·파티/길드·시장 일관성·대규모 전투의 복제/권한 경계를 정의한다. 직업의 기술 연계/자원/재사용 대기, 장비 강화 결과, 경제 거래는 클라이언트가 권위 있게 결정하지 않게 한다. 먼저 단일 테스트 서버와 소규모 관찰자에서 끝까지 도는 루프를 구현하고, 부하/운영 지표가 확인되기 전 월드 전체 규모를 약속하지 않는다.
3. 싱글플레이는 별도 `GF_OpenWorldActionAdventure` 조합 후보로 캠페인 진행·지역 상태·저장/복원·동료 관계·보스/탐험·탈것을 묶는다. MMO 서버/시장/길드 의존성을 포함하지 않는다. 붉은사막 DLC의 해양·수중·주거·시설·관계·추가 이야기는 선택 데이터/시스템 확장으로 취급해 기본 게임의 세이브를 깨지 않게 한다.
4. 두 게임이 공유하는 것처럼 보이는 말·배·전투·퀘스트도 구현을 바로 공유하지 않는다. 현재 [`WesternHorse`](../Source/GameFramework/Kits/Genre/RPG/OpenWorldWestern/Rule/WesternHorse.h)는 코어/겁/유대/능력 해금이 붙은 서부극 승마 모델이다. 공통 탈것 수명·입력·좌석 인터페이스와 게임별 말 진행 규칙이 실제 두 번째 사용처로 검증될 때만 그 경계를 추출한다.
**수직 슬라이스:** MMO 는 [`LanternVale`](../Source/Games/LanternVale/LanternValeGame.cpp)(설명은 그 머리 주석), 싱글은 [`Velmora`](../Source/Games/Velmora/VelmoraGame.cpp)(확장 팩 · 세이브 승계 포함)가 섰다. 길드 · 거래 · 경제(통화 원장)는 온라인 키트(`Social` · `Trade` · `Economy`)의 서비스라 슬라이스의 게임 서버 행동에 넣지 않았다.

**남은 것:** 실제 대규모 MMO 부하 목표와 온라인 운영 비용은 별도 설계·측정 대상으로 남긴다.

### 하우징 · 주거 건축 키트 (요청 2026-11-07)

**결론:** 선택형 하우징 키트는 가치가 있다. 붉은사막식 자유 건축과 심즈형 가구 배치를 함께 지원할 수 있는 공통 저작·배치·저장 기반을 만들되, 생활 시뮬레이션 규칙 전체를 그 키트에 묶지 않는다. “하우징 키트”는 게임 안의 플레이어 건축/꾸미기 기능이고, 에디터의 씬 제작 도구와는 별도 표면이다.

- **재사용할 수 있는 기반:** [`PlacementRule`](../Source/Engine/Environment/Placement/PlacementRule.h)은 시드 기반 산포 배치 규칙이며, [`CitySimulation`](../Source/GameFramework/Kits/Genre/Strategy/CityBuilder/CitySimulation.h)은 격자 도시의 건물 배치·주거 단계·인구를 시뮬레이션한다. [`ThemePark`](../Source/GameFramework/Kits/Genre/Simulation/ThemePark/ParkLayout.h)은 놀이기구 배치 데이터를 보유하고 키트 전용 에디터 패널도 둔다. 이들은 각각 산포, 도시 타일 규칙, 테마파크 레이아웃으로 범위가 정해져 있어, 집의 방/벽/바닥/가구를 플레이어가 자유롭게 배치하는 범용 하우징 시스템은 아니다. [`GameStateSnapshot`](../Source/GameFramework/Runtime/Foundation/Framework/Save/GameStateSnapshot.h)은 게임 상태 저장 봉투를 제공하고 프리팹 스폰/오브젝트 수명도 재사용할 수 있지만, 이것만으로 부지별 건축 편집 이력·영속 소유권·되돌리기 정책이 정해지는 것은 아니다.
- **권장 키트 경계:** `GF_Housing` 같은 선택적 기능 키트는 부지/구역 소유, 배치 가능한 부품 카탈로그, 배치 검증과 미리보기, 구매·회수, 부지별 저장 데이터를 소유한다. 액션 RPG는 이를 거점/주거 기능으로 조합하고, 심즈형 게임의 욕구·가구 효용·가구 구성원 관계·일정은 별도 생활 시뮬레이션 장르 키트가 소유한다. MMO에서 쓸 때는 소유권과 최종 배치 판정/저장을 서버 권위로 두며, 싱글플레이 전용 키트가 온라인 의존성을 끌어오지 않게 한다.
- **저장·불러오기:** 씬의 임시 `GameObjectHandle`이나 스폰 결과를 저장 정본으로 삼지 말고, 부지 ID와 안정적인 배치 ID, 카탈로그 부품 ID/버전, 변환, 필요한 변경값만 직렬화한다. 부지 로드 시 같은 입력에서 같은 오브젝트를 재구성하고, 삭제/이동/복제/재로드에서 중복 생성이나 유실이 없게 한다. 기존 게임 저장 봉투와 통합하되 건축 데이터의 스키마 버전, 제거된/교체된 콘텐츠 참조 오류, 세이브 마이그레이션과 팩 의존성을 명시한다.
- **에디터와 인게임 UX를 분리한다:** 인게임 Build Mode는 플레이어 입력, 카메라, 지면/벽 스냅, 충돌, 비용과 즉시 되돌리기를 다룬다. 에디터 키트 확장은 부지 경계/방 앵커/입구, 스냅 지점과 배치 충돌, 카탈로그 프리뷰 및 검증을 콘텐츠 제작자가 저작하도록 한다. 기존 ThemePark 레이아웃 편집기는 특정 키트 소유의 좋은 참고 사례지만, 그 코드를 주거 편집기로 복제하기보다 실제 하우징 스키마와 뷰포트 도구에 맞는 확장으로 만든다.
- **범위 관리:** 첫 단계에서는 구조물의 안정성 시뮬레이션, 임의 메시를 벽/지붕으로 변환하는 절차 생성, NPC가 가구를 자동 이용하는 범용 플래너, 온라인 동시 건축을 넣지 않는다. 심즈류 완성도를 목표로 할 때는 방 인식·문/통로 연결·가구 사용 가능성/효용·가구원 상태/일정이 별도 시스템과 데이터 계약을 요구한다. 붉은사막식 콘텐츠도 게임별 건축 자유도와 부지/시설 경제 규칙을 프로파일/키트에서 정하고 공용 배치 규칙으로 합쳐 버리지 않는다.
- **검증:** 부품 겹침/지지면 오류/잘못된 스냅/부지 경계/재화 부족/콘텐츠 누락을 명시적으로 거부한다. 배치·회전·이동·삭제·Undo/Redo 후 저장/재로드, 카탈로그 업데이트/삭제 후 구세이브, 여러 방과 층, 대량 가구의 프레임·메모리 예산을 자동화 시나리오로 확인한다. 멀티플레이를 지원하는 경우 권한 없는 클라이언트 배치, 동시 편집 충돌과 서버 재접속도 별도 검증한다.
- **남은 것([`GF_Housing`](../Source/GameFramework/Kits/Feature/World/Housing/), 시연 Empty `maps/housing.scene.xml`):** 첫 판은 칸 벽(심즈의 모서리 벽 아님)이고 부지는 축 정렬이다. 서버 권위 배치(클라이언트 요청 → 서버 판정 → 부지 상태 방송 · 늦은 참가 — 넷 키트 메시지 영역 · 요청 검증 · 복제 테스트 · 둘 실행 시나리오)가 없고, 공유 땅을 쓰는 실제 게임 씬(판 상태와 부지가 한 오브젝트)도 없다.

### 핫 리로드와 스크립팅 언어 판단 (코드 대조 2026-11-07)

**결론: 핫 리로드가 이미 있으므로 스크립팅 언어가 불필요하다고 단정할 수는 없지만, 모드 지원 요구가 생겼다고 곧바로 범용 런타임 스크립트 VM이 필요한 것도 아니다.** Lua 도입은 모드가 새 로직을 요구하는지 확인할 때까지 보류한다.

- [`GameModuleManager`](../Source/ModuleManager/GameModuleManager.cpp)는 개발 빌드에서 게임 모듈을 다시 빌드·교체하고, 언로드 전에 상태를 직렬화해 새 인스턴스에서 복원한다. 이 방식은 C++ 로직 변경의 반복 속도를 높이지만 컴파일·링크가 필요하며, 임의의 상태·작업·외부 포인터까지 보존하는 런타임 코드 편집 기능은 아니다. 배포 빌드는 정적 링크라 개발용 동적 리로드가 없다.
- 스크립트 언어가 주는 별도 가치는 빌드 없이 새 동작을 작성·실행하거나, 사용자 모드를 지원하거나, 콘텐츠 제작자가 프로그래머 개입 없이 규칙을 조합하는 것이다. 그런 사용자/워크플로 요구가 없다면 언어 런타임, C++ 바인딩, 디버거·프로파일러, 오류 추적, 패키징, 샌드박스와 버전 호환성을 추가 유지하는 비용이 이점보다 크다.
- 현재 코드에는 제한된 로직을 표현하는 데이터 기반 시스템이 이미 있다: [`BehaviorTreeAsset`](../Source/GameFramework/Runtime/Character/AI/BehaviorTreeAsset.h)의 노드·태스크, [`ConditionExpr`](../Source/Engine/Behavior/ConditionExpr.h)의 검증·컴파일되는 조건식, [`DialogueGraphAsset`](../Source/Engine/Dialogue/DialogueGraphAsset.h)의 대화 흐름, [`AnimationGraphPlayer`](../Source/Engine/Animation/Graph/AnimationGraphPlayer.h)의 상태 전이. 값·콘텐츠·그래프로 충분히 표현되는 요구는 기존 형식을 확장하고, 일반 프로그래밍 언어를 새로 넣지 않는다.
- 재검토 조건은 구체적인 실제 기능이 기존 API/에셋 형식으로 표현되지 않고, 반복적으로 C++ 변경·빌드를 요구해 제작 병목을 만든다는 근거다. 그때는 먼저 좁은 목적의 데이터 형식/노드/표현식 확장으로 해결 가능한지 본다. 범용 언어가 꼭 필요하면 그때 Lua 등 후보를 평가하고, 서버 권한·네트워크 결정성·시간/메모리 한도·허용 API·스크립트 버전·세이브 호환·툴링을 포함해 별도 설계한다. 임의의 엔진 포인터를 노출하거나 신뢰하지 않는 스크립트를 권한 제한 없이 실행하는 방식은 허용하지 않는다.

**요약:** 핫 리로드는 프로그래머의 C++ 반복을 빠르게 하는 도구이고, 스크립팅은 콘텐츠 제작자/모드 제작자가 새 로직을 런타임에 작성하게 하는 별도 제품 기능이다. 사용자가 모드 지원을 제안했으므로 모드의 범위는 검토 대상으로 올린다. 단, 콘텐츠 모드는 기존 데이터/에셋 형식으로 먼저 지원하고, Lua 등 VM은 모드가 새 로직을 실제로 저작해야 한다는 요구가 확인될 때만 별도 평가한다.

### 모드 지원 키트 (코드 · Graphify 대조 2026-11-07)

**방향:** 선택 키트 `GF_ModSupport`(acb3da4925 — 매니페스트 · 검증 · 결정적 순서 · 구성 해시 · 서버 허용 목록, 세션 시작 때 고정, 덮어쓰기 층 93652d042b, 와이어 · 세이브 해시 f579fb599c)가 섰다. 모든 게임에 강제하는 엔진 기능이 아니라 게임이 필요할 때 의존하는 선택 기능이고, **쿠킹된 콘텐츠·데이터 모드**로 한정한다 — 임의의 네이티브 코드 DLL을 로드하는 것을 모드 지원으로 간주하지 않는다. 아래는 그 경계의 판단이다.

- [`PackPatchTrust`](../Source/Engine/Content/Pack/PackPatchTrustSource.h)는 패치 사슬의 서명 검증을 위한 것이다. 이를 일반 모드 팩의 제작자 신뢰/허용 목록으로 확대해 해석하지 않는다. 팩의 CRC는 손상을 찾는 무결성 검사이지 제작자 인증이 아니며, 팩 암호화도 신뢰/권한 경계가 아니다. 외부 모드를 허용한다면 서명·출처·권한 정책과 거부 이유를 명시해야 한다.
- C++ 동적 모듈과 핫 리로드는 개발 도구이고 배포 빌드는 정적 링크다([`GameModuleManager`](../Source/ModuleManager/GameModuleManager.h)). 서드파티 바이너리 모드는 ABI/컴파일러/플랫폼 호환과 임의 코드 실행 권한을 요구하므로 첫 모드 키트의 범위에 넣지 않는다. XML/데이터가 호출할 수 있는 동작도 등록된 엔진 액션으로 제한한다. 스크립트가 필요해지면 허용 API, 샌드박스, 실행량 제한, 오류 격리와 버전 호환을 별도 설계한다.

**권장 책임 경계**

1. 엔진의 `PackManager`/VFS는 일반 콘텐츠 팩 기능으로 유지한다. `ModSupport` 키트는 모드 매니페스트, 팩 파일, 버전/엔진 호환 범위, 의존·충돌, 활성 여부와 우선순위를 읽어 검증하고 정해진 순서로 적용하는 게임 프레임워크 계층의 조정자다. 기존 팩 기능을 복제하지 않는다.
2. 처음에는 `ContentOnly` 모드만 허용한다: 허용된 에셋/카탈로그/시나리오 등 기존 데이터 스키마로 표현되는 것만 패키징한다. 모드 전용 도메인·경로 규칙, 기본 게임 에셋 덮어쓰기 허용 범위, 모드끼리 같은 리소스를 제공할 때의 결정적 우선순위, 필수/선택 의존 실패 정책을 정한다. 모드 로드 뒤 이미 캐시에 든 에셋이 오래된 내용을 반환하지 않도록 마운트·언마운트와 캐시 무효화/재로드 경계를 함께 만든다.
3. 모드 활성/비활성은 팩 하나의 마운트 조작만으로 끝나지 않는다. 에셋 핸들·공유 캐시·씬 오브젝트·진행 중 로드 태스크가 해당 팩을 참조하는 동안의 안전 규칙을 정한다. 가장 단순한 1차 정책은 실행 중 토글 대신 다음 게임 시작/세션 시작 때 모드 구성을 고정하는 것이다. 런타임 토글이 실제 요구가 될 때만 안전한 언마운트/에셋 해제를 추가한다.
4. 선택 가능한 `GF_..._ModSupport` Runtime 키트에 모드 검색/검증/적용, 로드 결과와 오류 보고, 활성 모드 목록/구성 해시를 둔다. 제작·활성화 UI가 필요하면 같은 키트의 `Editor/` 확장이나 호스트 도구로 분리한다. 실제 배포 채널(수동 설치/워크숍 등)은 매니페스트와 패키지 계약이 정해진 뒤 어댑터로 추가하고, 특정 스토어 연동을 엔진에 박지 않는다.
5. 모드를 허용하는 멀티플레이 게임은 서버가 허용 모드 목록을 정하고 클라이언트와 콘텐츠/버전 해시를 맞춰야 한다. 서버 권위·치트 방지·저장 호환·네트워크 결정성 정책이 없는 임의 모드 조합은 동기화 불일치를 만든다. 모드가 없는 환경의 기존 세이브와 패키지 실행은 영향받지 않아야 한다.

**범위에서 뺀 것:** 네이티브 코드 플러그인과 범용 스크립트 VM, 워크숍 연동은 사용자 요구와 보안·배포 비용이 확인될 때까지 범위에서 제외한다. 실행 중 모드 토글은 실제 요구가 될 때만 안전한 언마운트/에셋 해제와 함께 더한다(3 번).

### 게임 액션 데이터 저작과 에디터 (코드 대조 2026-11-07)

**방향:** 액션의 매개변수·조합은 XML/데이터로 저작하고, 그래프·프로퍼티 패널로 안전하게 편집한다. XML과 에디터는 대안이 아니다. XML이 저장 형식의 정본이고 전용 에디터는 그 형식을 검증하며 조작하는 UX다. 새 C++ 로직을 XML만으로 자동 생성한다고 약속하지 않는다.

- 용어를 나눠 본다. `GameActionCall`/`ActionRegistry`는 행동 트리·대화·스케줄 등이 공유하는 제한된 이름 있는 동작 어휘다. GAS 형태의 `Ability`는 별도의 실행기·복제·수명 모델을 가진다. 양쪽을 단일 "Action" 실행기로 합치지 않는다.
- **이미 가능한 것:** [`ActionRegistry`](../Source/Engine/Behavior/ActionRegistry.h)는 이름, 반영된 매개변수 구조체, 실행 함수를 등록한다. 같은 어휘를 텍스트 또는 XML 속성으로 읽고 실행 시 미리 해석한다. [`BehaviorTreePanel`](../Source/GameFramework/Plugin/Editor/GameplayTools/AI/BehaviorTreePanel.h)는 작업 목록을 레지스트리에서 가져와 선택하고, 선택한 동작의 반영된 매개변수를 프로퍼티 그리드로 편집한다. 행동 트리 모델·로더·에디터는 `BehaviorTreeXML`과 `BehaviorTreeNodeParams`를 공유한다. 따라서 “행동 트리 액션을 XML로 쓰거나 편집할 기능이 전혀 없다”는 문제는 아니다.
- **남은 간극:** 등록된 동작은 결국 C++ 함수다. 기존 XML은 어떤 동작을 어떤 값으로 호출할지 표현하지만, 새 동작의 실행 의미나 복잡한 분기·상태를 XML에서 새로 정의하지 않는다. 현재 RTS 액션 7개는 매개변수 없는 C++ 작업으로 등록돼 있어 `<Action task="rts.attack"/>`처럼 호출은 되지만, XML만으로 공격 규칙을 새로 만들 수는 없다.

**권장 고도화 순서**

1. 액션 등록 메타데이터를 보강한다: 편집기에 보일 설명·분류·적용 문맥(행동 트리/대화/스케줄)을 제공하고, 기존 매개변수 `PROPERTY` 메타데이터를 값 범위·선택지·리소스 참조·도움말의 정본으로 쓴다. 에디터에 하드코딩된 별도 액션 목록이나 중복 스키마를 만들지 않는다.
2. 행동 트리 에디터의 기존 동작 선택·매개변수 편집을 발견 가능하게 한다: 분류/검색, 동작 설명과 실행 문맥, 잘못된 작업·매개변수의 인라인 문제 표시, 선택한 동작을 바꿀 때 호환되지 않는 매개변수 값이 어떻게 처리되는지 명확히 한다. 현재 있는 기능을 재작성하지 않고 실제 UX 결함만 개선한다.
4. 범용 그래프나 일반 스크립트 실행기를 만들기 전에, 실제 콘텐츠가 어느 표현에 막히는지 사례를 수집한다. 단순 값·분기·반복·기존 노드 조합은 기존 데이터 형식에 노드를 추가하고, 임의 상태/복잡한 로직은 C++ 확장 또는 별도 평가된 스크립팅 요구로 남긴다. XML 파일이 지나치게 길거나 중복되는 경우에만 `_base`/공유 데이터로 공통 부분을 추출한다.
5. 검증 완료 기준: UI 변경은 `.claude` 밖의 프로젝트 규칙 [`EditorUI`](agents/EditorUI.md)에 따라 토큰·다크/라이트/고대비·클리핑·캡처 시나리오를 확인한다. 모델/스키마는 XML round-trip, 잘못된 참조와 범위 값 거부, 저장 후 다시 로드한 동등성 테스트를 갖춘다.

**우선순위 판단:** 기존 행동 트리 액션 에디터 · Ability Catalog 편집 창은 있으므로 새로 짓지 않는다. ActionRegistry 검색/설명 개선(1 · 2 번)은 실제 사용성 문제가 확인되면 보완한다.

### VFX 저작 · 캐릭터 잔상/분신 연출 고도화 (코드 대조 2026-11-07)

**남은 것:** 잔상의 GPU · CPU 메모리 측정(시나리오 `game.empty.afterimage`)과 품질 단계별 상한. 분신은 실제 게임플레이에서 필요할 때 별도 캐릭터 프리팹/애니메이션 인스턴스로 — 복제 포즈 재생형인지 AI/전투가 가능한 독립 개체인지 요구를 먼저 가른다. 임의 그래프/모듈 시스템을 먼저 도입하지 않는다.

### 날씨·캐릭터·지면 표면 상호작용 (코드 대조 2026-11-07)

## 통합 담당 판단(2026-11-08)

위 감사를 코드 규칙(구조로 푼다 · 폴백 없음 · 미사용은 쓰게 · 대기를 쌓지 않음)에 맞춰 정했다. "요구가 확인될 때만" 으로 적힌 항목 중 상용 엔진과 비교해 지금 정할 수 있는 것은 정했고, 측정이 필요한 것은 측정을 일감으로 넣었다(재지 않고 미루지 않는다).

**계약(S2, 들어감).** 프로세스당 Engine 하나 · GameInstance 하나 — 중복은 시작 오류다(다중 엔진 요구가 생기면 그때 컨텍스트로 옮긴다). 카탈로그 참조는 데이터 표 한 벌(`datacatalogreferences.xml`)이고 런타임 · 쿠커가 같이 읽는다 — 필수 참조가 깨지면 로드 · 쿠킹 실패, 선택 참조는 경고, XML 의 같은 종류 id 겹침은 오류(코드 API `add*` 의 교체 동작은 유지).

**에디터 작업 안전(S3, 들어감).** 소스 관리 연동은 Git 만, 외부 계정 · 유료 공급자 없음. 자동 저장은 세대별 임시본이고 원본을 덮지 않는다.

**측정과 성능 후보(S8).** 아래 표의 값으로 고치거나, 이득이 없으면 측정값을 이 절에 적고 닫는다. RHI 스레드 · 에디터 UI 스레드는 이 측정에서 병목이 나올 때만 시제품.

*S8 측정 표(7 차 — 계측 · 켤 코드는 커밋했고 값은 통합 실행이 채운다. "켤 코드" 커밋은 따로라 값이 흔들림 안이면 그 커밋만 되돌린다).*

| 후보 | 재는 법(Release, 반복) | 볼 값 | 판정 | 켤 코드 | 값 |
|---|---|---|---|---|---|
| 대기 호출 표 | `App --test --test-filter=OnlineServerCallTableTest.CompletionCostByPending` 5 번, 이 커밋과 그 앞 | 로그 `OnlineServerCallTableTest.CompletionCost` P 16 · 256 · 4096 의 ns/완료, 부하 봇 서버 `/metrics` 의 `service_store_pending_peak` | 실제 P 에서 차이가 흔들림보다 크면 둔다 | 호출 번호 색인(`OnlineServerCallTable::_mapCall`) | |
| 반사 검증 중첩 타입 | `--test-filter=ReflectionValidationTest.NestedElementCost` 5 번, 이 커밋과 그 앞 | 로그 `ReflectionValidationTest.NestedElementCost` ns/원소 | 차이가 흔들림보다 작으면 되돌린다 | 호출 한 번 동안의 기억(`ReflectionValidationInternal::NestedTypeCache`) | |
| 파서 토큰화 | 스탬프를 지우고 변경 없음 · 헤더 하나 · 모듈 전체를 3 번씩 | 끝 줄 `ReflectionParser.TokenizeStats`(호출 · 토큰 · 워커 합 ms)와 `Done in` | 토큰화가 전체의 10% 를 넘으면 저장소 재사용 | 없음(값 뒤) | |
| 씬 저장 비동기 | `-game=AbilityArena` 에디터 시나리오 `game/abilityarena/automation/editor/scenesave` 3 번 | `GameTimer.Ms` 의 `Scene.save.serialize` · `Scene.save.write` · `Scene.save.base` | write 가 16 ms 를 넘고 serialize 보다 크면 write 를 워커로 | 없음 — S3(자동 저장)가 같은 함수를 고쳐 값 뒤 한 번에 | |
| 씬 쿠킹 병렬 | `--cook-all` 을 DDC 없이 · 있이 각 2 번 | `Headless.CookStageTimes` · `SceneCooker.SceneCookTime`(합 · 가장 느린 씬) | scenes 가 쿠킹 벽시계의 절반을 넘고 가장 느린 씬이 합의 1/4 아래면 씬 단위 병렬 설계 | 없음(캐시 격리가 먼저) | |
| GPU 업로드 · 텍스처 | shooter3d · meadowvillage 첫 진입, DX12 · Vulkan · GL, `-gv_assetLoadReport=1` × `-gv_gpuUploadQueue=0/1` 3 번 | Asset 표의 `Texture` · `TextureUpload` 행, `RT.TextureUpload.pump` · `.submitted` · `.polled` · `.waiting`, `GT.GPUUpload.flush` | pump 가 RT 의 10% 를 넘거나 waiting 이 계속 0 이 아니면 RHI Thread 형 시제품 | 없음 | |
| 렌더 그래프 레벨 폭 | `game/empty/automation/benchmark` · Shooter3D 플레이, `-gv_renderGraphParallelMinWidth=2 · 3 · 4 · 99`, DX12 · Vulkan 3 번 | RT 프레임 중앙값, `RT.Graph.parallelLevel` · `.parallelLevels` · `.parallelPasses` · `.serialPasses` · `.maxLevelWidth` | 3 · 4 가 2 보다 흔들림 밖으로 빠르면 기본값을 바꾼다. 그림은 같아야 한다 | 폭 문턱 gv(기본 2 = 지금 동작) | |

**개발 그림은 사진에 들지 않는다(2026-11-09, 들어감).** 캔버스 겹치기 등록(`SW_CANVAS_OVERLAY`)은 칠할 층(`CanvasOverlayLayer`)을 밝힌다. 디버그 드로우 게임 뷰 겹치기는 디버그 HUD 와 같은 개발 겹치기 층 — 사진 캡처 뒤에 화면에만 합치고(에디터 게임 뷰는 `rendertarget/dev_overlay` 텍스처), 스크린샷 · `<ExpectImage>` 골든에 들지 않는다. 캔버스 테스트 그림만 주 출력 층이다 — Canvas 패스 자체가 확인 대상이라 사진에 들어야 한다. 대조: 유니티 기즈모는 게임 뷰 캡처에 들지 않고 언리얼 `HighResShot` 은 기본으로 UI · `stat` 글자를 뺀다. Godot 의 디버그 그리기는 뷰포트 안이라 캡처에 든다 — 골든이 게임 출력을 재는 이 저장소는 앞의 둘을 따른다.

**하지 않기로 정한 것.** Meadow 밭 크기는 소유 타입 상수로 둔다(레벨별 크기 요구 없음). Shooter3D 탄착 표면 목록은 코드 상수로 둔다. 범용 스크립팅 VM · 네이티브 모드 DLL · 원격 명령 서버는 넣지 않는다.

**리소스 라이선스(사용자 결정 2026-11-08: 문제가 되면 다른 리소스).** 저장소의 Quaternius 묶음 여덟은 묶음 페이지가 CC0 이고 QAL 은 소급하지 않아 그대로 둔다(근거는 `Content/common/credits.md` 머리). 새 리소스는 무료 · 공개 라이선스만(CC0 우선, MIT 처럼 재배포가 되는 것은 고지와 함께 — [NEXT](NEXT.md) 리소스 규칙), QAL · CC-BY · 불명확한 것은 다른 CC0 출처로 바꾼다.

### 지역 효과의 범용 구조(사용자 2026-11-08 — 용암 · 바다 · 늪 · 유황 · 독가스)

**정한 것(들어감).** 지역 효과는 입력 → 모듈 → 반응이다. 입력은 셋(그린 볼륨 `ZoneVolumeComponent`, 표면 종류 · 물리 재질 `surface:<종류>` — 접촉 `ZoneContactComponent`, 런타임 영역 = 볼륨 + 수명 · 퍼짐 프리팹)이고 접촉 방식 넷(겹침 · 디딤 · 부딪힘 · 잠김)이 같은 처리기(`ZoneTracker`)를 지난다. 프로필은 `*.zone.xml` — 이름 붙은 모듈 목록이고 `_base` 는 하나, 재사용 모듈은 `*.zonemodule.xml`, 세 층(프리셋 · 변형 · 배치 덮어쓰기 `_listModuleOverride`)을 데이터 상속이 합치며 로드 때 합친 결과를 검사한다(모르는 종류 · 매개변수 · 식 · 동작 · 카탈로그 참조는 로드 실패). 수치 칸은 조건식 문법의 식(위치 값 `zoneDepth01` · `distanceToCenter` · `immersion` · `timeInside` · `impactSpeed`, 대상 속성 · 태그), 사건은 진입 · 체류 · 이탈 · 조건 전환(`Became` · `Lost`, `delay`), 경우 목록은 처음 맞는 하나, 저항 · 면역은 이펙트 계산 단계가 깎는다. 모듈 종류(처리기는 서로를 모른다 — 새 효과는 `SW_ZONE_MODULE` 한 줄): `Action`(이펙트 · 스킬 · 즉사 · 이동 · 규칙 신호 — 주기 피해 · 둔화 · 중독 · 가라앉음 스택), `PhysicsField`(중력 배율 · 균일/중심/회전/흐름 방향 × 세기 식 · 유체 저항 · 부력 · 마찰 — 캐릭터와 강체 모두), `Blackboard`(NPC 질의), `NavArea`(행위자 종류별 비용 · 금지), `SurfaceChannel`(젖음 · 진흙 · 그을음), `SurfaceStamp`(변형 층 도장 — 아래 표면 변형 층). 반응은 태그 · 속성만 읽는 컴포넌트다: 외형 내리기(`VisualSinkReactionComponent`), 점프 탈출(`JumpEscapeReactionComponent`), 시야(`ScreenReactionComponent` — 태그를 가진 동안 따라가는 카메라 디렉터에 후처리 덧칠 `CameraDirector::setPostProcessOverlay` 를 페이드로). 프레임 일은 `ZoneSystem` 하나가 틱 뒤에 돈다. 데이터: `Content/game/empty/zones/`(용암 · 깊은 용암 · 바다 · 늪 · 독 늪 · 유황 · 분출구 · 독가스 살포 · 모래 · 느린 사구 · 사구 · 소용돌이 · 무중력 · 빙판 · 가시덩굴 · 버프 샘 · 스킬 영역 · 즉사 구덩이), 값 시나리오 `game.empty.zone*` · `sinkingsand`. 비교: 언리얼 GAS 이펙트 Component 목록 · PhysicsVolume, 유니티 Volume Override · Effector, Godot `Area3D`(중력 덮어쓰기 · 감쇠 — 영역이 필드를 주고 바디가 받는 같은 자리)를 한 프로필의 모듈로 합쳤다(영역마다 컴포넌트를 늘리지 않는다).

### 표면 변형 층(사용자 2026-11-08 — 지나온 길 · 파묻힘 · 빨려드는 구멍)

**정한 것(들어감).** 씬 서비스 `SurfaceDeformSystem` 하나가 카메라 앞 창의 CPU 높이 격자에 도장(`SurfaceStampDef` — Print · Trail · Funnel, 깊이 · 테두리 · 메워지는 속도 · 회전)을 찍고 바뀐 칸만 텍스처로 보낸다. 지면 머티리얼(`surface_deform.hlsl`)이 정점을 내리고 법선을 기울인다 — 충돌 · 내비 · 높이장은 그대로다. 도장을 내는 길은 셋이고 모두 데이터다: 표면 종류의 발자국(`SurfaceKindDef::_footprint` — 눈 전용 고리는 없앴다), 오브젝트 도장(`SurfaceStampComponent` Hold · Trail — 끌린 자국 · 바퀴 자국), 지역 프로필 모듈 `SurfaceStamp`(대상 발 · 당김 중심에 매 프레임 다시 찍어 머무는 동안 메워지지 않는다, 수치 칸은 위치 값의 식 — 파묻힘 테두리 · 안쪽일수록 깊은 깔때기). 렌더 타깃에 GPU 로 그리지 않고 CPU 격자인 것은 도장이 프레임에 몇 개뿐이고 값 시나리오가 깊이를 그대로 읽으며 백엔드 코드가 늘지 않아서다(언리얼 RVT · 유니티 CustomRenderTexture 와 다른 점 — 도장이 수백 개로 늘면 컴퓨트로 옮긴다). 시나리오: `game.empty.surfacedeform` · `surfacefootprint` · `sinkingsand`(지역 도장 rim).

### 관성 이동과 탑승 가속감(사용자 2026-11-08 — 썰매처럼 경사에서 빨라지고 느려지고, 타면 가속이 느껴지게)

**정한 것(들어감).** 탈것 이동은 둘이다 — Jolt 강체 · 바퀴(`WheeledVehicleComponent`, 시뮬레이션 레이싱)와 바디 없이 힘을 적분하는 `KinematicVehicleComponent` · `KinematicVehicleMotor`(아케이드 · 썰매 · 수레 · 호버 · 레일, 옛 `Arcade…` 이름은 막았다). 바깥 힘은 `InertiaSettings::computeAlongAcceleration` 한 벌(경사 중력 − 디딘 표면의 구름 저항 − 공기 저항 v², 질량으로)을 키네마틱 탈것 · 말(`MountMovementComponent`) · 롤러코스터가 함께 쓰고, 구동은 출력 한계(`_maxPower`, `limitDriveAcceleration` — P / (m·v))로 자른다: 다리 가속 한계가 경사 당김보다 커도 출력이 모자라면 오르막에서 P / (m·g·sin) 에 선다(언리얼 Chaos Vehicle 토크 곡선 · 유니티 motorTorque 자리를 바디 없는 모델에서는 일률 한 칸으로). 조작감은 모듈(`Grip` · `Drift` · `Boost` · `AirControl` · `Hover` · `Rail`), 튜닝은 `*.vehicle.xml`(`_base` 상속 — 무거운 썰매 = 질량 한 칸, 배치는 변형 파일을 가리킨다). 탑승 가속감은 좌석(`VehicleSeatComponent::_rideFeel`)이 탈것 가속도를 재어 탑승자 기울임 · 시야각 킥 · 충격 흔들림 · 소리 매개변수를 낸다. 배는 선체 강체 + 폰툰 부력 · 키 · 돛 · 방향별 저항의 조합(`BoatComponent` · `BoatHelmComponent` · `WeatherWaveComponent`). 시나리오: `game.empty.inertia`(카트 오르막 감속 · 썰매 내리막 가속 · 빙판이 눈보다 멀리 · 탑승자 기울임 · 레일, 골든 `inertia_sled`), `game.empty.mountslope`(평지 말 구보 속도, 0.2 rad 비탈 말 ≈ 4.1 m/s), `game.empty.boat`. 카트 트랙 전용 시나리오는 두지 않는다 — 트랙의 내리막 · 오프로드 · 빙판은 같은 모터 · 같은 표면 종류라 `inertia` 의 판정과 겹친다. 비행(양력 · 자세)은 범위 밖이다.

