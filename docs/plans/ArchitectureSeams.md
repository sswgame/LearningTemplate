# 아키텍처 이음매와 리팩토링 트랙

> 상태: 후보 · 선행: 없음 · 영역: engine · 옛번호: 없음 · 진행: 해당없음

> 질문: 프로젝트가 커진 지금 아키텍처를 다시 설계해야 하는가, 한다면 무엇을 어떤 순서로 바꾸는가?
> 독자: 통합 담당과 다음 구조 작업을 맡을 작업자.
> 결론: 큰 틀(Core → Engine → GameFramework 기반 → 키트 → 게임, Editor · 플러그인, RuntimeAPI · ModuleHost)은 다시 짜지 않는다. 의존 그래프는 DAG 이고 방향은 언리얼 · Godot 과 같다. 고칠 곳은 이음매 다섯과, 그 단계에 같이 하는 리팩토링 트랙(중복 기능 통합 · 불필요한 래핑 걷기)이다. Engine · GameFramework 의 링크 단위 분할은 0 단계 재측정 전까지 하지 않는다.

2026-10-25 설계 검토(`design-reviewer`, 근거 수준은 항목마다 표시)의 결론을 옮긴 것이다. 근거는 코드 · 측정 · 추정으로 가른다. 아래 수치는 검토 시점(main `4d43a7c11`) 값이라 낡을 수 있고, 0 단계가 다시 잰다.

## 1. 고칠 이음매

| 순위 | 문제 | 영향 | 근거 |
|---|---|---|---|
| 1 | 검증 처리량이 병목이다. 측정대기 계획 문서 59/101, 검증 항목 212 개가 쌓였고, 작업자가 빌드하지 않아 의미 충돌이 통합 빌드에서야 드러난다 | 구조 변경이 확인되지 않은 채 쌓인다 | 코드 |
| 2 | 테스트 대상이 소스를 손으로 고른다. EditorTest 는 Editor `.cpp` 130 경로를 나열하고 리플렉션 단계를 따로 둔다. EngineTest 는 플러그인 소스 13 경로를 직접 컴파일한다. SmokeTest · EditorUITest · FuzzTest 에도 손 목록이 있다 | 30 일에 120 커밋이 몰린 충돌 지점, "목록은 하나" 원칙 위반 | 코드 |
| 3 | 대형 단일 문서와 생성물이 병합 충돌을 부른다. 결정 기록 373 KB(30 일 326 커밋), README 진행 표, `NEXT.md`, `ConfigReference.json` | 통합 G 에서 충돌 셋. 30 KB 훅 때문에 절 단위로만 읽는다 | 코드 |
| 4 | 측정 기준선이 낡았다. "분할하지 않음" 결정이 10 월 초 ~ 중순의 작은 워크로드에 기댄다(Engine.dll 링크 1.9 s, 콜드 풀 빌드 115 s, GF 기반 약 58k 줄 — 지금 GF 기반은 약 9.4 만 줄) | 옳은 분할을 "이득 없음" 으로 덮고 있을 수 있다 | 코드 |
| 5 | 전역 가변 정적 상태가 남았다(292 파일에 628 개, 래칫 진행 중). `getActive` 류가 정적 사본의 참조를 돌려주는 것을 게이트가 못 본다 | 핫 리로드 · 다중 월드 · 테스트 격리 | 코드 |
| 6 | 서버에서 렌더러를 빼는 일을 폴더 정규식 · `SW_WITH_CLIENT_CODE` · 리플렉션 제외 정규식이 나눠 맡는다 | 중하. 이미 동작하고 틈은 링크 오류로 드러난다 | 코드 |
| 7 | 변경이 `FrameRenderer` 에 몰린다(헤더 1,344 줄, 값 멤버 117 개, 10 일에 40 커밋) | 병합 충돌과 단일 책임. 빌드 팬아웃은 작다 | 코드 |
| 8 | 층 표의 주석("UI 는 렌더러와 서로 include 하지 않는다")이 게이트와 어긋난다(`RenderPacketBuilder.cpp` 가 `UISystem.h` 를 include). 게이트는 방향만 본다 | 낮음 | 코드 |

하지 않을 것: Engine.dll 층 DLL 분할, 키트 DLL 병합, 대상마다 include 루트 나누기(VFS overlay), 결정 하나당 파일 하나(ADR), 측정 없는 `FrameRenderer` 가상 인터페이스화, 티어 표를 손으로 재배열하기.

## 2. 이행 단계

각 단계는 따로 빌드되고 커밋 하나 또는 묶음 하나로 되돌릴 수 있다. 파일이 겹치지 않아 1a · 1b · 2a · 3 · 4 는 동시에 띄울 수 있다. 2b 는 2a 뒤에(2c 는 2a 와 독립으로 먼저 끝났고 남은 목록만 2a 뒤), 0 · 5 · 6 은 혼자 한다.

| 단계 | 내용 | 확인 | 롤백 |
|---|---|---|---|
| 0 | 측정 기준선(Ninja-Debug 와 Lean, 콜드 풀 빌드, 증분 5 곳, 헤더 증분 2 곳, `.ninja_log` 링크 시간, Dev 기동 비용). 통합 혼자, 빌드가 겹치면 무효 | `08_Verification.md` 형식 표. DLL 링크가 증분의 30 % 를 넘으면 5 단계를 연다 | 해당 없음 |
| 1a | 결정 기록 5 절을 영역별 파일로 나눈다. 본문에는 1 ~ 4 절과 목록만 둔다 | `CheckDocPaths`, lint-suite | 커밋 revert |
| 1b | README 진행 표를 생성 파일로 빼고 `CheckStatusBoard` 대상을 바꾼다 | `CheckStatusBoard`, `plans --check` | revert |
| 2a | `EditorCore` OBJECT(ImGui 없음)를 만든다. 대상은 폴더 규칙(`Editor/Common` 의 Asset · Commands · Config · Localization · SourceControl · Workspace). EditorModule 과 EditorTest 가 같은 오브젝트를 받고 EditorTest 의 손 목록 79 줄을 지운다 | 전 프리셋 구성, EditorTest, 에디터 시나리오 | revert |
| 2b | Panels · Viewport 의 헤드리스 파일(약 37 개)을 패널별 `Model` 하위 폴더로 옮겨 폴더 규칙에 더한다 | EditorTest | 폴더별 revert |
| 2c | (2a 와 독립으로 갈라져 먼저 끝냈다) `sw_addEditorExtensionModel()` 이 전역 속성에 모델 **소스 경로**를 올리고 EngineTest 가 읽는다(오브젝트가 아니다 — 정의 · include · PCH 가 컴파일하는 테스트 타깃 것이어야 해서, 사유는 `ModuleTargets.cmake` 함수 주석). 손 목록 13 줄을 지웠다. 같은 일로 SmokeTest 의 핫 리로드 소스는 ModuleHost CMake 가 한 번 선언한 목록(정본)에서, EditorUITest 의 Platform 소스는 GLOB 으로, 테스트 하네스 기동 소스는 `sw_addTestHostRuntime` 한 곳에서 파생한다. **남은 것(2a 의 EditorCore 링크 뒤)**: `ImGuiOsInputFilter` · `ImGuiKeyForward` 의 손 목록, `TestResourceDataSchema` 의 데이터 종류 문자열 파서(못 바꾸는 이유는 함수 주석), EngineTest 의 Editor 소스 3개(`ImageUtil` · `EditorThemeTokens` · `EditorThemeTokensDataKind`) | EngineTest 전체 | revert |
| 3 | 래칫을 줄이고 정적 사본 참조 접근자를 게이트에 넣는다. `EngineConfig` · `GameConfig` · `Render2DSettings` 를 범위 소유로 옮긴다 | `-L lint`, 설정 핫 리로드 테스트, 큐브 20k 벤치 | 파일별 revert |
| 4 | 통합이 main 으로 올리기 전에 `-k 0` 빌드와 `ctest -L nogpu|lint` 증거 파일을 요구하고, GPU 항목은 정해진 시각에 `verify-batch` 로 몰아 돌린다 | 스크립트 자가 테스트 | revert |
| 5 (조건부) | 0 단계에서 링크가 큰 몫이거나 서버에서 뺄 폴더가 렌더러 밖으로 늘면 `Engine_Render` OBJECT 분할 | 네 백엔드 × Dev · Shipping · Server | revert |
| 6 | 렌더 기능 객체를 엔진 내부 기능 목록으로 옮겨 `FrameRenderer` 헤더 변경을 줄인다(이미 반쯤 쓰는 `RealtimeProbeUpdater` · `VfxParticleRenderer` 값 멤버 패턴). 0 단계 측정 뒤에 정한다 | 렌더 GPU 테스트, 큐브 벤치 RT p50 · p99 | revert |

## 3. 리팩토링 트랙(이행 단계와 같이 한다)

동작을 바꾸지 않는다. 단위마다 커밋 하나, 같은 테스트 통과, 성능 경로는 전후 비교(큐브 20k 벤치)로 확인한다. 작업자는 빌드하지 않고 통합이 `-k 0` 빌드로 한 번에 검증한다.

#### 3.1 중복 기능 통합

같은 일을 하는 코드가 둘 이상이면 정본 하나를 정하고 나머지를 호출로 바꾼다. 새 추상화를 만들기 전에 이미 있는 공용 형식(`RegistrationList` · `addLogged`, `StringUtil`, 서비스 접근 API)으로 모은다. 합친 뒤 옛 모양이 다시 생기지 못하게 형식으로 막고(`[[nodiscard]]`, 공용 API 만 공개), 게이트는 형식으로 못 막는 틈에만 둔다.

알려진 후보:

- 이름을 손으로 거절하는 레지스트리(`LocalStoreFactory` · `SQLDriverRegistry` · `LoginService` · `ModuleRenderPassRegistry` · `EditorAssetTypeActions`)를 `RegistrationList` 로.
- 손 불리언 파서를 `StringUtil::tryParseBool` 로. `ElementGrid.cpp` 의 `cellBytes` 손 계산을 공용 계산으로.
- `TestResourceDataSchema` 의 종류 표(엔진 등록표 `SW_RESOURCE_DATA_KIND` 의 사본)를 지우고 등록표를 읽게.
- 테스트 소스 목록이 두 벌인 곳(2a · 2b · 2c 와 같은 일).
- 같은 사실이 데이터 두 곳에 있는 곳(예: HUD 의 `ItemName` 과 `WeaponName` 이 서로 다른 데이터 파일에 있어 이름이 달랐다 — 한 건은 고쳤고 같은 모양을 더 찾는다).
- 같은 값을 여러 곳이 따로 만드는 곳(예: `FrameProfiler::getFrameCount` 를 프레임 번호로 쓴 곳 — `UISystem` 과 `RenderPacketBuilder` 둘은 고쳤고 나머지를 찾는다).

찾는 방법: graphify 의 커뮤니티 · god node 로 후보를 좁히고, 같은 이름 · 같은 역할의 함수 쌍을 grep 으로 확인하고, 결합도가 높은 곳부터 합친다. 복붙 탐지 도구를 쓸지는 그때 정한다.

#### 3.2 불필요한 래핑 걷기

한 함수를 이름만 바꿔 감싸거나 인자를 그대로 넘기기만 하는 래퍼(전달 전용 함수, 호출자가 하나뿐인 얇은 층, 값을 그대로 돌려주는 접근자)는 걷고 호출자가 원 함수를 직접 부른다.

판정 기준(걷는 것과 두는 것을 가른다):

- **걷는다**: 본문이 호출 하나뿐이고 인자와 반환이 같다. 이름만 다르다. 호출자가 하나이거나 같은 층이다. 테스트가 래퍼 자체를 단언하지 않는다.
- **둔다**: 층 경계를 지키는 얇은 면(다른 층이 아래 층 심볼에 직접 닿지 않게 하는 것), 서비스 접근 어휘(`get` / `find`), 검증 · 로그 · 소유권을 더하는 것, 리플렉션 · 직렬화 등록 창구, 플랫폼별 구현을 가리는 것, 오버로드를 모으는 것, 공개 API 로 문서화된 것.
- 모호하면 두고 보고에 적는다. 걷은 뒤에 같은 래퍼가 다시 생기지 않게 하려면 공용 API 를 직접 부르는 쪽이 읽기 쉬움을 규칙 문서에 한 줄 적는다.

찾는 방법: 함수 본문이 호출 한 줄인 함수를 정적으로 훑어 후보를 모으고(그래프의 호출 간선 수가 하나인 노드), 위 기준으로 걷을 것과 둘 것을 가른다.

#### 3.3 그 밖의 리팩토링

- 층 표의 "서로 include 하지 않는다" 주석을 금지 간선 키로 바꿔 게이트가 보게 한다(코어 규칙 `coreBannedLayer` 와 같은 구조).
- 긴 함수 분리는 새 대형 변경이 들어간 파일부터 같은 방식(단계 함수)으로 이어간다.
- `FrameRenderer` 정리는 6 단계.

## 4. 사용자 결정이 필요한 것

- **GPU 검증을 CI 로 자동화할지**(외부 공개 · 비용): 공개 저장소의 자가 호스팅 GPU 러너는 보안 위험이 있고 유료 러너는 비용이 든다. 결정 없이 갈 수 있는 길은 로컬 GPU 기계의 예약 작업으로 `verify-batch` 를 돌리는 것이고 4 단계는 그쪽을 기본으로 둔다.
- **재검토 제안**(사용자의 규칙이라 에이전트가 바꾸지 않는다): 작업자가 맡은 대상 하나만 컴파일하게 허용하는 안(`cmake --build --target <소유 대상>`, 테스트 · 실행은 안 함). 통합 G 의 `getState()` 같은 의미 충돌을 통합 전에 잡는다. CPU 경합은 작업자 수 상한으로 다룬다.

## 5. 확인하지 못한 것

링크 · 증분 · 기동 시간(0 단계가 잰다). `EditorCore` 후보 폴더의 ImGui 사용은 직접 include 만 셌다(전이 include 는 미확인). dllexport 정의로 컴파일한 OBJECT 를 테스트 실행 파일에 넣었을 때 clang-cl 과 Linux Clang 에서 문제가 없는지. 언리얼 · 유니티 렌더러 구조와 ADR 관행은 기억에 기댄 것이라 결정 근거로 쓰지 않았다.
