# 다음 세션 인계

> 질문: 무엇이 남았고 무엇부터 하는가?
> 독자: 이어서 일하는 세션(에이전트와 사람).
> 결론: 아래 **남은 일**만 본다 — 끝난 일은 git 기록과 계획 문서([StructureRefactor](StructureRefactor.md))에 있다. 순서는 1 → 6. 이 문서에는 끝난 일을 쌓지 않는다(끝나면 줄을 지운다).

## 일하는 법 (요약)

- 단계마다 코드 → `py -3 -m Scripts lint` → 커밋. 묶음 끝에 빌드(`cmake --build --preset Ninja-Debug`) · 단위(`build/Ninja-Debug/Bin/App.exe --test`) · 전체 `ctest`(build/Ninja-Debug, 약 5 분) 한 번.
- **테스트마다 로그의 Error · Warning 을 모두 분류한다** — `build/Ninja-Debug/Testing/Logs/*.log`, id 없는 줄과 `[id:…]` 줄을 나눠 테스트가 선언한 것인지 본다. 통과만 보지 않는다.
- 느린 곳은 새로 재지 말고 로그의 `[Scenario] … timeline:` · `stage reset took` · `Backend swap took` 줄을 모은다(`Test/README.md` 의 시간 기록).
- 규칙: 구조로 푼다 · 폴백 없음 · 게이트는 최후 · `#if` 대신 빌드 단위 · 용어는 `docs/agents/NamingCpp.md` 의 표. 시나리오 체계는 `Test/README.md` 가 정본.
- Linux 는 WSL 클론(`~/LT-linux`)에서 바뀐 파일의 문법 검사만 했다 — 실제 빌드 · 테스트는 아직.

## 남은 일

### 1. 진행 중 (2026-10-31)

| 일 | 상태 |
|---|---|
| Shooter3D 체인 실패 둘 | `firelook` 은 시나리오 결함(같은 프레임에서 `<Expect>` 아래의 입력이 틱 전에 든다 — 이제 읽을 때 거절) · `crowd`(`Animation.CrowdSharedUnits` 가 서지 않음) — 작업자 조사 중 |
| `OnlineLoadBot.localServer` | Release ctest 에서 180 초 시간 초과, 보고서 없음, 콘솔 출력 없음 — 작업자 조사 중 |
| SOLID · 프로젝트 구성 점검 | 작업자 진행 중(아래 5 절) |

### 2. 테스트 · 시나리오 공백

| 일 | 내용 |
|---|---|
| 통합 검증은 두 구성 | Debug(`ctest`) 와 Release(`ctest` — 지어진 게임마다 `Scenario.<팩>` 체인)를 모두 돈다. 배포 구성은 `py -3 -m Scripts shipping-check` |
| 흔들리는 게임 골든 | killcam(dx11 · gl 28 %) · cape(dx12 · gl 약 2 %) · bodyviews(약 1.7 %)가 한 번씩 졌다 — 외형 · 에셋이 늦게 도착한 순간을 찍는 흔들림으로 보인다. 찍기 전 자리 잡기가 외형 로드를 세는지 |
| 남은 단위 테스트 | Online `OnlineServiceHost` · `OnlineServiceClient` · `OnlineServerRuntime` · Local · Config · Audit, Kits 의 `none` 줄, Serializer(SerializerUtil · SchemaMigrate · DataPath 처리기). 이식에 하네스가 필요한 것: 자식 프로세스(크래시 · 로그), LoopbackCluster(네트워크), AnimationTestUtil, RHIFakeDevice, FakeFontRasterizer, 임시 프로젝트 마운트, Reflection 큰 파일 다섯 · 파서. `DDSLoader` 는 픽셀 바이트 수를 형식 크기와 견주지 않는다 |
| 벽시계 한도와 부하 | 다른 빌드가 CPU 를 쓰는 동안 `wallTimeoutSeconds` 150 초를 넘는 시나리오가 있다(flowterrain 의 백엔드 교체 7 번이 혼자서도 19 초). 통합 ctest 는 빌드 없이 돌리고, 교체 시간을 줄이거나 한도를 교체 수에 맞춘다 |
| 편집기 UI 자리 잡기 대기 | `EditorClick` 앞 고정 대기 262 곳 — 입력 단계가 "대상 표시 사각형이 두 프레임 같을 때까지" 붙잡게 하면 한꺼번에 지울 수 있다 |
| id 없는 경고 · 오류 | 테스트가 밟는 경로마다 `SW_LOG_*_ID` 를 붙였지만 다른 로더에 더 남았다 — 같은 모양 |

### 3. 프레임 밖으로 뺄 일 (TaskManager)

| 일 | 위치 |
|---|---|
| 미리보기 씬의 외형 대기 | `CharacterAppearanceComponent` — 미리보기 씬이 틱을 돌게(언리얼 FPreviewScene) |
| `DevCommand` 단계 평균 87 ms/프레임의 원인 | timeline 줄의 시나리오별 `DevCommand` |

### 4. 구조 개편 남은 것 ([StructureRefactor](StructureRefactor.md))

| 주제 | 남은 것 (2026-10-31) |
|---|---|
| T8 | Engine 의 `SW_SHIPPING` 약 120 곳(Reflection 메타 · RHI 검증 · Config), `ModuleCompiler` 소유, `Engine.cpp` 의 `SW_WITH_CLIENT_CODE` 셋, 배포의 모듈 해제(`unregisterModuleTypes`) 없음 → `ElementRuleTable` · `InteractionCatalog` 캐시가 남았다는 경고 둘. Dev + 자동화 끔 구성은 Editor 가 실행기를 include 해 깨질 것 |
| T9 | 등록자의 모듈 이름 — CMake 가 대상마다 `SW_REGISTRAR_MODULE` 을 주고 매크로가 찍는다(설계는 StructureRefactor T9). 런타임 모듈 이름 = 대상 이름 가정을 핫 리로드 시나리오로 확인 |
| T6 | 에디터 사용자 외형 프리셋 · 썸네일을 슬롯으로(에디터 쪽 저장소 주인), `AccountDeviceSecret` 을 `GameLocalStore` 와 맞춤 |
| T10 | `ShaderCooker::getSubfolderForFormat` 이 모르는 형식에 기본 폴더를 돌려준다(폴백) |
| 옛 형식 호환 | 약어 변경으로 `packSHA256` · `buildID` 와 리플렉션 멤버 이름(`_meshID` 등)이 바뀌어 이미 배포된 패치 매니페스트 · 크래시 요약 · 세이브는 읽지 못한다 — 호환이 필요해지면 이주 |

### 5. 그 뒤

- Linux 실제 빌드 · 테스트(X11 · Wayland · ALSA · epoll 은 Windows 가 짓지 않는다 — CI 후보).
- `GameFramework/Base` · `Plugin/Editor/Base` 이름(모듈 이름 · 매니페스트가 따라간다).

### 6. 다음 설계 (긴 것)

- 빌드 속도: `MaterialPanel` · `PrefabPanel`(각 50 초대, Release)을 `-ftime-trace` 로 나눈다.
- 시나리오 범용화: 재생기 · 판정 층을 나눌 때 `AutomationSession`(이미 `ISubsystem` + `IScenarioPlayer`)에 인터페이스를 더 붙이지 않고 재생기를 별도 객체로. 시퀀서 이벤트 트랙이 시나리오를 부르는 쪽, 액션 단위 녹화 · 리플레이(고스트 · 킬캠). 단계 등록은 키트 · 게임이 아직 0.
- 리로드: 배포 `IAssetCache` 에 reload 가 없게 — 리로드는 DevTools 빌드 단위의 리로더 객체가 캐시를 참조해 공개 API(무효화 · 다시 요청)로 한다(구성, 캐시에 인터페이스를 더 붙이지 않는다 — 캐시 8 개가 다중 상속이 된다). `IAssetCache` 는 이름은 `I*` 인데 상태(`_stats`)를 가진 기반이다 — 이름을 내용대로(`AssetCacheBase` 등).
- Core 에 Engine 과 같은 층 껍데기(`#error`) 빌드 형식.
- 창 바둑판 간격의 창 틀 두께가 Win32 대략 값이다(`AutomationBackendMatrix`) — 플랫폼 창 틀 API 가 생기면.
- 전용 서버 종료 때 태그별 증가 진단(Game 약 0.5 KB) 출처.
