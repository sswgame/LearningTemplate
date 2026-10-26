# 아키텍처 이음매와 리팩토링 트랙

> 상태: 후보 · 선행: 없음 · 영역: engine · 옛번호: 없음 · 진행: 해당없음

> 질문: 프로젝트가 커진 지금 아키텍처를 다시 설계해야 하는가, 한다면 무엇을 어떤 순서로 바꾸는가?
> 독자: 통합 담당과 다음 구조 작업을 맡을 작업자.
> 결론: 큰 틀(Core → Engine → GameFramework 기반 → 키트 → 게임, Editor · 플러그인, RuntimeAPI · ModuleHost)은 다시 짜지 않는다. 의존 그래프는 DAG 이고 방향은 언리얼 · Godot 과 같다. 고칠 곳은 이음매 다섯과 그 단계에 같이 하는 리팩토링 트랙(중복 기능 통합 · 불필요한 래핑 걷기)이다. Engine · GameFramework 의 링크 단위 분할은 하지 않는다 — 0 단계 재측정(2026-10-25)에서 링크가 `.cpp` 증분의 29 ~ 61 % 로 커졌지만 먼저 링크 옵션(5a, GHASH)을 재고, 그래도 30 % 를 넘을 때 다시 본다.

2026-10-25 설계 검토(`design-reviewer`)의 결론이다(main `4d43a7c11`). 빌드 · 기동 수치는 0 단계(main `eb2a1fa59`)가 다시 쟀다.

## 1. 고칠 이음매

| 순위 | 문제 | 영향 |
|---|---|---|
| 1 | 검증 처리량이 병목이다. 측정대기 계획 문서 59/101, 검증 항목 212 개가 쌓였고 작업자가 빌드하지 않아 의미 충돌이 통합 빌드에서야 드러난다 | 구조 변경이 확인되지 않은 채 쌓인다 |
| 2 | 테스트 대상이 소스를 손으로 고른다. EditorTest 는 Editor `.cpp` 130 경로 + 리플렉션 단계, EngineTest 는 플러그인 소스 13 경로, SmokeTest · EditorUITest · FuzzTest 에도 손 목록 | 30 일에 120 커밋이 몰린 충돌 지점, "목록은 하나" 원칙 위반 |
| 3 | 대형 단일 문서와 생성물이 병합 충돌을 부른다(결정 기록 373 KB, README 진행 표, `NEXT.md`, `ConfigReference.json`) | 통합 G 에서 충돌 셋 |
| 4 | 빌드 기준선이 낡았다. 지금 Debug 콜드 풀 빌드 266.5 s(Lean 236.3 s), `Engine.dll` 링크 2.3 ~ 5.8 s(중앙값 약 2.7 s), GF 기반 9.43 만 줄. 링크는 Engine · GF · 에디터 `.cpp` 증분의 29 ~ 61 % | 층 DLL 분할 전에 링크 옵션(GHASH)이 먼저 — 5 단계 판정([08 검증](../08_Verification.md) "아키텍처 이음매 0 단계 기준선") |
| 5 | 전역 가변 정적 상태가 남았다(래칫 진행 중). `getActive` 류가 정적 사본의 참조를 돌려주는 것을 게이트가 못 봤다 | 핫 리로드 · 다중 월드 · 테스트 격리 |
| 6 | 서버에서 렌더러를 빼는 일을 폴더 정규식 · `SW_WITH_CLIENT_CODE` · 리플렉션 제외 정규식이 나눠 맡는다 | 중하. 틈은 링크 오류로 드러난다 |
| 7 | 변경이 `FrameRenderer` 에 몰린다(헤더 1,344 줄, 값 멤버 117 개, 10 일에 40 커밋) | 병합 충돌과 단일 책임. 빌드 팬아웃은 작다 |
| 8 | 층 표의 주석("UI 는 렌더러와 서로 include 하지 않는다")이 게이트와 어긋난다(`RenderPacketBuilder.cpp` 가 `UISystem.h` 를 include). 게이트는 방향만 본다 | 낮음 |

하지 않을 것: Engine.dll 층 DLL 분할, 키트 DLL 병합, 대상마다 include 루트 나누기(VFS overlay), 결정 하나당 파일 하나(ADR), 측정 없는 `FrameRenderer` 가상 인터페이스화, 티어 표를 손으로 재배열하기.

## 2. 이행 단계

각 단계는 따로 빌드되고 커밋 하나 또는 묶음 하나로 되돌릴 수 있다. 1a · 1b · 2a · 3 · 4 는 동시에 띄울 수 있다. 2b 는 2a 뒤에(2c 는 2a 와 독립으로 먼저 끝났고 남은 목록만 2a 뒤), 0 · 5 · 6 은 혼자 한다.

| 단계 | 내용 | 확인 | 롤백 |
|---|---|---|---|
| 0 (끝남, 2026-10-25) | 측정 기준선. 판정: DLL 링크가 `.cpp` 증분의 29 ~ 61 %(Engine `.cpp` 52 ~ 61 %)라 30 % 기준을 넘지만 5 단계의 `Engine_Render` OBJECT 는 여전히 `Engine.dll` 에 링크되어 링크 시간을 줄이지 않는다 → 5a 를 먼저. `FrameRenderer.h` 팬아웃 27 TU(18.5 s) | [08 검증](../08_Verification.md) 표 | 해당 없음 |
| 1a | 결정 기록 5 절을 영역별 파일로 나눈다. 본문에는 1 ~ 4 절과 목록만 | `CheckDocPaths`, lint-suite | revert |
| 1b | README 진행 표를 생성 파일로 빼고 `CheckStatusBoard` 대상을 바꾼다 | `CheckStatusBoard`, `plans --check` | revert |
| 2a | `EditorCore` OBJECT(ImGui 없음)를 만든다. 대상은 폴더 규칙(`Editor/Common` 의 Asset · Commands · Config · Localization · SourceControl · Workspace). EditorModule 과 EditorTest 가 같은 오브젝트를 받고 EditorTest 의 손 목록 79 줄을 지운다 | 전 프리셋 구성, EditorTest, 에디터 시나리오 | revert |
| 2b | Panels · Viewport 의 헤드리스 파일(약 37 개)을 패널별 `Model` 하위 폴더로 옮겨 폴더 규칙에 더한다 | EditorTest | 폴더별 revert |
| 2c | (먼저 끝남) `sw_addEditorExtensionModel()` 이 전역 속성에 모델 **소스 경로**를 올리고 EngineTest 가 읽는다(정의 · include · PCH 가 컴파일하는 타깃 것이어야 해서 — 사유는 `ModuleTargets.cmake` 주석). SmokeTest 핫 리로드 소스는 ModuleHost CMake 의 목록에서, EditorUITest 의 Platform 소스는 GLOB 으로, 하네스 기동 소스는 `sw_addTestHostRuntime` 한 곳에서 파생. **남은 것(2a 뒤)**: `ImGuiOsInputFilter` · `ImGuiKeyForward` 손 목록, `TestResourceDataSchema` 의 종류 문자열 파서, EngineTest 의 Editor 소스 3개(`ImageUtil` · `EditorThemeTokens` · `EditorThemeTokensDataKind`) | EngineTest 전체 | revert |
| 3 | 래칫을 줄이고 정적 사본 참조 접근자를 게이트에 넣는다. `EngineConfig` · `GameConfig` · `Render2DSettings` 를 범위 소유로 | `-L lint`, 설정 핫 리로드 테스트, 큐브 20k 벤치 | 파일별 revert |
| 4 | 통합이 main 으로 올리기 전에 `-k 0` 빌드와 `ctest -L nogpu|lint` 증거 파일을 요구하고 GPU 항목은 정해진 시각에 `verify-batch` 로 몰아 돌린다 | 스크립트 자가 테스트 | revert |
| 5a | Debug 링크 옵션 A/B: 컴파일 `/clang:-gcodeview-ghash` + 링크 `/DEBUG:GHASH`(지금 `lld-link /debug` 에 GHASH 없음, `Engine.pdb` 125 MB). 같은 날 번갈아 `GameObjectManager.cpp` 증분의 `Engine.dll` 링크 시간과 콜드 풀 빌드를 잰다. 줄지 않거나 여전히 증분의 30 % 를 넘으면 층 DLL 분할을 다시 본다 | 0 단계 표와 같은 방법, Debug 경고 0 · `nogpu` | 옵션 revert |
| 5 (조건부, 링크 이유로는 열지 않음) | 서버에서 뺄 폴더가 렌더러 밖으로 늘면 `Engine_Render` OBJECT 분할 | 네 백엔드 × Dev · Shipping · Server | revert |
| 6 (빌드 이유로는 낮음) | 렌더 기능 객체를 엔진 내부 기능 목록으로 옮겨 `FrameRenderer` 헤더 변경을 줄인다(`RealtimeProbeUpdater` · `VfxParticleRenderer` 값 멤버 패턴). 헤더 증분 27 TU 중 16 개가 `FrameRenderer*.cpp` 자신이라 줄일 것은 바깥 11 TU 뿐 — 하면 이유는 병합 충돌(10 일 40 커밋)이고 1 ~ 4 단계 뒤에 한다 | 렌더 GPU 테스트, 큐브 벤치 RT p50 · p99 | revert |

## 3. 리팩토링 트랙(이행 단계와 같이 한다)

동작을 바꾸지 않는다. 단위마다 커밋 하나, 같은 테스트 통과, 성능 경로는 전후 비교(큐브 20k 벤치). 작업자는 빌드하지 않고 통합이 `-k 0` 빌드로 한 번에 검증한다.

#### 3.1 중복 기능 통합

같은 일을 하는 코드가 둘 이상이면 정본 하나를 정하고 나머지를 호출로 바꾼다. 새 추상화 전에 이미 있는 공용 형식(`RegistrationList` · `addLogged`, `StringUtil`, 서비스 접근 API)으로 모은다. 합친 뒤 옛 모양이 다시 생기지 못하게 형식으로 막고(`[[nodiscard]]`, 공용 API 만 공개) 게이트는 형식으로 못 막는 틈에만 둔다.

알려진 후보:
- 이름을 손으로 거절하는 레지스트리(`LocalStoreFactory` · `SQLDriverRegistry` · `LoginService` · `ModuleRenderPassRegistry` · `EditorAssetTypeActions`)를 `RegistrationList` 로.
- 손 불리언 파서를 `StringUtil::tryParseBool` 로. `ElementGrid.cpp` 의 `cellBytes` 손 계산을 공용 계산으로.
- `TestResourceDataSchema` 의 종류 표(`SW_RESOURCE_DATA_KIND` 의 사본)를 지우고 등록표를 읽게.
- 테스트 소스 목록이 두 벌인 곳(2a · 2b · 2c).
- 같은 사실이 데이터 두 곳에 있는 곳(HUD 의 `ItemName` · `WeaponName` 한 건은 고쳤고 같은 모양을 더 찾는다), 같은 값을 여러 곳이 따로 만드는 곳(`FrameProfiler::getFrameCount` 를 프레임 번호로 쓴 곳 — `UISystem` · `RenderPacketBuilder` 는 고쳤고 나머지를 찾는다).

찾는 방법: graphify 의 커뮤니티 · god node 로 후보를 좁히고 같은 이름 · 역할의 함수 쌍을 grep 으로 확인해 결합도가 높은 곳부터 합친다.

#### 3.2 불필요한 래핑 걷기

이름만 바꿔 감싸거나 인자를 그대로 넘기기만 하는 래퍼는 걷고 호출자가 원 함수를 직접 부른다.
- **걷는다**: 본문이 호출 하나뿐이고 인자와 반환이 같다. 호출자가 하나이거나 같은 층이다. 테스트가 래퍼 자체를 단언하지 않는다.
- **둔다**: 층 경계를 지키는 얇은 면, 서비스 접근 어휘(`get` / `find`), 검증 · 로그 · 소유권을 더하는 것, 리플렉션 · 직렬화 등록 창구, 플랫폼별 구현을 가리는 것, 오버로드를 모으는 것, 문서화된 공개 API.
- 모호하면 두고 보고에 적는다. 찾는 방법: 함수 본문이 호출 한 줄인 함수를 정적으로 훑는다(그래프의 호출 간선 수가 하나인 노드).

#### 3.3 그 밖의 리팩토링

- 층 표의 "서로 include 하지 않는다" 주석을 금지 간선 키로 바꿔 게이트가 보게 한다(`coreBannedLayer` 와 같은 구조).
- 긴 함수 분리는 [LongFunctions](LongFunctions.md) 가 맡는다. `FrameRenderer` 정리는 6 단계.
- Core `.cpp` 하나를 고치면 `ReflectionParser.exe` 가 다시 링크되고 리플렉션 코드젠 명령 64 개가 모두 다시 돌아 증분이 13.9 s 다(Lean 7.0 s). 출력이 같으면 다시 쓰지 않는 `restat` 류, 또는 파서가 `Core_objects` 전체 대신 필요한 부분만 링크하는 쪽을 본다.
- Dev 기동 중앙값 5.3 s 중 `ModuleTypes` 가 2.1 s 로 가장 크다 — 모듈 DLL 적재인지 타입 등록인지 먼저 나눠 잰다.

## 4. 사용자 결정이 필요한 것

- **GPU 검증을 CI 로 자동화할지**(외부 공개 · 비용): 공개 저장소의 자가 호스팅 GPU 러너는 보안 위험이고 유료 러너는 비용이 든다. 결정 없이 갈 길은 로컬 GPU 기계의 예약 작업으로 `verify-batch` 를 돌리는 것이고 4 단계는 그쪽이 기본이다.
- **재검토 제안**(사용자의 규칙이라 에이전트가 바꾸지 않는다): 작업자가 맡은 대상 하나만 컴파일하게 허용하는 안(`cmake --build --target <소유 대상>`, 테스트 · 실행은 안 함) — 통합 전에 `getState()` 같은 의미 충돌을 잡는다.

## 5. 확인하지 못한 것

GHASH 효과는 5a 가 잰다. `EditorCore` 후보 폴더의 ImGui 사용은 직접 include 만 셌다(전이 include 미확인). dllexport 정의로 컴파일한 OBJECT 를 테스트 실행 파일에 넣었을 때 clang-cl 과 Linux Clang 에서 문제가 없는지. 언리얼 · 유니티 렌더러 구조와 ADR 관행은 기억에 기댄 것이라 결정 근거로 쓰지 않았다.

## 6. 3 단계 진행 (2026-10-25, 통합 L 에서 빌드 · nogpu · 백엔드 교체 시나리오 확인)

끝난 커밋: `CheckServiceAccess` 가 정적 사본의 참조를 내주는 `static const T& getActive()` 도 센다(예외 표 종류 `PendingScopeMove`), `EngineConfig` 를 `ConfigManager` 소유로, `GameConfig` 정적 사본 제거, `Render2DSettings` 를 `Render2DSettingsSubsystem` 소유로(서비스 표에 행 셋 추가). 전역 가변 정적 래칫 231 파일 · 상한 합 494 → 227 · 489. 결정은 [결정 기록 5-4](../decisions/5-4_DataConfig.md) 의 끝 항목.

다음 커밋 단위:
- **`GameConfig::getActive()` 전달 창구 지우기** — 에디터 호출처 열 파일(`EditorDataTableCommands` · `EditorGlobalVariableCommands` · `EditorSceneFileCommands` · `EditorCommandGUI` · `LocalizationTools` · `ContentBrowserPanel` · `PackagingPanel` · `ProjectSettingsPanel` · `EditorSelfTestPanelCases`)과 게임 프레임워크 셋(`GameInstanceBase.cpp` → `game::getSubsystem<GameConfig>()`, `CharacterEditorPanel.cpp` · `ParkLayoutPreview.cpp` → `editor::findSubsystem<GameConfig>()`)을 범위 창구로. `GameInstanceBase` 를 옮길 때 `TestGameFramework` 의 `_hostServices` 에 `GameConfig` 를 걸어야 한다. 끝나면 `GameConfig.cpp` 와 `CheckServiceAccess.toml` 의 `GameConfig` 줄을 지운다.
- **`EditorThemeUtil::getActiveTheme`** — 에디터 컨텍스트 소유로(`PendingScopeMove`). 에디터 작업자 몫.
- **래칫 큰 몫(상한 합 489)** — `Test/EngineTest` 98 · `Test/CoreTest` 44 · `Core/Diagnostics` 37(`CrashPath` 가 맞는 것이 많다 — 예외 표로 올릴 후보) · `Engine/Graphics` 30 · `Engine/Input` 19. 테스트의 정적 칸 대부분(`TestTask` 24 등)은 사용자 자료 없는 함수 포인터 작업 API 때문이라 작업 제출에 사용자 자료나 호출 가능 객체를 받는 길을 먼저 연 뒤 옮긴다. `GameTimeScale`(정지 요청 수 · 비배율 경과)은 정적 API 호출처가 넓어 서비스로 옮기는 별도 단위.
