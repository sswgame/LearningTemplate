# 다음 세션 인계

> 질문: 무엇이 남았고 무엇부터 하는가?
> 독자: 이어서 일하는 세션(에이전트와 사람).
> 결론: 아래 **남은 일**만 본다 — 끝난 일은 git 기록과 계획 문서([StructureRefactor](StructureRefactor.md))에 있다. 순서는 A → 0 → 2 → 5. 이 문서에는 끝난 일을 쌓지 않는다(끝나면 줄을 지운다).

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

**상태:** `8738843639` 까지 push(R4 + R1 일부). 본 트리는 R1 · R3 · R5 일부를 더 합쳤고 Debug 단위 테스트의 복제 서버 경합(성능 #10 병렬 경로)을 고쳐 다시 검증 중이다. 6 차 R2 · R3 · R5 · R7 · R8 이 돌고 있다. 규칙 · 범위는 `.claude/handoff/brief_round6.md` · `brief_round7.md`(Git 밖), 조사 문서는 `.claude/handoff/surveys/`. 합칠 때마다 빌드 · 시나리오 · 골든(그림을 눈으로 본 뒤)을 맞춘다.

**작업자가 못 끝낸 것도 한다(사용자 지시 2026-11-07).** 보고의 "옮기지 못한 목록" 은 통합 때 그 자리에서 처리하거나 다음 작업자 범위에 넣는다 — 인계에 쌓아 두지 않는다.

**1. 6 차 작업자 범위(요약 — 정본은 brief_round6.md).**
- R1 성능 나머지(`refactor_performance.md` #2 · #3 · #5 · #9 · #10 · #11 · #13 · #15 · #17, 컴파일 #18 ~ #20).
- R2 온라인: 응답 구조체 분리, 결과 열거형 축소, 계정 클라이언트를 `OnlineProtocolClient` 로, `isRetryable`, 서버 단계 4 · 5, `GF_Server_OnlineCore` 분리, `HTTPLink` 가 핸들 없이 있을 수 없게(통합에서 `HTTP2Client` 의 `setHandle` 누락을 고쳤다).
- R3 층 · 구조 · 이름: 서버에서 UI 빼기(`CameraDirectorComponent` 사진 모드가 아직 `UISystem` 을 쓴다), CMake · Scripts 의 `if(WIN32) else()`, `structure_survey.md` 파일 이동 표, App 정리, 이름 나머지(`CombatHealth` 멤버, 지역 `director`, 읽기 동사, `…Catalog`, 애니메이션 "유닛", `…Fields` → `…Params`).
- R4 렌더: `.reflprobe` 생성 + 시차 보정 + 배치, 디더 clip 을 표면 계산 뒤로 옮기고 Vulkan 1.2 로 낮추기(그 뒤 dzn 으로 시나리오), DQ 스키닝, HDRI 하늘, 볼륨 안개 · 자동 노출 시나리오.
- R5 게임 적용 1: 시네마틱, 절단(Shooter3D 해골), 공간 오디오, `Ballistics`, `PrefabPool`, 튜토리얼 힌트 · 자동 저장 볼륨 · 왕복 발판, `ListViewWidget`, 차량 씬, 2D 기능.
- R6 게임 적용 2: 온라인 키트를 Shooter3D 에, WildWest 규칙, 업적, 락스텝 RTS, 텔레메트리 · 크래시 업로드(끝점은 설정), 장르 키트 시연.
- R7: 개발 명령 시나리오, 감시 값 판정, 등록 프레임 나누기, `unfinished_survey.md` 6 절 미사용 처리, 접속 뒤 합성 대조 · 조건 보존 저장 시나리오, 조건을 받지 않는 형식 목록, 단위 테스트 공백(`NetSecurityLoginCrypto` · `AccountTicketConnect` · `PlatformLoginClient`), 아래 4 번 확인거리.
- R8 플랫폼: `waitIdle` 지연 해제 → WebGPU 백엔드 → Emscripten → Android Vulkan(Android 는 iOS 제외, 웹은 WebGPU 만, 콘솔 대기). 시작 전 요구 판 · 필수 기능을 대조한다.
  - **모바일 · 데스크톱 화면 대응도 플랫폼 완료 조건에 넣는다.** 그래픽 API 차이는 기존 RHI 백엔드가 맡고([`RHIBackendInfo`](../Source/Engine/Graphics/RHI/RHIBackendInfo.h)), 게임 UI 는 가능한 한 같은 위젯/화면 데이터를 쓰되 해상도 · 종횡비 · DPI · 안전 영역 · 입력 방식에 따라 레이아웃과 조작법을 바꾼다. 현재 [`UIScaleSettings`](../Source/Engine/UI/Layout/UIScale.h)는 배율 규칙, [`SafeZonePanel`](../Source/Engine/UI/Layout/SafeZonePanel.h)은 레이아웃 안전 영역, [`UIResolutionPresetTable`](../Source/Engine/UI/Layout/UIResolutionPresetTable.h)은 에디터 미리보기 해상도를 제공하고 Box/Grid/Wrap 패널도 있다. 다만 `makeViewport`의 안전 여백은 지금 디버그용 대칭 비율로 만들어지며, 조사한 경로에서는 Android OS의 실제 노치/시스템 바 여백을 뷰포트에 전달하는 연결을 확인하지 못했다. 해상도 견본만으로 기기 미리보기도 끝난 것은 아니다.
  - Android/Vulkan 검증에서는 창·표면의 크기/회전/수명주기와 실제 안전 여백 공급을 잇고, UI 를 단순히 축소하는 대신 좁은 화면에서 재배치하고 터치 타깃·포인터/터치 입력을 점검한다. GPU 쪽은 플랫폼별 별도 렌더러를 만들기보다 RHI 능력과 데이터 기반 품질 등급으로 대역폭 · 메모리 · 전력 예산에 맞춘다. 완료 기준은 데스크톱/세로·가로 모바일의 해상도 · DPI · 안전 영역 · 글자 배율 조합을 에디터에서 미리 보고, 동일 조건의 레이아웃/입력 시나리오를 자동 검증하는 것이다. 이 UI/그래픽 품질 트랙은 UI 를 렌더할 수 있는 것과 실제 기기에서 읽고 조작하기 좋은 것을 구별한다.

**1b. 7 차 작업자 범위(2026-11-07 감사 — 본문 · 판단은 [DesignBacklog](DesignBacklog.md) 의 "통합 담당 판단" 절이 정본).**
- S1 결함: 1 인칭 총구 폴백 · `chance` 굴림 · 관통탄 다중 명중 · 포탑 비행 중 차폐 · 머티리얼 파라미터 묶음 핫 리로드 · 셰이더 쿠킹 스탬프 의존 목록 · X11 조합 로캘 · 대기 카메라 종횡비 · `DepletingValue` · 종료 코드 `InitializationFailed` · WSL 경로 · 생성 표 잠금 · 파서 동시성 상한.
- S2 계약: 단일 Engine · GameInstance 계약 · 초기화 되감기 · 서비스 등록 결과 전파 · 수명 테스트 · 카탈로그 참조 엄격도 · 테스트 틀 격리 · JSON 보고.
- S3 에디터 작업 안전: 자동 저장 · 복구, 이동 트랜잭션 · 폴더 이동, 저장 검색 · 통합 검색, Map Check Fix, Assert 설정, `CallInEditor`, 소스 관리 변경 목록, 배치 고스트 · 표면.
- S4 시각 저작: 본 마스크 · 소켓 기즈모 · 타일셋 편집기 · 스프라이트 슬라이스 · 안개 도구 · G-buffer 보기 · 명세 · Ability 카탈로그 편집기.
- S5 연출: 시퀀서 바인딩 · 컷 차선 · 정지 계약 · 트랙 · 컷씬 디버거 · 관통 킬캠 · 잔상 · VFX 미리보기.
- S6 진단: 버그 리포트 입력 · 리로드 집계 · 성능 산출물 · 게임플레이 디버거들 · 공간 효과 · 이동 제약.
- S7 표면: 공통 표면 질의 · 날씨 · 침수 → `Wet` · 발자국 · 눈 흔적 · 모래 함몰.
- S8 측정 · 성능 후보 · 매직 리터럴 집계 · 장르 키트 시연(K1) · 장르 완결 루프 시나리오.
- 2 차 묶음: `GF_Housing` · `GF_ModSupport` · 오픈월드 싱글 · 액션 MMO 수직 슬라이스 · 반사 RPC 메타데이터의 실제 경로.

**2. 사용자에게 받을 것.** 영수증 · 푸시 · 외부 로그인의 실제 계정과 키(지금은 로컬 가짜 끝점까지 잇는다 — `Config/Server/Online/Providers/`, `Scripts/net/OnlineLocalRun.py`). 새 의존성(Dawn, 인코더 라이브러리)의 라이선스.

**3. Linux.** 큰 일이 끝난 뒤 `LinuxScenarios.py all`, `.linux` 골든, 에디터 체인 18 단계(아래 5 절). 엔진은 Vulkan 1.2 로 낮췄다(R4) — dzn(`~/lt-deps/dzn`)으로 시나리오를 다시 돌려 본다.

**4. 확인거리.**
- 1 인칭 탄도선이 시점 총에 가려 화면에 남지 않는다(`ShooterPlayerComponent` 총구가 몸 무기). 시나리오 `projectilecombo` 는 값(`Shooter3D.TracerWidth`)으로 본다.
- 엔진 서비스 생성 순서가 의존 · 이름 순이다 — 생성자 안에서 다른 서비스를 찾는 공장이 있으면 드러난다.
- 자식이 더한 전이 id 가 부모와 겹치면 조용히 합쳐진다. 머티리얼 파라미터 묶음은 핫 리로드가 없다.
- 리플렉션 형식의 `_base` 는 문서 표(`Content/engine/settings/datacataloginheritance.xml` 의 `<Document>`)만 읽는다 — 새 문서 형식에 `_base` 를 쓰면 표에 줄을 더한다.
- 쓰는 것만 담는 쿠킹: engine 팩이 크게 줄었다 — 게임을 `-cooked-content` 로 띄워 코드가 경로를 조립해 읽는 에셋이 빠지지 않았는지 본다. 팩 쿠킹은 참조 칸 설정(`AssetReferenceFields.json`)을 `PackReach` 작업 안에서 읽는다 — 그 설정이 바뀌면 팩이 다시 구워지는지 본다.

**결정:** 범용 스크립팅 언어는 현재 도입하지 않는다. 핫 리로드와 기존 데이터 기반 기능이 다루지 못하는 실제 요구(예: C++ 빌드 없이 디자이너가 새 동작을 작성)가 확인될 때만 재검토한다([핫 리로드와 스크립팅 언어 판단](DesignBacklog.md#핫-리로드와-스크립팅-언어-판단)). 런타임 아틀라스는 하지 않기로 했다. 메모리 풀은 측정 뒤에, GC 는 권하지 않는다.

**줄끝:** `.gitattributes` 가 `* text=auto eol=lf` 다.

사람이 보는 실행: `py -3 Scripts/dev/WatchScenarios.py [--preset 프리셋] [--backend 백엔드] [--stages a,b] [체인…]`.

### 0. 사용자가 시킨 일 — 빠짐없이 (2026-11-01)

에이전트 없이 직접 한다. 이름만 보지 말고 내용을 읽고 판단한다. 빌드 통과는 판단 기준이 아니라 마지막 확인이다.

1. 코드 전체를 다시 훑어 SOLID · 프로젝트 구성(층 · 책임 · 의존 방향 · 폴더)을 전면적으로 고친다 — StructureRefactor 7 · 8 절의 "남김" · "다음" 줄 포함.
2. 확인한 상태(Debug 빌드 · 단위 · ctest, Release 빌드 · ctest, 오류 · 경고 분류)만 중간중간 origin main 에 푸시한다.
3. 아래 2 ~ 6 절(테스트 공백 · 흔들리는 골든 · T8 · T9 …)도 남은 일이다.
4. 로우폴리 에셋을 실제 게임 품질로(CC0 만, 원본 바이너리는 Git LFS — 사용자 결정). Quaternius 열 팩은 `/AssetLibrary/Quaternius/`(무시)에 받아 두었고, 게임에 쓰는 것만
   `common/models_raw · textures_raw/quaternius/<묶음>/` 로 옮긴다(credits.md). 농장 · 서부 · 놀이공원 · 투기장 · 나일 소품, HarvestValley 농부 · 마을 사람(외형 · 피팅 · UAL 클립), MeadowVillage 밭 칸은 했다. 새 리소스는 "무료 상업 이용"만으로 승인하지 않는다 — CC0 여부와 원본 파일 재배포 가능성까지 묶음별로 확인한다. Quaternius는 2026-08-28 갱신된 QAL에서 상업 이용은 허용하지만 원본 에셋 단독 재배포를 금지하므로 CC0만이라는 현재 기준에는 자동 포함되지 않는다. 기존 `common/models_raw/quaternius`에는 라이선스 원문 파일이 없으므로 기존 묶음도 원본 다운로드의 당시 라이선스 증거를 확인한다(아래 키트 리소스 절).

### 2. 테스트 · 시나리오 공백

| 일 | 내용 |
|---|---|
| 흔들리는 게임 골든 | 2026-11-04 Debug · Release 전체 ctest 각 일곱 번과 Release Shooter3D 체인 세 번에서 한 번도 지지 않았다(killcam · cape · bodyviews · useroptions · GL 컨텍스트 잃음 모두). 다시 지면 그 로그의 단계 · 백엔드 · 직전 `Assets.Pending` 을 적는다 — GL 컨텍스트 잃음은 GL 디버그 출력(`KHR_debug`)으로 드라이버 리셋인지 VAT 인덱스 드로우인지 가린다 |
| 남은 단위 테스트 | 폴더 집계(`py -3 Scripts/dev/TestCoverage.py`)는 테스트가 다른 폴더에 있어도 "없음" 으로 센다 — 실제로 비었는지는 클래스 이름을 테스트에서 찾아 본다. 2026-11-05 에 채운 것: 파괴 상태 · 그래프, 외형 해석, 리그 IK, 턴 중계, RESP 저장소, 메시 쪼개기, 시퀀서 · 단축키 · 전역 변수 · 모듈 · 환경설정 · 테스트 실행기 · 문서 패널 · 테마 값 · Sync 탭 Model, UI 레이아웃 · 포커스 · 이벤트, 날씨 · 시계, 조종 시스템 · 의도 기록, 차량 좌석, 행동 트리, 기믹 회로, 어빌리티, 모듈 카탈로그, 프로파일러 · 타일맵 · 데이터 테이블 · 카탈로그 · 콘텐츠 브라우저 · UI 미리보기 Model, 인터랙터, 프리팹 덮어쓰기, 팩 리더, 크래시 보고, 립싱크, 카메라 모드. `UIPreviewModel` 은 화면이 있어야 해 시나리오 `editor/uipreview` · `uipreviewdesigner` 가 맡는다. 장르 키트 Rule 은 모두 채웠다 — 게임이 싣지 않는 키트는 Release `UnitTests`, 게임이 싣는 키트는 `UnitTests.<게임>`, 온라인 키트는 `UnitTests.OnlineKits` 가 돈다(에디터 케이스는 `Scenario.editor` 의 `editor.unitTests`). 크게 남은 것: 화면 없이 도는 패널 Model 밖의 큰 파일(`Editor/Common` · `Editor/Panels` 의 그리기 코드는 시나리오 몫이다). 온라인 키트는 싣는 게임이 없어 `UnitTests.OnlineKits`(OnlineLoadBot `-test`, Release — 봇이 온라인 키트를 모두 싣는다)가 돌린다 — 매칭 · 채팅 거르기 · 라이브 이벤트 · 친구 관계 · 거래 상태 기계 · 우편 · 경제 구매 · 순위 보드 정의 · GM 권한 · 지급은 채웠다, 계정 로그인 저장(가짜 암호 — 연동 · 마지막 로그인 수단 · 탈퇴 유예 포함)과 서버 목록 `ServerListModel`(실패 길 · 루프백 서버로 응답 길)도 채웠다. 2026-11-07: 경제 클라이언트 · 지갑 거울 · 스토어 결제 흐름, 푸시 제공자(FCM · APNs) · 발송기 · 제공자 설정, 티켓 주 키 · 외부 로그인 제공자 설정을 채웠다. 계정 키트에 아직 없는 것: `NetSecurityLoginCrypto`, 클라이언트 쪽 `AccountTicketConnect` · `PlatformLoginClient`(`SystemExternalBrowser` 는 운영체제 호출이라 시나리오 몫). 2026-11-06 서비스 층도 채웠다 — 순위표 · 길드 · 매칭 대기열 · 파티 로비 · 채팅 · 친구 · 경제 영수증 · 거래 · 로그인 · 우편함 · 라이브 이벤트 · 서버 디렉터리 · GM, 그리고 외형 선택 · 퀘스트 일지 · 예약 작업 · 보존 쓸기 · 월드 플래그 · 록온 · 대화 러너 · UI 화면 서비스 · UI 애니메이션. 네트워크 하네스는 있다 — Core 엔드포인트는 `LoopbackCluster`, 온라인 키트 끝단은 `OnlineHostTestUtil.h`(`OnlineTestFixture` — 호스트 + 클라이언트 묶음, 키트 DLL 이 include 하고 키트 서버 쪽은 같은 프로토콜 항목의 가짜로 올린다). 하네스는 있다(`AnimationTestUtil` · `RHIFakeDevice` · `FakeFontRasterizer`, 자식 프로세스는 `Core/Process` · 크래시 보고). 2026-11-06 에 더 채운 것: 모듈 의존 그래프 · 언로드 큐 · 섀도 복사본 거절, UI 문서, 가상 입력, 개발 명령, 스프라이트 클립, 캐릭터 데이터 읽개, UI 입력 소비 · 입력 방식, 포인터 잡기 취소, 게임 흐름의 화면 없는 규칙(팁 · UI 없는 로딩 화면 · 타이틀 없는 게임). 별도 프로그램인 `Programs/ReflectionParser`는 fixture 기반 통합 테스트 8개와 CLI 인자 실패 테스트 2개를 둔다(아래 Reflection 조사 참조). 아직 단위 테스트가 없는 곳은 섀도 복사본의 성공 길(실제 모듈 바이너리가 있어야 해 핫 리로드 시나리오가 맡는다), 화면이 뜨는 게임 흐름(시나리오 `loadingscreen` 이 맡는다). 집계의 나머지 "없음" 폴더는 위 안내대로 클래스 이름으로 하나씩 본다 |

### 5. 그 뒤

- Linux 에디터 체인 나머지(WSL lavapipe, 140 단계 중 약 18 단계): 2026-11-06 처음 끝까지 돌았다. 원인을 못 가린 것 — `audiomixer`(M 단추를 눌러도 음소거 수가 그대로), `graphaddnode` · `graphviewtools`(노드 추가 · 정렬이 안 먹는다), `viewportselect`(Ctrl 클릭 선택), `assetmanage`(휴지통 `Asset.Recycled`), `meshmodifiers`, `idle`(창 초점 없는 프레임 상태), `previewswap` · `flowboundaries`(GL 없음으로 보인다), `selftests` 의 `dpi.monitorScaleFollows`(Xvfb 에서만). 믹서를 단독으로 돌리면 팔레트 입력부터 먹지 않는다 — WSLg · Xvfb 에서 창이 입력 초점을 받지 못하는 것이 공통 원인으로 보이니, 창 관리자와 초점이 있는 화면에서 먼저 다시 돌린다. 그래픽 골든(`screen` · `viewmodes` · `emptyscene` · `loadingscreen` · `sceneviewgameview` · `screenrectview` · `flowterrain` · `terrainbrush`)은 크기부터 다르다 — 골든을 만든 Windows 모니터 DPI 에 묶인 것으로 보이며, 어느 PC 에서나 같은 크기가 되게 하거나 Linux 골든을 따로 둔다. lavapipe 검증 레이어가 `blendEnable` + `R16G16_SNORM` 을 잡는다(`viewmodes`) — 포맷 기능 확인 없이 블렌드를 켠다. 시나리오 게임 체인은 Debug 텍스처 임포트가 너무 느려 돌려 보지 못했다.

코드를 직접 확인한 결함과, 인계 문서에만 있고 이번엔 다시 돌리지 않은 항목을 나눴습니다. 빌드와 시나리오는 돌리지 않았습니다.
