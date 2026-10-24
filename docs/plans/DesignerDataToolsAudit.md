# 디자이너용 데이터 도구 점검

> 상태: 측정대기 · 선행: 없음 · 영역: editor · 옛번호: D3-13 · 진행: 6/6

**남은 확인(통합에서 확인):** 매니페스트 id `behavior-tree-tests`(로더 · 편집 모델 · 지휘관 트리 테스트), 매니페스트 id `catalog-curve-model-test`(곡선 도우미 테스트), 매니페스트 id `behavior-tree-editor-selftests`(행동 트리 창의 에디터 자체 테스트)(`Test/QA/VerifyBatch.json`). 확인이 끝나면 통합 담당이 이 문서를 닫는다.

스크립팅 층을 두지 않으므로([결정 기록](../09_Decisions.md) 2절) 코드 없이 로직 · 재질 · 효과를 짜는 도구가 중요하다. 2026-10-22 에 코드로 지금 상태를 확인했고(D0), 빠진 것을 단위로 쪼개 D1 ~ D5 를 2026-10-24 에 끝냈다(코드는 끝, 빌드 · 실행 확인은 위 매니페스트).

## 1. 지금 있는 것

| 도구 | 있는가 | 근거 |
|---|---|---|
| 노드 그래프 위젯 | 있음 | `Editor/Common/Widgets/EditorNodeGraph` (imgui-node-editor, 규칙 `EditorNodeGraphRules`). 애니메이션 그래프 · 대화 그래프 · 기믹 회로 그래프 · 행동 트리가 쓴다 |
| 애니메이션 상태 머신 | 있음 | `AnimationGraphPanel`(노드 · 조건 전이 · 레이어와 마스크 `AnimationGraphLayerEditor`) |
| 게임플레이 논리 회로 | 있음 | `GimmickGraphPanel`(GameFramework 에디터 플러그인 Base, 기믹 회로 데이터) |
| 대화 그래프 | 있음 | `DialogueGraphPanel` |
| 머티리얼 편집 | 값 편집만(결정) | `MaterialPanel` 은 마스터 머티리얼 XML 의 값과 미리보기 구다. 노드로 셰이더를 짜는 그래프는 두지 않기로 했다(D3, 결정 기록 5-11) |
| 행동 트리 | 있음 | 데이터 파일 `*.bt.xml`(`BehaviorTreeAsset`) + 작업 이름 등록 표(`BehaviorTaskRegistry`, `SW_BEHAVIOR_TASK`) + 편집 창 `BehaviorTreePanel`(GameFramework 에디터 플러그인 Base, 화면 없는 모델 `BehaviorTreeEditModel`) |
| 파티클 | 보기 + 표 편집 + 곡선 편집 | `VfxPreviewPanel` 은 보기 전용(같은 파일을 두 편집기가 쥐지 않게), 효과 파일(`*.vfx.xml`, 데이터 종류 `vfx`)은 카탈로그 편집기(`CatalogPanel`)로 고친다. 곡선 · 색 그래디언트는 표 행의 `Curves` 단추가 여는 창에서 고친다(D4) |

## 2. 단위

| 단위 | 내용 | 상태 |
|---|---|---|
| D0 | 지금 상태 점검(1절) | 끝남 |
| D1 | 행동 트리 데이터 형식 - `*.bt.xml` 로더(`BehaviorTreeAsset`)가 `BehaviorTree::add*` 로 짓는다. 노드 종류 · 비교 · 중단 이름표와 노드별 속성 표는 `BehaviorTreeNames` 한 곳(로더와 편집기가 같이 쓴다). 잎이 부르는 함수는 이름으로 적고 `BehaviorTaskRegistry` 가 잇는다. RTS 지휘관 AI(`RTSCommanderAI`)를 코드 트리에서 `game/starskirmish/data/commander.bt.xml` 로 옮겼다 | 끝남 |
| D2 | 행동 트리 편집기 - `BehaviorTreePanel`(`EditorDocumentPanel`, Data 종류의 `.bt.xml` 만 받음): 노드 더하기(툴바 · 캔버스 오른쪽 클릭) · 지우기(아래까지) · 위/아래 순서 · 링크 잇기로 부모 바꾸기 · 속성 칸 · 작업 고르기(등록 표) · 저장 전 문제 목록, 되돌리기. 노드는 트리 모양대로 자동 배치(위치는 파일에 없다). 플레이 중에는 선택한 오브젝트의 `AIControllerComponent` 실행기의 활성 노드를 `running` 으로 보인다 | 끝남 |
| D3 | 머티리얼 그래프를 둘지 결정 - 두지 않는다(결정 기록 5-11, 다시 볼 조건 적음) | 끝남 |
| D4 | VFX 곡선 · 그래디언트 편집 - 카탈로그 표 행의 `Curves` 창(`CatalogTableEditor`): 곡선 그래프 · 색 그래디언트 띠 · 키 칸(시각 · 값 · 보간) 편집, 키 더하기 · 빼기(빈 채널 포함). 곡선은 이름이 아니라 모양(`_listKey` 자식)으로 찾는다(`CatalogCurveModel`). 미리보기 창은 보기 전용 규칙을 지켰다 | 끝남 |
| D5 | 일반 게임플레이 상태 머신이 기믹 회로 · 행동 트리 · 퀘스트 · 대화 · 애니메이션 상태 머신으로 갈음되는지 판단 - 갈음된다(결정 기록 5-11, 다시 볼 조건 적음) | 끝남 |

## 3. 설계 메모

- 행동 트리 종류(`behaviortree`)는 `SW_RESOURCE_DATA_KIND` 에 올리지 않고 `ResourceDataSchemaTest` 표에 둔다 - 레지스트리는 카탈로그 편집기의 파일 목록이기도 해서 전용 편집기가 있는 종류를 올리면 같은 파일을 두 편집기가 쥔다([DataKindRegistry](DataKindRegistry.md) 3절).
- 편집기가 저장하면 파일의 주석은 사라진다(트리를 글로 새로 쓴다) - 설명은 노드 `name` 에 적는다. 카탈로그 편집기의 곡선 편집은 원문의 그 구간만 고쳐 주석이 남는다.
- 읽기에서 모르는 노드 · 속성 · 비교 · 중단 이름과 등록되지 않은 작업은 오류이고 기본값으로 바꾸지 않는다(폴백 금지). 편집기는 틀린 중간 상태(자식 없는 컴포짓, 아직 안 고른 작업)를 들고 문제 목록으로 알리고, 저장은 막지 않되 로그에 경고를 남긴다.

## 남은 것

- 지휘관 AI(`RTSCommanderAI`)는 `AIControllerComponent` 에 붙지 않아 행동 트리 창의 활성 노드 표시가 닿지 않는다: [Candidates-editor](Candidates-editor.md) `BehaviorTreeLiveCommander`.
- 실행 확인은 머리의 매니페스트 id 셋이다(빌드 포함).
