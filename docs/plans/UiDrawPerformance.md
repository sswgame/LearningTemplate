# 런타임 UI 그리기 성능의 남은 것

> 상태: 측정대기 · 선행: 없음 · 영역: ui · 옛번호: D6-6 · 진행: 0/2

목표(`GT.UI` p50 0.3 ms) 안이다(측정값과 걷기 분해는 [검증과 측정](../08_Verification.md) 2절 "런타임 UI 벤치": 글 10 칸/프레임 `GT.UI.Paint` p50 0.23 ms + `GT.UI.Layout` 0.06 ms). 더 줄일 후보 둘을 단위로 둔다.

## 1. 단위

| 단위 | 내용 | 선행 |
|---|---|---|
| U1 | 패널마다 하위 출력 캐시(Slate Invalidation Panel) | 없음 |
| U2 | 조상의 `kTransform` 이면 자손까지 다시 칠하는 그림 캐시의 변환을 캐시 밖에서 곱하기 | 없음 |

## 2. 도입 판정 (2026-10-24 정함)

구현하지 않고 측정으로 판정한다. 두 단위 모두 그림 캐시의 무효화 계약(조상 불투명도 · 자르기가 캐시에 반영됨)을 바꾸는 일이라, 목표 안에 있는 값을 더 줄이려고 들이면 무효화 누락이라는 새 결함 모양이 생긴다.
측정은 매니페스트 id `ui-draw-performance-measures` 가 돈다. 도입 기준은 하나다 — 아래 세 측정 중 하나라도 `GT.UI.Paint` + `GT.UI.Layout` p50 합이 목표 0.3 ms 를 넘으면 그 측정을 푸는 단위를 구현한다. 모두 안이면 이 문서를 `끝` 으로 닫는다(하지 않음).

| 측정 | 인자 (Release, VSync 끔, 600 프레임 × 3, 혼자) | 푸는 단위 |
|---|---|---|
| 멈춘 화면 | `-gv_benchUiWidgets=10000` | U1 (출력 재사용이 이미 있어 넘을 일이 드물다) |
| 일부만 바뀜 | `-gv_benchUiWidgets=10000 -gv_benchUiChurn=10` | U1 |
| 조상 변환 | `-gv_benchUiWidgets=10000 -gv_benchUiOpacityPulse=true` | U2 |

창 크기 바꿈(전체 재배치) 한 프레임 · 첫 프레임 글리프 래스터화가 몇 프레임에 퍼지는지는 표 밖이다(워밍업 60 프레임이 버린다 — 글리프 쪽은 [GlyphRasterizationHitch](GlyphRasterizationHitch.md), 프레임 예산으로 들어갔다).
