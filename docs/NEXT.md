# 다음 세션 인계

> 질문: 무엇이 남았고 무엇부터 하는가?
> 독자: 이어서 일하는 세션(에이전트와 사람).
> 결론: 아래 **남은 일**만 본다 — 끝난 일은 git 기록과 계획 문서([StructureRefactor](StructureRefactor.md))에 있다. 순서는 1 → 6. 이 문서에는 끝난 일을 쌓지 않는다(끝나면 줄을 지운다).

## 일하는 법 (요약)

- 단계마다 코드 → `py -3 -m Scripts lint` → 커밋. 묶음 끝에 빌드 · 단위(`build/Ninja-Debug/Bin/App.exe --test`) · 전체 `ctest` 를 Debug(약 5 분)와 Release(지어진 게임마다 `UnitTests.<게임>` · `Scenario.<팩>`, 약 15 분) 둘 다 한 번. 배포 구성은 `py -3 -m Scripts shipping-check`.
- **테스트마다 로그의 Error · Warning 을 모두 분류한다** — `build/Ninja-Debug/Testing/Logs/*.log`, id 없는 줄과 `[id:…]` 줄을 나눠 테스트가 선언한 것인지 본다. 통과만 보지 않는다.
- 느린 곳은 새로 재지 말고 로그의 `[Scenario] … timeline:` · `stage reset took` · `Backend swap took` 줄을 모은다(`Test/README.md` 의 시간 기록).
- 규칙: 구조로 푼다 · 폴백 없음 · 게이트는 최후 · 구성 차이는 크기대로(`docs/agents/BuildConfigSwitch.md`) · 용어는 `docs/agents/NamingCpp.md` 의 표. 시나리오 체계는 `Test/README.md` 가 정본.
- Linux 는 WSL 클론(`~/LT-linux`, `git fetch origin main` 뒤 `~/lt-scripts/build.sh`)에서 Linux-Debug 전체 빌드 · 단위 테스트(`Bin/App --test`)를 돌린다(2026-11-02 통과). 시나리오는 WSLg 에서 Vulkan 만 돈다 — GL 은 WSLg 드라이버에 `GL_ARB_gl_spirv` 가 없어 기동이 물러나고(77), 창 관리자(Weston XWayland)가 아이콘화 요청을 무시해 `engine/automation/windowpresent` 의 최소화 단계가 진다.
  Wayland 창 층(`SW_ENABLE_WAYLAND`)은 기본 클론 빌드에 없다 — `~/lt-deps/root` 에 sudo 없이 푼 `libxkbcommon-dev` · `wayland-protocols`(`apt-get download` + `dpkg -x`, .pc 의 prefix 를 그 폴더로)를 `PKG_CONFIG_PATH` 로 주고 `cmake --preset Linux-Debug -B build/Linux-Wayland -DSW_ENABLE_WAYLAND=ON` 로 짓는다. 실행은 `SW_WINDOW_BACKEND=wayland`.

## 남은 일

### 0. 사용자가 시킨 일 — 빠짐없이 (2026-11-01)

에이전트 없이 직접 한다. 이름만 보지 말고 내용을 읽고 판단한다. 빌드 통과는 판단 기준이 아니라 마지막 확인이다.

1. 코드 전체를 다시 훑어 SOLID · 프로젝트 구성(층 · 책임 · 의존 방향 · 폴더)을 전면적으로 고친다 — StructureRefactor 7 · 8 절의 "남김" · "다음" 줄 포함.
2. 확인한 상태(Debug 빌드 · 단위 · ctest, Release 빌드 · ctest, 오류 · 경고 분류)만 중간중간 origin main 에 푸시한다.
3. 아래 2 ~ 6 절(테스트 공백 · 흔들리는 골든 · T8 · T9 …)도 남은 일이다.
4. 로우폴리 에셋을 실제 게임 품질로(CC0 만, 원본 바이너리는 Git LFS — 사용자 결정). Quaternius 열 팩은 `/AssetLibrary/Quaternius/`(무시)에 받아 두었고, 게임에 쓰는 것만
   `common/models_raw · textures_raw/quaternius/<묶음>/` 로 옮긴다(credits.md). 농장 · 서부 · 놀이공원 · 투기장 · 나일 소품과 HarvestValley 농부 · 마을 사람(외형 · 피팅 · UAL 클립)은 했다. 남은 것:
   - Shooter3D 몸을 KayKit 에서 UBC 로(리그 `player_body.rig.xml` · 1인칭 숨김 칸 · 망토 사슬 · 무기 소켓을 UBC 뼈로, 해골은 Bestiary 가 QAL 이라 못 쓴다 — 다른 CC0 몬스터를 찾는다), 무기는 Sci-Fi Essentials 총.
   - 옷의 두건(레인저 Hood)은 머리 꾸미기를 감추는 규칙과 함께, 농부 동작은 UAL2 의 `farm_harvest` · `farm_plantseed` · `farm_watering` 클립으로.
   - 놀이공원 나무(`PropScatter` 는 모델마다 머티리얼 하나라 구간 모델을 못 받는다), Empty 쇼케이스 · MeadowVillage · StarSkirmish 의 소품, 손으로 쓴 PBR 머티리얼이 견본 칸을 하나씩 베끼는 구조(머티리얼 인스턴스 · 부모).
5. 에디터 UI · UX(taste-skill 앱 화면 규칙) — 테마는 키 → 값 표(`SW_THEMECOLOR` · `SW_THEMEMETRIC`), 크롬 · 이름표 1차 손질은 했다. 남은 것:
   - 에디터 코드의 영어 화면 글 약 410 줄(`ImGui::Text( "…" )` · 단추 · 탭 · 칸 이름표)을 `SW_LOCKEY` + `editor.strings.xml`(에디터 플러그인 · 키트 에디터는 `engine.strings.xml` — 그 프로젝트가 GameFramework 를 모은다)로. 흔한 동사는 공용 `EdVerb.*` 가 있다. 새 키마다 ko(engine 은 ko · ja) 번역이 있어야 ToolCheckText 가 지나간다(`-export-po` → po 채우기 → `-import-po`).
     명령의 라벨 · 툴팁 · 설명은 이제 데이터(`Content/editor/commands/*.commands.xml`)에 글 그대로 있다 — 현지화 키로 바꿀 때 그 파일의 칸을 키로 바꾼다.
6. **상용 엔진 기준 구조 점검 — 남은 것** (사용자 지시: "상용 엔진과 비교해 우리 구조의 차이와 결함을 Source 전체에서 찾아 고친다"). 판단 기준 ·
   끝낸 경계는 StructureRefactor 9 절(GPU 자원은 프록시 / 한 번 만들기 / 게시, 스레드 경계 표). 끝낸 경계: 머티리얼 · 메시 · 텍스처 · 에디터 텍스처 · 오디오 ·
   애니메이션 · 물리 입력 · 내비 · 네트워크 · 에셋 도착. 남은 것:
   1. 남은 경계를 같은 기준(9 절 표)으로 Source 전체에서 찾는다.

### 2. 테스트 · 시나리오 공백

| 일 | 내용 |
|---|---|
| 흔들리는 게임 골든 | killcam(dx11 · gl 28 %) · cape(dx12 · gl 약 2 %) · bodyviews(약 1.7 %)가 한 번씩 졌다 — 셋 다 Shooter3D 체인이고 그 체인은 이제 백엔드를 차례로 돈다(프레임 100 ms 부하가 사라졌다, Test/README "어디서 시간이 드는가"), 남는지 본다 — 외형 · 에셋이 늦게 도착한 순간을 찍는 흔들림으로 보인다. 찍기 전 자리 잡기가 외형 로드를 세는지. 머티리얼 PSO 를 만드는 중인 프레임은 이제 자리 잡음에서 뺀다(`Assets.Pending`) — 남는 흔들림만 본다. Debug `Scenario.empty` dx12 의 `game.empty.useroptions` 가 한 번 `colorvision.ppm` 캡처를 남기지 않고 졌다(2026-11-01, 다음 실행은 통과) — 캡처 단계가 파일을 쓰기 전에 판정이 읽는지. Release `Scenario.shooter3d` gl 이 한 번 GL 컨텍스트를 잃었다(2026-11-02, `-j 4` · `game.shooter3d.enemydeath` 에서 해골 VAT 군중이 처음 준비된 직후 `GL_CONTEXT_LOST`, 혼자 다시 돌리면 통과) — 드라이버 리셋인지 VAT 인덱스 드로우의 GPU 멈춤인지 가린다(GL 디버그 출력 · 같은 단계를 GL 로 반복) |
| 남은 단위 테스트 | 테스트 없는 폴더 41(`py -3 Scripts/dev/TestCoverage.py`) — 거의 온라인 키트다. 온라인 키트는 싣는 게임이 없어 `UnitTests.OnlineKits`(OnlineLoadBot `-test`, Release — 봇이 온라인 키트를 모두 싣는다)가 돌린다 — 매칭 · 채팅 거르기 · 라이브 이벤트 · 친구 관계 · 거래 상태 기계 · 우편 · 경제 구매 · 순위 보드 정의 · GM 권한 · 지급은 채웠다, 나머지(계정 로그인 저장 `LoginStoreLogic` · 서버 목록 `ServerListModel` — 클라이언트 하네스가 필요하다)는 `MemoryServiceDatabase` 로 저장 규칙부터. 이식에 하네스가 필요한 것: 자식 프로세스(크래시 · 로그), LoopbackCluster(네트워크), AnimationTestUtil, RHIFakeDevice, FakeFontRasterizer, 임시 프로젝트 마운트, Reflection 큰 파일 다섯 · 파서 |

### 5. 그 뒤

- Linux 시나리오 체인(X11 · Wayland · ALSA · epoll 은 Windows 가 짓지 않는다): 빌드 · 단위 테스트는 WSL 에서 돈다. 체인은 ICCCM 아이콘화를 따르는 창 관리자와 `GL_ARB_gl_spirv` 가 있는 GL 이 있는 리눅스 PC(또는 CI)에서 돌린다.

### 6. 다음 설계 (긴 것)

- 빌드 속도(2026-11-04 `-ftime-trace`, Release 단독): `MaterialPanel` · `PrefabPanel` 은 이제 각 1.1 초다. 가장 느린 단위는 `Engine.cpp` · `EngineSubsystemSet.cpp`(각 8.5 초 — 백엔드 6 초 · 최적화 2.9 초, 프런트 2.2 초). 서브시스템을 한 파일이 모두 include 해 만들어 그 헤더들의 인라인 함수를 이 TU 가 코드로 낸다 — 서브시스템마다 제 TU 에서 등록하는 공장(언리얼 `USubsystem` 의 자동 등록)이면 이 둘이 가벼워진다. 재는 법: `compile_commands.json` 명령에 `/clang:-ftime-trace` 를 붙여 scratch 로 낸다.
- 시나리오 범용화: 재생기 · 판정 층을 나눌 때 `AutomationSession`(이미 `ISubsystem` + `IScenarioPlayer`)에 인터페이스를 더 붙이지 않고 재생기를 별도 객체로. 시퀀서 이벤트 트랙이 시나리오를 부르는 쪽, 액션 단위 녹화 · 리플레이(고스트 · 킬캠). 단계 등록은 키트 · 게임이 아직 0.
- 창 바둑판 간격의 창 틀 두께가 Win32 대략 값이다(`AutomationPerBackend`) — 플랫폼 창 틀 API 가 생기면.
