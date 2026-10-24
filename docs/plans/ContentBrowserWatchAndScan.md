# 콘텐츠 브라우저 감시 지연과 스캔 비용

> 상태: 측정대기 · 선행: 없음 · 영역: editor · 옛번호: 없음 · 진행: 2/2

**남은 확인(통합에서 확인):** 매니페스트 id `asset-hot-reload-test`, 매니페스트 id `content-watch-fence-scenarios`(`Test/QA/VerifyBatch.json`). 확인이 끝나면 통합 담당이 이 문서를 닫는다.

참조 역색인은 2026-10-23 에 "마지막 변경을 따라왔다"(`ContentBrowserReferenceSearch::isCurrent`)를 준비로 보고 보이는 패널에서 시계를 잡게 고쳤습니다(`assetmanage` 간헐 실패).
같은 모양의 폴더 목록 대기는 [Candidates-editor](Candidates-editor.md) 의 `ContentBrowserAsyncWaits` 에 남아 있고, 여기는 그 나머지 둘입니다.

## 1. 단위

| 단위 | 내용 | 상태 |
|---|---|---|
| CW1 | 감시 지연 울타리: 에디터 밖의 쓰기(`<Scratch>` 복사)는 OS 파일 감시가 알려야 변경 번호가 오릅니다. 알림 전에는 낡은 색인이 맞다고 보입니다(보통 ms, 스캔 하나보다 짧음). `AutomationScratch::prepare` 가 사본을 만들 때마다 오르는 번호(`getPrepareCount`)를 에디터가 보고, 새 사본이면 `AssetHotReload::pollContentWatchFence` 가 감시 트리(`Resource/scratch/contentfence.tmp`)에 표지 파일을 쓰고 그 사건이 올 때까지 시나리오 시작을 막습니다(`SW_AUTOMATION_START_BLOCKER`, 5 초 시한 — 넘으면 오류 줄을 남기고 풀어 시나리오가 멈추지 않게 합니다). 같은 감시 흐름의 순서라 표지가 왔으면 그 앞의 사본 쓰기도 알려졌습니다. 표지는 번호만 올리고 바뀐 파일로 세지 않습니다. 사본이 일으킨 원본 임포트는 워커에서 돌므로(결정 기록 5-11) 표지가 온 뒤에도 `AssetHotReload::isImportBusy` 가 거짓이 될 때까지 막습니다(시한 120 초) | 끝남(빌드 미확인) |
| CW2 | 스캔 비용: 전체 스캔이 Release 2 초, Debug 6~19 초라 보이는 콘텐츠 브라우저에서 파일이 바뀔 때마다 그만큼 시계를 잡았습니다. 증분 갱신: `AssetHotReload` 가 바뀐 파일의 리소스 id 를 모으고(`takeChangedPaths` — 폴더가 생기거나 사라지거나 이름이 바뀌었거나 256 개를 넘으면 모르는 것으로 쳐 전체 스캔), `ContentBrowserReferenceSearch` 가 색인이 있고 스캔이 돌지 않을 때 그 파일이 모두 **색인이 아는 파일의 내용 변경**이면 `EditorReferenceIndex::refreshFile` 로 그 파일만 다시 읽습니다. 파일이 생기거나 사라지면(없던 파일이 생기면 다른 파일의 참조가 "있는 파일" 로 바뀝니다) 전체를 다시 스캔합니다. 테스트 `AssetHotReloadTest.TreeChangesAreTrackedPerFileAndFoldersForceAFullRescan` | 끝남(빌드 미확인) |

## 2. 확인

- `EditorTest --test_filter=AssetHotReloadTest.*`(변경 기억 · 폴더 · 한도 · 표지). 울타리의 실제 감시 왕복과 증분 갱신이 에디터 체인 시간을 줄이는지는 `assetmanage` · `contentthumbnails` 를 포함한 에디터 시나리오 체인에서 본다(`AppScenarioTest.EditorScenariosPassOn<백엔드>`) — 울타리가 시한을 넘기면 `[ContentWatchFence]` 오류 줄이 로그 게이트를 진다.
- 증분 갱신 뒤 색인이 전체 스캔과 같은지는 `assetmanage` 가 참조 단언으로 이미 본다. 값 비교 테스트(전체 스캔 결과 = 증분 결과)가 필요해지면 `ContentBrowserReferenceSearch` 를 EditorTest 가 링크하도록 나눈 뒤 더한다.
