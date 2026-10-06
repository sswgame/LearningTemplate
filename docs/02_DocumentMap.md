# 문서 지도

어떤 사실을 어느 문서가 갖는지와, 저장소의 모든 README 목록입니다. **한 사실은 한 곳에 적고 나머지는 그곳을 링크합니다.**
같은 내용이 두 문서에 있으면 한쪽만 고쳐지고 다른 쪽이 조용히 낡습니다.

## 층마다 쓰는 것

| 문서 | 쓰는 것 | 쓰지 않는 것 |
|---|---|---|
| [README.md](../README.md) | 이 엔진이 무엇인지, 빌드와 실행 명령, 폴더 소개, 다음에 읽을 문서 | 사용법, 설계, 측정값 |
| [ARCHITECTURE.md](../ARCHITECTURE.md) | 빌드 타깃의 관계, Dev와 Shipping의 차이, C-ABI 경계, 엔진 계층 원칙 | 한 모듈 안의 규칙(그 모듈 README) |
| [CLAUDE.md](../CLAUDE.md), [AGENTS.md](../AGENTS.md) | 에이전트가 매번 읽는 작업 규칙과 명령 요약(영어) | 원본 문서가 따로 있는 설명의 전문 |
| `docs/` | 시작하기, 핫 리로드, 설정처럼 모듈 여럿에 걸친 주제 | 한 모듈의 규칙 |
| 모듈 README(`Source/**/README.md` 등) | [문서 쓰기 지침](10_WritingDocs.md) 3절의 일곱 절 | 함수 레퍼런스(헤더 주석이 원본), 폴더 트리, 경위 |
| [백로그](06_Backlog.md) | 남은 일, 결정을 기다리는 질문, 조건이 오면 할 일 | 교훈(README 함정 절), 결정([09](09_Decisions.md)), 검증([08](08_Verification.md)), 이력 |
| 생성 문서([docs/Config](Config/README.md)) | 설정 키, 인자, 전역 변수, 빌드 옵션의 참조표 | 손으로 고친 내용(다시 생성하면 사라집니다) |

- 문서는 한국어 현재형으로 씁니다. "예전에는 …했다" 같은 경위와 날짜는 커밋 메시지에 쓰고, 지난 결함은 "…하면 …가 깨진다"는 현재형 주의로 씁니다.
- 문체와 용어는 [문서 쓰기 지침](10_WritingDocs.md)을 따릅니다.
- 측정값은 측정 방법(명령과 빌드 구성)과 함께 적고, 날짜나 기계 사정은 적지 않습니다.
- 경로와 링크는 `Scripts/lint/gate/CheckDocPaths.py` 가 검사합니다. 아직 없는 파일은 백로그에만 적습니다. 경로의 형태만 보여 주는 예시는 `<게임>` 처럼 꺾쇠로 씁니다.
- README를 새로 만들면 아래 목록에 한 줄을 더합니다. 목록에 없는 README는 같은 게이트가 잡습니다.

## 주제 안내 (`docs/`)

| 문서 | 내용 |
|---|---|
| [01 시작하기](01_GettingStarted.md) | 빌드부터 첫 게임 오브젝트, 핫 리로드, 테스트까지 따라 하는 튜토리얼 |
| [02 문서 지도](02_DocumentMap.md) | 이 문서 |
| [03 핫리로드와 C-ABI](03_LiveReload_and_ABI.md) | 모듈을 다시 로드하는 순서, 실패했을 때의 처리, 리로드에서 지킬 규칙 |
| [04 코딩 규칙 예시](04_CodingGuidelines.md) | [AGENTS.md](../AGENTS.md) 규칙의 절마다 붙인 한국어 예시 |
| [05 RHI 프레임 계약](05_RHI_FrameContract.md) | 프레임과 렌더 타깃의 순서 계약, 함정, 검증 절차 |
| [06 백로그](06_Backlog.md) | 남은 일(할 일 목록) |
| [07 설정](07_Configuration.md) | 설정 값을 어느 파일에 두는지, 우선순위, 배포본과 핫 리로드 |
| [08 검증과 측정](08_Verification.md) | 작업을 끝내기 전에 돌릴 것, 측정값 읽는 법, 테스트를 쓸 때의 함정, CI |
| [09 결정 기록](09_Decisions.md) | 정한 방향, 하지 않기로 한 것, 측정으로 기각한 안, 바뀐 이름 |
| [10 문서 쓰기 지침](10_WritingDocs.md) | 문서 종류와 층, 모듈 README의 틀, 문장 규칙, 용어 대조표 |
| [11 작업 방식](11_Workflow.md) | 워크트리와 적용 담당, 동시 빌드, 검증 단계, push 와 CI, 결정 권한, 커밋 메시지, 에이전트 정의와 세션 가드 |
| [다음 세션 인계](plans/NEXT.md) | 진행 중인 배치의 상태와 다음 세션이 할 일(배치 경계마다 고쳐 쓴다) |

## 모듈 README

### 기반 라이브러리, 경계, 실행 파일
- [Source/Core](../Source/Core/README.md) — 기반 라이브러리(로그, 메모리, 컨테이너, 압축, 네트워크 공통 계층)
- [Source/Core/Task](../Source/Core/Task/README.md) — 워커 풀, 태스크 그래프, `TaskFuture`
- [Source/RuntimeAPI](../Source/RuntimeAPI/README.md) — App과 모듈 사이의 C-ABI 계약, 서비스 테이블
- [Source/App](../Source/App/README.md) — 실행 파일, 프레임 순서, 헤드리스 실행, 진단 방법
- [Source/Server](../Source/Server/README.md) — 전용 서버 실행 파일의 시작과 설정

### 엔진
- [Source/Engine](../Source/Engine/README.md) — 폴더 계층 표, 루트 파일(시작과 종료), 상용 엔진과의 비교
- [Graphics](../Source/Engine/Graphics/README.md) — RHI, 머티리얼, 바인딩 계약, 소유와 수명
  - [RHI](../Source/Engine/Graphics/RHI/README.md), [Renderer](../Source/Engine/Graphics/Renderer/README.md), [Shader](../Source/Engine/Graphics/Shader/README.md), [2D](../Source/Engine/Graphics/2D/README.md)
- [Object](../Source/Engine/Object/README.md) — 게임 오브젝트, 컴포넌트, 틱, 구조 변경
  - [Component/2D](../Source/Engine/Object/Component/2D/README.md), [Component/Physics](../Source/Engine/Object/Component/Physics/README.md)
- [Scene](../Source/Engine/Scene/README.md) — 씬, 씬 매니저, 씬 파일, 쿠킹
- [Reflection](../Source/Engine/Reflection/README.md) — 리플렉션 런타임(생성기는 [ReflectionParser](../Tools/ReflectionParser/README.md))
- [Serialization](../Source/Engine/Serialization/README.md) — XML, JSON, 바이너리 직렬화의 규칙과 함정
- [Module](../Source/Engine/Module/README.md) — 모듈 매니페스트, 모듈 타입 등록, 엔진 ABI 스탬프
- [Text](../Source/Engine/Text/README.md) — 런타임 텍스트(글꼴, SDF 글리프, 셰이핑, 줄 바꿈)
- [UI](../Source/Engine/UI/README.md) — 게임 UI(위젯 트리, 무효화, 이벤트, 포커스, 화면 스택, 문서, 데이터 바인딩)
  - [UI 문서와 데이터 바인딩](../Source/Engine/UI/Document/README.md), [엔진 기본 화면과 접근성](../Source/Engine/UI/Screens/README.md)
- [Animation](../Source/Engine/Animation/README.md), [Audio](../Source/Engine/Audio/README.md), [Input](../Source/Engine/Input/README.md), [Localization](../Source/Engine/Localization/README.md)
- [Physics](../Source/Engine/Physics/README.md) — [Jolt](../Source/Engine/Physics/Jolt/README.md)와 [Box2D](../Source/Engine/Physics/Box2D/README.md) 백엔드
- [Spatial](../Source/Engine/Spatial/README.md), [Navigation](../Source/Engine/Navigation/README.md), [Environment](../Source/Engine/Environment/README.md)
- [Character](../Source/Engine/Character/README.md), [Destruction](../Source/Engine/Destruction/README.md)
- [UserSettings](../Source/Engine/UserSettings/README.md), [Telemetry](../Source/Engine/Telemetry/README.md), [Automation](../Source/Engine/Automation/README.md), [Utility/Profiling](../Source/Engine/Utility/Profiling/README.md)

### 에디터 · 게임 쪽
- [Source/Editor](../Source/Editor/README.md) — 에디터 모듈, 임포트, 패널을 더하는 법
- [Source/GameFramework](../Source/GameFramework/README.md) — 장르 공통 기반(온라인 기반 `Base/Online` 포함), 키트 규칙, 기반 폴더의 계층
  - [Ability](../Source/GameFramework/Base/Ability/README.md), [Appearance](../Source/GameFramework/Base/Appearance/README.md), [Gimmick](../Source/GameFramework/Base/Gimmick/README.md),
    [Interaction](../Source/GameFramework/Base/Interaction/README.md), [Spline](../Source/GameFramework/Base/Spline/README.md),
    [AI/Director](../Source/GameFramework/Base/AI/Director/README.md), [AI/Schedule](../Source/GameFramework/Base/AI/Schedule/README.md)
- [Source/Games](../Source/Games/README.md) — 게임을 더하는 법, 게임 목록
  - [AbilityArena](../Source/Games/AbilityArena/README.md), [HarvestValley](../Source/Games/HarvestValley/README.md), [MeadowVillage](../Source/Games/MeadowVillage/README.md), [NileCity](../Source/Games/NileCity/README.md),
    [Shooter3D](../Source/Games/Shooter3D/README.md), [StarSkirmish](../Source/Games/StarSkirmish/README.md),
    [ThemeParkTycoon](../Source/Games/ThemeParkTycoon/README.md), [VoxelCraft](../Source/Games/VoxelCraft/README.md)

### 빌드, 도구, 데이터, 테스트
- [cmake](../cmake/README.md) — CMake 계층과 헬퍼 함수(`CheckCmakeReadme` 가 검사)
- [Scripts](../Scripts/README.md) — 파이썬 도구, 린트 폴더(gate, fixer, report, selftest), 커밋 훅
  - [Scripts/generate](../Scripts/generate/README.md), [Scripts/setup](../Scripts/setup/README.md)
- [Tools](../Tools/README.md) — [ReflectionParser](../Tools/ReflectionParser/README.md), [Blender 내보내기](../Tools/DCC/Blender/README.md), [VS Code 실행 인자 GUI](../Tools/launch-args/README.md), 온라인 부하 테스트 봇(`Tools/OnlineLoadBot`, 설명은 Tools README 안)
- [Config](../Config/README.md) — 설정 파일의 두 계층
- [Resource](../Resource/README.md) — 리소스 경로, 소문자 규칙, 텍스처 규칙, [Empty 게임 팩](../Resource/game/empty/README.md)
- [Test](../Test/README.md) — 테스트 실행 파일, 라벨, 스위트 규칙, QA 러너

---
[◀ 이전: 시작하기](01_GettingStarted.md) | [🏠 위키 홈으로 돌아가기](../README.md) | [▶ 다음: 핫리로드 및 ABI 가이드](03_LiveReload_and_ABI.md)
