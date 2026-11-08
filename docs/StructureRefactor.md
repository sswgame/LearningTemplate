# 구조 리팩토링 설계 — 경로 · 신원 · 경계를 한 규칙으로

> 질문: Source 아래에서 일관성이 깨진 곳(한 층에 두 방식, 입력 종류를 추측하는 함수, 폴백, 같은 일을 하는 장치 둘)을 어떤 구조로, 어떤 순서로 고치는가?
> 독자: 이 리팩토링을 이어서 하는 세션(에이전트와 사람).
> 결론: 근본 원인은 하나다 — **경로 · 리소스 신원 · 명령 인자를 형식 없는 `string` 으로 넘기고, 답을 아는 쪽(기동 · CMake · 데이터 계층)이 값을 넘기지 않아 받는 쪽마다 글자로 다시 추측한다.** 그래서 규칙(1 절)과 기반 형식(2 절)으로 막는다. 이 문서는 그 규칙 · 주제별 결정 · SOLID 점검의 판단과 남은 것만 둔다.

## 1. 규칙

| 규칙 | 뜻 | 상용 엔진 |
|---|---|---|
| R1 OS 경계 하나 | 파일 · 폴더 · 라이브러리 · 환경 변수 · 실행 파일 경로 같은 OS 호출은 `PlatformFileUtil`(과 그 짝) 한 곳만 한다. Win32 · POSIX 네이티브로 통일하고 `std::filesystem` 은 걷어 낸다 — 한 층에 오류 모델 · UTF-16 변환이 둘이면 결함이 난다 | 언리얼 `IPlatformFile`, Godot `FileAccess`·`DirAccess` |
| R2 디스크 I/O 는 절대 경로만 | `FileUtil` · `PlatformFileUtil` 의 I/O 는 절대 경로만 받는다. 상대 · `C:foo` · `\foo` 는 오류. 경로 글 연산(`joinPath` 등)은 대상이 아니다 | |
| R3 리소스는 리소스 경로만 | `AssetPaths` · `AssetIO` 는 `ResourcePath{도메인(선택), 상대 경로}` 만 받는다. 도메인을 비우면 우선순위로 찾고, 넣으면 그 도메인만. 문자열에서 도메인을 추측하지 않는다 | 언리얼 `/Game/…`, 덮어쓰기는 pak 우선순위 |
| R4 쓰기는 FileUtil 이 절대 경로로 | `AssetPaths` · `AssetIO` 는 읽기와 경로 풀기만. 있는 파일은 리소스 경로를 디스크 위치로 풀어 쓰고, 새 파일은 도메인 루트 아래에 잇는다 | |
| R5 상대 → 절대는 경계에서 한 번, 기동 때 정한 기준으로 | 명령줄 · 설정 · 콘솔 · 시나리오 값은 들어오는 자리에서 **기동 때 고정한 루트** 기준으로 한 번 바꾼다. 실행 중 작업 폴더는 읽지 않는다 | 언리얼 `FPaths::ProjectSavedDir` |
| R6 폴백 없음 | 필요한 값이 없으면 오류로 멈춘다. "못 찾으면 받은 글", "실패하면 기본값", "실패하면 다른 구현" 은 없앤다 | |
| R7 확인과 실행은 같은 해석 | 있는지 · 쓸 수 있는지를 묻는 쪽과 실제로 읽고 쓰는 쪽이 같은 함수로 푼다 | |
| R8 같은 일은 하나 | 같은 판정 · 정규화 · 표 · 백오프 · 파서는 하나만 둔다. 답을 아는 쪽이 값을 넘긴다(재추측 금지) | |
| R9 구성 차이는 크기대로 | 값뿐이면 `build::kShipping` 상수(두 갈래 모두 컴파일), 갈래 하나가 다른 구성에 없는 헤더를 쓰면 그 자리 `#if`, 기능 전체가 없으면 빌드 단위(`<이름>Shipping.cpp` · 폴더째 빼기). 몇 줄짜리 차이로 파일 짝을 만들지 않는다([BuildConfigSwitch](agents/BuildConfigSwitch.md)) | 언리얼 Runtime · Developer · Editor 모듈과 `#if !UE_BUILD_SHIPPING` |

## 2. 기반 형식 (0 단계 — 나머지가 모두 기댄다)

| 형식 · 장치 | 내용 | 없어지는 것 |
|---|---|---|
| `AbsolutePath` + 엄격한 판정 | 검증하는 생성 함수로만 만든다. Windows 는 `X:/` · `X:\` · UNC, POSIX 는 `/`. **지금 `FileUtil::isAbsolutePath`(FileUtil.cpp:450)는 `C:foo` · `\foo` 를 절대로 본다** | 느슨한 판정 |
| `ProcessRoots` | 기동 때 한 번(실행 파일 위치 · `-project=`): binary · project · content · saved · logs · crash · userData · packs · DDC. 못 정하면 기동 오류. 지금 Saved 기준이 **일곱**(작업 폴더 · 실행 파일 폴더 · 프로젝트 · `UserDataPath` 상대 폴백 · `TestRunner` · RenderDoc · 크래시 폴더 "로그 부모" 세 곳)이다 | `getSavedFolderPath` 의 작업 폴더 의존, `AssetPaths::initialize` 의 작업 폴더에서 위로 찾기, 팩 폴더 후보 체인(AssetManager.cpp:155), `AutomationSession` 의 작업 폴더 복구, `"Saved"` 리터럴, `UserDataPath::makeFallback` |
| `ResourcePath{DomainID, RelPath}` | 만들 때 정규화(소문자 · `/` · `./` 제거 · `..` 거절). 문자열에서 암시적으로 못 만든다. 캐시 · 쿠킹 입력의 키는 **해석이 끝난** 값 | `AssetID` 의 무정규화, 캐시마다 다른 키 정규화 다섯(SharedAssetTable · PrefabCache · AssetPaths 캐시 · CookJob)과 Texture · Material · Mesh · UIText · PhysicsMeshShape 의 `normalizePath` 키 |
| `DomainTable` | 도메인 이름표(engine · common · editor · `game/<팩>` · `dlc/<이름>` · scratch · 붙인 디스크 도메인) · 우선순위 · 쿠킹 대상을 한 표로. 데이터의 한 줄 참조(`common/x`)를 두 칸으로 가르는 일은 이 표의 최장 일치 한 곳에서만 | 도메인 판정 셋(`mapGlobalIDToRoot` 의 디스크 폴더 존재 · `PackManager::trySplitDomainPrefix` 의 글자 · 팩 파일 이름 `_` 변환 세 곳), 도메인 나열 둘(`buildResourceFolders` · `PackCooker::collectDomains`), 우선순위 표 둘 |
| 입력 경계 형식 | `CommandLineManager` 의 값 종류 `DiskPath`(parse 때 R5 로 절대화, 오류는 모아 기동 실패), `gv_` 보류 값도 같은 변환기, 개발 명령 인자 읽기 `readDiskPathArgument` · `readResourcePathArgument`, 시나리오 단계 등록이 속성마다 경로 종류를 선언(`{out}` 은 디스크 칸에서만) | `setValue` 의 "실패하면 0 넣고 읽음 표시", `"--service"` 글자 비교, 토큰 나누기 둘, `prefab.spawn` 류 추측, 녹화 경로의 글자 치환(`makeReplayID`), `bindOutputDirectory` 의 모든 속성 치환 |

## 3. 주제별 결정 · 남은 것

앞으로도 지킬 결정과 남은 것만 둔다(이 설계로 옮긴 단계의 기록은 git 에 있다).

### T2 · T3 리소스 경로 · 읽기 쓰기
- `TextureCache` 의 렌더 타깃 기본 크기(256)는 그대로 둔다 — 카메라가 나중에 크기를 알리면 `resizeRenderTarget` 이 다시 만들어 맞추므로 폴백이 아니라 자리 표시 값이다(언리얼 렌더 타깃 에셋의 기본 크기와 같다). 씬 데이터의 `string _…Path` 칸(컴포넌트 44 개)을 참조 형식으로 바꾸는 일은 하지 않는다 — 그 값이 디스크 입구(`FileUtil`)로 가는 곳은 0 이고(조사), 7 단계부터 상대 경로가 OS 경계에 닿으면 오류라 남은 위험도 드러난다. 리플렉션 · 세 직렬화기 · 에디터 위젯을 바꾸는 비용에 견줘 얻는 것이 작다(새 결함이 이 모양으로 나오면 그때 형식으로).
- `collectAssets` 는 소스 · 팩 구성 모두 합집합이다(같은 이름은 앞선 도메인 · 높은 팩) — 언리얼 `IterateDirectory` 가 pak 과 낱개를 합쳐 훑는 것과 같다. 팩의 도메인은 팩 안(스트링 풀 맨 앞, 형식 2)에 적고 파일 이름으로 추측하지 않는다(언리얼 pak mount point).
- 문서 패널의 "읽을 문서 정하기" 여섯 줄 11 벌은 `EditorDocumentPanel::acceptDocumentToLoad` 하나로, 읽기 · 쓰기 자리는 `getDocumentDiskPath` 하나로 모았다. 기반 밖 자체 저장 패널 8 개(Catalog · InputMap · AudioEvents · DataTable · ProjectSettings · GlobalVariables · UIPreview · AudioGraph)는 단일 문서 기반으로 옮기지 않는다 — 이미 `IEditorPanel` 의 저장 규약(`saveDocument` · `revertDocument` · `canSaveDocument` - Ctrl+S · 닫기 확인)을 함께 쓰고, 여러 파일 · 페이지를 다루는 창에 "문서 하나" 정체성을 지어내야 한다. 언리얼도 에셋 편집기(`FAssetEditorToolkit`)와 설정 창(디테일 뷰)을 나눈다.
- `AssetPaths` 의 공개 해석은 `toDiskPath`(리소스 id → 자리) · `findLooseFile`(있는 낱개 → 자리) · `toAssetID`(자리 → id) · `collectLooseFolders`(나열) 와 설정(`setSearchPriority` · `mountDiskDomain`). 개발 명령의 디스크 경로 인자(`readDiskPathArgument`)는 글로 둔다 — 받는 쪽 대부분이 글을 받고, 저장 함수로 갈 때만 `FileUtil::requireAbsolutePath` 로 형식에 올린다. 문서 · 직렬화기의 `saveToFile`(XML · JSON · Archive)은 `FileUtil` 과 같은 층의 원시 연산이라 글을 받는다(OS 경계가 상대 경로를 거절한다).
- 붙인 디스크 도메인(`isMountedDiskDomainID`)은 팩을 보지 않는다.
- 시나리오 단계 속성마다 경로 종류를 선언하지 않는다 — `{out}` 은 명령 글 안에서도 쓰여 속성 형식으로 막을 수 없고, 경로를 받는 명령이 형식을 검사한다(언리얼 자동화 스펙도 속성에 형식이 없다).

### T6 저장 파이프라인
- 런타임 저장소(`GameLocalStore`, 게임 범위): 언리얼은 플랫폼 `ISaveGameSystem` 위에 `AsyncSaveGameToSlot`(완료 델리게이트), 유니티는 앱 수명 `persistentDataPath` 다 - 봉투 · 백엔드가 GameFramework 에 있어 엔진 범위에 둘 수 없고 슬롯 폴더가 게임마다 달라 게임 범위에 둔다. 백엔드는 코드가 `file` 로 고른다(SQLite 키트를 gamesettings 로 고르는 칸은 쓰는 게임이 생길 때). 완료 배분은 기반 `LocalStoreChannel`(GameFramework `Online/Local`)로 뺐고 `GameLocalStore` 는 그것을 든 게임 범위 서브시스템이다.
- 에디터 사용자 자료(`CharacterEditorLocalStore`): 에디터는 게임 인스턴스 밖이라 게임 범위 `GameLocalStore` 를 쓸 수 없고, 사용자 자료라 콘텐츠가 아니다 - 언리얼은 에디터 사용자 설정을 `Saved/`, 유니티는 `EditorPrefs` 에 둔다. 쓰는 확장이 하나라 그 확장이 주인이고, 둘째가 생기면 공용 에디터 플러그인으로 옮긴다. 스크린샷 창구가 파일로만 내놓아 캡처는 사용자 데이터 폴더의 임시 파일을 거친다(창구가 바이트를 내면 바로 받는다).

### T7 같은 일 하나로
- 디렉터 컴포넌트의 카탈로그 읽기는 컴포넌트 칸의 경로로 씬마다 다른 데이터라 게임 범위 `loadRequiredCatalog` 로 옮기지 않고 실패 등급만 ERROR 로. 파일 글 되돌리기 둘(프리팹은 캐시를 바로, 리그는 파일 감시가 다시 읽는다)은 뒤따르는 일이 달라 합치지 않는다.

### T8 구성 차이
- 플랫폼 폴더는 CMake 가 고른다(`sw_filterPlatformSources` — Core · Engine · DevTools). 플랫폼 폴더 `.cpp` 에 파일 전체 플랫폼 `#if` 를 두지 않는다(언리얼 UBT 의 플랫폼 폴더 규칙).
- 리플렉션의 에디터 메타데이터(카테고리 · 표시 이름 · 툴팁 · 커스텀 메타 · 편집 조건 · 슬라이더 범위 …)는 옆 표(`ReflectionEditorMetadata.h`)에 두고 런타임 메타데이터는 포인터(`_pEditor`)로만 가리킨다 — 구조체 배치가 구성마다 같다. 값은 헤더마다의 개발 전용 단위(`<이름>.gen.meta.cpp`)가 정적 등록으로 올리고 `TypeRegistry` 가 등록 때 붙이며, 배포 구성은 그 단위를 짓지 않는다(언리얼 `WITH_METADATA` 자리를 `#if` 대신 빌드 단위로). 새 표시 · 편집 필드는 `ReflectionEditorMetadata.h` 에, 런타임 필드는 `ReflectionMetadata.h` 에 더한다.
- 전용 서버는 플랫폼 구현을 폴더째 고른다 — `Window/Linux` · `Input/Linux`(와 오디오 · UI)는 짓지 않고(`SW_UNBUILT_ENGINE_FOLDERS`, 헤더는 `serverbuild` 게이트) 창 없는 대체는 `Window/Headless` · `Input/Headless` 가 정의한다(클라이언트는 `Headless` 를 짓지 않는다). 엔진 층에 `SW_WITH_CLIENT_CODE` 구역은 빌드가 무엇을 담는지 말하는 파일(`Config/Dev/ClientCodeGuards.json`)에만 둔다.
- **남긴 구성 분기와 이유**:
  - `GameObjectManager::resolveComponent` 의 타입 확인 — 뜨거운 길의 검사(언리얼 `DO_CHECK` 자리)라 함수 호출로 바꾸면 비용이 는다.
  - 스위치를 정의하는 자리(`ContentSource.h` · `AnimationRewind.h` · `ProfilerBackend.h` · `ModuleCatalog::getCurrentConfiguration`)는 대상이 아니다. `SW_PROFILER_TRACE_ENABLED`(ServerApp 1)는 그대로다.
  - 개발에서만 허용하는 동작(평문 공개 게임 포트 · 개발 인증서 · 가짜 제공자)은 구성 이름이 아니라 개발 대체물 등록으로 묻는다(`Core/Network/DevOnlineExtension.h` — 배포는 등록하지 않는다).

## 5. 남은 확인

- 배포 구성(통합에서 한 번): `py -3 -m Scripts shipping-check` — `Ninja-Shipping` · `Ninja-Shipping-Server` 를 짓고(쿠킹 포함), 배포 App 을 몇 초 돌려 창 닫기로 0 종료, 서버를 몇 초 돌린 뒤 종료 신호(Ctrl+Break · SIGTERM)로 0 종료, 배포 App 의 `-scenario` 가 기동 오류인지(배포 구성은 자동화 실행기 · 테스트용 변수를 짓지 않는다) 보고 로그의 [Error] · [Warning] 을 모아 찍는다. CTest 가 아닌 이유: 프리셋마다 빌드 폴더가 달라 한 ctest 트리에 담기지 않는다.
- 새로 만들 확인(원칙 4 — 손 확인 대신 자동화): 저장 왕복 시나리오가 아직 없는 패널 — Catalog · InputMap + 재생 · BehaviorTree · Rig · Terrain · AudioEvent/Graph. 틀(`panel.saveDocument` · `panel.reloadDocument` · `asset.snapshotScratch` · `asset.expectScratchUnchanged`, 예: `saveroundtripmaterial.scenario.xml`)은 있다. 이 패널들은 경로로 열리지 않거나(자체 저장 패널 · 리그) 고정 파일을 쓰므로 `scratch` 사본을 가리킬 문서 경로 입구가 먼저 필요하다.

## 7. SOLID 점검 — Core · Engine (2026-11-01)

근거: `Scripts/dev/StructureAudit.py`, 정적 함수 이름 겹침 스크립트(Core · Engine `.cpp` 의 `static` 함수 이름이 두 파일 이상), 층 표(`CoreLayers.toml` · `EngineLayers.toml`)와 실제 include · 전방 선언 대조, 큰 파일 읽기. graphify 는 이 질문(중복 · 층)에 범위를 좁혀 주지 못해 쓰지 않았다. 이름이 같아도 하는 일이 다른 것(`appendFloat` 넷 중 셋, `isSpace` 둘, `isNameChar` 둘, `makePerpendicular` 둘, `isLive` 둘, `toTicks` 둘)은 표에서 뺐다.

| 순위 | 종류 | 문제 | 고친 모양 | 상태 |
|---|---|---|---|---|
| 11 | 게이트 없음 | Core 티어(`CoreLayers.toml`)는 낮은/높은 둘로만 지어 티어 사이 역류를 빌드가 막지 않았다 | **끝(2026-11-01)** — Log 아래 · 위는 빌드(낮은 부분의 `#error` 껍데기), 높은 티어끼리는 커밋 린트(`Scripts/lint/CoreTiers.py`)가 직접 include 를 본다. 높은 티어 여섯마다 빌드 부분을 지으면 부분 · pch 가 늘어 빌드가 느려진다 — 형식으로 싸게 막을 수 없는 틈이라 게이트를 둔다. 지금 위반 0 건 | 끝 |
| 13 | OCP | `BindingKind`(10 종) switch 가 3 파일(평가 · 리맵 직렬화 · 글리프), 자동화 · UI · 녹화에 몇 개 | **닫음(2026-11-06, 그대로 둔다)** — 종류에 딸린 값은 이미 표 하나(`BindingKinds`, `Count` static_assert)이고, 남은 switch 는 종류마다 다른 동작인데 모두 `-Werror=switch`(+`covered-switch-default`) 아래라 종류를 늘리면 빠진 자리가 컴파일 오류다 — 함수 표로 바꿔도 얻는 검사가 없고 간접 호출만 는다. 언리얼 `UInputModifier` 처럼 종류를 클래스로 여는 것은 바인딩이 플러그인에서 늘어날 때의 일이다. 도형 삼각화는 `PhysicsShapeMesh` 하나로 | 끝 |
| 14 | SRP | `TaskManager.cpp` 1540 · `NetEndpoint.cpp` 1337 · `StringUtil.cpp` 1297 · `MemoryProfiler.cpp` 1009 줄 | 크기만 보고 파일을 나누지 않는다(8.3). 제 상태와 규칙이 있는 묶음만 본다. `NetEndpoint` 는 닫음 — 슬롯 · 챌린지 · 봉인은 이미 형식(`NetSlotTable` · `NetChallenge` · `NetPacketSealer`)이고 남은 것은 한 잠금 아래의 핸드셰이크 상태 기계다. `StringUtil` 은 `Core/String`(티어 4 글자 도구)으로 옮겼고 UTF 변환 · 검사는 그 안에 둔다(언리얼 `Core/String` 이 변환 · 분할을 한 갈래에 두는 것과 같다). **남김**: `MemoryProfiler` 의 누수 창(칸 6 · 함수 8) → `MemoryLeakWindow`, 정적 함수 `judgeLeakCurve` · `printLeakWindowReport` → 기준 · 보고서 형식의 멤버. 호출부에 `TestMemoryProfiler` 가 있어 테스트를 함께 고칠 수 있을 때 한다 | 남김 |
| 15 | 같은 일 둘 | Wayland xdg 리스너 둘, MemoryTag 이름표가 `Diagnostics`(enum 은 `Memory`) | 이름표는 끝(2026-11-02 — 열거형과 이름 표가 `SW_MEMORY_TAG_LIST` 하나에서 나온다). Wayland 리스너는 끝(2026-11-03 — 창 · 스플래시가 `WaylandShell` 하나의 핑 응답 · configure 확인 · 첫 configure 대기 · 이벤트 펌프를 쓴다). float3 성분별 곱 12 곳은 `float3::multiply`, 나눗셈 둘은 뜻대로 `Pose::computeScaleRatio` · `RigPoseBuffer::removeParentScale`, CRC32 는 `HashUtil::computeCRC32`(2026-11-01) | 끝 |
| 16 | 이름 | 호스트 창구 헤더와 서비스 순서 표의 겹침, `Engine/FileSystem` ↔ `Core/File`, 관측 넷 | **끝(2026-11-01)** — 호스트 창구는 `Engine/Module/HostInterfaces.h`, 콘텐츠 주소 · 읽기 폴더는 `Engine/Content`. 관측 넷은 두기로 했다 — 역할이 다르다(Core `Diagnostics` 크래시 · 호출 스택 · 교착, `Profiling` 프레임 프로파일러, `Observability` 지표 · 디버그 채널, `Telemetry` 바깥으로 보내기 — 언리얼 HAL 크래시 · Stats · Analytics 와 같은 갈래) | 끝 |

- **근본 원인(한 줄)**: 바닥 도우미가 찾기 어려운 자리(Foundation · Container · TU 전용 Internal)에 있어 호출부가 사본을 만들었고, 층 검사는 include 글만 봐서 전방 선언 칸 · 함수 포인터로 역류가 숨었다.

## 8. SOLID 점검 — GameFramework · Editor · App

2026-11-01. 범위: `GameFramework`(Runtime · Kits · Plugin/Editor) · `Editor` · `App` · `ModuleManager` · `DevTools` · `Games`(Core · Engine 내부는 다른 작업자). 방법: `Scripts/dev/StructureAudit.py`(폴더 크기 · 모호한 이름 · 정적 전용 타입), 영역별 정독 셋(키트 · 게임 / 편집기 패널 / 호스트 · 모듈 관리). graphify 그래프는 작업 트리에 없어 쓰지 않았다.

### 8.1 우선순위 표

장기 이득(같은 결함이 다시 나올 자리 수 × 결함의 무게) 순이다. "닫음" 은 하지 않기로 한 판단이고, 남은 "끝" 줄은 코드 주석이 가리켜 둔다.

| # | 문제 | 근거 | 조치 · 상태 | 상용 엔진 |
|---|---|---|---|---|
| 7 | App · Server 의 종료 순서 · 프레임 본문 두 벌 | 프레임(beginFrame → fixed/update → 리로드 갱신 → 상태 해시)이 두 파일에 | **끝(2026-11-01)** — 겹치던 게임 걸음(고정 스텝 반복 → 갱신, 프로파일 구간)은 `GameModuleManager::updateGameFrame` 하나. 나머지는 실행 파일마다 다른 일이다(App: 에디터 UI · 페이싱 · 사용자 설정 · 백엔드 교체, Server: 서비스 재시작 · 틱 지표 · 드레인). 종료의 `shutdownDependentsOf` 는 모듈 이미지 호스트가 있는 App 만의 일이다 | FEngineLoop::Tick |
| 8 | 파일 감시 넷이 나란히 | `LiveReloadManager` 의 날 감시기, `FileWatchDispatcher` 셋(설정 · 콘텐츠 폴더 · 편집기 애셋), 셰이더는 손 단축키 | **닫음(2026-11-01, 같은 구현 하나다)** — 넷 모두 OS 감시기 추상 하나(`IFileWatcher::createNative` — ReadDirectoryChangesW · inotify)를 쓰고, 접두사 · 확장자 거르기는 `FileWatchDispatcher` 하나다. 뿌리 폴더(설정 · 콘텐츠 · 모듈 폴더)마다 인스턴스가 따로일 뿐이다(언리얼 DirectoryWatcher 도 폴더마다 등록한다). 셰이더는 일부러 수동 재컴파일(Ctrl+F8 — 언리얼 `recompileshaders changed` 와 같다) | 언리얼 DirectoryWatcher 모듈 하나 |
| 12 | 온라인 소도우미 복사 | JSON `readText` 넷, HTTP `isTransientStatus` 둘, `encodeID` 둘 | **닫음(2026-11-01, 같은 일이 아니다)** — `encodeID` 는 저장 형식이 다르고(16 진 글 · 버전 + varint, 합치면 저장된 값이 바뀐다), `isTransientStatus` 는 제공자마다 뜻이 다르다(애플 401 · 스팀 401 · 403). `readText` 는 `JSONValue::findPathText` 의 한 줄 포장이고, 기본값을 받는 것(로그인 제공자 설정)은 종류마다 정한 문서화된 기본이며 필수 칸은 `isValidSettings` 가 따로 본다 | 끝 |
| 14 | Runtime 에 장르 기믹 | `Runtime/Gameplay/Gimmick/Genre/*` 약 2.2k 줄 | **닫음(2026-11-01, 두기로)** — 콘텐츠를 보니 이 컴포넌트들은 장르 키트 하나가 아니라 공용 프리팹 라이브러리(`common/prefabs/gimmicks/{shooter,platformer,racing,horror,…}`)가 쓰고, 쇼케이스와 여러 게임이 장르를 가리지 않고 가져다 쓴다. 장르 키트로 쪼개면 공용 프리팹을 쓰는 게임마다 그 키트를 매니페스트에 넣어야 해 결합이 는다 — 언리얼 GameplayFramework · 스타터 콘텐츠처럼 공용 런타임 라이브러리로 둔다 | 끝 |
| 16 | 큰 패널 · 정적 모음(SRP) | CharacterEditorPanel 1389 줄 · ContentBrowser 1174 · UIPreview 1169 · ProfilerPanel 1120 · InputMapPanel 1015, 정적 모음 몇 | **닫음(2026-11-01, 쪼개지 않는다)** — 패널의 상태는 이미 Model 클래스(ImGui 없는 층)가 들고 패널은 그리기만 한다. 탭마다 클래스 · 파일로 더 가르면 파일 수만 늘고(사용자 판단: 분리가 과하다) 얻는 검사가 없다. 탭이 독립된 상태 · 수명을 갖게 될 때만 그 탭을 뗀다. 사용처가 하나인 도우미는 반대로 그 .cpp 안으로 합친다(`docs/agents/Helpers.md`) | 끝 |
| 17 | 큰 인터페이스 | ModuleAPI `EditorAPI` 함수 칸 22, `IEditor` 순수 가상 16 | **닫음(2026-11-01, 두기로)** — 호스트 ↔ 에디터 모듈 경계의 C ABI 표 하나다(수명 · 프레임 · 입력 · 뷰포트 · 플레이). 역할별 하위 표로 가르면 간접 참조와 ABI 판 변경만 늘고 얻는 검사가 없다 — 호출하는 쪽은 호스트 하나(`EditorModuleManager`)뿐이다. 언리얼 모듈 · 플러그인 경계도 모듈마다 인터페이스 하나다 | 끝 |
| 18 | 층 역류 | ModuleAPI 의 `Export/GameFrameworkAPI.h`(`SW_GF_API` · `SW_KIT_API`) — 계약 층이 위층 이름을 알았다 | **끝(2026-11-01)** — 매크로 정의는 `GameFramework/GameFrameworkExports.h` 로. 계약 층에서 쓰던 곳은 게임 서비스 선언 하나뿐이라 그 매크로(`SW_GAMESERVICE_API`)는 자기 빌드 정의(`SW_GAMESERVICE_EXPORTS` / `IMPORTS`, GF 줄이 준다)로 선다. 키트 · 게임은 원래 `GameFrameworkExports.h` 를 거쳐 받아 include 가 따라가지 않았다 | 끝 |

키트 사이 장르 로직 중복은 찾지 못했다(라운드 · 차례 · 카운트다운 · 게이지 · 땅 · 격자는 이미 Runtime 하나). 게임 코드가 다른 게임 · 키트가 게임을 include 하는 역류도 없다.

### 8.2 결정

- `GameFramework/Base` 를 `Runtime` 으로: 폴더만 바꾸고 대상 이름(`GameFramework`) · 내보내기 매크로(`SW_GF_API`) · 매니페스트는 그대로다. 언리얼 플러그인의 `Source/Runtime` · `Source/Editor` 와 같은 뜻이고 형제 `Kits/` · `Plugin/` 와 겹치지 않는다. 층을 하나 걷어 Runtime 내용을 `GameFramework/` 바로 아래 두는 안은 키트 · 플러그인과 섞여 버렸다.
- `Plugin/Editor/Base` 를 `GameplayTools` 로: 내용이 행동 트리 · 어빌리티 · 기믹 · 일정 도구이고 다른 확장이 기대지 않는다(기반이 아니다). 형제 폴더(CharacterEditor · MeshModifiers …)처럼 내용 이름을 쓴다.
- 자동 플레이 되돌리기는 디렉터 기반(`GameModeComponent`)이 아니라 조종 영역(`ControlSystem`)에 둔다 — 빙의 넘기기는 디렉터가 아니어도 쓰는 일이다. AI 조종자를 찾거나 만드는 쪽은 게임마다 달라(프리팹 · 코드) 그대로 둔다.
- 소리 그래프는 그래프 문서 템플릿(`EditorGraphDocumentPanel<T>`)이 아니라 문서 기반을 쓴다 — 링크 표(`_listLink`)가 없고 연결이 받는 노드의 입력 목록이라 템플릿의 요구를 맞추지 못한다.

### 8.3 큰 클래스의 상태 묶음 (2026-11-01)

크기만 보고 cpp 를 나눈 것은 모두 합쳤고(같은 클래스는 한 파일), 제 상태와 규칙이 있는 묶음만 클래스로 뗐다.

- RTSWorld → `RTSVision`(팀 시야 · 다시 칠하기 타이머) · `RTSUnitBuckets`(이웃 버킷) · 내비 `FlowFieldCache`(목적지별 흐름장 공유 — 다른 키트도 쓴다). 유닛 슬롯은 그대로다: Core `SlotHandleTable` 은 잠금 · 원자값 동시 표이고, 여기는 슬롯 순서 · LIFO 빈 칸이 결정적 결과와 저장 바이트에 들어간다. 옮기기 전후 같은 동작은 `RTSWorldTest`(공개 조회 요약 고정값)가 묶는다.
- InputMap → `InputTriggerRules`(바인딩 상태 · 문턱 · 홀드 · 탭 · 연타 · 끌기 · 반복 · 펄스 판정 — 언리얼 `UInputTrigger` 자리, 장치 · 레이어를 모른다) · `InputBinding.h`(스키마 형식). 맵은 장치 읽기와 액션 합산만 한다.
- AudioEngine 명령 큐 → Core `LockedSwapQueue`(잠금과 목록을 한 형식에 — 목록을 잠금 없이 만지는 모양을 쓸 수 없다). 재생 id 는 큐에 넣는 잠금 안에서(`pushStamped`). 같은 큐로 DeferredDelegateQueue · ContentBrowserImportQueue · Fracture 맞은 사건도. 손 잠금이 남은 곳(GameObjectStore 시작 대기열 — 잠금 순서 `SW_ACQUIRED_AFTER`, Loopback · FractureNetworkSync · RenderExtensionRegistry · TextureCache — 잠금 안에서 다른 상태도 만진다)은 순수 큐가 아니다.
- AudioEngine 보이스 슬롯 · 잡기 · 반환 · 실제/가상 고르기 → `AudioVoicePool`(언리얼 FMixerSourceManager 자리). 버스 · 감쇠 · 인스턴스 번호 풀이는 엔진.
- ScheduleSystem 빌린 서비스 → `Services` 묶음 하나. 경로 · 자리 제공자는 생성자부터 기본 제공자를 가리켜 null 이 없다.
- 에디터 콘솔 명령 모음(`EditorDevCommands.cpp`) → 기능의 명령 파일 옆(언리얼 Exec 명령이 그 시스템에 있는 것과 같다). 남은 진단 도구 폴더는 `Commands/Diagnostics/` — `/Dev/` 는 빌드 규칙에서 "배포가 짓지 않는 단위" 라는 뜻이다.
- cpp · 헤더 짝: 짝 없는 cpp 는 스스로 등록(탭 · 서버 서비스 · 데이터 종류 · 자동화 단계 · 명령), 플랫폼 · 라이브러리 · 형식별 구현, 모듈 진입점만 남았다. 하나만 include 하던 pimpl 헤더(`MaterialCacheImpl.h` · `TextureCacheImpl.h`)는 cpp 안으로, 빈 cpp(`TypedHandleTestComponent.cpp`)는 지웠다.

## 9. 점검 방법의 빈틈 — 동작 기준 (2026-11-01)

- **놓친 것**: `Material` 을 두 스레드가 고치는 모양(렌더 스레드 초기화 · 인스턴스 `updateRHI`), 게임 스레드의 직접 그리기(`FrameRenderer::execute( scene )`)가 패킷 경로 · 미리보기와
  같은 일을 세 번 하던 것, 테스트 없는 초상화 굽기의 결함 셋(메시 도착 · 등장 디더 · 창 백버퍼 읽기).
- **근본 원인(한 줄)**: 7 · 8 절 점검은 글자(함수 이름 겹침 · include 층 · 파일 크기)만 봤다 — 스레드 소유는 형식에 없어 보이지 않았고, 같은 일은 이름이 달라 겹침 검사에 걸리지 않았고, 실행되지 않는 길은 결함이 드러나지 않았다.
- **바꾼 형식**: 머티리얼은 게임 스레드 객체 + 렌더 프록시(`MaterialRenderProxy`), 다른 스레드의 렌더 일은 `RenderThread::executeAndWait`, 디바이스 프레임(`beginFrame` · `endFrame`) ·
  `GPUScene` · 프록시 진입은 렌더 스레드 역할을 확인한다(Debug). 화면 밖 그리기는 `FrameRenderer::executeOffscreenPacket` 한 길.
- **소유 지도(2026-11-01 · 2026-11-04)**:

  | 객체 | 고치는 스레드 | 모양 | 판단 |
  |---|---|---|---|
  | `Material` | 게임 스레드(객체) · 렌더 스레드(`MaterialRenderProxy`) | 둘로 나뉨(형식) | 끝 |
  | `Texture2D` | 디바이스 스레드가 만들고 렌더 스레드가 스트리밍으로 바꿔 끼움 | 다른 스레드가 읽는 핸들 · SRV 는 원자값, 역할은 함수 머리에 | 유지 |
  | `Mesh` | 게임 스레드 · 워커(`GPUUploadQueue::flush`) 또는 렌더 스레드(GL · 큐 끔 · 큐 없는 캡처 · 동적 메시) | 만드는 쪽은 스냅샷마다 하나(`GPUSceneSnapshot::_bMeshesCreatedBeforeHandoff`) | 끝 |
  | 캡처 · 미리보기 하위 렌더러 | 렌더 스레드만(`executeOffscreenPacket`) | 렌더 스레드 명령 | 끝 |
  | 씬 → 패킷 채우기 | 게임 스레드, 씬마다 `RenderPacketSceneCollector` 하나 | 한 벌 | 끝 |
  | 컴포넌트 훅(`onRegister` · `onPostLoad`) | 게임 스레드만 — 비동기 로드 워커는 생성 · 직렬화만, 씬을 짓는 배치가 훅을 배치 끝으로 미루고 비동기 로드는 `applyScene` 까지 쥔다 | 형식(`GameObjectManager::beginDeferredRegistration` · `Component::dispatchRegister`) | 끝 |
  | 렌더 스레드 훅(에디터 준비 · 그리기 · Present 뒤, 개발 도구 프레임 시작) | 게임 스레드가 걸고 렌더 스레드가 부른다 | 세터가 받은 패킷을 다 끝낸 뒤(`pollPackets`) 패킷 실행 역할(`_packetThread`)을 빌려 바꾼다, 에디터 훅은 모듈 매니저가 사는 동안만(`App::bindEditorRenderHooks`) — 언리얼 `FlushRenderingCommands` 뒤 교체 | 끝 |
  | 렌더 스레드가 쥐는 렌더 쪽 객체(디바이스 · 주 렌더러 · 화면 밖 씬 렌더러) | 붙일 때 한 번 | `RenderThread::attach` 인자 셋 — 도는 중에 바꾸는 세터 없음 | 끝 |
  | 입자(VFX) | 게임 스레드가 단계마다 새 배열, GPU 방출기는 누적 단계 요청 | 불변 공유 배열(`shared_ptr<const vector<GPUParticle>>`) · 값 요청(`VFXGPUEmitterRequest`, 정의는 불변 공유) — 한 번 만들기 | 끝 |
  | 지형 청크 메시 | 게임 스레드(정점은 병렬 잡) | 고칠 때마다 새 `Mesh` 로 배치 교체 — 한 번 만들기(메시 행과 같다) | 끝 |
  | 글리프 아틀라스 · UI 그리기 목록 | 게임 스레드 | 패킷의 값 목록 + 구간 바이트 사본(차분, 받는 즉시 옮김) · 놓치면 원자 표시(`requestFullUpload`) | 끝 |
  | 조명 · 그림자 · 프로브 · 쿠키 | 게임 스레드가 모으고 렌더러가 아틀라스 · 맵을 소유 | 패킷 값(빛 목록 · 그림자 배치), 복셀 격자는 불변 공유, 쿠키는 경로 | 끝 |
  | 렌더 타깃(게임 · 에디터 RT, 트랜지언트 목록) | RT 텍스처는 디바이스 스레드(`TextureCache::resizeRenderTarget`), 트랜지언트는 렌더 스레드 | 텍스처 행과 같은 게시, 목록 · 읽기 결과는 사본(`RenderTargetRegistry`) | 끝 |
  | 잡 결과 · 바깥 알림(에디터 백그라운드 잡, PSO 빌더, 물리 접촉 보고, 라이브 리로드 빌드 알림) | 워커가 적고 게임 스레드가 꺼낸다 | 잠금 아래 칸 + 세대 번호 — 잠금 계약을 형식으로(`SW_GUARDED_BY`, clang `-Wthread-safety`). 접촉 보고의 스텝 뒤 비우기도 같은 잠금 | 끝 |
  | 워커가 날 `this` 로 객체를 고치는 비동기 읽기(`Material` · `RenderPassAsset` · `RenderPipelineAsset::load…Async`) | 워커 | 지움 — 비동기 읽기는 에셋 도착 길 하나(워커는 바이트, 객체는 게임 스레드) | 끝 |
  | 로그 리스너 · 파일 감시 | 로그를 쓴 아무 스레드 · 감시 워커 | 리스너는 잠근 다중 델리게이트(해제가 진행 중 방송을 기다린다) + 받는 쪽 잠금 칸, 감시는 큐 → 게임 스레드 `pollEvents` | 끝 |
  | 씬 장부(`ComponentRegistry` · `CameraRegistry` · `TagIndex`) | 씬마다 한 스레드(짓는 동안 워커, 받은 뒤 게임 스레드), 쓰기는 잠금 | 읽기 창은 등록 · 해제가 없는 구간에만 쓴다(`ComponentRegistry::View`) — 태그 색인은 읽기도 공유 잠금 | 유지 |
- **로드 스레드 판단 기준**(언리얼 비동기 로딩: 로딩 스레드 Serialize, 게임 스레드 PostLoad · OnRegister / 유니티 Awake 도 메인 스레드): 워커가 짓는 동안 부르는 코드는 엔진 장부(트랜스폼 계층 ·
  타입 버킷)뿐이고, 컴포넌트가 쓰는 훅은 게임 스레드에서 돈다 — 훅을 쓰는 쪽(게임 모듈 포함)이 스레드를 생각하지 않게 한다. 예전 모양(워커가 훅까지)은 컴포넌트가 데이터뿐이던
  2026-08 의 비동기 로드에 2026-09 등록제 훅이 더해지며 다시 보지 않은 것이다(의도된 근거 없음). 받는 프레임의 등록 비용은 `endDeferredRegistration` 한 곳에서 나눌 수 있다.
- **화면 밖 씬 렌더러 판단 기준**(언리얼 `FScene` · `FSceneRenderer` · RDG): 씬 데이터의 수명은 씬의 수명이고(미리보기 씬이 사라질 때 · 캡처가 끝날 때 내린다 — `OffscreenSceneRenderers`),
  주 렌더러만 하는 일(GPU 시간 보고 · 모듈 확장 · 렌더 타깃 목록 · 고정 PSO 미리 만들기)은 표시가 아니라 주입(`FrameRendererHost`)으로 가른다. 씬마다 렌더러 하나(패스 상수 링 · 그래프 커맨드
  리스트를 씬마다 따로)를 **유지한다** — 한 디바이스 프레임에 여러 씬을 그리므로 링 · 리스트를 나눠 쓰면 뒤 실행이 앞 실행의 상수를 GPU 가 읽기 전에 덮는다(언리얼도 뷰 패밀리마다
  `FSceneRenderer` 를 만들고 RDG 가 실행마다 할당한다). 함께 쓰는 것은 PSO 캐시(같은 파이프라인)이고, 그래프 컴파일은 씬 수명에 한 번이다.
- **GPU 자원 판단 기준**(언리얼 · 유니티 · Godot 비교): GPU 자원은 렌더 쪽이 소유하고 만들고 지우는 길은 하나다. 게임 스레드가 쥐는 모양은 데이터가 바뀌는지로 고른다.
  게임 스레드가 GPU 자원이 생긴 뒤에도 값을 바꾼다 → **렌더 프록시**(렌더 쪽 사본, `Material` / `MaterialRenderProxy` = 언리얼 `FMaterialRenderProxy`).
  바꾸지 않는다(바뀌면 새 객체) → **한 번 만들기**(만드는 쪽 하나, `Mesh` = 언리얼 `FStaticMeshRenderData` + `BeginInitResource`).
  렌더 쪽이 핸들을 바꿔 끼운다(스트리밍) → **게시**(원자값 핸들 · SRV, 게임 스레드는 읽기만, `Texture2D` = 언리얼 `FTextureReference`). 잠금으로 두 생성자를 덮지 않는다.
- **패킷에 실린 차분 · 알림 판단 기준**(2026-11-04, 언리얼 폰트 캐시 갱신은 그리기와 따로 렌더 명령으로 간다): 프레임 패킷은 그리지 않고 버려질 수 있다(시점 없음 · 컨텍스트 실패 · 그래프 준비 전).
  패킷에는 그 프레임의 **전체 상태**(스냅샷 · 그리기 목록)만 싣고, 한 번뿐인 **차분 · 알림**은 렌더 스레드가 받는 즉시(`RenderThread::executePacket` 머리) 옮긴다 —
  글리프 아틀라스 업로드(묶음 번호 `GlyphAtlasUpload::_sequence` + 건너뛰면 `GlyphAtlas::requestFullUpload`, 첫 프레임 글자가 HUD 에서 빠졌던 결함), 사라진 미리보기 씬 키(`_listPendingRetiredSceneKey`).
  게임 스레드 쪽 큐(`TextureCache::flushRegionUpdates`)는 패킷에 실리지 않아 늦어질 뿐 잃지 않는다. 새 차분을 패킷에 더할 때 같은 자리에서 받는다.
- **점검에 더할 축**: 소유 지도(객체 → 스레드, 렌더러 인스턴스 → 소유자, 디바이스 호출자), 일 지도(하는 일로 묶은 함수들), 명령줄 도구마다 ctest.

## 10. 메시 인덱스 버퍼 (2026-11-02 — 사용자 지시 "인덱스 버퍼 추가")

까닭: 메시가 색인 없는 정점 흐름(삼각형마다 정점 셋)이라 스키닝 · 정점 셰이더 · 업로드가 고유 정점의 약 3 배를 돈다. 농장 측정에서 캐릭터 하나가 GPU 0.6 ms(스키닝 컴퓨트가 대부분)였다.
언리얼 · 유니티 · Godot 은 모두 고유 정점 + 인덱스(16/32 비트)이고 구간 · LOD 는 인덱스 범위다. 이중 모드(색인 있음 · 없음)는 두지 않는다 — 모든 메시가 색인을 가진다(내장 도형 · 스프라이트 · 절차 메시 포함).

남은 것: 모든 골든 대조(정점 순서가 같으면 그림이 같아야 한다 — 차이는 버그로 본다)와 농장 · 군중의 캐릭터당 GPU 측정.

## 11. 스킨 인스턴스 — 메시 사본 대신 컴포넌트마다 결과 구간 (2026-11-02 — 캐릭터 비용 측정에서)

까닭: GPU 스키닝 결과 · 팔레트 · 배치의 정체성 키가 `Mesh*` 라, 포즈가 다른 캐릭터마다 메시 사본(`Mesh::createSkinInstance` — 정점 · 스킨 · 모프 · LOD 를 CPU 에서 통째로 복사)을 만든다.
모프 풀은 원본을 `getSkinDataID` 로 한 번만 올리지만 나머지는 사본마다다 — 정점 풀이 사본마다 정점 · 인덱스를 다시 올리고(마을 사람 200 명이 정점 풀 4M 을 채웠다), 배치가 사본마다 갈린다
(마을 사람 100 명 × 구간 10 = 변형 배치 1,000 · 배치 3,000, `load/townsfolk`). 언리얼은 스켈레탈 메시의 렌더 데이터 한 벌을 모든 컴포넌트가 나눠 쓰고 컴포넌트마다 스킨 객체
(`FSkeletalMeshObjectGPUSkin` · 스킨 캐시 항목)만 가진다. 유니티 · Godot 도 렌더러(인스턴스)마다 스킨 버퍼를 두고 메시는 공유한다.

| 층 | 바꿀 것 |
|---|---|
| 정체성 | 스킨 칸(`GPUSkinSlot` — 스킨을 내는 유닛 · 군중 묶음마다 GT 가 빈 칸 목록에서 받는 오래 가는 번호)을 팔레트 항목(`GPUSkinPalette::_pMesh` 대신)과 인스턴스가 싣는다. 구간 컴포넌트(`MeshSectionComponent`)는 따르는 주 메시 컴포넌트의 칸을 싣는다. 프레임마다 바뀌는 번호(팔레트 순서)를 쓰지 않는 까닭 — 인스턴스를 매 프레임 다시 올리게 된다 |
| 모프 풀 | 결과 구간을 스킨 칸마다 하나(`_listSkinResultOffset` 의 키), 원본은 지금처럼 스킨 데이터 번호마다 한 번. RT 가 칸 → 결과 시작 표(`g_SwSkinResultOffset`, 칸 수만큼의 uint)를 바뀐 프레임에만 올린다 |
| 인스턴스 | `InstanceData::_reserved1` → `_skinSlot`(변형 없으면 무효). `MorphPool::computeElement` 가 인스턴스의 칸으로 표를 읽는다(부르는 곳 셋 — `InstanceData::loadAnimatedVertex` · `highlight_outline` · `material_shader_main` — 이 모두 인스턴스를 쥐었다) — `BatchData::_morphVertexBase` 는 지운다 |
| 배치 | 사본이 없으니 같은 메시 · 구간 · 머티리얼의 캐릭터가 한 배치다(드로우 수가 캐릭터 수와 무관). 변형 거절(`_bDeformRefused`)은 인스턴스 단위로 — 컬링(`gpu_cull.hlsl`)이 변형이 필요한데 표의 칸이 무효인 인스턴스를 가시 목록에 넣지 않는다(레스트 포즈로 대신 그리지 않는다) |
| 애니메이션 | `SkeletalMeshComponent::resolveSkinInstanceMesh` 의 사본 · 군중의 홀로 메시(`acquireSoloMesh`) · 묶음 메시를 지운다 — 묶음은 키 하나를 나눠 쓴다 |
| 확인 | 스킨 · 모프 · 군중 골든 네 백엔드(그림이 같아야 한다), `load/characters` · `load/townsfolk`(배치 수 · 정점 풀 · CPU 메모리), 마을 사람 200 명 |

잰 것(DX12 Release): 마을 사람 100 명 배치 3,000 → 30 · 간접 호출 1,200 → 12 · 그림자 6.8 → 3.2 ms · 불투명 8.3 → 5.1 ms · GT 프레임 24.5 → 17.7 ms(스키닝 컴퓨트 8.4 ms 는 정점 수의 몫이라 그대로), 기사 200 명 GPU 4.7 → 2.9 ms · 배치 3.
마을 사람 한 명이 스킨 정점 28,696 개(레인저 장화 하나가 10,174)라 변형 풀(4M)이 200 명을 못 받았다 — 12 절이 풀었다.
모프만 있는 메시(변형 프로그램)는 결과가 메시마다라 배치 값(`BatchData::_morphVertexBase`) 그대로다. 모프 전용 메시를 유닛이 그릴 때 쓰는 고정 스킨 원본(`Mesh::acquireRigidSkinSource`)은 원본 내용 번호마다 한 벌을 나눈다(구간 · LOD 도 따라온다).

## 12. 구간 메시 LOD — 조립한 캐릭터 메시의 단계와 고른 단계만 스키닝 (2026-11-02 — 11 절 측정에서)

까닭: 합친 외형 메시는 실행 중에 만들어져 임포트 LOD 가 없고, `Mesh::setLODs` 가 구간 · 모프 메시를 거절했다. 스킨 캐시는 메시의 모든 정점을 칸마다 스키닝했다.
언리얼은 스켈레탈 메시 LOD 마다 섹션 목록을 두고(`FSkeletalMeshLODRenderData::RenderSections`), 컴포넌트가 CPU 에서 고른 LOD 하나만 GPU 스킨 캐시가 변형한다. Mutable 은 조립한 메시를 워커에서 짓고 진행 중인 같은 요청을 합친다.

| 층 | 한 것 |
|---|---|
| 메시 | 단계마다 LOD 0 구간 순서의 범위(`MeshLODLevel::_listSectionRange` → `MeshLOD::_listSectionRange`, 그릴 흐름 기준). 모프 메시도 받고 낮은 단계에는 모프가 없다. `findDrawSection` 이 단계의 구간 범위를 쓴다 |
| 선택 | 구간 컴포넌트는 주 메시의 단계를 따른다(`MeshComponent::setLODIndex`). 스킨 칸 · 구간 메시는 늘 CPU 가 고른다 — GPU 인스턴스 LOD 그룹과 CPU 선택이 한 규칙(`MeshLODSystem::isGPUSelectable`)을 쓴다 |
| 스킨 캐시 | 팔레트가 단계를 싣고(`GPUSkinPalette::_lodIndex`, 군중 묶음은 멤버마다 거리가 달라 모든 단계) 모프 풀이 그 단계 정점만 스킨 행으로. 칸 표 원소는 (결과 시작, 단계 첫 정점)(`GPUSkinResultOffset`, 셰이더 `uint2`) |
| 단순화 | `CharacterMeshLOD::buildLODs` — 구간마다 `meshopt_simplify`(경계 잠금, 옷과 몸 틈이 벌어지지 않게), 단계가 쓰는 정점만 스킨과 함께 모은다. 표는 데이터(`engine/character/characterlod.xml`) |
| 짓기 | 외형의 굽기 · 합치기 · LOD 를 `CharacterAppearanceMeshBuild` 로 묶어 워커에서(`AssetManager::launchLoadWork` — 게임 모듈은 태스크 매니저를 보지 않는다, 로드 단계라 결정적 실행이 기다린다). 캐시 읽기 · 쓰기와 걸기는 게임 스레드. 짓는 중인 같은 차림은 키로 나눈다 |

잰 것(DX12 Release, `load/townsfolk` 200 명): 스키닝 컴퓨트 7.5 ms(100 명일 때 8.4) · 그림자 3.5 · 불투명 5.4 ms · GT 프레임 17.3 ms, 칸마다 스킨 정점 평균 16,653(LOD 0 은 28,696), 변형 · 정점 풀 한도 안.
