# 에디터 시나리오가 저장소 소스 트리에 직접 쓴다

> 상태: 구현중 · 선행: 없음 · 영역: editor · 옛번호: 없음 · 진행: 2/4

에디터 시나리오를 한 번 돌리면 추적 중인 리소스가 바뀌거나 잔여 파일이 남았다(2026-10-20 DX12 묶음). `animgraphlayers/animgraphlayers.animgraph.xml` 이 줄바꿈이 바뀐 채 남고, `assetmanage` 가 `new_material.material` · `new_material_2.material` 을 남기고 추적 중인 `probe.material(.meta)` 를 지웠다. 잔여 파일이 있으면 다음 실행이 다른 이름(`new_material_2`)을 만들어 시나리오가 진다.

**근본 원인**: 시나리오가 `Resource/` 원본에 쓴다. 파일을 지우거나 되돌리는 것은 증상 처리다.

## 1. 결정

- **시나리오가 쓰는 폴더를 선언하고, 실행기가 실행마다 새 사본으로 보낸다.** `<Scenario>` 아래 `<Scratch from="<추적 중인 폴더 id>" as="scratch/<이름>"/>`. 시작할 때 `Resource/scratch/<이름>` 을 지우고 원본을 복사한다(`AutomationScratch::prepare`) — 글 파일 안의 원본 id 는 사본 id 로 바꾸고, `.meta` 의 guid 는 새로 만든다(원본과 겹치지 않게). `Resource/scratch/` 는 `.gitignore` 에 있다.
- **사본을 리소스 루트 밖(`Saved/`)이 아니라 루트 안의 보통 도메인 폴더로 둔 이유**: 편집기에는 `ResourcePaths::getRootFolderPath()` 에 id 를 직접 붙이는 곳이 43 군데(콘텐츠 브라우저 열기 · 이동, 역색인 · 감시 루트, 가져오기)라, 루트 밖으로 경로를 돌리는 덮개(id 접두사 → 다른 폴더)는 그 길을 모두 놓친다. 루트 안의 도메인이면 모든 길이 같은 파일을 본다.
- **작업 트리가 깨끗한지 보는 게이트는 두지 않는다.** 사본이 쓰는 곳 자체를 바꾸므로 같은 모양(원본에 씀)을 형식으로 막는다. 남는 틈은 `<Scratch>` 를 선언하지 않은 시나리오가 쓰는 경우이고, 그것은 S3 의 이관과 S4 의 판단으로 닫는다.

## 2. 단위

| 단위 | 내용 | 상태 |
|---|---|---|
| S1 | `Engine/Automation/AutomationScratch.h` — `isValidFolder` · `prepare` · `rebaseText`, 시나리오 `<Scratch>` 읽기, 실행기의 `prepareScratchFolders`(실패면 LoadError — 원본에 쓰며 돌지 않는다), `.gitignore`, 자동화 README 함정 줄. 테스트 `AutomationScratchTest.*` 셋 | 끝남(빌드 미확인) |
| S2 | 결정 기록(이 문서 1절) | 끝남 |
| S3 | `assetmanage` · `animgraphlayers` 시나리오를 `<Scratch>` 와 사본 id 로 옮긴다(`content.open`, `gv_editorProbeAsset`, `ExpectLog` 의 id, Quick Open 글). 실행 확인 | 남음 |
| S4 | 쓰기를 선언하지 않은 시나리오가 `Resource/` 에 쓰면 실행기가 실패로 적는 검사(`ResourcePaths::getWritePath` 가 `scratch/` 밖이면 알림)를 둘지 판단 | 남음 |

## 남은 것

- **S3 의 함정**: Quick Open 은 파일 이름으로 찾으므로 사본과 원본이 같은 이름이면 둘 중 하나가 걸린다 — 단계를 사본 id 로 여는 명령(`content.open` 같은 id 인자)으로 바꾼다. 역색인은 `Resource/` 를 훑어 원본과 사본을 모두 센다 — `Editor.ReferenceCount` 기대값이 사본 쪽만 세는지 실행으로 확인한다.
- 개발 PC 에서 시나리오를 돌린 뒤 쿠킹하면 `Resource/scratch/` 가 팩에 들 수 있다 — 쿠커의 도메인 목록에서 빼는지 S3 때 확인한다.
- 빌드 · `EngineTest --test_filter=AutomationScratchTest.*:AutomationScenarioTest.*`.
