# 문서 지도

어떤 사실을 어느 문서가 갖는지와, 저장소의 README 목록입니다. **한 사실은 한 곳에 적고 나머지는 그곳을 링크합니다.**
같은 내용이 두 문서에 있으면 한쪽만 고쳐지고 다른 쪽이 조용히 낡습니다.

엔진 구조를 설명하는 문서(`ARCHITECTURE.md`, `Source/Engine` · `Core` · `GameFramework` · `Editor` · `ModuleHost` · `App` · `Server` · `RuntimeAPI` 의 README,
시작하기 · 핫 리로드 · RHI 프레임 계약)는 지웠습니다. 엔진이 계속 바뀌어 낡기 쉬우므로, 구조가 굳은 뒤에 필요한 것만 다시 씁니다.
그때까지 설계 사실의 원본은 헤더 주석과 [결정 기록](09_Decisions.md), 규칙의 원본은 [AGENTS.md](../AGENTS.md) 입니다.

## 층마다 쓰는 것

| 문서 | 쓰는 것 | 쓰지 않는 것 |
|---|---|---|
| [README.md](../README.md) | 이 엔진이 무엇인지, 빌드와 실행 명령, 폴더 소개, 다음에 읽을 문서 | 사용법, 설계, 측정값 |
| [CLAUDE.md](../CLAUDE.md), [AGENTS.md](../AGENTS.md) | 에이전트가 매번 읽는 작업 규칙과 명령 요약(`CLAUDE.md` 는 한국어, `AGENTS.md` 는 영어 제목에 한국어 본문) | 원본 문서가 따로 있는 설명의 전문 |
| `docs/` | 백로그, 결정, 검증, 작업 방식, 설정처럼 모듈 여럿에 걸친 주제 | 한 모듈의 규칙 |
| 게임 · 도구 README | [문서 쓰기 지침](10_WritingDocs.md) 3절의 일곱 절 | 함수 레퍼런스(헤더 주석이 원본), 폴더 트리, 경위 |
| [백로그](backlog/README.md) | 남은 일, 결정을 기다리는 질문, 조건이 오면 할 일 | 결정([09](09_Decisions.md)), 검증([08](08_Verification.md)), 이력 |
| 생성 문서([docs/Config](Config/README.md)) | 설정 키, 인자, 전역 변수, 빌드 옵션의 참조표 | 손으로 고친 내용(다시 생성하면 사라집니다) |

- 문서는 한국어 현재형으로 씁니다. "예전에는 …했다" 같은 경위와 날짜는 커밋 메시지에 쓰고, 지난 결함은 "…하면 …가 깨진다"는 현재형 주의로 씁니다.
- 문체와 용어는 [문서 쓰기 지침](10_WritingDocs.md)을 따릅니다.
- 측정값은 측정 방법(명령과 빌드 구성)과 함께 적고, 날짜나 기계 사정은 적지 않습니다.
- 경로와 링크는 `Scripts/lint/gate/CheckDocPaths.py` 가 검사합니다. 아직 없는 파일은 백로그에만 적습니다. 경로의 형태만 보여 주는 예시는 `<게임>` 처럼 꺾쇠로 씁니다.
- README를 새로 만들면 아래 목록에 한 줄을 더합니다. 목록에 없는 README는 같은 게이트가 잡습니다.

## 주제 안내 (`docs/`)

| 문서 | 내용 |
|---|---|
| [02 문서 지도](02_DocumentMap.md) | 이 문서 |
| [04 코딩 규칙 예시](04_CodingGuidelines.md) | [AGENTS.md](../AGENTS.md) 규칙의 절마다 붙인 한국어 예시 |
| [백로그](backlog/README.md) | 남은 일(할 일 목록). 영역별 파일과 상태 태그, 목록 명령 |
| [07 설정](07_Configuration.md) | 설정 값을 어느 파일에 두는지, 우선순위, 배포본과 핫 리로드 |
| [08 검증과 측정](08_Verification.md) | 작업을 끝내기 전에 돌릴 것, 측정값 읽는 법과 기준선, 검증할 때의 함정, CI 실패 읽기 |
| [09 결정 기록](09_Decisions.md) | 정한 방향, 하지 않기로 한 것, 측정으로 기각한 안, 바뀐 이름 |
| [10 문서 쓰기 지침](10_WritingDocs.md) | 문서 종류와 층, README의 틀, 문장 규칙, 용어 대조표 |
| [11 작업 방식](11_Workflow.md) | 워크트리와 적용 담당, 동시 빌드, 검증 단계, push 와 CI, 결정 권한, 커밋 메시지, 에이전트 정의와 세션 가드 |
| [다음 세션 인계](plans/NEXT.md) | 진행 중인 배치의 상태와 다음 세션이 할 일(배치 경계마다 고쳐 쓴다) |
| [엔진 분할 계획](plans/EnginePartition.md) | 엔진 분할의 남은 단계와 순서 |
| [빌드 속도 계획](plans/BuildSpeed.md) | 빌드 속도의 남은 단계(유니티 범위, 무거운 단일 TU) |
| [서버 디버깅 계획](plans/ServerDebug.md) | 에디터에서 서버 · 멀티플레이를 보는 단위와 운영 단위 |

`docs/plans/` 의 계획 문서는 단계가 끝나면 그 단계를 지우고, 전부 끝나면 파일을 지웁니다. 결과는 결정 기록과 검증 문서로, 남은 일은 백로그로 옮깁니다.

## README

### 엔진 모듈
- [Automation](../Source/Engine/Automation/README.md) — 자동화 시나리오 형식, 단계와 탐침, 대상 고르기 규칙, 결정적 재생과 녹화
- [Schedule](../Source/GameFramework/Base/Actor/AI/Schedule/README.md) — NPC 하루 일정 런타임, 칸 고르기 · 일찍 나서기 · 끼어들기 · 경로 제공자

### 게임
- [Source/Games](../Source/Games/README.md) — 게임을 더하는 법, 게임 목록
  - [AbilityArena](../Source/Games/AbilityArena/README.md), [HarvestValley](../Source/Games/HarvestValley/README.md), [MeadowVillage](../Source/Games/MeadowVillage/README.md), [NileCity](../Source/Games/NileCity/README.md),
    [Shooter3D](../Source/Games/Shooter3D/README.md), [StarSkirmish](../Source/Games/StarSkirmish/README.md),
    [ThemeParkTycoon](../Source/Games/ThemeParkTycoon/README.md), [VoxelCraft](../Source/Games/VoxelCraft/README.md), [WildWest](../Source/Games/WildWest/README.md)

### 에디터 확장
- [MeshModifiers](../Source/GameFramework/PlugIn/Editor/MeshModifiers/README.md) — 메시 모디파이어 스택 플러그인(`GF_EditorPlugIn_MeshModifiers`), 모디파이어 목록 · 편집 UX
- [CharacterEditor](../Source/GameFramework/PlugIn/Editor/CharacterEditor/README.md) — 캐릭터 편집 플러그인(`GF_EditorPlugIn_CharacterEditor`), 소켓 · 외형 선택 편집 모델과 패널 뼈대

### 빌드, 도구, 데이터, 테스트
- [cmake](../cmake/README.md) — CMake 계층과 헬퍼 함수(`CheckCmakeReadme` 가 검사)
- [Scripts](../Scripts/README.md) — 파이썬 도구, 린트 폴더(gate, fixer, report, selftest), 커밋 훅
  - [Scripts/generate](../Scripts/generate/README.md), [Scripts/setup](../Scripts/setup/README.md)
- [Tools](../Tools/README.md) — [ReflectionParser](../Tools/ReflectionParser/README.md), [Blender 내보내기](../Tools/DCC/Blender/README.md), [VS Code 실행 인자 GUI](../Tools/launch-args/README.md), [온라인 부하 테스트 봇](../Tools/OnlineLoadBot/README.md)
- [Config](../Config/README.md) — 설정 파일의 두 계층
- [Resource](../Resource/README.md) — 리소스 경로, 소문자 규칙, 텍스처 규칙, [Empty 게임 팩](../Resource/game/empty/README.md)
- [Test](../Test/README.md) — 테스트 실행 파일, 라벨, 스위트 규칙, 테스트를 쓰는 규칙과 도우미, QA 러너
