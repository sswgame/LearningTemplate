<!-- 생성 문서입니다. 손으로 고치지 말고 원본 코드를 고친 뒤 다시 만듭니다: py -3 Scripts/generate/GenerateConfigReference.py -->

# MemoryBudget

[설정 색인](README.md) · [어디에 두나](../07_Configuration.md)

| | |
|---|---|
| 파일 | `Config/Engine/MemoryBudget.json` |
| 층 | 엔진 기본값 |
| 읽는 곳 | `MemoryBudgetMonitor::loadBudgetFile` (`Engine` Config 단계) |
| 언제 | 기동 |
| 배포본 | 읽지 않음(배포본에는 메모리 프로파일러가 없다) |
| 커밋 | 한다 |

JSON 키는 아래 필드 이름 그대로입니다(앞의 `_` 포함). 적지 않은 필드는 기본값입니다. 모르는 키나 읽지 못하는 값은 로드 오류입니다.

## 필드

예산 파일(`Config/Engine/MemoryBudget.json`)이 담는 내용입니다. 리플렉션으로 읽습니다(`ConfigManager::readConfigJSON` — 모르는 키 · 읽지 못한 값은 오류).

원본: [`Source/Engine/Profiling/MemoryBudgetMonitor.h`](../../Source/Engine/Profiling/MemoryBudgetMonitor.h)

| 필드 | 타입 | 기본값 | 범위 | 단위 | 설명 |
|---|---|---|---|---|---|
| `_listBudget` | `vector<MemoryBudgetEntry>` | — |  |  | One budget line per tag - tags without a line have no budget (no warning) |

## `MemoryBudgetEntry`

예산 파일의 한 줄입니다(`_listBudget` 의 원소). 같은 태그 두 번 · 0 이하 크기는 `applyBudgetJSON` 이 오류로 돌립니다.

원본: [`Source/Engine/Profiling/MemoryBudgetMonitor.h`](../../Source/Engine/Profiling/MemoryBudgetMonitor.h)

| 필드 | 타입 | 기본값 | 범위 | 단위 | 설명 |
|---|---|---|---|---|---|
| `_tag` | `string` | — |  |  | Memory tag name (MemoryTag: Texture, Mesh, Audio, Animation, Physics, Navigation, Scene, UI, Script ... case-insensitive) |
| `_megabytes` | `float64` | `0.0` | 0.0 ~ - |  | Budget in MiB, greater than 0. One [MemoryBudget] warning when it is exceeded |
