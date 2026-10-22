# 층 게이트

> 상태: 구현중 · 선행: 없음 · 영역: project · 옛번호: 없음 · 진행: 4/5

2026-10-23 사용자와 정한 결정: **"티어 테이블 = 계산값" 일치를 게이트 하나로 강제하고, 같은 티어 간선 · 순환(SCC) 검사를 더한다.** 근거는 2절의 측정이다. 추정 · 확인하지 못한 것은 5절에 구분해 적는다.

## 1. 근본 원인

규칙 데이터가 `RunEngineLayerGraph.py --apply` 의 출력이라, 위반이 생기면 테이블 재계산으로 흡수된다(`:21`, `:240`). 게이트는 `destTier > sourceTier`(`CheckEngineLayers.py:285`)만 보므로, 같은 티어의 역방향 include 는 테이블이 낡을 때 통과한다(커밋 `d252e0e03` 본문이 확인). Core 게이트는 `>=`(`CheckCoreLayers.py:72`)라 더 엄격하다.
구조로 푸는 방향은 두 가지다 — 티어 상향을 사유 필드 없이 쓰지 못하게 하거나, 테이블을 사람이 적는 승인 값으로 두고 계산값과의 일치를 강제한다. 게이트는 형식으로 막지 못하는 틈에만 둔다.

## 2. 사실 — 측정 (2026-10-23)

- 게이트 4종은 현재 OK 이고 include 방향 위반은 0건이다.
- 그런데 티어 테이블이 DAG 계산과 어긋난다 — Bootstrap 13(계산 9, `CheckEngineLayers.toml:117`), Vfx 10(계산 11, 같은 티어 간선 Vfx → Environment, `toml:96`), Core Spatial 6(계산 4, `CheckCoreLayers.toml:21`).
- 10월 변경 29건의 분류: 새 폴더 · 층 11, 티어 올림 · 내림 5, 예외 추가 2, 정리 · 조임 11, 금지 층 줄임 0. 느슨해진 변경은 거의 없고, 틈은 구조에만 있다.
- 덮이지 않는 쌍: RuntimeAPI → Editor 1건(`EditorModuleExports.h:11`, 주석 예외만).
- 직접 include 검사는 `Engine/` 접두만 본다. `Source/Engine` 이 PUBLIC include 디렉터리라 접두 없는 include 가 교차 해석될 수 있다(실측 교차 0건이라 우선순위가 낮다).
- `CheckCoreNetworkLayers` 는 TOML 이 아니라 `.py` 의 `_kNetworkTier` 딕셔너리다 — 데이터 주도 원칙에 어긋난다.

## 3. 단계

| 단계 | 내용 | 읽을 문서 | 고칠 문서 | 검증 | 상태 |
|---|---|---|---|---|---|
| T1 | "테이블 = 계산값" 일치 게이트 하나. 테이블이 계산값과 다르면 사유 필드 없이는 통과하지 못하게 한다(Bootstrap · Vfx · Core Spatial 세 불일치를 정리하거나 사유를 적는다) | `CheckEngineLayers.py` · `CheckCoreLayers.py` · `RunEngineLayerGraph.py`, `CheckEngineLayers.toml` · `CheckCoreLayers.toml`, [ScriptsStructure](ScriptsStructure.md) | 이 문서, Scripts README | 게이트 4종 OK, 일부러 낡힌 테이블이 게이트에서 실패 | 완료 |
| T2 | 같은 티어 간선 · 순환(SCC) 검사 — Engine 게이트의 `>` 를 같은 티어 역방향까지 막는다(Vfx → Environment 같은 간선이 대상) | 위와 같음 | 이 문서 | 순환을 만드는 include 가 실패한다 | 완료 |
| T3 | RuntimeAPI → Editor 1건(`EditorModuleExports.h:11`)을 테이블에 한 줄로 명시한다(지금은 주석 예외뿐) | `CheckEngineLayers.toml` | 규칙 TOML | 게이트 OK | 완료 |
| T4 | `CheckCoreNetworkLayers` 의 `_kNetworkTier` 를 TOML 로 옮긴다(작은 기계적 단위) | `CheckCoreNetworkLayers.py` | 규칙 TOML, Scripts README | 게이트 결과 동일 | 완료 |
| T5 | 접두 없는 include 가 교차 해석되는지 검사를 더한다(우선순위 낮음) | `CheckEngineLayers.py` | 이 문서 | 실측 교차 0건 유지 | 남음(낮음) |

## 4. 순서

T1 · T2 · T3 · T4 는 같은 규칙 파일을 만지므로 한 작업자가 한다. [AutomationSplit](AutomationSplit.md) ① 탐침 단계도 `CheckEngineLayers.toml` 의 티어 테이블을 고치므로 서로 순서를 둔다(Roadmap).

## 5. 구현 결과 (2026-10-23)

구조는 이렇다 — 계산(SCC · Kahn 티어 · "표 = 계산값" 판정)은 `Scripts/lint/LayerGraph.py` 한 곳이고 게이트와 보고서가 같은 함수를 부른다.

- **T1 표 = 계산값.** Engine · Core 게이트가 전체 훑기(`--files` 없이)에서 include 그래프로 티어를 직접 계산해 표와 맞춘다. 표 < 계산값은 실패, 표 > 계산값은 `[tierReason]`(폴더 → 이유)에 이유가 있는 폴더만 통과, 계산값과 같아진 이유 줄 · 표에 없는 폴더의 이유 줄은 실패(낡은 줄). 형식으로 막은 부분: `RunEngineLayerGraph --apply` 는 **내리기 · 같은 값만** 쓰고 올리기는 `--raise-reason "<사유>"` 가 있을 때만 쓰며(사유는 그 줄 위 주석으로 남는다), `[tierReason]` 폴더는 건드리지 않는다. 그래서 위반이 표 재계산으로 말없이 흡수되는 길이 닫힌다. 게이트를 둔 틈: 표를 손으로 고치는 것(형식으로 못 막는다 — 표가 데이터 파일이다) · 사유 글이 맞는지(리뷰). `--files`(커밋 훅)에서는 그래프가 일부라 이 검사를 건너뛴다(요약에 그렇게 찍는다) — 전체 훑기는 린트 묶음 · CI 가 한다.
- **T2 같은 티어 · 순환.** Engine 게이트의 티어 검사를 `>` 에서 `>=` 로(최상위 `[tier]` · `[subtier]` 둘 다), 순환(SCC)은 계산 단계에서 따로 실패시킨다. Core 는 원래 `>=` 였다. Core/Network 게이트도 같은 층의 다른 폴더(Transport ↔ Security)를 막는다.
- **T3 RuntimeAPI → Editor.** RuntimeAPI 는 `Editor/` · `GameFramework/` include 를 금지하고, 계약 예외는 `[exemption]` 의 `contract:<파일>|<include>` 줄(이유 필수 · 낡으면 실패)로만 넘긴다. 지금 한 줄(`EditorModuleExports.h`).
- **T4 Network 층 표.** `_kNetworkTier` 를 `rules/CheckCoreNetworkLayers.toml` 의 `[tier]` 로 옮겼다(`"<root>"` 가 뿌리, `[tier]` 가 없으면 오류로 멈춘다). 게이트 결과 동일.
- **표 쪽에서 맞춘 것.** Vfx 10 → 11(Vfx → Environment 같은 티어 간선, 계산값), Core Spatial 6 → 4(계산값 — 의존이 Common · Container · Math 뿐이고 로그 매크로를 안 쓴다, Network 만 쓴다). Bootstrap 13 은 계산값 9 와 달라 `[tierReason]` 에 기존 주석의 의도(정적 저장소 정리가 위쪽을 내려다본다)를 이유로 옮겼다 — 코드 소유자가 의도인지 확인한다. 코드는 건드리지 않았고 새로 드러난 include 위반은 없다.
- **테스트.** 게이트 `selfTestCases`(같은 티어 역방향 · 사유 없는 상향 · 표 < 계산값 · 순환 · RuntimeAPI → Editor · Core 두 모양 · Network 같은 층)와 `Test/PythonTest/TestLayerTierGate.py`(판정 함수 · `--apply` 올리기 거부).
- GameFramework `base_folder_order` · `base_layer` 는 같은 재계산 구조가 **아니다** — 손으로 쓴 표이고 계산 · `--apply` 가 없다(게이트는 `>=` 로 같은 순서도 막는다). 다만 그래프 계산값과 맞춰 보는 검사는 없다(여유가 조용히 남는 점은 Engine · Core 와 같은 모양이라, 필요하면 `LayerGraph` 를 재사용해 붙일 수 있다).

## 6. 확인하지 못한 것

- GameFramework 표에 "표 = 계산값" 검사를 붙일지(재계산 구조는 아니라 급하지 않다).
- Bootstrap 13 이 의도인지는 코드 소유자가 확인한다(이유 줄의 문구는 기존 주석을 옮긴 것이다). Vfx · Spatial 은 계산값으로 맞췄다 — 의도였다면 `[tierReason]` 으로 되돌린다.
- Engine 게이트는 `.xxx` 목록 파일도 보게 됐다(보고서가 원래 보던 파일과 맞춤) — 위반은 새로 나오지 않았다.
