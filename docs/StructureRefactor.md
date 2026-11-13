# 구조 규칙 — 경로 · 신원 · 경계 · 소유

> 질문: 경로 · 리소스 신원 · 명령 인자 · 스레드 소유는 어떤 규칙과 형식으로 다루고, 구조 점검에서 무엇을 정했는가?
> 독자: Source 의 경계 · 소유 구조를 고치거나 점검하는 세션(에이전트와 사람).
> 결론: 근본 원인은 경로 · 신원 · 인자를 형식 없는 `string` 으로 넘겨 받는 쪽이 글자로 다시 추측한 것이었다. 규칙(1 절)과 기반 형식(2 절)으로 막았고 모두 코드에 있다. 이 문서에는 규칙, 앞으로도 지킬 결정, 스레드 소유 판단 기준(9 절), 메시 구조 결정(10 ~ 12 절)만 남긴다. 단계별 기록은 git 에 있다.

## 1. 규칙

코드 주석이 `(R2)` 처럼 이 번호를 가리킨다.

| 규칙 | 뜻 | 상용 엔진 |
|---|---|---|
| R1 OS 경계 하나 | 파일 · 폴더 · 라이브러리 · 환경 변수 · 실행 파일 경로 같은 OS 호출은 `PlatformFileUtil`(과 그 짝) 한 곳만 한다. Win32 · POSIX 네이티브로 통일하고 `std::filesystem` 은 쓰지 않는다 — 한 층에 오류 모델 · UTF-16 변환이 둘이면 결함이 난다 | 언리얼 `IPlatformFile`, Godot `FileAccess` · `DirAccess` |
| R2 디스크 I/O 는 절대 경로만 | `FileUtil` · `PlatformFileUtil` 의 I/O 는 절대 경로만 받는다. 상대 · `C:foo` · `\foo` 는 오류. 경로 글 연산(`joinPath` 등)은 대상이 아니다 | |
| R3 리소스는 리소스 경로만 | `AssetPaths` · `AssetIO` 는 `ResourcePath{도메인(선택), 상대 경로}` 만 받는다. 도메인을 비우면 우선순위로 찾고, 넣으면 그 도메인만. 문자열에서 도메인을 추측하지 않는다 | 언리얼 `/Game/…`, 덮어쓰기는 pak 우선순위 |
| R4 쓰기는 FileUtil 이 절대 경로로 | `AssetPaths` · `AssetIO` 는 읽기와 경로 풀기만. 있는 파일은 리소스 경로를 디스크 위치로 풀어 쓰고, 새 파일은 도메인 루트 아래에 잇는다 | |
| R5 상대 → 절대는 경계에서 한 번 | 명령줄 · 설정 · 콘솔 · 시나리오 값은 들어오는 자리에서 **기동 때 고정한 루트** 기준으로 한 번 바꾼다. 실행 중 작업 폴더는 읽지 않는다 | 언리얼 `FPaths::ProjectSavedDir` |
| R6 폴백 없음 | 필요한 값이 없으면 오류로 멈춘다. "못 찾으면 받은 글", "실패하면 기본값", "실패하면 다른 구현" 은 없다 | |
| R7 확인과 실행은 같은 해석 | 있는지 · 쓸 수 있는지를 묻는 쪽과 실제로 읽고 쓰는 쪽이 같은 함수로 푼다 | |
| R8 같은 일은 하나 | 같은 판정 · 정규화 · 표 · 백오프 · 파서는 하나만 둔다. 답을 아는 쪽이 값을 넘긴다(재추측 금지) | |
| R9 구성 차이는 크기대로 | 규칙은 [BuildConfigSwitch](agents/BuildConfigSwitch.md) | 언리얼 Runtime · Developer · Editor 모듈과 `#if !UE_BUILD_SHIPPING` |

## 2. 기반 형식

| 형식 | 자리 | 막는 것 |
|---|---|---|
| `AbsolutePath` | `Core/Path/AbsolutePath.h` — `tryMake` · `tryResolve` 로만 만든다(Windows `X:/` · UNC, POSIX `/`) | 느슨한 판정 — `FileUtil::isAbsolutePath` 는 `C:foo` · `\foo` 도 절대로 보므로 디스크 입구는 이 형식(`FileUtil::requireAbsolutePath`)으로 받는다 |
| `ProcessRoots` | `Core/File/ProcessRoots.h` — 기동 때 한 번(실행 파일 위치 · `-project=`): binary · project · content · saved · logs · crash · userData · packs · DDC. 못 정하면 기동 오류 | 작업 폴더 의존, Saved 기준 여럿, `"Saved"` 리터럴 |
| `ResourcePath` · `DomainTable` | `Engine/Content/ResourcePath.h` — 만들 때 정규화(소문자 · `/` · `..` 거절), 문자열에서 암시적으로 못 만든다. 도메인 이름표 · 우선순위 · 쿠킹 대상은 표 하나, 한 줄 참조(`common/x`)를 가르는 일은 이 표의 최장 일치(`DomainTable::split`)만 | 캐시마다 다른 키 정규화, 도메인 판정 · 나열 · 우선순위 표 여러 벌 |
| 입력 경계 | 명령줄 값 종류 `CommandLineDiskPath`(parse 때 R5 로 절대화, 오류는 모아 기동 실패), `gv_` 보류 값도 같은 변환기, 개발 명령 `readDiskPathArgument` | "실패하면 0 넣고 읽음 표시", 인자 글자 비교 · 추측 |

## 3. 주제별 결정

앞으로도 지킬 결정만 둔다.

### T2 · T3 리소스 경로 · 읽기 쓰기
- `TextureCache` 렌더 타깃 기본 크기(256)는 폴백이 아니라 자리 표시 값이다 — 카메라가 크기를 알리면 `resizeRenderTarget` 이 다시 만든다.
- 씬 데이터의 `string _…Path` 칸을 참조 형식으로 바꾸지 않는다 — 그 값이 디스크 입구로 가는 곳이 없고, 상대 경로가 OS 경계에 닿으면 R2 가 오류로 드러낸다. 새 결함이 이 모양으로 나오면 그때 형식으로.
- `collectAssets` 는 소스 · 팩 구성 모두 합집합이다(같은 이름은 앞선 도메인 · 높은 팩, 언리얼 `IterateDirectory`). 팩의 도메인은 팩 안(스트링 풀 맨 앞)에 적고 파일 이름으로 추측하지 않는다(언리얼 pak mount point).
- 에셋 파일 하나를 고치는 패널은 모두 `EditorDocumentPanel` 기반(`acceptDocumentToLoad` · `getDocumentDiskPath`), 연 문서 경로가 없는 패널은 `EditorStateDocumentPanel` 이다. `IEditorDocumentPanel` 의 생성자는 이 두 기반에만 열려 있다(언리얼 `FAssetEditorToolkit` 과 설정 창의 구분).
- `AssetPaths` 의 공개 해석은 `toDiskPath` · `findLooseFile` · `toAssetID` · `collectLooseFolders` 와 설정(`setSearchPriority` · `mountDiskDomain`)뿐이다. 붙인 디스크 도메인(`isMountedDiskDomainID`)은 팩을 보지 않는다.
- 개발 명령의 디스크 경로 인자와 문서 · 직렬화기의 `saveToFile` 은 글을 받는다 — 저장으로 갈 때만 `FileUtil::requireAbsolutePath` 로 형식에 올리고, OS 경계가 상대 경로를 거절한다.
- 시나리오 단계 속성마다 경로 종류를 선언하지 않는다 — `{out}` 은 명령 글 안에서도 쓰여 속성 형식으로 막을 수 없고, 경로를 받는 명령이 검사한다.

### T6 저장 파이프라인
- 런타임 저장소 `GameLocalStore` 는 게임 범위다 — 봉투 · 백엔드가 GameFramework 에 있고 슬롯 폴더가 게임마다 다르다(언리얼 `ISaveGameSystem` + `AsyncSaveGameToSlot`). 완료 배분은 기반 `LocalStoreChannel`.
- 에디터 사용자 자료(`CharacterEditorLocalStore`)는 게임 인스턴스 밖이라 따로이고, 콘텐츠가 아니라 사용자 자료다(언리얼 에디터 설정의 `Saved/`). 쓰는 확장이 둘이 되면 공용 에디터 플러그인으로 옮긴다.

### T7 같은 일 하나로
- 디렉터 컴포넌트의 카탈로그 읽기는 씬마다 다른 데이터라 게임 범위 `loadRequiredCatalog` 로 옮기지 않는다(실패는 ERROR). 파일 글 되돌리기 둘(프리팹 · 리그)은 뒤따르는 일이 달라 합치지 않는다.

### T8 구성 차이
- 플랫폼 폴더는 CMake 가 고른다(`sw_filter_platform_sources`). 플랫폼 폴더 `.cpp` 에 파일 전체 `#if` 를 두지 않는다(언리얼 UBT 플랫폼 폴더 규칙).
- 리플렉션 에디터 메타데이터는 `ReflectionEditorMetadata.h` 옆 표, 런타임 메타데이터는 포인터(`_pEditor`)로만 가리킨다 — 구조체 배치가 구성마다 같다. 값은 개발 전용 단위(`<이름>.gen.meta.cpp`)가 올리고 배포는 짓지 않는다(언리얼 `WITH_METADATA` 를 빌드 단위로). 새 표시 · 편집 필드는 `ReflectionEditorMetadata.h`, 런타임 필드는 `ReflectionMetadata.h`.
- 전용 서버는 플랫폼 구현을 폴더째 고른다(`SW_EXCLUDED_ENGINE_FOLDERS`, 창 없는 대체는 `Window/Headless` · `Input/Headless`). 클라이언트 · 서버를 가르는 전처리 매크로는 없고 상수 `build::kWithClientCode` · `kWithServerCode` 만 있다(옛 이름은 `TargetMacroCheck.h` 가 독으로 막는다).
- 남긴 구성 분기: `GameObjectManager::resolveComponent` 의 타입 확인(뜨거운 길, 언리얼 `DO_CHECK`), 스위치를 정의하는 자리(`ContentSource.h` · `AnimationRewind.h` · `ProfilerBackend.h` · `ModuleCatalog::getCurrentConfiguration`). 개발에서만 허용하는 동작(평문 포트 · 개발 인증서 · 가짜 제공자)은 구성 이름이 아니라 개발 대체물 등록으로 묻는다(`Core/Network/DevOnlineExtension.h`).

## 5. 배포 구성 확인

`py -3 -m Scripts shipping-check` — `Ninja-Shipping` · `Ninja-Shipping-Server` 를 짓고(쿠킹 포함) 배포 App · 서버를 몇 초 돌려 0 종료, 배포 App 의 `-scenario` 가 기동 오류인지 보고 로그의 [Error] · [Warning] 을 모은다. 프리셋마다 빌드 폴더가 달라 CTest 가 아니다. 통합에서 한 번(preflight).

## 7. SOLID 점검 — Core · Engine

모두 끝났거나 닫았다(번호는 코드 주석이 가리킨다).

| # | 문제 | 지금 모양 |
|---|---|---|
| 11 | Core 티어 사이 역류를 빌드가 막지 않았다 | 티어마다 빌드 부분(`Core_tier<N>_objects`)과 위 티어 헤더의 `#error` 껍데기, 티어 이하만 담은 pch |
| 13 | `BindingKind` switch 여럿 | 그대로 둔다 — 종류 값은 표 하나(`BindingKinds`)이고 남은 switch 는 `-Werror=switch` 아래라 종류를 늘리면 빠진 자리가 컴파일 오류다 |
| 14 | 큰 파일 | 크기만 보고 나누지 않는다(8.3). `MemoryProfiler` 의 누수 창만 `MemoryLeakWindow` · `LeakCurveCriteria` · `LeakWindowReport` 로 뗐다. `StringUtil` 은 `Core/String` |
| 15 | 같은 일 둘 | Wayland xdg 리스너는 `WaylandShell` 하나(핑 응답 · configure 확인 · 첫 configure 대기 · 이벤트 펌프), MemoryTag 이름표는 `SW_MEMORY_TAG_LIST` 하나, float3 성분 곱은 `float3::multiply`, CRC32 는 `HashUtil::computeCRC32` |
| 16 | 이름 | 호스트 창구는 `Engine/Module/HostInterfaces.h`, 콘텐츠 주소는 `Engine/Content`. 관측 넷(Diagnostics · Profiling · Observability · Telemetry)은 역할이 달라 둔다 |

근본 원인: 바닥 도우미가 찾기 어려운 자리에 있어 호출부가 사본을 만들었고, 층 검사가 include 글만 봐서 전방 선언 · 함수 포인터로 역류가 숨었다.

## 8. SOLID 점검 — GameFramework · Editor · App

### 8.1 우선순위 표

"닫음" 은 하지 않기로 한 판단이다.

| # | 문제 | 판단 | 상용 엔진 |
|---|---|---|---|
| 7 | App · Server 의 프레임 본문 두 벌 | 끝 — 겹치던 게임 걸음(고정 스텝 반복 → 갱신, 프로파일 구간)은 `GameModuleManager::updateGameFrame` 하나. 나머지는 실행 파일마다 다른 일이다 | `FEngineLoop::Tick` |
| 8 | 파일 감시 넷 | 닫음 — 모두 OS 감시기 하나(`IFileWatcher::createNative`)와 거르기 하나(`FileWatchDispatcher`)이고 뿌리 폴더마다 인스턴스일 뿐이다. 셰이더는 일부러 수동 재컴파일(Ctrl+F8) | DirectoryWatcher |
| 12 | 온라인 소도우미 복사 | 닫음 — `encodeID` 는 저장 형식이, `isTransientStatus` 는 제공자마다 뜻이 다르다 | |
| 14 | Runtime 에 장르 기믹 | 닫음 — 공용 프리팹 라이브러리(`common/prefabs/gimmicks/`)가 여러 게임에서 쓴다. 키트로 쪼개면 결합이 는다 | GameplayFramework · 스타터 콘텐츠 |
| 16 | 큰 패널 | 닫음 — 상태는 Model 클래스가 들고 패널은 그리기만 한다. 탭이 독립된 상태 · 수명을 가질 때만 뗀다 | |
| 17 | 큰 인터페이스(`EditorAPI` · `IEditor`) | 닫음 — 호스트 ↔ 에디터 모듈 C ABI 표 하나, 호출자는 `EditorModuleManager` 뿐이다 | 모듈마다 인터페이스 하나 |
| 18 | 층 역류 — 계약 층(ModuleAPI)이 `SW_GF_API` · `SW_KIT_API` 를 알았다 | 끝 — 정의는 `GameFramework/GameFrameworkExports.h`, 계약 층의 게임 서비스 선언은 자기 빌드 정의(`SW_GAMESERVICE_API`) | |

### 8.2 결정

- `GameFramework/Runtime` · `Kits/` · `Plugin/` — 언리얼 플러그인의 `Source/Runtime` 과 같은 뜻이다(대상 이름 · 매크로는 그대로).
- `Plugin/Editor/GameplayTools` — 기반이 아니라 내용(행동 트리 · 어빌리티 · 기믹 · 일정 도구) 이름을 쓴다.
- 자동 플레이 되돌리기는 디렉터 기반이 아니라 조종 영역(`ControlSystem`)에 둔다.
- 소리 그래프는 그래프 문서 템플릿(`EditorGraphDocumentPanel<T>`)이 아니라 문서 기반을 쓴다(링크 표가 없다).

### 8.3 큰 클래스의 상태 묶음

크기만 보고 cpp 를 나누지 않는다(같은 클래스는 한 파일). 제 상태와 규칙이 있는 묶음만 클래스로 뗀다.

- RTSWorld → `RTSVision` · `RTSUnitBuckets` · `FlowFieldCache`. 유닛 슬롯은 순서 · LIFO 빈 칸이 결정적 결과와 저장 바이트에 들어가 Core `SlotHandleTable` 로 옮기지 않는다.
- InputMap → `InputTriggerRules`(트리거 판정, 언리얼 `UInputTrigger`) · `InputBinding.h`.
- 잠금 + 목록 큐는 Core `LockedSwapQueue` 하나(목록을 잠금 없이 만지는 모양을 쓸 수 없다). 손 잠금이 남은 곳은 잠금 안에서 다른 상태도 만져 순수 큐가 아니다.
- AudioEngine 보이스 슬롯 → `AudioVoicePool`(언리얼 FMixerSourceManager).
- 에디터 콘솔 명령은 기능의 명령 파일 옆에 둔다. `/Dev/` 폴더는 빌드 규칙에서 "배포가 짓지 않는 단위" 라는 뜻이다.
- 짝 없는 cpp 는 스스로 등록하는 단위 · 플랫폼 구현 · 모듈 진입점만 둔다.
- 패킷에 실린 차분 · 알림의 규칙은 9 절(정본 [CppStyle](agents/CppStyle.md))이다.

## 9. 스레드 소유 판단 기준

규칙(GPU 자원의 프록시 · 한 번 만들기 · 게시, 렌더 스레드 일, 로드 워커, 화면 밖 씬 렌더러, 패킷에 실린 차분 · 알림, 워커 결과, 씬 장부)의 정본은 [CppStyle](agents/CppStyle.md) "Thread and GPU resource ownership" 이다. 7 · 8 절 같은 점검에는 글자 대조 말고 소유 지도(객체 → 스레드)와 일 지도(하는 일로 묶은 함수)를 더한다.

## 10. 메시 인덱스 버퍼

모든 메시가 고유 정점 + 인덱스를 가진다(내장 도형 · 스프라이트 · 절차 메시 포함). 이중 모드(색인 없음)는 두지 않는다 — 언리얼 · 유니티 · Godot 과 같다. 구간 · LOD 는 인덱스 범위다.

남은 것: 모든 골든 대조(정점 순서가 같으면 그림이 같아야 한다 — 차이는 버그)와 농장 · 군중의 캐릭터당 GPU 측정.

## 11. 스킨 인스턴스 — 메시 사본 대신 칸마다 결과 구간

포즈가 다른 캐릭터마다 메시 사본을 만들지 않는다. 메시(원본)는 한 벌을 나누고, 스킨을 내는 유닛 · 군중 묶음마다 게임 스레드가 받는 오래 가는 스킨 칸(`AnimationSkinSlot`)이 결과 구간을 가진다(언리얼 `FSkeletalMeshObjectGPUSkin` · 유니티 · Godot 의 인스턴스마다 스킨 버퍼와 같다).

- 정체성: 팔레트 항목과 인스턴스(`InstanceData::_skinSlot`)가 칸을 싣는다. 구간 컴포넌트는 주 메시 컴포넌트의 칸을 따른다. 프레임마다 바뀌는 번호(팔레트 순서)는 쓰지 않는다 — 인스턴스를 매 프레임 다시 올리게 된다.
- 모프 풀: 결과 구간은 칸마다, 원본은 스킨 데이터 번호마다 한 번. 칸 → 결과 시작 표(`g_SwSkinResultOffsets`)는 바뀐 프레임에만 올린다.
- 배치: 같은 메시 · 구간 · 머티리얼의 캐릭터가 한 배치다(드로우 수가 캐릭터 수와 무관). 변형이 필요한데 칸이 무효인 인스턴스는 컬링이 가시 목록에 넣지 않는다(레스트 포즈로 대신 그리지 않는다).
- 모프만 있는 메시는 결과가 메시마다라 배치 값(`_morphVertexBase`)이다. 그 메시를 유닛이 그릴 때 쓰는 고정 스킨 원본(`Mesh::acquireRigidSkinSource`)은 원본 내용 번호마다 한 벌을 나눈다.
- 잰 것(DX12 Release, 마을 사람 100 명): 배치 3,000 → 30, 그림자 6.8 → 3.2 ms, 불투명 8.3 → 5.1 ms, GT 프레임 24.5 → 17.7 ms.

## 12. 구간 메시 LOD — 조립한 캐릭터 메시의 단계와 고른 단계만 스키닝

언리얼 `FSkeletalMeshLODRenderData::RenderSections` 처럼 단계마다 구간 범위를 두고, 고른 단계만 스킨 캐시가 변형한다.

- 메시: 단계마다 LOD 0 구간 순서의 범위(`MeshLOD::_listSectionRange`). 모프 메시도 받고 낮은 단계에는 모프가 없다.
- 선택: 구간 컴포넌트는 주 메시의 단계를 따른다. 스킨 칸 · 구간 메시는 CPU 가 고르고, GPU 인스턴스 LOD 와 CPU 선택이 한 규칙(`MeshLODSystem::isGPUSelectable`)을 쓴다.
- 스킨 캐시: 팔레트가 단계를 싣고 그 단계 정점만 스킨 행으로(`GPUSkinResultOffset` = 결과 시작 · 단계 첫 정점).
- 단순화: `CharacterMeshLOD::buildLODs` — 구간마다 `meshopt_simplify`(경계 잠금, 옷과 몸 틈이 벌어지지 않게), 표는 데이터(`engine/character/characterlod.xml`).
- 짓기: 외형의 굽기 · 합치기 · LOD 는 `CharacterAppearanceMeshBuild` 로 워커에서(`AssetManager::launchLoadJob` — 로드 단계라 결정적 실행이 기다린다), 캐시와 걸기는 게임 스레드, 짓는 중인 같은 차림은 키로 나눈다.
- 잰 것(DX12 Release, `load/townsfolk` 200 명): 스키닝 컴퓨트 7.5 ms · GT 프레임 17.3 ms, 칸마다 스킨 정점 평균 16,653(LOD 0 은 28,696).
