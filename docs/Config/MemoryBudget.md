<!-- 생성 문서 — 손으로 고치지 않는다. 정본은 코드다. 다시 만들기: py -3 Scripts/generate/GenerateConfigReference.py -->

# MemoryBudget

[설정 색인](README.md) · [어디에 두나](../07_Configuration.md)

| | |
|---|---|
| 파일 | `Config/Engine/MemoryBudget.json` |
| 층 | 엔진 기본값 |
| 읽는 곳 | `MemoryBudgetMonitor::loadBudgetFile` (`EngineLoop` Config 단계) |
| 언제 | 기동 |
| 배포본 | 읽지 않음(배포본에는 메모리 프로파일러가 없다) |
| 커밋 | 한다 |

JSON 키는 아래 표의 키 그대로입니다. 모르는 키는 로드 오류입니다 — 읽기 코드가 이 표(`ConfigKeyDoc`)로 검사합니다.

## 칸

예산 파일의 뿌리입니다.

정본: [`Source/Engine/Utility/Profiling/MemoryBudgetMonitor.cpp`](../../Source/Engine/Utility/Profiling/MemoryBudgetMonitor.cpp)

| 칸 | 타입 | 기본값 | 범위 | 단위 | 설명 |
|---|---|---|---|---|---|
| `_listBudget` | `object[]` | — |  |  | 태그마다 예산 한 줄 — 없는 태그는 예산이 없다(경고하지 않는다) |

## `kArrMemoryBudgetEntryKeyDoc`

`_listBudget` 한 줄입니다. 같은 태그 두 번 · 0 이하 크기는 오류입니다.

정본: [`Source/Engine/Utility/Profiling/MemoryBudgetMonitor.cpp`](../../Source/Engine/Utility/Profiling/MemoryBudgetMonitor.cpp)

| 칸 | 타입 | 기본값 | 범위 | 단위 | 설명 |
|---|---|---|---|---|---|
| `_tag` | `string` | — |  |  | 메모리 태그 이름(`MemoryTag` — Texture · Mesh · Audio · Animation · Physics · Navigation · Scene · UI · Script …, 대소문자 무시) |
| `_megabytes` | `number` | — |  |  | 예산(MiB, 0 보다 크다). 넘으면 `[MemoryBudget]` 경고 한 번 |
