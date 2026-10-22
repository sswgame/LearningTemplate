# 층 게이트

> 상태: 설계 · 선행: 없음 · 영역: project · 옛번호: 없음 · 진행: 표

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
| T1 | "테이블 = 계산값" 일치 게이트 하나. 테이블이 계산값과 다르면 사유 필드 없이는 통과하지 못하게 한다(Bootstrap · Vfx · Core Spatial 세 불일치를 정리하거나 사유를 적는다) | `CheckEngineLayers.py` · `CheckCoreLayers.py` · `RunEngineLayerGraph.py`, `CheckEngineLayers.toml` · `CheckCoreLayers.toml`, [ScriptsStructure](ScriptsStructure.md) | 이 문서, Scripts README | 게이트 4종 OK, 일부러 낡힌 테이블이 게이트에서 실패 | 남음 |
| T2 | 같은 티어 간선 · 순환(SCC) 검사 — Engine 게이트의 `>` 를 같은 티어 역방향까지 막는다(Vfx → Environment 같은 간선이 대상) | 위와 같음 | 이 문서 | 순환을 만드는 include 가 실패한다 | 남음 |
| T3 | RuntimeAPI → Editor 1건(`EditorModuleExports.h:11`)을 테이블에 한 줄로 명시한다(지금은 주석 예외뿐) | `CheckEngineLayers.toml` | 규칙 TOML | 게이트 OK | 남음 |
| T4 | `CheckCoreNetworkLayers` 의 `_kNetworkTier` 를 TOML 로 옮긴다(작은 기계적 단위) | `CheckCoreNetworkLayers.py` | 규칙 TOML, Scripts README | 게이트 결과 동일 | 남음 |
| T5 | 접두 없는 include 가 교차 해석되는지 검사를 더한다(우선순위 낮음) | `CheckEngineLayers.py` | 이 문서 | 실측 교차 0건 유지 | 남음(낮음) |

## 4. 순서

T1 · T2 · T3 · T4 는 같은 규칙 파일을 만지므로 한 작업자가 한다. [AutomationSplit](AutomationSplit.md) ① 탐침 단계도 `CheckEngineLayers.toml` 의 티어 테이블을 고치므로 서로 순서를 둔다(Roadmap).

## 5. 확인하지 못한 것

- GameFramework 의 `base_folder_order` 가 같은 재계산 구조인지.
- Bootstrap 13 · Vfx 10 · Core Spatial 6 의 불일치가 의도인지 낡음인지는 코드 소유자가 정한다(측정은 불일치만 보였다).
