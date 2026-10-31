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
| Shooter3D 체인 실패 둘 | `firelook`(한 프레임 클릭에 두 발) · `crowd`(`Animation.CrowdSharedUnits` 가 서지 않음) — 작업자가 Release 를 따로 지어 조사 중. 체인은 Release 의 `App -game=Shooter3D -scenario=<목록> -scenario-backends=all` |
| 옛 단위 테스트 이식 | 작업자 진행 중 — 옛 `Test/`(커밋 `3f70e665f^`)의 Core 56 · Reflection 14 먼저, Engine 순수 함수 |

### 2. 테스트 · 시나리오 공백

| 일 | 내용 |
|---|---|
| 게임 체인을 ctest 에 | Debug(활성 게임 Empty)는 다른 게임을 짓지 않아 게임 시나리오 · 필수 데이터 · .ogg 결함을 보지 못했다(이번에 셋 다 그렇게 숨어 있었다). Release(모든 게임)에 게임마다 체인 항목을 두거나 통합 때 돌리는 스크립트로 |
| `harvestvalley.scheduleday` | 새 일정 하루 시나리오 — 처음 돌린다(`gv_timeScale` 이 판 시계에 곱해지는지, 0 일 날씨 맑음 가정) |
| 남은 단위 테스트 | Online `OnlineServiceHost` · `OnlineServiceClient` · `OnlineServerRuntime` · Local · Config · Audit, Kits 의 `none` 줄(TacticsSRPG · KartRacing 등), Serializer(SerializerUtil · SchemaMigrate · DataPath 처리기), Reflection 값 · 인스턴스. `DDSLoader` 는 픽셀 바이트 수를 형식 크기와 견주지 않는다. 주석이 가리키는 `EditorTexturePipelineTest.DDSFormatFileMatchesTheLoaderTables` 는 없다 |
| 벽시계 한도와 부하 | 다른 빌드가 CPU 를 쓰는 동안 `editor.flowterrain` 이 `wallTimeoutSeconds` 150 초를 넘었다(백엔드 교체 7 번이 혼자서도 19 초). 혼자 돌리면 통과. 통합 ctest 는 빌드 없이 돌리고, 교체 시간을 줄이거나 한도를 교체 수에 맞춘다 |
| 편집기 UI 자리 잡기 대기 | `EditorClick` 앞 고정 대기 262 곳 — 입력 단계가 "대상 표시 사각형이 두 프레임 같을 때까지" 붙잡게 하면 한꺼번에 지울 수 있다(엔진 변경 + 전체 시나리오) |

### 3. 프레임 밖으로 뺄 일 (TaskManager)

| 일 | 위치 |
|---|---|
| 렌더 스레드의 동기 `acquire` | `CanvasTextureLeases.cpp` 렌더 타깃 · `FrameRendererViews.cpp` 렌더 텍스처 · `MaterialInstance.cpp` 텍스처 |
| 게임 스레드의 메시 동기 `acquire` | `FoliageComponent` · `VFXComponent` · `NavGeometryCollector` · `NileDirectorComponent` · `FarmDirectorComponent` — 바운드를 그 자리에서 써서 상태 기계가 필요 |
| 미리보기 씬의 외형 대기 | `CharacterAppearanceComponent` — 미리보기 씬이 틱을 돌게(언리얼 FPreviewScene) |
| 직렬 셰이더 컴파일 | `findOrCreateExtensionProgramPSO` · 쿠키 아틀라스 · 입자 · 컴퓨트 PSO |
| `DevCommand` 단계 평균 87 ms/프레임의 원인 | timeline 줄의 시나리오별 `DevCommand` |

### 4. 구조 개편 남은 것 ([StructureRefactor](StructureRefactor.md))

| 주제 | 남은 것 (2026-10-31) |
|---|---|
| T8 | 배포(Shipping) 구성으로 구성 · 빌드 · 링크 확인(새 `…Shipping.cpp` 들, 정의 없는 `RHIBackendSwitcher` 시험 기호, `/WHOLEARCHIVE` 에 기댄 정적 등록). Engine 의 `SW_SHIPPING` 약 120 곳(Reflection 메타 · RHI 검증 · Config), `ModuleCompiler` 소유 |
| T9 | 등록자의 모듈 이름 — CMake 가 대상마다 `SW_REGISTRAR_MODULE` 을 주고 매크로가 찍는다(설계는 StructureRefactor T9). 런타임 모듈 이름 = 대상 이름 가정을 핫 리로드 시나리오로 확인. 봇 ctest `OnlineLoadBot.localServer` 는 Release 에서만 생긴다 — 돌려 본다 |
| T6 | 에디터 사용자 외형 프리셋 · 썸네일을 슬롯으로(에디터 쪽 저장소 주인), `AccountDeviceSecret` 을 `GameLocalStore` 와 맞춤 |
| T10 | `ShaderCooker::getSubfolderForFormat` 이 모르는 형식에 기본 폴더를 돌려준다(폴백) |

### 5. 그 뒤

- SOLID · 프로젝트 구성 점검과 구조 리팩토링(중복 제거 · 일원화 · 레이어 구분 · 최적화) — 사용자 요청 2026-10-31.
- Linux 실제 빌드 · 테스트(X11 · Wayland · ALSA · epoll 은 Windows 가 짓지 않는다 — CI 후보).
- `GameFramework/Base` · `Plugin/Editor/Base` 이름(모듈 이름 · 매니페스트가 따라간다).

### 6. 다음 설계 (긴 것)

- 빌드 속도: `MaterialPanel` · `PrefabPanel`(각 50 초대, Release)을 `-ftime-trace` 로 나눈다.
- 시나리오 범용화: 재생기 · 판정 층을 나눌 때 `AutomationSession`(이미 `ISubsystem` + `IScenarioPlayer`)에 인터페이스를 더 붙이지 않고 재생기를 별도 객체로. 시퀀서 이벤트 트랙이 시나리오를 부르는 쪽, 액션 단위 녹화 · 리플레이(고스트 · 킬캠). 단계 등록은 키트 · 게임이 아직 0.
- 리로드: 배포 `IAssetCache` 에 reload 가 없게 — 리로드는 DevTools 빌드 단위의 리로더 객체가 캐시를 참조해 공개 API(무효화 · 다시 요청)로 한다(구성, 캐시에 인터페이스를 더 붙이지 않는다 — 캐시 8 개가 다중 상속이 된다). `IAssetCache` 는 이름은 `I*` 인데 상태(`_stats`)를 가진 기반이다 — 이름을 내용대로(`AssetCacheBase` 등).
- Core 에 Engine 과 같은 층 껍데기(`#error`) 빌드 형식.
- 창 바둑판 간격의 창 틀 두께가 Win32 대략 값이다(`AutomationBackendMatrix`) — 플랫폼 창 틀 API 가 생기면.
- 전용 서버 종료 때 태그별 증가 진단(Game 약 0.5 KB) 출처.
