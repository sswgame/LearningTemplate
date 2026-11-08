# 다음 세션 인계

> 질문: 무엇이 남았고 무엇부터 하는가?
> 독자: 이어서 일하는 세션(에이전트와 사람).
> 결론: 아래 **남은 일**만 본다 — 끝난 일은 git 기록에, 앞으로도 지킬 결정은 정본 문서(docs/agents · 설계 문서)에 있다. 순서는 A → 0 → 2 → 5. 이 문서에는 끝난 일을 쌓지 않는다(끝나면 줄을 지운다).

## 일하는 법 (요약)

- 단계마다 코드 → `py -3 -m Scripts lint` → 커밋. 묶음 끝에 `bash Scripts/dev/VerifyBundle.sh` 한 번 — Windows Debug · Release · 서버 · 배포 구성이 모두 통과한 뒤에만 Linux(`Scripts/dev/LinuxScenarios.py all`)가 돈다. 끝 줄 `VERIFIED=<sha>` 면 push.
- **테스트마다 로그의 Error · Warning 을 모두 분류한다** — `build/Ninja-Debug/Testing/Logs/*.log`, id 없는 줄과 `[id:…]` 줄을 나눠 테스트가 선언한 것인지 본다. 통과만 보지 않는다.
- 느린 곳은 새로 재지 말고 로그의 `[Scenario] … timeline:` · `stage reset took` · `Backend swap took` 줄을 모은다(`Test/README.md` 의 시간 기록).
- 규칙: 구조로 푼다 · 폴백 없음 · 게이트는 최후 · 구성 차이는 크기대로(`docs/agents/BuildConfigSwitch.md`) · 용어는 `docs/agents/NamingCpp.md` 의 표. 시나리오 체계는 `Test/README.md` 가 정본.
- Windows 전용 서버(`Ninja-Debug-Server` — 빌드 · `Server.exe --test`)도 묶음 끝에 함께 돌린다. 키트는 짓지 않는다(`SW_BUILD_UNUSED_KITS` 는 Release 만) — 서버 키트 테스트는 `Ninja-Release` 의 `App --test` 가 돌린다.
- Linux 는 WSL 클론(`~/LT-linux`)에서 돈다 — 입구는 `py -3 Scripts/dev/LinuxScenarios.py`(명령은 파일 머리말, 절차는 `linux-verify` 스킬). 매니페스트에서 포트가 빠지면(2026-11-05 `vulkan`) 공유 설치본 보호 장치가 설치를 건너뛰어 오버레이 패치(imgui-node-editor 의 `NavigateToRect` · `GetGroupSize`)가 들어가지 않는다 — 혼자 쓰는 클론이라 한 번 `-DSW_VCPKG_FORCE_INSTALL=ON` 으로 구성한다. 시나리오는 Linux-Release 를 Xvfb(`apt-get download` + `dpkg -x`, 창 관리자 없음 · 사용자 화면을 건드리지 않음, `-screen 0 2560x1440x24`) 안에서 돌린다 — Vulkan 은 `VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json`(lavapipe), GL 은 `LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe`(Mesa 26 은 4.5 문맥에 `ARB_gl_spirv` 를 내준다), 소프트웨어라 느리니 `-gv_scenarioWallScale=4`. `Scenario.<팩>` 명령에서 `-scenario-backend-split` 을 빼고 `-scenario-backends=vk` 로 돌린다. 임포트 결과는 Windows 에서 복사한다(Debug 임포트는 너무 느리다). 골든은 `<시나리오>.<이름>.<백엔드>.linux.ppm` 으로 따로 둔다. Xvfb 에는 창 관리자가 없어 `engine/automation/windowpresent` 의 최소화 단계는 건너뛴다. 메시 원본은 Git LFS 라 클론에 `git-lfs` 와 `.git/lfs/objects` 복사가 있어야 임포트된다. Xvfb 의 `xkbcomp` 경로는 실행 파일에 박혀 있다. 고정 화면 글꼴: 카탈로그가 찾는 파일을 `~/.local/share/fonts` 에 둔다(Noto CJK, LiberationSans).
  전용 서버 Debug 도 클론에서 함께 짓는다 — 렌더러를 빼는 구성이라 클라이언트 빌드가 못 잡는 층 위반이 여기서만 드러난다: `cmake -S . -B build/Linux-Server -G Ninja -DCMAKE_BUILD_TYPE=Debug -DSW_TARGET_TYPE=Server` 에 Linux-Debug 캐시의 컴파일러 · 툴체인 · `VCPKG_INSTALLED_DIR` 을 **절대 경로로** 넘긴다(상대 경로면 컴파일러 확인에서 진다), 실행은 `Bin/Server -gv_serverExitAfterTicks=60` 과 `Bin/Server --test`.
  Wayland 창 층(`SW_ENABLE_WAYLAND`)은 기본 클론 빌드에 없다 — `~/lt-deps/root` 에 sudo 없이 푼 `libxkbcommon-dev` · `wayland-protocols`(`apt-get download` + `dpkg -x`, .pc 의 prefix 를 그 폴더로)를 `PKG_CONFIG_PATH` 로 주고 `cmake --preset Linux-Debug -B build/Linux-Wayland -DSW_ENABLE_WAYLAND=ON` 로 짓는다. 실행은 `SW_WINDOW_BACKEND=wayland`.

## 남은 일

### A. 지금 할 일(2026-11-07 인계 — 계정을 옮겨도 이 절부터)

설계 정본: [ResourceStructure](ResourceStructure.md). 사용자가 "**Linux 는 큰 작업이 모두 끝난 뒤**" 로 정했다 — 그때까지 `bash Scripts/dev/VerifyBundle.sh --no-linux`(지면 고친 뒤 `--from release|server|shipping` 으로 그 단계부터). 설계는 앞으로의 기능 확장 기준이고, 공유할 수 있는 것은 한 벌 · 변형은 차이만이다. 사용자 지시(2026-11-07): Source 전체 리팩터링(성능 · 공통 · 확장 · 층)과 미완성 기능을 남김없이 끝낸다.

**상태:** `b42033ca6c` 까지 push. 규칙 · 범위는 `.claude/handoff/brief_round6.md` · `brief_round7.md`(Git 밖), 조사 문서는 `.claude/handoff/surveys/`. 합칠 때마다 빌드 · 시나리오 · 골든(그림을 눈으로 본 뒤)을 맞춘다.

**작업자가 못 끝낸 것도 한다(사용자 지시 2026-11-07).** 보고의 "옮기지 못한 목록" 은 통합 때 그 자리에서 처리하거나 다음 작업자 범위에 넣는다 — 인계에 쌓아 두지 않는다.

**0. 2026-11-08 인계(여기부터).** 본 트리를 묶음 검증 중이다(`bash Scripts/dev/VerifyBundle.sh --show`) — 통과하면 `VERIFIED=<sha>` 를 push. 남은 일:
- **갈래 B(등록 · 싱글턴) 재작업 중**(워크트리 `agent-aad26201fdd926cf2`): 병합을 되돌렸다 — 함수 정적을 파일 범위 정적(`atomic<uint32> s_…`)으로 옮긴 15 곳(싱글턴 그대로), 엔진 서비스가 묶이기 전에 읽히는 상태(ResourceTags · ResourceComposition)를 서비스로 옮겨 기동이 멈춘 것. 기준: 깃발은 `SW_LOG_*_ONCE_ID` · 주인 멤버, 번호 발급기는 내는 주인의 멤버, 바인딩 전 상태는 코어 범위 · 리소스 루트. 함수 · 파일 범위 가변 정적 린트(정적 등록 틀 · 침입형 등록 머리만 예외)를 PreCommit 에 건다.
- **갈래 D(주인 타입) 진행 중**(워크트리 `agent-ac30d8c0317ae7eb8`): 상태와 로직 합치기, `…Internal` · 헤더의 static 전용 타입(예: MarkupToken + MarkupTagScanner)을 주인 타입으로, `EditorWindowTitleState` 의 입력 모으기.
- **폴더 · 파일 자리(B · D 를 합친 직후):** 기준 [FolderLayout](agents/FolderLayout.md), 목록 [FolderMoves](FolderMoves.md) — Core String 재분할(StringUtil · UTF8Util · formatString)부터, 스레드 간 큐는 `Core/Concurrency/Queue`, 합칠 파일 14 묶음, 약어 표기(FourCC 등), App · Server 의 개발 기능 → DevTools, Engine/Profiling 의 보고 · 내보내기 → DevTools, Rendering · 2D → Engine/Visual. 린트로 막는다(CoreTiers 를 모든 티어로, Util 폴더 금지, 약어 검사).
- **`sw::expected<T, E>` 한 벌(사용자 결정):** 구현 하나(C++23 `std::expected` 인터페이스 + `and_then` · `value_or`, 오류는 형식 있는 종류 + 문맥), 없음이 정상인 조회는 별칭 `sw::optional<T> = expected<T, Absent>`. `std::optional` 488 곳 · `bool f( …, string& outError | outReason )` 218 함수를 옮기고 린트로 막는다. 폴더 이동 다음.
- **DevTools 서비스를 에디터에 보이기:** 호스트의 손 목록(`EditorModuleManager::bindDevToolServices`)을 없애고 DevTools 서브시스템이 스스로 "모듈에 보임" 을 등록하게(모듈 공개 서브시스템 `SW_MODULE_SUBSYSTEM` · `ModuleRegistrationHost` 마운트 창구와 같은 모양).
- **navcrowd 부하 시나리오(Scenario.empty.load.navcrowd):** 도착 1000 까지 174 프레임인데 비동기 경로 질의 결과가 17 건(기대 200 이상). 결정적 실행 정책은 R2 전후로 같다. 질의 처리량 · 수확 시점을 프로파일로 본다.
- **에디터 시나리오가 느린 프레임:** 새 패널이 처음 열리는 프레임이 약 1 초(`EditorWaitSettled has waited 2 s (2 frame(s))`). 느린 프레임 트레이스(`-gv_traceSlowTickMs`)로 그 프레임의 동기 일(에셋 · 썸네일 · PSO)을 찾아 비동기로.
- **Core/Container (1) 남은 것:** 컨테이너가 아닌 헤더 이동은 위 폴더 이동에 들어 있다. 세대 핸들을 손으로 짠 곳(LoopbackStreamTransport `StreamConnectionHandle` · PathRequestQueue)을 SlotHandleTable 로 합칠지.
- 작업자 보고의 남은 범위(통합 때 하거나 다음 작업자 범위): MVVM 남은 View 66 개(`Config/Dev/EditorViewIncludes.json` 목록에 더해 가며, 공용 GUI 헤더가 `Workspace/` 를 끌어오지 않게 한 뒤 `#error` 껍데기), 매 프레임 통째로 미루는 타입(Surface 셋 · Zone 둘 · JumpEscape)을 씬 서비스로, Voxel · AutoAim 조종자를 감각 + 행동 트리로, CLDR 복수형 규칙 글을 plurals.xml 과 대조, `find*Index` 반환형을 optional 로(→ expected 한 벌 이전과 함께).
- 빌드 속도: 유니티 배치 크기 측정(Release 깨끗한 빌드 12 · 24 · 48)이 남았다.
- 그 뒤: Empty 게임 이름을 TestBed 로 전수 개명(옛 이름 린트로 막기), S8 측정 6 건, dzn SPIR-V→DXIL 충돌, 웹 · Android 실제 빌드.

**1. 합친 작업자 커밋의 남은 확인.**
- R2: 스캐폴드 `new-online-kit` 로 키트 하나를 실제로 지어 본다.
- R7: 게임을 `-cooked-content` 로 띄워 경로를 조립해 읽는 에셋이 빠지지 않는지 본다.
- R8: 웹 · Android 를 실제로 짓고 고친다(남은 플랫폼 일은 [PlatformTargets](PlatformTargets.md) "남은 일").

**2. 사용자에게 받을 것.** 영수증 · 푸시 · 외부 로그인의 실제 계정과 키(지금은 로컬 가짜 끝점까지 잇는다 — `Config/Server/Online/Providers/`, `Scripts/net/OnlineLocalRun.py`). 새 의존성(인코더 라이브러리)의 라이선스.

**3. Linux.** 큰 일이 끝난 뒤 `LinuxScenarios.py all`, `.linux` 골든, 에디터 체인 18 단계(아래 5 절). 엔진은 Vulkan 1.2 로 낮췄다(R4) — dzn(`~/lt-deps/dzn`, GPU Vulkan)과 Mesa d3d12(GPU GL)로도 시나리오를 돌린다. 지금 `LinuxScenarios.py` 는 lavapipe · llvmpipe(소프트웨어)로 고정이다 — 드라이버를 고르는 칸을 더한다.

**결정:** 범용 스크립팅 언어는 현재 도입하지 않는다. 핫 리로드와 기존 데이터 기반 기능이 다루지 못하는 실제 요구(예: C++ 빌드 없이 디자이너가 새 동작을 작성)가 확인될 때만 재검토한다([핫 리로드와 스크립팅 언어 판단](DesignBacklog.md#핫-리로드와-스크립팅-언어-판단)). 런타임 아틀라스는 하지 않기로 했다. 메모리 풀은 측정 뒤에, GC 는 권하지 않는다.

**줄끝:** `.gitattributes` 가 `* text=auto eol=lf` 다.

사람이 보는 실행: `py -3 Scripts/dev/WatchScenarios.py [--preset 프리셋] [--backend 백엔드] [--stages a,b] [체인…]`.

### 0. 사용자가 시킨 일 — 빠짐없이 (2026-11-01)

에이전트 없이 직접 한다. 이름만 보지 말고 내용을 읽고 판단한다. 빌드 통과는 판단 기준이 아니라 마지막 확인이다.

1. 코드 전체를 다시 훑어 SOLID · 프로젝트 구성(층 · 책임 · 의존 방향 · 폴더)을 전면적으로 고친다 — StructureRefactor 7 · 8 절의 "남김" · "다음" 줄 포함.
2. 확인한 상태(Debug 빌드 · 단위 · ctest, Release 빌드 · ctest, 오류 · 경고 분류)만 중간중간 origin main 에 푸시한다.
3. 아래 2 ~ 6 절(테스트 공백 · 흔들리는 골든 · T8 · T9 …)도 남은 일이다.
4. 로우폴리 에셋을 실제 게임 품질로(CC0 만, 원본 바이너리는 Git LFS — 사용자 결정). Quaternius 열 팩은 `/AssetLibrary/Quaternius/`(무시)에 받아 두었고, 게임에 쓰는 것만
   `common/models_raw · textures_raw/quaternius/<묶음>/` 로 옮긴다(credits.md). 새 리소스는 묶음 페이지가 CC0 인 것만 들인다(`Content/common/credits.md` 머리).

### 2. 테스트 · 시나리오 공백

| 일 | 내용 |
|---|---|
| 흔들리는 게임 골든 | 2026-11-04 Debug · Release 전체 ctest 각 일곱 번과 Release Shooter3D 체인 세 번에서 한 번도 지지 않았다(killcam · cape · bodyviews · useroptions · GL 컨텍스트 잃음 모두). 다시 지면 그 로그의 단계 · 백엔드 · 직전 `Assets.Pending` 을 적는다 — GL 컨텍스트 잃음은 GL 디버그 출력(`KHR_debug`)으로 드라이버 리셋인지 VAT 인덱스 드로우인지 가린다 |
| 남은 단위 테스트 | 폴더 집계(`py -3 Scripts/dev/TestCoverage.py`)는 테스트가 다른 폴더에 있어도 "없음" 으로 센다 — 집계의 "없음" 폴더를 클래스 이름으로 테스트에서 찾아 하나씩 본다(하네스 · 키트 케이스가 도는 곳은 `Test/README.md` "하네스"). 온라인 `AccountTicketConnect` 의 성공 · 만료 · 재시도 길([DesignBacklog](DesignBacklog.md) "구조와 시스템") |

### 5. 그 뒤

- Linux 에디터 체인 나머지(WSL lavapipe, 140 단계 중 약 18 단계): 2026-11-06 처음 끝까지 돌았다. 원인을 못 가린 것 — `audiomixer`(M 단추를 눌러도 음소거 수가 그대로), `graphaddnode` · `graphviewtools`(노드 추가 · 정렬이 안 먹는다), `viewportselect`(Ctrl 클릭 선택), `assetmanage`(휴지통 `Asset.Recycled`), `meshmodifiers`, `idle`(창 초점 없는 프레임 상태), `previewswap` · `flowboundaries`(GL 없음으로 보인다), `selftests` 의 `dpi.monitorScaleFollows`(Xvfb 에서만). 믹서를 단독으로 돌리면 팔레트 입력부터 먹지 않는다 — WSLg · Xvfb 에서 창이 입력 초점을 받지 못하는 것이 공통 원인으로 보이니, 창 관리자와 초점이 있는 화면에서 먼저 다시 돌린다. 그래픽 골든(`screen` · `viewmodes` · `emptyscene` · `loadingscreen` · `sceneviewgameview` · `screenrectview` · `flowterrain` · `terrainbrush`)은 크기부터 다르다 — 골든을 만든 Windows 모니터 DPI 에 묶인 것으로 보이며, 어느 PC 에서나 같은 크기가 되게 하거나 Linux 골든을 따로 둔다. lavapipe 검증 레이어가 `blendEnable` + `R16G16_SNORM` 을 잡는다(`viewmodes`) — 포맷 기능 확인 없이 블렌드를 켠다. 시나리오 게임 체인은 Debug 텍스처 임포트가 너무 느려 돌려 보지 못했다.

