# 5-3. Core 와 플랫폼

> 질문: 5-3. Core 와 플랫폼 영역에서 이미 정한 것은 무엇이고 근거는 무엇인가?
> 독자: 같은 제안을 다시 하려는 사람, 정한 이유를 찾는 사람.
> 결론: 결정 기록 5 절의 한 영역이다. 번호 체계와 쓰는 법은 [결정 기록](../09_Decisions.md) 5절에 있다.

- **macOS 는 지원 대상이 아닙니다**(2026-10-04). 코드는 지웠고 `CheckTargetMacros` 가 `SW_PLATFORM_MACOS` 를 막습니다. 복원은 그 전 git 기록에서 찾습니다.
  `Vcpkg.cmake` 의 APPLE 분기와 레거시 스탬프 이관, `arm64-osx` · `x64-osx` 트리플릿은 손대지 않는 파일이라 사용자가 정리합니다.

- **파일 존재 술어는 `FileUtil::exists`(무엇이든), `isDirectory`, `isRegularFile` 셋입니다**(2026-10-06). `exists` 는 `std::filesystem::exists` 와 같은 뜻입니다. 경로 전용 타입(`FilePath`)은 만들지 않습니다.
  파일 오류는 그 자리에서 경고하고 `[[nodiscard]] bool` 로 돌려주며, 실패를 알리지 않는 시도는 `FileUtil::tryRemoveFile` 하나입니다(언리얼 `IFileManager::Delete` 의 `bQuiet`).
- **`std::hash<fixed_string>` 은 대소문자를 구분합니다**(2026-10-06). `string` 과 `wstring` 의 해시와 같은 규칙입니다.
- **`fixed_string` 은 길이 필드 없이 배열 하나만 듭니다**(2026-10-11). `size()` 는 앞 N 칸의 경계 있는 검색이고, 길이를 정하는 연산이 꼬리를 0 으로 채워
  바깥이 `data()` 에 널 없이 써도 길이가 맞습니다. 캐시하던 `mutable` 길이는 const `size()` 가 써서 읽기끼리 데이터 레이스였습니다.
  EASTL `fixed_string` · folly `FixedString` 은 길이를 들지만 바깥이 버퍼에 직접 쓰는 계약이 없습니다. 이 저장소는 ImGui · `formatstring` · Win32 가 `data()` 에 쓰므로 C 배열과 같은 계약을 택했습니다.
  넘침은 잘라 경고하고, 잘림이 틀린 값이면 `try_assign` · `try_append` 를 씁니다. 측정은 Release, 같은 PC 에서 옛 · 새 바이너리를 번갈아 세 번 돌린 중앙값입니다(`FixedStringBenchTest`).

  | 항목 | 전 | 후 |
  |---|---|---|
  | `sizeof` `fixed_string<15>` · `<31>` · `<63>` · `<160>` | 20 · 36 · 68 · 168 | 16 · 32 · 64 · 161 |
  | `sizeof` `fixed_wstring<15>` | 36 | 32 |
  | `sizeof` `CrashBreadcrumbStore` · `CrashContextStore` | 5380 · 4996 | 5156 · 4660 |
  | `fixed_string<64>::size()`(길이 8~47) | 5.7 ns | 5.7 ns |
  | `fixed_string<64> = const utf8*` | 8.6 ns | 8.8 ns |
  | `fixed_string<64> = fixed_string<64>` | 8.4 ns | 2.0 ns |
  | `fixed_string<64> == fixed_string<64>` | 6.1 ns | 5.5 ns |
  | `clear()` + 짧은 `append` — `<64>` · `<8192>` | 7.9 · 10.1 ns | 5.5 · 68.7 ns |

  `clear()` 가 O(N)이 된 것이 유일한 손해입니다. `fixed_string<8192>` 를 되풀이해 비우는 호출처는 없습니다(8KB 는 로그 줄 버퍼 하나이고 만들 때 한 번 0 으로 채웁니다).
  꼬리를 길이만큼만 채우는 안은 ImGui 가 널 뒤에 남긴 옛 글자를 지우지 못해 기각했습니다. 작은 배열(256 바이트 이하)의 대입은 크기가 컴파일 때 정해져 배열 전체 0 채우기가 그 자리에 펼쳐집니다.
- **Win32 API 는 W 버전을 이름으로 부르고, A 버전도 막습니다**(2026-10-06). 전역 `UNICODE` 정의는 대상마다 정의가 빠지면 말없이 A 버전으로 돌아가서 택하지 않았습니다. 언리얼, 유니티, Godot 모두 Windows 경계에서는 W 버전만 씁니다.
  X11 창 제목은 `XStoreName` 과 `_NET_WM_NAME` 을 함께 적습니다(SDL2, GLFW, Godot 과 같음).
- **벽시계는 `WallClock` 하나만 읽습니다**(2026-10-06). 서비스는 현재 시각을 매개변수로 받고, 테스트는 가짜 시각을 넣습니다.
- **문자열 타입은 담는 것으로 고르고, `string` 을 `fixed_string` 으로 일괄 치환하지 않습니다**(2026-10-10). 규칙은 [agents/NamingCpp](../agents/NamingCpp.md#function-names) 에 있습니다.
  `sw::string` 은 MSVC STL 의 SSO 라 15바이트까지 힙을 쓰지 않고 `sizeof` 는 32입니다. `fixed_string<N>` 은 4의 배수로 올린 (N+1) 에 4를 더한 크기이고,
  N 을 넘으면 잘라 내며 경고하고, `size()` 는 매번 `strlen` 이었습니다. 그래서 짧은 이름은 이득이 없고, 긴 텍스트는 잘리며 스택만 커집니다.
  후보를 조사했더니 프레임 경로를 지나는 것은 0건이었습니다. 프레임 경로의 이름은 `hashed_string` 이나 기존 저장소를 가리키는 `string_view` 로 넘깁니다
  (`FrameRenderer::resolvePresentSource` 가 사본 대신 뷰를 돌려줍니다). `fixed_string` 은 상한이 코드 상수이고, 힙이 금지된 경로(크래시 · 로그 · 시그널)나
  고정 버퍼가 필요한 곳(ImGui 입력)에만 씁니다. 언리얼도 `FString` 을 기본으로 두고 인라인 버퍼(`TStringBuilder<N>`)는 임시 조립에만 씁니다.
- **Engine 폴더 사이의 작은 고리는 "위층이 아래층 등록표에 등록" 과 "둘을 다 아는 층으로 옮기기" 로 풉니다**(2026-10-11, 엔진 분할 1 단계). 자동화 실행기는 UI 를 모르고 UI 가 단계 · 로딩 화면 시작 차단 조건(`SW_AUTOMATION_START_BLOCKER`)을 올립니다(언리얼 Automation 도 모듈이 테스트 · 래치 명령을 등록).
  정점 애니메이션 굽기(`MeshVertexAnimationBaker`)는 `Object/SceneAnimation` 으로, 물리 디버그 선 어댑터는 조립점 `Engine.cpp` 로 옮겨 `Graphics` 가 `Animation` · `Physics` 를 include 하지 않습니다(언리얼 AnimToTexture 도 별도 플러그인).
- **엔진 내장 에셋 캐시는 기능 폴더가 등록하고 `AssetManager` 는 등록표만 봅니다**(2026-10-11, 엔진 분할). 캐시 클래스의 .cpp 에 `SW_BUILT_IN_ASSET_CACHE( MaterialCache )` 한 줄을 두고(`Resource/Cache/BuiltInAssetCache.h`),
  매니저는 생성자에서 표를 훑어 소유 캐시를 만들고 종류 이름 순으로 올립니다. 타입으로 찾는 창구는 `getAssetCache<T>()` 입니다(`getMaterialCache` 넷은 지웠습니다).
  언리얼 서브시스템 · 고도 `register_types` 의 `add_resource_format_loader` · 유니티 `ScriptedImporter` 와 같은 방향입니다.
  **에셋 캐시는 서브시스템으로 만들지 않습니다**(2026-10-14, C-27 ② 판정). 캐시는 경로 → 에셋 표일 뿐 세울 것 · 내릴 것이 없고(`IAssetCache` 에 초기화 · 종료가 없다),
  캐시끼리 기대는 순서도 없다(비우기 순서는 종류 이름 순으로 고정, 에셋 사이 참조는 공유 포인터). 판정 기준 "수명 범위 · 초기화/종료 순서 · 의존이
  있는 서비스" 에 들지 않는 값 저장소다. 수명은 `AssetManager`(소유 캐시) · 소유한 시스템(UI 문서 · 게임 데이터 캐시의 등록/해제)이 정한다. 언리얼도
  자산 캐시(`FStreamableManager` · 셰이더 맵 캐시)를 서브시스템으로 두지 않는 것으로 알고 있다.
  그래서 `Source/Engine/Resource/AssetManager.cpp` 의 배선 예외가 없어졌습니다. 모듈(게임 · 키트)의 캐시는 이 표가 아니라 `registerAssetCache` 로 올리고 내립니다(표 항목은 내리지 않으므로).
- **캐시는 `CacheStats` 레지스트리에 이름으로 올려 적중 · 실패 · 비우기 · 항목 수를 세고, 새 캐시는 상한과 넘칠 때의 동작을 선언합니다**(2026-10-25, 캐시 계측). 숫자 없이 캐시를 더하거나 키우지 않으려는 것이다
  (측정 없이 최적화하지 않는 원칙의 캐시 판). 규칙: ① 상한(개수나 바이트)과 **넘칠 때의 동작**을 코드 주석에 선언하고 전체 비우기는 쓰지 않는다(오래된 세대만 버리는 두 세대 교대나 LRU) — 경계에서 쓰이던 항목이 한꺼번에 다시 계산되는 thrash 를 막으려는 것이다.
  ② 무효화 사유를 열거한다(키가 담는 입력이 바뀌어 키가 달라지는지, 알림으로 비우는지). ③ `Core/Diagnostics/CacheStats.h` 에 이름으로 올리고 조회 자리에서 `recordHit` · `recordMiss`, 비울 때 `recordClear` 를 부른다. 같은 이름을 두 번 올리면 거절하고 오류를 남긴다.
  **`IAssetCache` 를 고치지 않고 별도 레지스트리를 둔 까닭**: `IAssetCache` 는 게임 모듈이 상속하는 ABI 라 가상 함수나 데이터 멤버를 더하면 이미 빌드된 모듈의 vtable · 레이아웃이 어긋난다. 또 `IAssetCache` 가 아닌 캐시(텍스트 측정 · 물리 셰이프)도 같은 길로 세려면
  캐시 인터페이스 밖의 레지스트리가 필요하다. 카운터는 `relaxed` 원자 증가 하나(락 · 할당 없음, Shipping 에도 남는다)이고 칸은 정적 고정 배열이라 함수 정적 핸들이 엔진을 다시 세워도 유효하다. 보고는 `-gv_profileFrames` 측정 창 끝의 `[CacheStats]` 줄(캐시마다 한 줄, 창 시작 뒤의 값)이다.
  세지 못하는 캐시: 조회가 없는 어댑터(`AudioReloadCache` · `LocalizationReloadCache`)는 보고에 `no counters` 로 보이고, `ResourcePaths` 의 캐시는 이 변경의 범위 밖이다.
  텍스트 측정 캐시(`TextMeasureCache`)는 옛 "4096 개가 차면 전체 비우기" 를 세대 용량 4096 의 두 세대 교대로 바꿨다(항목은 최대 8192, 자주 쓰는 항목은 승격으로 남는다). 키는 면 연쇄를 담아 문화권 · 폰트 교체를 이미 따르므로 변경 알림에 연결하지 않는다(해시 단독 키라는 한계는 파일 머리말에 적었다).
  물리 셰이프 캐시(`PhysicsMeshShapeCache`)는 상한을 두지 않는다 — 경로가 콘텐츠의 메시 파일 수로 닫혀 있고 씬 교체 때 비우면 재방문마다 다시 읽기 때문이다(항목 수는 `[CacheStats]` 로 본다).
- **`IAssetCache` 는 계측 선언 없이 지을 수 없고, 경로 캐시는 "없음" 도 기억하되 세대가 낡음을 막습니다**(2026-10-25, 캐시 계약). ① 위 항목의 "`IAssetCache` 를 고치지 않는다" 를 뒤집는다: 모든 구현이 계측을 따르게 하는 길이 형식뿐이라(손으로 지키면 새 캐시가 잊는다) 생성자가
  `CacheTracking`(`counted( 종류 이름 )` 또는 `uncounted( 사유 )`)을 받는다. 레이아웃이 바뀌므로 엔진과 게임 · 키트 모듈은 함께 다시 빌드한다(모듈이 옛 엔진 DLL 에 붙는 구성은 없다). 조회가 없는 어댑터는 사유를 적고 보고에 `no counters (<사유>)` 로 보인다.
  ② `ResourcePaths::findLooseFile` 의 캐시는 음성 결과(디스크에 없음)를 같은 표에 둔다. 낡음은 둘로 막는다 — 검색 목록 · 루트 · 마운트가 바뀌면 `clearPathCache` 가 올리는 **배치 세대**(해석을 시작할 때 잡은 세대가 달라졌으면 결과를 저장하지 못한다),
  이 프로세스가 파일을 만들면 오르는 **`FileUtil::getCreationEpoch`**(음성 칸은 기억한 때의 에포크가 같을 때만 믿는다). 다른 프로세스(쿠커 자식 · git)가 만든 파일은 에포크가 못 본다 — 그런 파일을 먼저 묻고 나중에 읽는 흐름은 `clearPathCache` 를 부른다.
  계측 칸 `ResourcePath`(적중 = 캐시가 답, 실패 = 디스크를 훑음)와 `ResourcePathMissing`(적중 = 기억한 "없음" 으로 답, 실패 = 훑었더니 없었음)으로 효과를 본다. ③ `TextMeasureCache` 는 해시 단독 키를 버리고 **해시 후 원문 확인**(`TextMeasureKey`: 글 사본 + 문맥 해시)이다.
- **씬마다 하나인 기능 서비스는 기능 폴더가 등록하고 `GameObjectManager` 는 레지스트리만 봅니다**(2026-10-12, 엔진 분할 2 단계). 서비스 .cpp 에 `SW_SCENE_SERVICE( SceneNavigation, Navigation )` 한 줄을 두면 매니저가 생성자에서 만들고, 그 줄의 프레임 단계에서 `update` 를, `clear` 에서 `onSceneCleared` 를 부르며, 찾는 창구는 `getSubsystem<T>()` 입니다(`Object/GameObject/Service/ISceneService.h`).
  씬 서비스는 서브시스템이고 매니저의 씬 범위(`SubsystemScope`, 부모는 엔진 범위)가 소유한다(2026-10-14, C-27 ①) — 선언한 의존으로 세우고 같은 단계의 갱신도 그 순서다.
  언리얼 `UWorldSubsystem` · `UTickableWorldSubsystem` 과 같은 자리로 알고 있다. 프레임 단계 갱신(`update`)은 서브시스템 계약 밖의 씬 서비스 몫이다 —
  초기화 · 종료는 수명이고 갱신은 프레임 단계 표(`SceneFrameStepList.xxx`)의 자리라서.
  언리얼 `UNavigationSystem`(월드 서브시스템) · 유니티 `NavMeshManager`(씬 코어 밖 모듈)와 같은 방향이라 `GameObject/` 는 `Component/Navigation` 을 include 하지 않습니다(`getSceneNavigation` 은 지웠습니다).
- **`AABB` 는 Core 수학 타입입니다**(2026-10-11, 엔진 분할). `Engine/Physics/Collision/AABB.h` → `Core/Math/AABB.h`(`Frustum.h` 옆) — 언리얼 `FBox` · `FBoxSphereBounds` 와 유니티 `Bounds` 도 물리가 아닌 수학 타입입니다. 레이어를 보는 겹침 질의 `queryOverlaps` 만 물리의 `CollisionLayers.h` 에 남고, `Spatial` 은 티어 4 → 1 로, Object → Physics include 는 41 → 36 으로 줄었습니다.

- **서브시스템(초기화 · 종료 짝 + 의존 순서)은 Core 의 계약입니다**(2026-10-14). `ISubsystem` · `SubsystemCollection` 은 `Core/Subsystem/`(티어 4 — 로그를 남기지 않아
  Log 아래)에 있고, 호스트 종류는 Core 의 비트 마스크(`SubsystemHostMask`)이며 비트의 뜻(클라이언트 · 전용 서버)은 엔진(`EngineSubsystemHost`)이 정합니다.
  언리얼은 `UEngineSubsystem` · `UWorldSubsystem` 처럼 수명 범위마다 나누고 `ShouldCreateSubsystem` 으로 호스트를 거르며, 순서는 `InitializeDependency`
  호출로 실행 중에 정하는 것으로 알고 있다. 여기서는 같은 모양(서브시스템이 이름 · 의존 · 호스트를 스스로 선언하고 목록이 호스트로 거른다)에 두 가지를 더 했다:
  의존은 선언 전체로 위상 정렬하고(동점은 이름 순 — 결정적), 의존 칸은 다른 서브시스템의 이름 상수(`kSubsystemName`)를 가리켜 철자가 틀리면 컴파일 오류다.
  모르는 이름 · 순환 · 호스트 어긋남은 `EngineInitSequenceTest` 가 호스트 종류마다 걸러진 실제 목록으로 잡는다(`EveryHostKindSortsItsOwnSubsystems`).
  목록은 로그를 남기지 않고 오류 글을 돌려준다(로거도 이 목록의 서브시스템이 될 수 있게). 정렬 오류는 부른 쪽이 표준 오류로 낸다.
  이름 상수는 층을 따라 아래로만 include 되므로, 낮은 층(비동기 IO · 물리)이 높은 층(모듈 이미지 · 리소스)보다 늦게 서야 하면 높은 층이 역방향 의존(`getSubsystemDependents`)으로 적는다 —
  목록에 없는 이름은 건너뛰어 "있으면 그 뒤" 인 선택 의존을 겸한다. 정방향 의존은 없으면 오류다. 언리얼에는 역방향 칸이 없는 것으로 알고 있다(층 규칙이 없어 필요가 없다).
- **서브시스템이 서비스를 소유하고, 컬렉션 하나가 범위 하나입니다**(2026-10-14, 정리 ④).
  호스트(엔진 루프 · 테스트 하네스)는 세 범위를 든다 — 부트스트랩(진단 도구 · 이름 풀 · 로거 · 명령줄+전역 변수 · 크래시 핸들러 · 리소스 루트 · 메모리 예산 ·
  정적 저장소), 서비스(이벤트 · 지역화 · 커맨드 스택 · 디버그 도구 · 프로파일러 · 외부 프로파일러 · 크래시 보고 · 백엔드 레지스트리), 엔진(기동 단계).
  메모리 예산은 추적만 부트스트랩에서 켜고 예산 표는 첫 프레임 끝에 읽는다 — 표는 리플렉션으로 읽는데 타입 등록부가 엔진 범위다(2026-10-14, C-27 ③;
  언리얼도 LLM 은 기동 초에 켜고 예산은 설정 뒤에 읽는 것으로 알고 있다). 엔진을 세우지 않는 도구(OnlineLoadBot)도 같은 부트스트랩 서브시스템을 쓴다(C-27 ⑥). 아래 범위가 다 선 뒤 위 범위가 서고,
  위 범위가 다 사라진 뒤 아래 범위가 내려간다. 서비스 범위는 "엔진 범위의 누가 소멸자에서 부르든 살아 있는" 옛 수명을
  구조로 지킨다 — 엔진 범위로 내리려면 쓰는 쪽마다 의존을 적어야 하는데, 소멸자 · 종료 경로의 쓰임을 모두 찾을 수 없어 택하지 않았다.
  세 범위는 게임 · 에디터와 같은 `SubsystemScope`(5-8)이고 타입 해시로 찾는다. 언리얼은 범위마다(엔진 · 월드 · 게임 인스턴스) 컬렉션이 있고
  `GetSubsystem<T>()` 가 부를 때마다 맵을 찾는 것으로 알고 있다. 여기서 엔진 창구(`engine::getSubsystem<T>()` 의 칸)는
  소유 · 채우기 없이 **바인딩 때 한 번 찾아 둔 조회 캐시**로 남는다 — 캐스트마다 타입 레지스트리를 묻는 자리가 이름 조회를 하지 않게. 필수 행이 비면 호스트가
  서지 않는다(옛 `CheckEngineServiceBinding` 게이트를 대신한다). 서브시스템이 아닌 행(값 묶음 · 소유한 서브시스템 안의 객체)은 호스트가 그 객체를
  범위에 `bind` 한다. 테스트는 하네스 범위를 부모로 둔 범위에 같은 타입을 넣어 서비스를 갈아 끼운다(`test::ScopedEngineServiceOverride`).
  하네스는 자기 호스트 비트(`kTestHarness`)로 서서 클라이언트 전용(글꼴 · UI · 텔레메트리 · 플레이어 설정)을 표의 자리로만 둔다.
  모듈(게임 · 에디터)이 받는 호스트 서비스도 범위다(정리 ④c): 호스트가 `SubsystemScope` 에 표의 행을 그 타입으로 걸어 포인터를 넘기고
  (`engine::bindModuleServices` — 게임 모듈에는 `GameVisible` 행만, 에디터에는 모듈 컴파일러까지), 모듈은 자기 범위 다음에 그 범위에서 찾는다.
  서비스 id 열거형 · id 배열 표(`ModuleServiceTable`) · 호스트 서비스 목록은 지웠다 — 찾는 열쇠는 범위와 같은 타입 해시 하나다. 엔진 루프 ·
  부트스트랩 · 헤드리스 헤더는 호스트만 include 한다(`CheckEngineLayers` 가 Games · GameFramework · Editor 에서 막는다).
- **서비스 접근은 소유 수명마다 한 창구, 한 어휘다 — `getSubsystem<T>()`(반드시 있음, `T&`) · `findSubsystem<T>()`(없을 수 있음, `T*`)**(2026-10-15, C-28 마감).
  엔진은 `engine::`, 씬은 `GameObjectManager::`, 게임은 `game::`(게임 범위 → 호스트), 에디터는 `editor::`(에디터 범위 → 호스트)이고 소유 객체(`EditorContext` · `GameInstanceBase`)의 멤버도 같은 두 이름이다.
  이름 붙은 엔진 접근자 서른 개(`engine::getTypeRegistry()` …) · `getBoundEngineServices` · `getService<T>` · `getSceneService<T>` · `isServiceBound<T>` 는 지웠다(별칭 없음).
  언리얼이 범위 객체마다 `GetSubsystem<T>()` 를 두는 것과 같은 모양이다. 유니티 서비스 로케이터 · Godot `Engine::get_singleton( "이름" )` 의 이름 조회는 철자 오류가 실행 때에야 드러나 택하지 않았다.
  우리 쪽 확장 셋: ① 엔진 칸은 바인딩 때 한 번 찾고 템플릿이 칸 색인을 컴파일 때 고른다 — 목록 밖 타입이나 빌 수 있는 행(`SW_ENGINE_SERVICE_OPT`)에 `getSubsystem` 을 쓰면 `static_assert` 다.
  ② `CheckServiceAccess` 가 옛 창구와 새 `static getInstance()` 를 막는다(상태 없는 코덱 · 드라이버 · 링크 때 등록 표만 예외 표에 이유와 함께). ③ 목은 범위로 넣는다
  (`test::ScopedEngineServiceOverride` · `game::ScopedGameScope` · `editor::ScopedEditorScope`). 호스트가 게임 서비스를 묶지 않은 실행(쿠커 · 테스트 하네스)에서 게임 창구는 엔진 범위를 본다
  (`Engine/Common/EngineScope.h`). GameFramework 의 표 읽기가 타입 레지스트리를 이 창구로 찾는다.
  ④ 이름 검사의 틈(`.cpp` 익명 접근자, 네임스페이스 수준 변수, `static inline` 멤버, `thread_local`, `ScopeOwned` 로 감싼 전역 칸)은 모양으로 막는다: `CheckMutableStatics` 가 정적 저장 기간의 가변 변수(상수 아닌 것)를
  장소와 이름에 상관없이 위반으로 본다(예외는 `rules/CheckMutableStatics.toml` 에 `<파일>:<변수>` 와 이유). 소유자는 칸을 감싼 형식이 아니라 칸을 든 객체다.
  신규 위반부터 막고 기존은 이전 대기 표(`[pending]`, 파일별 위반 총수 상한)로 줄여 간다(사용자 결정, 2026-10-25) — 표는 늘지 못하고 줄었으면 `--regen-baseline` 으로 줄여야 통과한다.
  **단일체 게이트는 이름이 아니라 모양을 본다**(2026-10-25, `ResourceDataKindRegistry` 는 서비스로 옮겨 `SW_ENGINE_SERVICE` 목록에 있다): 참조를 돌려주는 인자 없는 정적 접근자와
  함수 안 정적 지역을 돌려주는 정의는 이름이 `instance()` · `shared()` 여도 잡는다. 이름을 보는 건 모양이 팩토리와 같아 못 가르는 포인터 · 스마트 포인터 반환뿐이다.
  `.cpp` 안 익명 · `…Internal` 이름공간의 정적 지역 접근자와, 헤더 선언만 있고 본문이 `.cpp` 에 있는 바깥 연결 자유 함수는 게이트가 보지 못하는 틈이다(현재 0건).
- **`.cpp` 안에서 함수 안 `static` 지역을 참조로 돌려주는 가변 상태 접근자(`static T& getXxx() { static T s; return s; }`)는 소유 구조로 옮긴다**(2026-10-25, Engine · Core · App · GameFramework).
  `CheckServiceAccess` 는 헤더만 훑어서 `.cpp` 의 익명 · `…Internal` 이름공간에 있던 같은 모양이 숨은 전역 가변 상태로 남아 있었다. 근본 원인은 "숨은 상태를 낳는 모양을 쓸 수 있다" 였으므로 접근자를 없애고 상태마다 소유자를 정했다.
  소유자는 모양으로 고른다. ① **소유 객체가 있으면 그 멤버**: 시나리오 시작 백엔드는 `RHIBackendSwitcher`, enum 글 파서는 `GlobalVariableManager`(변수가 `_pEnumTextParser` 로 자기 매니저의 파서를 가리킨다).
  ② **새 엔진 서비스**: 스크린샷 요청 우편함 `ScreenshotMailbox`(선택 행 — 전용 서버에는 없다. `RenderThread::requestScreenshot` 같은 정적 창구는 서비스를 찾아 부르고, 없으면 요청을 오류로 알리고 버린다).
  ③ **엔진 수명 범위(`ScopeOwned<T>`)**: 정적 함수가 부르는 캐시 · 표 — 메시 캐시 상태(공유 표 · 읽는 중인 로드 · 경고한 경로를 한 `State` 로), 물리 메시 셰이프의 읽기 함수, 공유 에셋 표(`SharedAssetCache<T>` 종류마다),
  내장 도형 · 9-슬라이스 · 스프라이트 텍스처 인스턴스 캐시, 셰이더 리플렉션 매니페스트(잠금 · 표 · 세대를 한 `ManifestState` 로), 게임 데이터 캐시(상호작용 · 원소 규칙)와 그 레지스트리, 일정 시스템 목록, "memory" 저장소 데이터.
  엔진이 내려가면 저장소째 사라지고 엔진을 다시 세우면 빈 채로 시작한다. 범위가 닫힌 뒤의 접근은 `getOrCreate` 가 단언하고, 읽는 쪽은 nullptr 을 그 자리에서 정한 뜻(없음 · 0)으로 받는다.
  ④ **"한 번만 알림" + 횟수**: 서버 버스 · 푸시 릴레이의 거절은 `LogOnce`(종류 `BusMessageRejected` · `PushRelayRejected`)의 `tryClaim` 한 호출이 세고(`getAttemptCount`) 한 번만 알린다 — 잠금 · 집합 · 카운터를 곳곳에 두지 않는다. 프레임마다 도는 길의 "필수 서비스 없음" 처럼 호출 지점마다 한 번이면 되는 줄은 `SW_LOG_ERROR_ONCE` · `SW_LOG_WARNING_ONCE`(종류 `CallSite`, 키는 파일 × 줄)를 쓴다.
  ⑤ **링크 때 등록 표**: `TickBatchRegistry` 는 등록 줄(`Entry`)을 등록 객체(`TickBatchRegistration<T>`)의 멤버로 두고 침입 연결 목록으로 잇는다(할당 없음, 머리는 상수 초기화). 엔진 범위가 서기 전에 올라가고 엔진을 다시 세워도 남아야 해서 `ScopeOwned` 로는 옮길 수 없다.
  ⑥ **호출 사슬 상태는 스레드가 소유**: 프리팹 펼치기의 순환 검사 스택 셋은 한 `PrefabThreadChain`(`thread_local` 한 변수)으로 모았다.
  ⑦ **Core 의 아래 층 · 핫 경로 칸은 상수 초기화되는 이름공간 변수 하나**(접근자 없음): 단언 대화상자 상태(`Macros.cpp`), 모듈 위치 바꾸기(`ModuleImageUtil`), 네트워크 흐름 링(`NetFlowLog`), 시나리오 사본 횟수(`AutomationScratch`), 고른 게임 이름(`GameSelection`).
  서비스 범위가 서기 전에도 서고 범위 없이 도는 도구 · 테스트가 부르는 정적 함수의 칸이라 범위에 맡길 수 없다.
  **옮기지 않은 곳과 이유**: `RHIRenderResource` 의 살아 있는 리소스 표(소유자가 리소스 객체 자신이고 정적 객체로 엔진보다 오래 사는 리소스가 있어 어느 범위보다 오래 살아야 한다 — 잠금과 목록을 한 객체로 모아 접근자를 둘에서 하나로 줄였다),
  링크 때 등록 표(`AssetTypeRegistry` · `DevCommandRegistry` · `DebugHUDRegistry` · `BehaviorTaskRegistry` 와 `RegistrationList` · `ServiceFactoryList` 계열은 등록 목록 통합 작업의 몫), 크래시 경로 저장소(`CrashContext`),
  카메라 흔들림 구독 델리게이트(`AnimationNotifyHandlerUtil::getCameraShakeRequested`), 범위가 닫힌 뒤에 빈 값을 내주는 폴백 정적(`Render2DSettings` · `ReflectionValidation`).
  `CheckServiceAccess` 의 모양 검사는 아직 헤더만 본다 — `.cpp` 로 넓히면 에디터 · 게임 영역에 남은 같은 모양이 모두 실패해 그 영역을 옮긴 뒤에 넓힌다.
- **`.cpp` 안의 "함수 안 `static` 지역을 참조로 돌려주는 접근자"(`static T& getXxx() { static T s; return s; }`)는 쓰지 않고, 상태에 소유자를 둔다**(2026-10-25, 에디터 영역).
  서비스 접근 게이트가 보지 못하는 숨은 전역 가변 상태였다 — 에디터를 둘 띄우거나 테스트를 병렬로 돌리면 섞이고, 모듈을 다시 불러도 값이 남거나 사라지는 때가 코드로 정해지지 않는다.
  소유자는 곳마다 가장 가까운 객체로 고른다. ① 값을 한 패널만 쓰면 패널 멤버이고 탐침은 `findPanel<T>()` 로 읽는다(`ScheduleDebugPanel` · `CharacterEditorPanel` · `RetargetPanel` 의 `Published`,
  `PreferencesPanel`; 패널의 다른 멤버에서 구해지는 값은 상태를 따로 두지 않는다 — `ModulesPanel` · `ShortcutsPanel`). ② 이미 있는 서브시스템이 주인이면 그 멤버이다(DPI 배율 → `EditorTheme`,
  텍스처 캐시 → `MeshModifierSession` 이 든 `MeshModifierTextureSampler` 인스턴스, 끌기 상태 → 이 확장이 기여한 서브시스템). ③ 한 파일 안에서만 쓰는 값(그리는 함수 · 탐침이 읽는 값 · 캐시)에는
  주인이 따로 없으므로 **에디터 범위의 상태 저장소** `EditorStateStore` 가 소유한다 — `getEditorState<T>()` 가 타입마다 값 하나를 처음 물을 때 `T{}` 로 만든다. 저장소는 `EditorContext` 가 맨 먼저 만들어
  컨텍스트와 함께 사라지고, 테스트는 `ScopedEditorStateService` 로 자기 몫을 건다. **EditorModule 안의 코드만 쓴다** — 확장 모듈 코드는 모듈이 내려갈 때 vtable 이 사라지므로 패널 멤버나
  `SW_EDITOR_SUBSYSTEM`(내려가기 전에 종료한다)을 쓴다. 상태 타입은 그 번역 단위의 `XxxInternal` 안에 둔다(타입 해시 열쇠라 이름이 겹치는 익명 타입 둘을 크기로 잡는다).
  `EditorUIContext::getCurrent()` 는 값을 따로 들지 않고 ImGui · ImPlot 의 현재 컨텍스트를 묻는다(주인이 ImGui 자신이다). 같은 규칙을 이어서 적용했다(2026-10-25):
  (가) 환경설정 섹션 값은 등록자 `EditorSettingsRegistrar<T>` 가 멤버로 든다 — 등록 항목이 값 포인터와 '파일 값 입힘' 표시를 가지므로 모듈이 내려가면 값과 표시가 함께 사라지고 다시 올라오면 새로 생긴다
  (함수 정적 인스턴스 · 입힘 주소 목록 · `forgetSectionsWithin` 을 지웠다). 등록되지 않은 섹션을 읽으면 기본값으로 대신하지 않고 멈춘다(`getSettingsRegistration`), 고칠 때는 `editPreferences<T>()`.
  (나) `ScopeOwned` 로 프로세스에 하나씩 둔 에디터 · 플러그인의 재사용 버퍼는 소유 상태 구조체로 옮겼다(상태 저장소, 메시 모디파이어는 확장 서브시스템 `MeshModifiersVisualizerState`). 저장소 없이 도는 테스트가 있는
  기하 유틸은 중간 버퍼를 인자로 받는다. 메시 모디파이어의 작업 관리자는 전역 설정 함수 대신 평가(`MeshModifierEvaluation::_pTaskManager`)가 싣는다.
  (다) `thread_local` 이 소유자인 상태는 짝을 맞출 대상이 스레드가 아니면 옮긴다 — `EditorChrome` begin/end 스택은 상태 저장소로, `makeIconLabel` 은 자기 버퍼를 든 값(`EditorIconLabel`)으로 돌려준다. X11 백엔드의 활성 포인터는 ImGui 가
  백엔드용으로 둔 `BackendPlatformUserData` 에 건다. 콜백에 사용자 포인터가 없는 ImGui 렌더러 가로채기의 원래 함수 포인터는 상태 저장소에서 찾는다.
  (라) Internal 의 가변 `inline static` 은 쓰는 객체의 멤버(`EditorDockLayout`)나 번역 단위 `State` 로 옮겼다. 옮기지 않은 것은 프로세스 단위가 성질인 값이다 — 엔진 · 코어의 ID 발급기와 파일 열기 계수기
  (`GameSound` · `AppearanceDatabase` · `PlatformFileUtil`), 디스크 마운트 · 경로 캐시(`ResourcePaths`), 쿠커의 해시 · 스탬프 캐시(`ShaderCookStamp` · `ContentHash` · `CookJob`), RenderDoc API 핸들, 모듈 리로드 세대(`ModuleShadowCopy`),
  엔진 수명 범위가 소유하는 `ScopeOwned` 캐시들. 에디터 범위가 소유할 값이 아니다.
- **스레드는 `sw::thread`, 백그라운드 잡의 상태는 한 번에 읽는다**(C-24). `Source/` 에서 `std::thread` 를 직접 만들면 `CheckStdThread` 가 막는다
  (예외: 래퍼 자신 · 크래시 재현 테스트). `sw::thread` 는 합류 대기를 잠금 관찰자에 알려 `join` 순환을 교착 검출기가 보게 하고, 로더 락 안의
  `join`(`DllMain` · 모듈 정적 소멸자)은 멈추는 대신 단언한다. `Test/` 의 쓰고 버리는 스레드는 대상이 아니다. 에디터 백그라운드 잡의
  `EditorBackgroundTask::take` 는 결과 유무와 대기 여부를 같은 락 안에서 읽어 `EditorTaskPoll`(`Idle` · `Running` · `Ready`) 하나로 돌려준다 —
  `take` 가 비었다고 `isPending` 을 따로 읽으면 그 사이 끝난 결과를 새 요청이 버린다(E-9 Quick Open 에서 나온 결함). 요청 여부는 `take` 의
  반환값으로 정하고, `isPending` 은 화면 표시용이다. 풀을 가리키는 핸들(`TaskHandle` · `TaskStageHandle`)의 수명 순서는 세대가 아니라
  참조 계수로 닫혀 있다 — 매니저가 사라질 때 남은 핸들이 풀을 이어받고(`TaskNodePool::detachOwner`) 마지막 핸들이 풀을 소멸시킨다.
- **엔진 루프 클래스는 `Engine` 이고 서비스 네임스페이스 `engine` 은 그대로 둔다**(2026-10-15, 정리 ⑥). 둘은 대소문자만 다르지만 C++ 에서 클래스(`sw::Engine`)와
  네임스페이스(`sw::engine`)는 다른 이름이고, 이 저장소의 규칙(타입은 PascalCase · 네임스페이스는 소문자, 예 `editor`)이 구분한다. 네임스페이스를 바꾸지 않는 까닭: `engine::` 가 엔진 범위 창구(`engine::getSubsystem<T>()`)의
  수명 이름이라(`game::` · `editor::` 와 짝) 이름만 바꾸는 큰 변경은 얻을 것이 적다. 클래스를 `EngineLoop` 로 두지 않는 까닭: 이 객체는 루프만이 아니라 부트스트랩 · 서비스 · 엔진 세 범위와 프레임 관찰자 ·
  종료 코드를 소유하는 엔진 호스트의 본체다. 언리얼의 `UEngine`(`GEngine`)도 틱 루프와 서브시스템 범위를 함께 가진 한 객체로 알고 있다(기억에 의존). 로컬 변수 `engine` 은 네임스페이스를 가리지 않는다 —
  한정 이름 조회(`engine::`)는 타입과 네임스페이스만 본다. 별칭 · 옛 이름은 두지 않는다(파일 `EngineLoop.h` · `.cpp` → `Engine.h` · `.cpp`, 멤버 `_engineLoop` → `_engine`).
- **호스트 범위가 엔진 범위의 부모다 — 엔진 앞에 서서 뒤에 사라지고, 호스트가 엔진에 내주는 것을 든다**(2026-10-15, 정리 ⑥). App · ServerApp 이 `SubsystemScope _host` 를 `_engine` 보다 앞에 두고
  (`Engine::initialize` 의 마지막 인자로 넘겨 부트스트랩 범위의 부모로 잇는다. 사슬은 엔진 → 서비스 → 부트스트랩 → 호스트). 거기에 거는 것은 둘이다.
  ① **모듈 이미지 창구**(`ModuleImageHostSubsystem`, `IModuleImageHost` 로 드러낸다): 엔진의 `ModuleTypes` 단계가 `loadTypeProviders` 를, `ModuleImages` 의 종료가 `onScenesReleased` 를
  자기 범위에서 부모로 올라가며 찾아 부른다. 옛 `Engine::setModuleTypeLoader` · `setOnScenesReleased` 와 델리게이트 멤버 둘은 지웠다(엔진이 호스트의 델리게이트를 들던 고리가
  "부모 범위의 호스트에 의존" 이 됐고, 호스트 범위가 없는 하네스는 자리만 둔다). DLL 언로드는 그대로 씬 뒤 · 서비스 앞(엔진 범위 종료의 `ModuleImages` 자리)에서 일어난다 —
  엔진 범위 종료는 모든 서브시스템에 `stopSubsystem` 을 먼저 돌린 뒤 `shutdownSubsystem` 을 도는 두 패스이고, 훅은 `ModuleImages` 의 종료 패스에서 `getSubsystemDependents`(입력 · 오디오 · 씬 · 비동기 IO) 뒤에 불린다.
  ② **자동화 구동기**(`AutomationSession`, App 만): 서브시스템이 됐다. 초기화는 하는 일이 없고 `-scenario` 를 읽은 App 이 엔진이 선 뒤 `begin` 으로 시작한다. 구동기는 엔진 서비스(로그 리스너 · 입력)를 빌려 쓰므로
  엔진 종료 앞에서 내려야 하는데 호스트 범위는 엔진 종료 안에서도 창구 때문에 살아 있어야 한다 — 그래서 창구가 `getSubsystemDependents` 로 구동기를 뒤에 서는 것으로 적고
  App 이 `shutdownDependentsOf( 창구 )` 로 구동기만 먼저 내린다(그 API 는 원래 백엔드 교체용이다). `Dev` 호스트 비트는 두지 않았다: 자동화는 Shipping 구성에서도 쓴다
  (`AppScenarioTest` 가 Shipping 에서 백엔드 스위치 없이 시나리오를 돌린다)라 `#if !defined( SW_SHIPPING )` 로 감쌀 개발 도구가 아니다. 개발 도구가 이 범위에 오면 호스트가 그 한 자리에서 `#if` 로 넣는다.
  언리얼은 엔진 서브시스템과 게임 인스턴스 서브시스템의 부모 · 자식으로 수명을 가르는 것으로 알고 있다(기억에 의존).
  `OnlineServerRuntime` 의 `_bHostStarted` · `_bSchedulerStarted` 는 지웠다 — 호스트(`OnlineServiceHost::shutdown`)는 자기 초기화 비트를 보고 스케줄러는 목록을 비울 뿐이라 시작 여부를 밖에서 적을 필요가 없었다.
  테스트 하네스(`TestHostRuntime`)는 호스트 범위를 두지 않는다 — 모듈 DLL 을 올리고 내리지 않고 타입은 리플렉션 단계가 이미 모았다(`EngineInitSequenceTest.ModuleImageStepsReachTheHostScopeThroughTheParentChain` 이
  범위 사슬 찾기를, 호스트 범위가 없을 때의 무동작을 지킨다).
  서브시스템 목록은 한 곳이다(`Engine/EngineSubsystemSet.cpp`, 2026-10-23) — 엔진 루프 · 하네스 · 기동 순서 테스트가 같은 세 함수로 범위를 채우고, 호스트마다 다른 것(인자 · 에디터 서비스 · 렌더러 단계 · 본문만 다른 대역)은 `*Setup` 의 이름 붙은 값으로만 준다. 목록이 셋일 때 테스트 쪽에서 `EditSessionState` 가 이미 빠져 있었다.
- **압축 해제의 원본 크기 상한은 코덱이 낸다 — `ICompressionCodec::decompressBound` 는 순수 가상이다**(2026-10-17, B-5). 야간 퍼저 `Compression` 대상이 81 바이트 입력(RLE · 원본 620 MB · 압축 0 바이트)으로
  10 초 타임아웃을 냈다. 헤더 검사는 절대 상한(`kMaxUncompressedSize`, 1 GiB)만 봐서 통과했고, 620 MB 를 resize(0 채우기)한 뒤에야 빈 페이로드로 실패해 clear 가 원소마다 소멸 루프를 돌았다.
  근본 원인은 헤더 값을 압축 크기와 견주지 않고 할당한 것이다. 최대 팽창률은 포맷마다 다르므로(RLE 65 · LZ4 255 · Deflate 1032 · Zstd 32768배) 코덱이 답하고, `CompressionStream` 은 두 해제 경로 모두
  할당 전에 `findCodecForHeader` 에서 견준다. 기본 구현("모르면 무한대")을 두지 않는 까닭: 그 코덱으로 적힌 스트림은 다시 헤더를 그대로 믿게 되어 같은 결함이 조용히 돌아온다. 절대 상한은 남긴다 —
  팽창률만으로는 Zstd 100 KB 가 3 GB 를 요구해도 통과한다. 같은 날 `vector` 의 소멸 루프는 `destroyRangeInternal` 하나로 모아 소멸자가 할 일이 없는 타입이면 루프를 두지 않고(`if constexpr`),
  `resize` 의 값 초기화는 산술 · 열거 · 포인터 타입의 64 칸 이상 구간만 `std::memset` 으로 한다(멤버 포인터의 널은 0 바이트가 아닌 ABI 가 있어 사용자 구조체는 넣지 않았다.
  짧은 구간까지 memset 으로 하면 크기가 변수인 함수 호출이 되어 `resize( size() + 1 )` 벤치가 Release 에서 두 배 느려졌다).

- 여러 스레드가 함께 더하는 카운터(활성 태스크 수 · 메모리 태그 통계)는 `ShardedCounter` · `ShardArray`(스레드 칸 16, 읽는 쪽이 합침)로 둔다(2026-10-18, 언리얼 LLM 스레드별 상태 · 리눅스 `percpu_counter` 와 같은 구조). "0 이 됐나" 를 보는 카운터는 같은 칸에서 올리고 내린다. 메모리 태그 최고치는 합을 읽을 때(프레임 · 패널) 본 값의 최고치다 — 측정(`docs/plans/StructuralOptimization.md`)에서 이득이 없으면 단위 커밋을 되돌린다.
- **`sw::make_unique` 는 함수 객체(변수 템플릿 `MakeUniqueFunction<T>`)입니다**(2026-10-22). 함수 템플릿이면 std 타입 인자가 ADL 로 `std::make_unique` 를 끌어와 STL 컨테이너 구성에서 호출이 모호해졌다. 일반 탐색이 객체를 찾으면 ADL 이 없다 — C++20 표준 라이브러리의 niebloid 와 같은 방식이다. `make_shared` 는 STL 구성에서 `using std::make_shared` 로 둔다(해제자가 같아서).
- **락을 쥔 채 로그 · 콜백 · 장치 · 외부 호출을 하지 않는다. 락은 잎 락이다**(2026-10-25). `mutex` 는 재진입 불가인데 로그는 싱크로 되돌아오므로, 락 안에서 남긴 로그가 같은 락을 다시 잡아 같은 스레드가 영구 정지한다(`AsyncLogSink::initialize` 가 락을 쥔 채 `FileLogOutput::open` 을 불러, 로그 폴더를 못 만들면 기동이 멈췄다. `addOutput` 의 거절 경고도 같은 모양). 모양으로 막는다 — 락 안에서는 포인터 복사(`snapshotOutputs`)와 상태 변경만 하고 호출은 락 밖에서 하며, 오류는 값으로 모아 두었다가 락을 놓은 뒤 남긴다(`ResourcePaths::mountDiskDomain` 도 같다). 장치가 자기 락을 쥐고 `write` 안에서 남기는 로그는 형식으로 막지 못해, `write` 깊이를 센 스레드 로컬이 stderr 로 돌린다(버리지 않는다). 리눅스 커널의 "락 안에서 printk" 규칙과 같은 방향이고, 언리얼은 로그 장치가 재진입을 스스로 막는다.

- **저장 전용 압축 타입(`half` · `unorm8/16` · `snorm8/16` · `OctahedralNormal` · `RGB10A2`)은 산술 연산자도 암시적 변환도 주지 않습니다**(2026-10-25, `Core/Math`). 계산은 `toFloat` 로 푼 `float32` 로 하고 결과를 `fromFloat` 로 담습니다.
  변환은 비트 연산을 직접 써서(F16C · NEON · `_Float16` 에 기대지 않음) 모든 플랫폼에서 같은 입력이 같은 비트이고 `constexpr` 입니다. 반올림은 가장 가까운 값(가운데는 짝수), 정규화 정수는 NaN 을 0 으로 자르고 snorm 은 정수 최솟값을 만들지 않습니다(D3D 규칙).
  월드 좌표와 큰 시간값에는 half 를 쓰지 않습니다(1024 이상에서 간격이 1 이 되어 0.4 m 이동이 사라집니다). 위치는 경계 정규화 `snorm16`, 법선 · 탄젠트는 옥타헤드럴, 색은 `unorm8x4` 입니다.
  언리얼은 `FFloat16` · `FPackedNormal` · 옥타헤드럴 법선을, 유니티는 `half` 와 `SNorm16` 정점 속성을 같은 목적(정점 · 인스턴스 크기 절감)으로 씁니다. 다른 점은 이 저장소가 `float32` 로의 암시적 변환까지 막아 `half + half` 를 실수로 쓰지 못하게 한 것입니다.
  적용 순서와 포맷 규칙은 [PackedTypes](../plans/PackedTypes.md)입니다.

- **반복문 안 `erase( begin() + index )` 와 `contains` 뒤 `push_back` 으로 중복 거르기는 새 코드가 못 쓴다**(2026-10-25). 숫자를 재지 않는 이유: "빨라질 것 같은 최적화" 가 아니라 한 프레임에 T 개가 한꺼번에 끝나면 이동이 O(T²) 로 느는 모양을 없애는 구조 정리라서 "성능 주장은 Release 숫자로만" 의 대상이 아니다.
  지우기는 `VectorUtil::removeIf`(순서 보존 · 한 번 훑기, 언리얼 `RemoveAll`), 중복 거르기는 집합이다. UI 의 재생 · 트윈 · 알림 · 전환 채널을 고쳤다. 같은 속성을 쓰는 애니메이션은 목록 순서대로 써서 마지막이 이기므로 swap-pop 은 아니다.
  전환 중 위젯 목록(`WidgetTree::_uniqueStyleTransition`)은 `unordered_set` 이다 — `sparse_set` 은 위젯 번호(프로세스 전체 증가 카운터)의 최댓값만큼 sparse 배열이 자라 맞지 않았다. 형식으로 못 막는 틈(표준 vector 멤버 호출)에만 게이트 `CheckIndexLoopErase` 를 기준선 래칫으로 둔다.

- **유일한 포인터 목록은 순서가 의미 없으면 `IndexedPtrList`(O(1) 등록 · 해제, swap-remove 로 순서가 바뀜), 등록 순서가 처리 순서이면 `AnimationOrderedList`(빈 칸 표시 후 접기, `forEach` 가 순회 중 등록 · 해제를 견딤)다**(2026-10-25, 통합 J). `AnimationSystem` LOD 클라이언트는 예산 판정이 등록 순서를 따르고 적용 훅이 순회 중 해제를 부르므로 후자, `PrimitiveRegistry` LOD 목록은 전자다. 언리얼은 순서 없는 `RemoveAtSwap` 과 순서 있는 `RemoveAt` 을 호출자가 고르게 하는데, 이 저장소는 그 선택을 컨테이너 형식으로 고정한다.
- **글로 받는 불리언 인자는 `StringUtil::tryParseBool` 하나로 읽습니다**(2026-10-25 통합 L, fixn · dup31). 철자 표는 1/0 · true/false · yes/no · on/off(대소문자 무관) 한 벌이고, 개발 명령은 `DevCommandRegistry.h` 의 `readOptionalBoolArgument` 로 받습니다. 손 철자 비교(`== "on"`, `!= "0"`)는 쓰지 않고 못 읽으면 조용히 false 가 아니라 거절합니다(`overlay.show <바> 2` 거절, `profile.trace 1|0|true|false` 수용은 이 결정의 동작 변화). 언리얼 `FParse::Bool` · Godot `String::to_bool` 도 한 함수입니다(기억에 의존).
- **종료 · 취소는 엔진 전체 활성 태스크(`waitAll`)가 아니라 자기가 낸 태스크만 기다리고, 끝나지 않는 대기는 시간 제한 뒤 이름을 남기고 멈춥니다**(2026-10-25 통합 L, fixm · fixr · fixw). 낸 쪽이 `TaskHandleTracker`(Core)로 핸들을 쥐고 `waitIdle` 로 기다립니다(에디터의 `EditorBackgroundJobTracker` 는 같은 일의 두 벌이라 지웠다). 메인 스레드의 `waitIdle` 은 메인 큐를 도와 교착을 피하고, `TaskManager::shutdown` 은 정지 신호 뒤 10 초 · `JoltJobSystem` 소멸자와 `AsyncFileIO` 는 30 초 안에 안 끝나면 남은 태스크 이름을 오류로 남기고 `std::abort()` 합니다(영구 대기보다 원인 있는 크래시가 낫다). 모듈 언로드 앞 대기만 엔진 전체 대기를 유지합니다 — 모듈 코드는 엔진이 낸 태스크의 델리게이트로도 들어와 입구를 한 곳으로 모을 수 없습니다. 언리얼 `FTaskGraphInterface` 의 종료도 자기 이벤트만 기다립니다(기억에 의존).
- **네이티브 창 핸들(`void*`)은 종류 확인과 값 확인이 한 `[[nodiscard]]` `try...` 함수에 든 형식으로만 읽고, 원 getter 는 기반 클래스 protected 로만 둡니다**(2026-10-25 통합 M, cifixl2 ~ cifixl4). 바깥은 `IWindow::tryGetX11Surface` · `tryGetWin32Handle`, ImGui 뷰포트는 `IImGuiPlatformBackend::tryGetViewportWin32Handle` · `tryGetViewportX11Surface`, 스왑체인은 `RHISwapChainDesc::tryGetWin32Handle`, 파생 캐스트는 `X11Window::tryCast` · `WaylandWindow::tryCast` 만 씁니다. 파생 창은 `getNativeHandle` · `getNativeDisplay` 를 public 으로 다시 열지 않습니다 — Wayland 창의 `wl_display*` 를 `Display*` 로 읽은 SIGSEGV 와 같은 모양이 호출처마다 종류 확인을 기억해야 하는 형식 탓에 여러 자리에 있었습니다. GLFW 는 플랫폼마다 함수(`glfwGetX11Window` · `glfwGetWin32Window`, 플랫폼이 다르면 오류)를, Godot 은 `DisplayServer::window_get_native_handle(HandleType)` 를 둡니다(기억에 의존). 여기는 종류 확인과 값 유무까지 한 호출에 묶어 실패를 무시할 수 없게 합니다.
