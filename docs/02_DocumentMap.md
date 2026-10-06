# 문서 지도

어떤 사실을 어느 문서가 갖는지와, 저장소의 모든 README 목록입니다. **한 사실은 한 곳에 적고 나머지는 그곳을 링크합니다.**
같은 내용이 두 문서에 있으면 한쪽만 고쳐지고 다른 쪽이 조용히 낡습니다.

## 층마다 쓰는 것

| 문서 | 쓰는 것 | 쓰지 않는 것 |
|---|---|---|
| [README.md](../README.md) | 무엇인지 · 빌드 명령 · 처음 돌리기 · 폴더 한 줄씩 · 문서 링크 | 사용법 · 설계 · 숫자(시험 수 · 성능) |
| [ARCHITECTURE.md](../ARCHITECTURE.md) | 구조의 정본 — 타깃 그래프 · Dev/Shipping · C-ABI 경계와 export 매크로 · 엔진 층의 원칙 · 엔진 전체에 걸친 주의사항 | 한 모듈 안의 계약(그 모듈 README) |
| [CLAUDE.md](../CLAUDE.md) · [AGENTS.md](../AGENTS.md) | 에이전트가 매번 읽는 작업 규칙과 명령 요약(영어). 사실마다 정본 문서를 가리킨다 | 정본이 따로 있는 설명의 전문 |
| `docs/` | 모듈 여럿에 걸친 주제 안내(시작하기 · 핫리로드 · 코딩 규칙 · 프레임 계약) | 한 모듈의 계약 |
| 모듈 README(`Source/**/README.md` 등) | 그 폴더의 계약 · 함정 · 남은 것 · 파일 배치 | 사용법 예제 — **헤더 주석(`/** @brief */`)이 정본**이다 |
| [백로그](06_Backlog.md) | 남은 일 · 결정을 기다리는 질문 · 조건이 오면 할 일 | 끝난 일의 교훈(그 영역 README 의 "함정 · 계약" 절) · 정한 방향과 기각한 안([09](09_Decisions.md)) · 검증법([08](08_Verification.md)) · 이력(`git log`) |
| 생성 문서([docs/Config](Config/README.md)) | 코드 · 데이터에서 만든 참조표(설정 키 · 인자 · 전역 변수 · 빌드 옵션) — 손으로 고치지 않는다 | |

- 문서는 한국어 · 현재형으로 씁니다. "예전에는 …했다" · 날짜 붙은 사연은 커밋 메시지로, 지난 결함은 현재형 주의("…하면 …가 깨진다")로 씁니다.
- 문체와 용어는 [문서 쓰기 지침](10_WritingDocs.md)을 따릅니다.
- 측정값은 재는 법(명령 · 구성)과 함께 적고 날짜 · 기계 사정은 적지 않습니다.
- 경로 · 링크는 `Scripts/lint/gate/CheckDocPaths.py` 가 검사합니다. 아직 없는 파일은 백로그에만 적습니다. 자리만 보이는 예시는 `<게임>` 처럼 꺾쇠로 씁니다.
- README 를 새로 만들면 아래 목록에 한 줄을 더합니다(목록에 없는 README 는 같은 게이트가 잡습니다).

## 주제 안내 (`docs/`)

| 문서 | 내용 |
|---|---|
| [01 시작하기](01_GettingStarted.md) | 도구 · 환경 구성 · 빌드 · 실행 인자 · 첫 시험 · 진단 도구 |
| [02 문서 지도](02_DocumentMap.md) | 이 문서 |
| [03 핫리로드와 C-ABI](03_LiveReload_and_ABI.md) | 모듈을 다시 읽는 순서 · 실패 정책 · 리로드에서 지킬 것 |
| [04 코딩 규칙 예시](04_CodingGuidelines.md) | [AGENTS.md](../AGENTS.md)(규칙 정본) 절마다 한국어 예시 — 규칙을 다시 적지 않는다 |
| [05 RHI 프레임 계약](05_RHI_FrameContract.md) | 프레임 · 렌더타깃 순서 계약 · 함정 · 검증 절차 |
| [06 백로그](06_Backlog.md) | 남은 일(할 일 목록) |
| [07 설정](07_Configuration.md) | 설정 값을 어디에 두는가(층 · 우선순위 · 배포본 · 핫 리로드). 칸 표는 생성 문서 [docs/Config](Config/README.md) |
| [10 문서 쓰기 지침](10_WritingDocs.md) | 문서 종류와 층, 모듈 README의 틀, 문장 규칙, 용어 대조표 |
| [08 검증과 측정](08_Verification.md) | 일을 끝내기 전에 돌릴 것 · 측정값 읽는 법 · 테스트를 쓸 때의 함정 · CI |
| [09 결정 기록](09_Decisions.md) | 정한 방향 · 하지 않기로 한 것 · 측정으로 기각한 안 · 옛 이름 → 지금 이름 |
| [11 작업 방식](11_Workflow.md) | 워크트리와 적용 담당 · 동시 빌드 · 검증 단계 · push 와 CI · 결정 권한 · 커밋 메시지 |

## 모듈 README

### 토대 · 경계 · 실행 파일
- [Source/Core](../Source/Core/README.md) — 토대 라이브러리(로그 · 메모리 · 컨테이너 · 압축 · 네트워크 공통 계층)
- [Source/Core/Task](../Source/Core/Task/README.md) — 워커 풀 · 태스크 그래프 · `TaskFuture`
- [Source/RuntimeAPI](../Source/RuntimeAPI/README.md) — App ↔ 모듈 C-ABI 계약 · 서비스 표
- [Source/App](../Source/App/README.md) — 실행 파일 · 프레임 순서 · 헤드리스 실행 · 모듈 내리기
- [Source/Server](../Source/Server/README.md) — 전용 서버 실행 파일 · 기동 · 설정

### 엔진
- [Source/Engine](../Source/Engine/README.md) — 폴더 티어 표 · 루트 파일(기동 · 종료) · 상용 엔진과의 대조
- [Graphics](../Source/Engine/Graphics/README.md) — RHI · 머티리얼 · 바인딩 계약 · 소유와 수명
  - [RHI](../Source/Engine/Graphics/RHI/README.md) · [Renderer](../Source/Engine/Graphics/Renderer/README.md) · [Shader](../Source/Engine/Graphics/Shader/README.md) · [2D](../Source/Engine/Graphics/2D/README.md)
- [Object](../Source/Engine/Object/README.md) — 게임 오브젝트 · 컴포넌트 · 틱 · 구조 변경
  - [Component/2D](../Source/Engine/Object/Component/2D/README.md) · [Component/Physics](../Source/Engine/Object/Component/Physics/README.md)
- [Scene](../Source/Engine/Scene/README.md) — 씬 · 씬 매니저 · 씬 파일 · 쿠킹
- [Reflection](../Source/Engine/Reflection/README.md) — 리플렉션 런타임(생성기는 [ReflectionParser](../Tools/ReflectionParser/README.md))
- [Serialization](../Source/Engine/Serialization/README.md) — XML · JSON · 바이너리 직렬화의 계약과 함정
- [Module](../Source/Engine/Module/README.md) — 모듈 매니페스트 · 모듈 타입 등록 · 엔진 ABI 스탬프
- [Text](../Source/Engine/Text/README.md) — 런타임 글자(글꼴 · SDF 글리프 · 셰이핑 · 줄 바꿈)
- [UI](../Source/Engine/UI/README.md) — 런타임(게임) UI(위젯 트리 · 무효화 · 사건 · 포커스 · 화면 스택 · 문서 · 데이터 바인딩)
- [Animation](../Source/Engine/Animation/README.md) · [Audio](../Source/Engine/Audio/README.md) · [Input](../Source/Engine/Input/README.md) · [Localization](../Source/Engine/Localization/README.md)
- [Physics](../Source/Engine/Physics/README.md) — [Jolt](../Source/Engine/Physics/Jolt/README.md) · [Box2D](../Source/Engine/Physics/Box2D/README.md) 백엔드
- [Spatial](../Source/Engine/Spatial/README.md) · [Navigation](../Source/Engine/Navigation/README.md) · [Environment](../Source/Engine/Environment/README.md)
- [Character](../Source/Engine/Character/README.md) · [Destruction](../Source/Engine/Destruction/README.md)
- [UserSettings](../Source/Engine/UserSettings/README.md) · [Telemetry](../Source/Engine/Telemetry/README.md) · [Automation](../Source/Engine/Automation/README.md) · [Utility/Profiling](../Source/Engine/Utility/Profiling/README.md)

### 에디터 · 게임 쪽
- [Source/Editor](../Source/Editor/README.md) — 에디터 모듈 · 임포트 · 패널을 더하는 법
- [Source/GameFramework](../Source/GameFramework/README.md) — 장르 공통 기반(`Base/` — 온라인 기반 `Base/Online` 포함) · 키트 규칙(공유 · 서버 · 클라이언트 키트) · 기반 폴더의 층
  - [Ability](../Source/GameFramework/Base/Ability/README.md) · [Appearance](../Source/GameFramework/Base/Appearance/README.md) · [Gimmick](../Source/GameFramework/Base/Gimmick/README.md) ·
    [Interaction](../Source/GameFramework/Base/Interaction/README.md) · [Spline](../Source/GameFramework/Base/Spline/README.md) ·
    [AI/Director](../Source/GameFramework/Base/AI/Director/README.md) · [AI/Schedule](../Source/GameFramework/Base/AI/Schedule/README.md)
- [Source/Games](../Source/Games/README.md) — 게임을 더하는 법 · 게임 목록
  - [AbilityArena](../Source/Games/AbilityArena/README.md) · [HarvestValley](../Source/Games/HarvestValley/README.md) · [MeadowVillage](../Source/Games/MeadowVillage/README.md) · [NileCity](../Source/Games/NileCity/README.md) ·
    [Shooter3D](../Source/Games/Shooter3D/README.md) · [StarSkirmish](../Source/Games/StarSkirmish/README.md) ·
    [ThemeParkTycoon](../Source/Games/ThemeParkTycoon/README.md) · [VoxelCraft](../Source/Games/VoxelCraft/README.md)

### 빌드 · 도구 · 데이터 · 시험
- [cmake](../cmake/README.md) — CMake 층 · 헬퍼 함수(`CheckCmakeReadme` 가 검사)
- [Scripts](../Scripts/README.md) — 파이썬 도구 · 린트 폴더(gate · fixer · report · selftest) · 커밋 훅
  - [Scripts/generate](../Scripts/generate/README.md) · [Scripts/setup](../Scripts/setup/README.md)
- [Tools](../Tools/README.md) — [ReflectionParser](../Tools/ReflectionParser/README.md) · [Blender 내보내기](../Tools/DCC/Blender/README.md) · [VS Code 실행 인자 GUI](../Tools/launch-args/README.md) · 온라인 부하 시험 봇(`Tools/OnlineLoadBot`, 계약은 Tools README 안)
- [Config](../Config/README.md) — 설정 파일의 두 층
- [Resource](../Resource/README.md) — 리소스 경로 · 소문자 · 텍스처 규칙 · [Empty 게임 팩](../Resource/game/empty/README.md)
- [Test](../Test/README.md) — 시험 실행 파일 · 라벨 · 스위트 규칙 · QA 러너

---
[◀ 이전: 시작하기](01_GettingStarted.md) | [🏠 위키 홈으로 돌아가기](../README.md) | [▶ 다음: 핫리로드 및 ABI 가이드](03_LiveReload_and_ABI.md)
