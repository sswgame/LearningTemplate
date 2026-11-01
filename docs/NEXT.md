# 다음 세션 인계

> 질문: 무엇이 남았고 무엇부터 하는가?
> 독자: 이어서 일하는 세션(에이전트와 사람).
> 결론: 아래 **남은 일**만 본다 — 끝난 일은 git 기록과 계획 문서([StructureRefactor](StructureRefactor.md))에 있다. 순서는 1 → 6. 이 문서에는 끝난 일을 쌓지 않는다(끝나면 줄을 지운다).

## 일하는 법 (요약)

- 단계마다 코드 → `py -3 -m Scripts lint` → 커밋. 묶음 끝에 빌드 · 단위(`build/Ninja-Debug/Bin/App.exe --test`) · 전체 `ctest` 를 Debug(약 5 분)와 Release(지어진 게임마다 `UnitTests.<게임>` · `Scenario.<팩>`, 약 15 분) 둘 다 한 번. 배포 구성은 `py -3 -m Scripts shipping-check`.
- **테스트마다 로그의 Error · Warning 을 모두 분류한다** — `build/Ninja-Debug/Testing/Logs/*.log`, id 없는 줄과 `[id:…]` 줄을 나눠 테스트가 선언한 것인지 본다. 통과만 보지 않는다.
- 느린 곳은 새로 재지 말고 로그의 `[Scenario] … timeline:` · `stage reset took` · `Backend swap took` 줄을 모은다(`Test/README.md` 의 시간 기록).
- 규칙: 구조로 푼다 · 폴백 없음 · 게이트는 최후 · 구성 차이는 크기대로(`docs/agents/BuildConfigSwitch.md`) · 용어는 `docs/agents/NamingCpp.md` 의 표. 시나리오 체계는 `Test/README.md` 가 정본.
- Linux 는 WSL 클론(`~/LT-linux`)에서 바뀐 파일의 문법 검사만 했다 — 실제 빌드 · 테스트는 아직.

## 남은 일

### 0. 사용자가 시킨 일 — 빠짐없이 (2026-11-01)

에이전트 없이 직접 한다. 이름만 보지 말고 내용을 읽고 판단한다. 빌드 통과는 판단 기준이 아니라 마지막 확인이다.

1. 코드 전체를 다시 훑어 SOLID · 프로젝트 구성(층 · 책임 · 의존 방향 · 폴더)을 전면적으로 고친다 — StructureRefactor 7 · 8 절의 "남김" · "다음" 줄 포함(아래 1 절).
2. 확인한 상태(Debug 빌드 · 단위 · ctest, Release 빌드 · ctest, 오류 · 경고 분류)만 중간중간 origin main 에 푸시한다.
3. 아래 2 ~ 6 절(테스트 공백 · 흔들리는 골든 · T8 · T9 …)도 남은 일이다.
4. 로우폴리 에셋을 실제 게임 품질로(CC0 만, 원본 바이너리는 Git LFS — 사용자 결정). 경로(`pbr_lit` · glTF PBR 임포트 · 월드 타일링)와 Shooter3D 경기장 · 메시 벤치(`-gv_stressTestScanProps`)는 했다.
   남은 것: 캐릭터(KayKit 기사 · 해골 — 리그 · 클립 · 망토 사슬 · 소켓이 이 뼈대에 묶였다. 후보는 CC0 사람형 베이스 + 클립 리타깃), 무기(Kenney 블래스터), 다른 게임 맵(Empty 쇼케이스 · ThemePark · HarvestValley …)의 바닥 · 벽 · 소품.
   캐릭터 후보 Quaternius Universal Base Characters · Universal Animation Library(둘 다 CC0, itch.io 무료판 Standard)는 스크립트 내려받기가 봇 차단으로 막혔다 — 사용자가 내려받아 `Content/common/models_raw/quaternius/` 에 zip 을 두면 이어서 한다.
5. 에디터 UI · UX(taste-skill 앱 화면 규칙) — 테마는 키 → 값 표(`SW_THEMECOLOR` · `SW_THEMEMETRIC`), 크롬 · 이름표 1차 손질은 했다. 남은 것:
   - 창마다 UX 2차: `editor/uiaudit.scenario.xml` 캡처 38 장 중 이번에 본 것은 인스펙터 · 콘텐츠 브라우저 · 프로파일러 · 환경설정 · 입력 맵 · 머티리얼 · 프로젝트 · 사용자 설정뿐이다. 나머지 30 장(그래프 · 시퀀서 · 데이터 표 · 멀티플레이 · 캐릭터 에디터 …)을 같은 눈으로 본다.
   - 에디터 코드의 영어 화면 글 약 500 줄(`ImGui::Text( "…" )` 등)을 `SW_LOCKEY` + `editor.strings.xml` 로 — 창 UX 를 고칠 때 그 창 것을 함께 옮긴다.
     명령의 라벨 · 툴팁 · 설명은 이제 데이터(`Content/editor/commands/*.commands.xml`)에 글 그대로 있다 — 현지화 키로 바꿀 때 그 파일의 칸을 키로 바꾼다.
6. **상용 엔진 기준 구조 점검 — 남은 것** (사용자 지시: "상용 엔진과 비교해 우리 구조의 차이와 결함을 Source 전체에서 찾아 고친다"). 판단 기준 ·
   끝낸 경계는 StructureRefactor 9 절(GPU 자원은 프록시 / 한 번 만들기 / 게시, 스레드 경계 표). 끝낸 경계: 머티리얼 · 메시 · 텍스처 · 에디터 텍스처 · 오디오 ·
   애니메이션 · 물리 입력 · 내비 · 네트워크 · 에셋 도착. 남은 것:
   1. **시나리오 고정 대기를 조건으로**: 에디터 입력 앞 `<Wait frames>` 784 곳(98 파일)은 한꺼번에 지우면 진다(2026-11-01 시도 — 앞 단계의 비동기 일을 기다리던 것이 섞였다).
      시나리오마다 무엇을 기다리는지 보고 `WaitUntil`(감시 값)로 바꾼다. 입력 단계가 스스로 기다리는 것(대상 위젯 · 키보드 초점)과 겹치는 것만 지운다.
   2. **시나리오 속도**(사용자 질문): 체인 하나가 백엔드 넷을 동시에 띄워 실제 시간이 부하에 묶인다(Release `Scenario.shooter3d` vk 가 crowd 단계 150 초 시한을 넘김 —
      같은 3185 프레임에 dx11 40 초 · dx12 78 초 · gl 111 초). timeline 줄을 모아 어디서 시간이 드는지 재고(기다림 · 단계 재설정 · 기동), 프레임 고정 진행 · 단계 묶기 · 백엔드별 슬롯을 판단한다.

### 1. 진행 중 (2026-11-01)

| 일 | 상태 |
|---|---|
| SOLID 남은 표 | StructureRefactor 7 절 15(Wayland 리스너 — Linux 빌드에서) |

### 2. 테스트 · 시나리오 공백

| 일 | 내용 |
|---|---|
| 흔들리는 게임 골든 | killcam(dx11 · gl 28 %) · cape(dx12 · gl 약 2 %) · bodyviews(약 1.7 %)가 한 번씩 졌다 — 외형 · 에셋이 늦게 도착한 순간을 찍는 흔들림으로 보인다. 찍기 전 자리 잡기가 외형 로드를 세는지. 머티리얼 PSO 를 만드는 중인 프레임은 이제 자리 잡음에서 뺀다(`Assets.Pending`) — 남는 흔들림만 본다. Debug `Scenario.empty` dx12 의 `game.empty.useroptions` 가 한 번 `colorvision.ppm` 캡처를 남기지 않고 졌다(2026-11-01, 다음 실행은 통과) — 캡처 단계가 파일을 쓰기 전에 판정이 읽는지 |
| 남은 단위 테스트 | 테스트 없는 폴더 41(`py -3 Scripts/dev/TestCoverage.py`) — 거의 온라인 키트다. 온라인 키트는 싣는 게임이 없어 `UnitTests.OnlineKits`(OnlineLoadBot `-test`, Release)가 돌린다 — 매칭 규칙은 채웠다, 나머지(계정 · 채팅 · 경제 · 순위 · 라이브옵스 · 우편 · 서버 목록)는 `MemoryServiceDatabase` 로 저장 규칙부터. 이식에 하네스가 필요한 것: 자식 프로세스(크래시 · 로그), LoopbackCluster(네트워크), AnimationTestUtil, RHIFakeDevice, FakeFontRasterizer, 임시 프로젝트 마운트, Reflection 큰 파일 다섯 · 파서 |
| 벽시계 한도와 부하 | 그물은 이제 조건 대기의 제 시한을 덮는다(`computeWallLimitSeconds`). 남은 것: 백엔드 교체가 많은 시나리오(flowterrain 의 교체 7 번이 혼자서도 19 초)의 교체 시간 자체를 줄인다 |

### 5. 그 뒤

- Linux 실제 빌드 · 테스트(X11 · Wayland · ALSA · epoll 은 Windows 가 짓지 않는다 — CI 후보).

### 6. 다음 설계 (긴 것)

- 빌드 속도: `MaterialPanel` · `PrefabPanel`(각 50 초대, Release)을 `-ftime-trace` 로 나눈다.
- 시나리오 범용화: 재생기 · 판정 층을 나눌 때 `AutomationSession`(이미 `ISubsystem` + `IScenarioPlayer`)에 인터페이스를 더 붙이지 않고 재생기를 별도 객체로. 시퀀서 이벤트 트랙이 시나리오를 부르는 쪽, 액션 단위 녹화 · 리플레이(고스트 · 킬캠). 단계 등록은 키트 · 게임이 아직 0.
- 창 바둑판 간격의 창 틀 두께가 Win32 대략 값이다(`AutomationPerBackend`) — 플랫폼 창 틀 API 가 생기면.
- 전용 서버 종료 때 태그별 증가 진단(Game 약 0.5 KB) 출처.
