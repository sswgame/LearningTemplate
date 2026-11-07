# 다음 세션 인계

> 질문: 무엇이 남았고 무엇부터 하는가?
> 독자: 이어서 일하는 세션(에이전트와 사람).
> 결론: 아래 **남은 일**만 본다 — 끝난 일은 git 기록과 계획 문서([StructureRefactor](StructureRefactor.md))에 있다. 순서는 A → 7 → 1 → 2 → 0 → 5. 이 문서에는 끝난 일을 쌓지 않는다(끝나면 줄을 지운다).

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

설계 정본: [ResourceStructure](ResourceStructure.md). 사용자가 2026-11-07 "**Linux 는 큰 작업이 모두 끝난 뒤**" 로 정했다 — 그때까지 `bash Scripts/dev/VerifyBundle.sh --no-linux`. 설계는 지금 사용처가 아니라 앞으로의 기능 확장 기준이고, 공유할 수 있는 것은 한 벌 · 변형은 차이만이다.

**상태(2026-11-07):** 3 차 작업자 W1 ~ W6 을 모두 본 트리 `main` 에 합쳤다. origin 은 `e3b2cbd81a` 이고, 그 뒤 로컬 커밋은 아직 검증 · push 하지 않았다(`git log origin/main..HEAD`). 먼저 할 일은 **아래 1 번**이다.

**1. 합친 것을 검증하고 push.** `bash Scripts/dev/VerifyBundle.sh --no-linux` 를 한 번 돌리고 지면 고친다. 끝 줄 `VERIFIED=<sha>` 만 `git push origin <sha>:main`. 그 뒤 작업자 워크트리(`.claude/worktrees/agent-*`)와 브랜치를 지운다. 특히 볼 곳:
- 쓰는 것만 담는 쿠킹(W2): engine 팩이 0.4MB 까지 준다는 추정이 의심스럽다(코드가 경로를 조립해 읽는 에셋이 빠졌을 수 있다) — 게임을 `-cooked-content` 로 띄워 본다.
- 참조 칸 전환(W3): `AssetReferenceFields.generated.json` 이 빌드로 다시 쓰였는지, 새 이름(`clip` · `tree` · `sound`)이 다른 뜻의 칸을 참조로 오판하지 않는지. `catalogcascade` 표가 `.interactions.xml` 이 되어 "표에 없는 .xml" 다시 읽기 길은 이 시나리오로 더는 보지 않는다.
- 단계 5(W1): 새 시나리오 `game.empty.resourcecondition`, `kCore` 9, 엔진 영역 처리기가 서버에도 달린 뒤의 접속. 빈 이름 칸을 이제 `"None"` 이 아니라 빈 글로 쓴다(통합 고침) — 저장 글을 비교하는 곳을 본다.

**2. 단계 5 가 남긴 것.** 조건을 받지 않거나 글 입구로 읽는 형식의 목록은 `docs/ResourceStructure.md` 끝 절(쓸 일이 생기면 그 형식을 옮긴다). 접속 뒤 대조 · 저장의 조건 지킴은 단위 테스트까지다 — 멀티 프로세스 · 에디터 저장 시나리오가 없다.

**3. 이름 정리 남은 것(규칙은 [NamingCpp](agents/NamingCpp.md) 용어 표).** 통합 뒤 개명 표의 옛 이름은 남지 않았다(2026-11-07 확인). 표와 도구는 `.claude/handoff/surveys/`(`rename.py` · `r*.txt` · `sortinc.py`, `rename.py` 는 단어 경계 치환 + 파일 줄기 `git mv`). 남은 것:
- `CombatHealth` → 체력 상태 기계 이름: `HealthState` 는 서버 상태 점검이 쓰고, `getCombatHealth().getHealth()` · 멤버 `_combatHealth` 와 기존 `_health` 칸이 섞여 기계적으로 못 바꿨다. 형식과 멤버를 함께 손으로 바꾼다.
- 지역 이름 `director` · `pDirector`(게임 모드를 가리키는 것만 `gameMode` 로 — 카메라 디렉터와 섞여 있다).
- 읽기 동사 나머지: `loadFromXMLText` · `loadFromXMLText`(XML 일 때), `loadFromXML` · `loadFromXML` · `loadFromXML` · `parseNode` → `loadFromXML( const XMLNode& )`, 에셋 id 를 받는 `loadFromFile( AssetID )`(머티리얼 · 그래프 등 → `loadFromAsset`, 겹치는지 먼저 본다), `SceneDocument::saveToFile( 경로 )` → `saveToFile`.
- `AppearanceRuleCatalog` · `SlotCatalog` → `…Catalog`(루트 원소 함께), 애니메이션 시스템의 "유닛"(`getUnits` · `_listUnit` · 감시 값 `Animation.Crowd*Units`) → SkeletalMesh, 키트 형식 접두어(`CardBattle` 은 CardGame 키트, `Horror` 가 두 키트에 겹침), 자동화 단계 인자 `…Fields` → `…Params`.
- 바꾸지 않기로 한 것: `Gimmick*`, `gv_` · `GlobalVariable`, `ControlIntent`, `*EditModel`, `LootCatalog`, `Ledger`.

**4. 구조 개선(사용자가 "시급" 으로 지목, 2026-11-07).** 조사 결과는 `.claude/handoff/surveys/` 에 있다(Git 밖). 구조는 장기 이득이면 과감히 바꾼다.
- `structure_survey.md` 파일 이동 표 20 줄: App/Tests 둘 → ModuleManager/Tests, `GameObjectHandle` · `ComponentHandle` → Engine/Foundation, `AssetTypeRegistry` → Engine/Content, `EngineCoreServiceSubsystems.cpp` → CoreServices, Editor/Popups 해체, ModuleManager 호스트 부품 → `ModuleManager/Host`. Popups 경로는 `editor.strings.xml` · `po/ko.po` 도 고친다. **App 파일은 런타임 기능이 아니라 App 에 있는 것이 맞다(사용자 확인) — Engine 으로 옮기지 않는다.**
- App 정리: `App::run` 쪼개기(`runFrame` · `makeFramePacingInputs`), 넷 세션 일 둘을 엔진 프레임 훅으로, 초상화 굽기를 `AppHeadlessTasks` 표로, App · Server 공용 부팅 `HostLaunch`(수명 순서 표는 조사 문서), 개명 `loadModuleLibrarys` → `loadModuleLibraries` · `_pModuleImageHost` → `_pModuleLoader` · `isEditorImageUsable` → `isEditorLibraryUsable` · `_gameModuleManager` → `_moduleManager` · `_backendSwap` → `_backendSwitcher`, 죽은 `SW_APP_SMOKE_TEST_SOURCES`, 낡은 SmokeTest 주석 약 10 곳.
- `server_online_survey.md` 서버 · 온라인 키트 단계 0 ~ 6: 메모리 저장소 폴백 제거 → 견본 게임 · Debug 에서 키트 짓기 → 프로토콜 서술자(메서드 · 알림 표, 핸들러 표, 클라이언트 `call<>`) → 키트별 이전 → `OnlineSession` → 형식 있는 키트 설정 · 서버 설정 하나 → 온라인 코어 · 서버 전용 모듈 분리 → 키트별 스키마 해시. 응답 보존 항목은 W6 이 끝냈다(0 단계에서 뺀다). 사용처 없는 키트 · 제공자(P15 — 영수증 넷, 푸시, Postgres · RESP 드라이버, HTTP/2 자체 구현)는 **지우지 않고 쓰게 만든다(사용자 결정 2026-11-07)** — 기반이 합쳐진 뒤 설정으로 골라 견본 서버에 싣고, 로컬에서 도는 저장소(Postgres · RESP)와 HTTP/2 는 실제로, 외부 계정이 필요한 영수증 · 푸시는 로컬 가짜 끝점으로 끝까지 잇는다. 실제 계정 · 키는 비용 · 외부 연동이라 사용자에게 받는다.

**5. 미완성 · 미사용 기능 완성(사용자 요청 2026-11-07).** `unfinished_survey.md` 의 상위 25 를 위부터: 서버 권위 명중 확인(`LagCompensation`), 네트 입력 두 길 합치기, 시네마틱 묶음, 온라인 키트 Shooter3D 싣기, WildWest 규칙 … 미사용은 지우기보다 쓰게 만드는 것이 기본이다(사용자 결정) — `SpatialOctree` · `SpatialQuadTree` · `FrameArenaAllocator` · `AtomicSlot` · `DualQuaternion` · `GravityComponent` 도 쓸 자리를 먼저 찾고, 정말 겹치거나 뜻이 없는 것(읽는 곳 없는 RHI 깃발)만 지운다. 엔드포인트 · Steam SDK 연동은 비용 · 외부라 사용자에게 묻는다. 완료 조건에 콘텐츠 배치와 시나리오를 넣고, 참조 0 을 세는 린트를 둔다.

**6. 플랫폼(사용자 결정 2026-11-07).** Android · 웹(최신 브라우저 WebGPU 만) 지원, iOS 제외, 콘솔은 대기(분기를 만들지도 막지도 않는다). 순서: (1) `waitIdle` 을 펜스 기반 지연 해제로(씬 활성화 · 핫 리로드 · 캡처, 데스크톱도 이득) → (2) 텍스처 압축을 쿠킹으로 — 임포트는 중간본, Config 의 대상별 형식 목록(BC · ASTC · ETC2) · 실행 때 장치가 지원하는 첫 형식(웹은 두 벌) → (3) Dawn 데스크톱 WebGPU 백엔드(시나리오 · 골든으로 검증, 셰이더는 SPIR-V → WGSL Tint) → (4) Emscripten 빌드(스레드 · 대기 모델, OffscreenCanvas, COOP/COEP) → (5) Android Vulkan — 시작 전에 엔진의 요구 판(Vulkan 1.3)과 필수 기능을 대상 기기와 대조한다(dzn 이 빌드 뒤에야 이 틈으로 막혔다). WebGPU 도 필수 기능을 먼저 대조한다. RHI 조사: 능력 깃발은 있다, 슬롯 바인딩 → 그리기별 바인드 그룹, 루트 상수는 DX11 처럼 상수 버퍼 슬롯으로 흉내, wave · 지오메트리 · 테셀레이션 쓰지 않음, 아토믹은 버퍼만, 바인드리스는 `SW_NATIVE_BINDLESS` 뒤, `R16G16_SNORM`(GBuffer)은 대체가 필요, `R32G32B32_FLOAT` 텍스처 없음, 모바일 BC 없음.

**7. Linux.** 사용자가 "해 보라" 고 했다. 합치기 전 `LinuxScenarios.py all` 은 `X11Window.cpp` 컴파일에서 졌고 고쳤다 — 검증 뒤 `all` 을 다시 돌려 결과대로 고친다. dzn(Mesa Dozen, D3D12 위의 Vulkan)은 2026-11-07 시험을 끝냈고 **지금 엔진은 쓸 수 없다**: `~/lt-deps/dzn` 에 Mesa 26.0.8 을 sudo 없이 지었고(`VK_ICD_FILENAMES=$HOME/lt-deps/dzn/install/share/vulkan/icd.d/dzn_icd.x86_64.json`, `LD_LIBRARY_PATH=$HOME/lt-deps/dzn/root/usr/lib/x86_64-linux-gnu`) vulkaninfo 에 장치 둘이 보이지만, dzn 은 Vulkan 1.2 · `shaderDemoteToHelperInvocation` 없음으로 소스에 고정돼 있고 엔진은 1.3 과 그 기능을 요구한다. Linux GPU 시험은 lavapipe 로 유지하고, Mesa 가 1.3 을 내면 다시 본다. 5 절의 18 단계와 `.linux` 골든도 이때.

**8. 메모리 풀 · GC:** 사용자에게 답했다 — GC 는 권하지 않고, 풀은 측정 뒤에. 지금은 구조 개선이 먼저다.

**9. 통합에서 드러난 확인거리.**
- 1 인칭 탄도선이 시점 총에 가려 화면에 남지 않는다(`ShooterPlayerComponent` 총구가 몸 무기). 시나리오 `projectilecombo` 는 값(`Shooter3D.TracerWidth`)으로 본다.
- 엔진 서비스 생성 순서가 목록 위치에서 의존 · 이름 순으로 바뀌었다(K7). 생성자 안에서 다른 서비스를 찾는 공장이 있으면 드러난다.
- 자식이 더한 전이 id 가 부모와 겹치면 조용히 합쳐진다(R4). 머티리얼 파라미터 묶음은 핫 리로드가 없다(R5).
- 리플렉션 형식의 `_base` 는 개발 실행도 문서 표(`Content/engine/settings/datacataloginheritance.xml` 의 `<Document>`)만 읽는다 — 새 문서 형식에 `_base` 를 쓰면 표에 줄을 함께 더한다.

**사용자에게 알릴 것:** 스크립팅은 Lua 로 추천했고 답이 없으면 Lua 로 한다. 런타임 아틀라스는 하지 않기로 했다.

**줄끝:** `.gitattributes` 가 `* text=auto eol=lf` 다(본 트리는 변환해 두었다). 워크트리에 CRLF 경고가 보이면 그 워크트리의 옛 파일이다.

사람이 보는 실행: `py -3 Scripts/dev/WatchScenarios.py [--preset 프리셋] [--backend 백엔드] [--stages a,b] [체인…]`.

### 0. 사용자가 시킨 일 — 빠짐없이 (2026-11-01)

에이전트 없이 직접 한다. 이름만 보지 말고 내용을 읽고 판단한다. 빌드 통과는 판단 기준이 아니라 마지막 확인이다.

1. 코드 전체를 다시 훑어 SOLID · 프로젝트 구성(층 · 책임 · 의존 방향 · 폴더)을 전면적으로 고친다 — StructureRefactor 7 · 8 절의 "남김" · "다음" 줄 포함.
2. 확인한 상태(Debug 빌드 · 단위 · ctest, Release 빌드 · ctest, 오류 · 경고 분류)만 중간중간 origin main 에 푸시한다.
3. 아래 2 ~ 6 절(테스트 공백 · 흔들리는 골든 · T8 · T9 …)도 남은 일이다.
4. 로우폴리 에셋을 실제 게임 품질로(CC0 만, 원본 바이너리는 Git LFS — 사용자 결정). Quaternius 열 팩은 `/AssetLibrary/Quaternius/`(무시)에 받아 두었고, 게임에 쓰는 것만
   `common/models_raw · textures_raw/quaternius/<묶음>/` 로 옮긴다(credits.md). 농장 · 서부 · 놀이공원 · 투기장 · 나일 소품, HarvestValley 농부 · 마을 사람(외형 · 피팅 · UAL 클립), MeadowVillage 밭 칸은 했다.

### 2. 테스트 · 시나리오 공백

| 일 | 내용 |
|---|---|
| 흔들리는 게임 골든 | 2026-11-04 Debug · Release 전체 ctest 각 일곱 번과 Release Shooter3D 체인 세 번에서 한 번도 지지 않았다(killcam · cape · bodyviews · useroptions · GL 컨텍스트 잃음 모두). 다시 지면 그 로그의 단계 · 백엔드 · 직전 `Assets.Pending` 을 적는다 — GL 컨텍스트 잃음은 GL 디버그 출력(`KHR_debug`)으로 드라이버 리셋인지 VAT 인덱스 드로우인지 가린다 |
| 남은 단위 테스트 | 폴더 집계(`py -3 Scripts/dev/TestCoverage.py`)는 테스트가 다른 폴더에 있어도 "없음" 으로 센다 — 실제로 비었는지는 클래스 이름을 테스트에서 찾아 본다. 2026-11-05 에 채운 것: 파괴 상태 · 그래프, 외형 해석, 리그 IK, 턴 중계, RESP 저장소, 메시 쪼개기, 시퀀서 · 단축키 · 전역 변수 · 모듈 · 환경설정 · 테스트 실행기 · 문서 패널 · 테마 값 · Sync 탭 Model, UI 레이아웃 · 포커스 · 이벤트, 날씨 · 시계, 조종 시스템 · 의도 기록, 차량 좌석, 행동 트리, 기믹 회로, 어빌리티, 모듈 카탈로그, 프로파일러 · 타일맵 · 데이터 테이블 · 카탈로그 · 콘텐츠 브라우저 · UI 미리보기 Model, 인터랙터, 프리팹 덮어쓰기, 팩 리더, 크래시 보고, 립싱크, 카메라 모드. `UIPreviewModel` 은 화면이 있어야 해 시나리오 `editor/uipreview` · `uipreviewdesigner` 가 맡는다. 장르 키트 Rule 은 모두 채웠다 — 게임이 싣지 않는 키트는 Release `UnitTests`, 게임이 싣는 키트는 `UnitTests.<게임>`, 온라인 키트는 `UnitTests.OnlineKits` 가 돈다(에디터 케이스는 `Scenario.editor` 의 `editor.unitTests`). 크게 남은 것: 화면 없이 도는 패널 Model 밖의 큰 파일(`Editor/Common` · `Editor/Panels` 의 그리기 코드는 시나리오 몫이다). 온라인 키트는 싣는 게임이 없어 `UnitTests.OnlineKits`(OnlineLoadBot `-test`, Release — 봇이 온라인 키트를 모두 싣는다)가 돌린다 — 매칭 · 채팅 거르기 · 라이브 이벤트 · 친구 관계 · 거래 상태 기계 · 우편 · 경제 구매 · 순위 보드 정의 · GM 권한 · 지급은 채웠다, 계정 로그인 저장(가짜 암호 — 연동 · 마지막 로그인 수단 · 탈퇴 유예 포함)과 서버 목록 `ServerListModel`(실패 길 · 루프백 서버로 응답 길)도 채웠다. 2026-11-07: 경제 클라이언트 · 지갑 거울 · 스토어 결제 흐름, 푸시 제공자(FCM · APNs) · 발송기 · 제공자 설정, 티켓 주 키 · 외부 로그인 제공자 설정을 채웠다. 계정 키트에 아직 없는 것: `NetSecurityLoginCrypto`, 클라이언트 쪽 `AccountTicketConnect` · `PlatformLoginClient`(`SystemExternalBrowser` 는 운영체제 호출이라 시나리오 몫). 2026-11-06 서비스 층도 채웠다 — 순위표 · 길드 · 매칭 대기열 · 파티 로비 · 채팅 · 친구 · 경제 영수증 · 거래 · 로그인 · 우편함 · 라이브 이벤트 · 서버 디렉터리 · GM, 그리고 외형 선택 · 퀘스트 일지 · 예약 작업 · 보존 쓸기 · 월드 플래그 · 록온 · 대화 러너 · UI 화면 서비스 · UI 애니메이션. 네트워크 하네스는 있다 — Core 엔드포인트는 `LoopbackCluster`, 온라인 키트 끝단은 `OnlineHostTestUtil.h`(`OnlineTestFixture` — 호스트 + 클라이언트 묶음, 키트 DLL 이 include 하고 키트 서버 쪽은 같은 프로토콜 항목의 가짜로 올린다). 하네스는 있다(`AnimationTestUtil` · `RHIFakeDevice` · `FakeFontRasterizer`, 자식 프로세스는 `Core/Process` · 크래시 보고). 2026-11-06 에 더 채운 것: 모듈 의존 그래프 · 언로드 큐 · 섀도 복사본 거절, UI 문서, 가상 입력, 개발 명령, 스프라이트 클립, 캐릭터 데이터 읽개, UI 입력 소비 · 입력 방식, 포인터 잡기 취소, 게임 흐름의 화면 없는 규칙(팁 · UI 없는 로딩 화면 · 타이틀 없는 게임). 아직 단위 테스트가 없는 곳: `Programs/ReflectionParser`(별도 프로그램이라 빌드로만 확인), 섀도 복사본의 성공 길(실제 모듈 바이너리가 있어야 해 핫 리로드 시나리오가 맡는다), 화면이 뜨는 게임 흐름(시나리오 `loadingscreen` 이 맡는다). 집계의 나머지 "없음" 폴더는 위 안내대로 클래스 이름으로 하나씩 본다 |

### 7. 규칙을 형식 · 린트로 강제하고 전수 정리 (2026-11-05 사용자 지적)

규칙 문서만 있고 검사가 없어 어긋남이 퍼졌다. 검사를 먼저 세우고(린트 — 새 위반을 막는다) 기존 위반을 한 번에 고친다.

끝났다. 그대로 두는 정적 상태: 상태 없는 객체(SQL 드라이버 · 애니메이션 코덱), 정적 초기화 전에도 세는 `CacheStats`, 테스트 등록표, OS 진입점, 모듈 경계 창구, `FileDialog`(떼어 둔 스레드가 객체를 쓴다), 아이콘 표 발행(글꼴 설정이 에디터 범위보다 먼저 돈다), `EditorExtensionRegistry` 의 종류별 목록(등록 줄의 수명이 모듈 이미지와 같아 편집기 범위보다 먼저 선다 — 언리얼 모듈 등록과 같은 자리).

### 5. 그 뒤

- Linux 에디터 체인 나머지(WSL lavapipe, 140 단계 중 약 18 단계): 2026-11-06 처음 끝까지 돌았다. 원인을 못 가린 것 — `audiomixer`(M 단추를 눌러도 음소거 수가 그대로), `graphaddnode` · `graphviewtools`(노드 추가 · 정렬이 안 먹는다), `viewportselect`(Ctrl 클릭 선택), `assetmanage`(휴지통 `Asset.Recycled`), `meshmodifiers`, `idle`(창 초점 없는 프레임 상태), `previewswap` · `flowboundaries`(GL 없음으로 보인다), `selftests` 의 `dpi.monitorScaleFollows`(Xvfb 에서만). 믹서를 단독으로 돌리면 팔레트 입력부터 먹지 않는다 — WSLg · Xvfb 에서 창이 입력 초점을 받지 못하는 것이 공통 원인으로 보이니, 창 관리자와 초점이 있는 화면에서 먼저 다시 돌린다. 그래픽 골든(`screen` · `viewmodes` · `emptyscene` · `loadingscreen` · `sceneviewgameview` · `screenrectview` · `flowterrain` · `terrainbrush`)은 크기부터 다르다 — 골든을 만든 Windows 모니터 DPI 에 묶인 것으로 보이며, 어느 PC 에서나 같은 크기가 되게 하거나 Linux 골든을 따로 둔다. lavapipe 검증 레이어가 `blendEnable` + `R16G16_SNORM` 을 잡는다(`viewmodes`) — 포맷 기능 확인 없이 블렌드를 켠다. 시나리오 게임 체인은 Debug 텍스처 임포트가 너무 느려 돌려 보지 못했다.
