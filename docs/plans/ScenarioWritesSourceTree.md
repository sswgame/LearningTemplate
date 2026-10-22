# 에디터 시나리오가 저장소 소스 트리에 직접 쓴다

> 상태: 끝 · 선행: 없음 · 영역: editor · 옛번호: 없음 · 진행: 4/4

에디터 시나리오를 한 번 돌리면 추적 중인 리소스가 바뀌거나 잔여 파일이 남았다(2026-10-20 DX12 묶음). `animgraphlayers/animgraphlayers.animgraph.xml` 이 줄바꿈이 바뀐 채 남고, `assetmanage` 가 `new_material.material` · `new_material_2.material` 을 남기고 추적 중인 `probe.material(.meta)` 를 지웠다. 잔여 파일이 있으면 다음 실행이 다른 이름(`new_material_2`)을 만들어 시나리오가 진다.

**근본 원인**: 시나리오가 `Resource/` 원본에 쓴다. 파일을 지우거나 되돌리는 것은 증상 처리다.

## 1. 결정

- **시나리오가 쓰는 폴더를 선언하고, 실행기가 실행마다 새 사본으로 보낸다.** `<Scenario>` 아래 `<Scratch from="<추적 중인 폴더 id>" as="scratch/<이름>"/>`. 시작할 때 `Resource/scratch/<이름>` 을 지우고 원본을 복사한다(`AutomationScratch::prepare`) — 글 파일 안의 원본 id 는 사본 id 로 바꾸고, `.meta` 의 guid 는 새로 만든다(원본과 겹치지 않게). `Resource/scratch/` 는 `.gitignore` 에 있다.
- **사본을 리소스 루트 밖(`Saved/`)이 아니라 루트 안의 보통 도메인 폴더로 둔 이유**: 편집기에는 `ResourcePaths::getRootFolderPath()` 에 id 를 직접 붙이는 곳이 43 군데(콘텐츠 브라우저 열기 · 이동, 역색인 · 감시 루트, 가져오기)라, 루트 밖으로 경로를 돌리는 덮개(id 접두사 → 다른 폴더)는 그 길을 모두 놓친다. 루트 안의 도메인이면 모든 길이 같은 파일을 본다.
- **작업 트리가 깨끗한지 보는 게이트는 두지 않는다.** 사본이 쓰는 곳 자체를 바꾸므로 같은 모양(원본에 씀)을 형식으로 막는다. 남는 틈은 `<Scratch>` 를 선언하지 않은 시나리오가 쓰는 경우이고, 그것은 S3 의 이관과 S4 의 판단으로 닫는다.

## 2. 단위

| 단위 | 내용 | 상태 |
|---|---|---|
| S1 | `Engine/Automation/AutomationScratch.h` — `isValidFolder` · `prepare` · `rebaseText`, 시나리오 `<Scratch>` 읽기, 실행기의 `prepareScratchFolders`(실패면 LoadError — 원본에 쓰며 돌지 않는다), `.gitignore`, 자동화 README 함정 줄. 테스트 `AutomationScratchTest.*` 셋 | 끝남(Debug 테스트 확인) |
| S2 | 결정 기록(이 문서 1절) | 끝남 |
| S3 | `assetmanage` · `animgraphlayers` 시나리오를 `<Scratch>`(`scratch/assetmanage` · `scratch/animgraphlayers`)와 사본 id 로 옮겼다(`content.open`, `gv_editorProbeAsset`, `ExpectLog` 의 id). Quick Open 은 파일 이름으로 찾아 원본과 사본 중 하나가 걸리므로 개발 명령 `asset.openScratch <사본 id>` 를 더했다(`scratch/` 밖의 id 는 받지 않는다 — 원본을 열어 고치는 경로를 형식으로 막는다). 역색인의 참조 도메인 목록(`EditorReferenceIndexInternal::kArrDomainPrefix`)에 `scratch/` 가 없어 사본 안의 참조를 세지 못하던 것을 고쳤다(`Editor.ReferenceCount` 가 0 이었다). 테스트 `AutomationScratchTest.MigratedEditorScenariosNameTheSourceFolderOnlyInScratch` · `EditorReferenceIndexTest.ScratchCopiesAreReferenceDomains`. 실행: DX12 Debug 에서 두 시나리오가 통과하고 `git status` 의 `Resource/` 가 깨끗하다 | 끝남(DX12 Debug 실행 확인) |
| S4 | 쓰기를 선언하지 않은 시나리오가 `Resource/` 에 쓰면 실행기가 실패로 적는 검사를 둘지 판단 — **두지 않는다.** 근거: 에디터 시나리오 체인 전체(`AppScenarioTest.EditorScenariosPassOnDirectX12`, 192 단계)를 돌린 뒤 `git status` 가 시작 전과 같았다(추적 파일 변경 · 새 파일 없음). 쓰는 시나리오는 이미 `<Scratch>` 로 옮겼고, 검사는 쓰기 경로(`ResourcePaths`)마다 실행기에 훅을 걸어야 해 비용이 크다. 새 시나리오가 원본에 쓰면 체인 뒤 `git status` 로 드러난다 | 끝남(하지 않기로 함) |

## 남은 것

- **S3 에서 확인한 것**: 역색인은 `Resource/` 를 훑어 원본과 사본을 모두 읽지만 참조는 도메인 접두(`engine/` · `common/` · `game/` · `editor/` · `scratch/`)로 시작하는 글만 센다 — `scratch/` 가 빠져 있던 것이 `Editor.ReferenceCount` 가 0 이던 까닭이다.
  원본 씬은 원본 id 를, 사본 씬은 사본 id 를 가리키므로 기대값(1)은 사본 쪽만 센 값이다. 쿠커의 도메인 목록은 `engine` · `common` · `game/<팩>` 뿐이라(`PackCooker::collectDomains`) `Resource/scratch/` 는 팩에 들지 않는다.
- 다른 에디터 시나리오가 `Resource/` 에 쓰는지는 체인을 돌려 확인했다(S4). 게임 시나리오(`game/*/automation`)는 돌리지 않았다.
- 빌드 · `EngineTest --test_filter=AutomationScratchTest.*:AutomationScenarioTest.*` · `EditorTest --test_filter=EditorReferenceIndexTest.*`.
