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
   - 에셋 종류 색 · 아이콘(등록 매크로의 RGBA) · 컴포넌트 분류 색을 테마 키(`Asset.*` · `Component.*`)로, 크기 리터럴(`GetFontSize() * N` 23 곳)을 `Layout.*` 수치로.
   - 아이콘 이름 → 글리프 표(`EditorIconGlyphs`)를 데이터로(아이콘 글꼴 교체 = 표 교체), 시간대 표(`TimeZoneUtil`)를 데이터 파일로.
   - `Saved/Test/<pid>/` 단위 테스트 쓰기 폴더가 실행마다 남아 수백 개다 — 끝나면 지우거나 한 폴더를 다시 쓴다.
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
| SOLID 남은 표 | StructureRefactor 7 절 8 · 9 · 11(높은 티어 사이) · 12 · 15(Wayland 리스너) · 16, 8 절 5 ~ 8 · 11 · 12 · 14 ~ 19 |

### 2. 테스트 · 시나리오 공백

| 일 | 내용 |
|---|---|
| 흔들리는 게임 골든 | killcam(dx11 · gl 28 %) · cape(dx12 · gl 약 2 %) · bodyviews(약 1.7 %)가 한 번씩 졌다 — 외형 · 에셋이 늦게 도착한 순간을 찍는 흔들림으로 보인다. 찍기 전 자리 잡기가 외형 로드를 세는지. 머티리얼 PSO 를 만드는 중인 프레임은 이제 자리 잡음에서 뺀다(`Assets.Pending`) — 남는 흔들림만 본다 |
| 남은 단위 테스트 | Online `OnlineServiceHost` · `OnlineServiceClient` · `OnlineServerRuntime` · Local · Config · Audit, Kits 의 `none` 줄, Serializer(SerializerUtil · SchemaMigrate · DataPath 처리기). 이식에 하네스가 필요한 것: 자식 프로세스(크래시 · 로그), LoopbackCluster(네트워크), AnimationTestUtil, RHIFakeDevice, FakeFontRasterizer, 임시 프로젝트 마운트, Reflection 큰 파일 다섯 · 파서. `DDSLoader` 는 픽셀 바이트 수를 형식 크기와 견주지 않는다 |
| 벽시계 한도와 부하 | 다른 빌드가 CPU 를 쓰는 동안 `wallTimeoutSeconds` 150 초를 넘는 시나리오가 있다(flowterrain 의 백엔드 교체 7 번이 혼자서도 19 초). 통합 ctest 는 빌드 없이 돌리고, 교체 시간을 줄이거나 한도를 교체 수에 맞춘다 |
| 바깥 시한 < 안쪽 대기 합 | 바깥 `timeoutFrames` 가 고정 대기와 `WaitUntil` 시한의 합보다 작은 시나리오가 45 개다(예: shooter3d cctvmonitor 1800 < 2135). 조건이 늦게 서면 crowd 처럼 덜 구체적인 "timeoutFrames reached" 로 진다 — 바깥 시한이 조건 대기 프레임을 세지 않게 하거나 읽을 때 거절(`AutomationStepValidation`) |
| id 없는 경고 · 오류 | 테스트가 밟는 경로마다 `SW_LOG_*_ID` 를 붙였지만 다른 로더에 더 남았다 — 같은 모양 |

### 3. 프레임 밖으로 뺄 일 (TaskManager)

| 일 | 위치 |
|---|---|
| 미리보기 씬의 외형 대기 | `CharacterAppearanceComponent` — 미리보기 씬이 틱을 돌게(언리얼 FPreviewScene) |
| `DevCommand` 단계 평균 87 ms/프레임의 원인 | timeline 줄의 시나리오별 `DevCommand` |

### 4. 구조 개편 남은 것 ([StructureRefactor](StructureRefactor.md))

| 주제 | 남은 것 (2026-10-31) |
|---|---|
| T8 | Engine 의 `SW_SHIPPING` 약 120 곳(Reflection 메타 · RHI 검증 · Config), `ModuleCompiler` 소유, 배포의 모듈 해제(`unregisterModuleTypes`) 없음 → `ElementRuleTable` · `InteractionCatalog` 캐시가 남았다는 경고 둘. Dev + 자동화 끔 구성은 Editor 가 실행기를 include 해 깨질 것 |
| T9 | 등록자의 모듈 이름 — CMake 가 대상마다 `SW_REGISTRAR_MODULE` 을 주고 매크로가 찍는다(설계는 StructureRefactor T9). 런타임 모듈 이름 = 대상 이름 가정을 핫 리로드 시나리오로 확인 |
| T6 | 에디터 사용자 외형 프리셋 · 썸네일을 슬롯으로(에디터 쪽 저장소 주인), `AccountDeviceSecret` 을 `GameLocalStore` 와 맞춤 |
| T10 | `ShaderCooker::getSubfolderForFormat` 이 모르는 형식에 기본 폴더를 돌려준다(폴백) |
| 옛 형식 호환 | 약어 변경으로 `packSHA256` · `buildID` 와 리플렉션 멤버 이름(`_meshID` 등)이 바뀌어 이미 배포된 패치 매니페스트 · 크래시 요약 · 세이브는 읽지 못한다 — 호환이 필요해지면 이주 |

### 5. 그 뒤

- Linux 실제 빌드 · 테스트(X11 · Wayland · ALSA · epoll 은 Windows 가 짓지 않는다 — CI 후보).

### 6. 다음 설계 (긴 것)

- 빌드 속도: `MaterialPanel` · `PrefabPanel`(각 50 초대, Release)을 `-ftime-trace` 로 나눈다.
- 시나리오 범용화: 재생기 · 판정 층을 나눌 때 `AutomationSession`(이미 `ISubsystem` + `IScenarioPlayer`)에 인터페이스를 더 붙이지 않고 재생기를 별도 객체로. 시퀀서 이벤트 트랙이 시나리오를 부르는 쪽, 액션 단위 녹화 · 리플레이(고스트 · 킬캠). 단계 등록은 키트 · 게임이 아직 0.
- 리로드: 배포 `IAssetCache` 에 reload 가 없게 — 리로드는 DevTools 빌드 단위의 리로더 객체가 캐시를 참조해 공개 API(무효화 · 다시 요청)로 한다(구성, 캐시에 인터페이스를 더 붙이지 않는다 — 캐시 8 개가 다중 상속이 된다). `IAssetCache` 는 이름은 `I*` 인데 상태(`_stats`)를 가진 기반이다 — 이름을 내용대로(`AssetCacheBase` 등).
- 창 바둑판 간격의 창 틀 두께가 Win32 대략 값이다(`AutomationPerBackend`) — 플랫폼 창 틀 API 가 생기면.
- 전용 서버 종료 때 태그별 증가 진단(Game 약 0.5 KB) 출처.
