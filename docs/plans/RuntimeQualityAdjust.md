# 부하 검증의 남은 것과 자동 품질 조정

> 상태: 구현중 · 선행: 없음 · 영역: quality · 옛번호: C-29 · 진행: 1/4

부하 검증(2026-10-15)이 들어갔다(쓰는 법은 [검증과 측정](../08_Verification.md) "부하 검증" 절, 결정은 [결정 기록](../09_Decisions.md) 5-2 끝). 첫 측정이 드러낸 것과 남은 것을 단위로 나눈다.

## 1. 사실 (2026-10-15 첫 측정)

- 개발 배치의 기동 열기 309 ~ 316 번 중 셋은 Shipping 에 없는 일이다: 키트 DLL 섀도 복사 61(읽고 쓰고 PDB 까지 복사), 모듈 매니페스트 70(`Bin/Modules/*.module.json` 낱개 — 언리얼 `.modules` 처럼 한 파일로 묶을 수 있다),
  셰이더 원본 해시 약 30(`.hlsl` · `.hlsli` 를 읽어 쿠킹 스탬프를 대조). LowEnd 흉내(8 ms)에서 이것만 약 1.3 s 다. 배포 기동을 재려면 쿠킹 팩을 올린 Release(`--cooked-dir`)로 부하 시나리오를 돌리는 길이 먼저 있어야 한다.
- 렌더 벤치 · aiskirmish 의 최악 프레임에 13 ~ 20 ms 한 프레임이 판마다 나오다 말다 한다 — 디스크와 무관하다(측정 창 안 파일 열기 0, 흉내를 꺼도 나온다).
- 실행 중 자동 품질 조정(언리얼 Dynamic Resolution · 유니티 Adaptive Performance)은 아직 없다. 지금은 `load scale` 이 밖에서 단계를 내려 예산을 지키는 첫 단계를 찾는다.
- 하드웨어 흉내는 코어 수 · 메모리 · 디스크 · 해상도 · 품질까지다. 클럭 · 캐시 · GPU 성능은 흉내 내지 않으니 LowEnd 의 프레임 시간이 Native 보다 짧게 나올 수 있다.
- 야간 잡(`load.yml`) 첫 실행(38813564584)은 성공했다 — 리눅스에서 OnlineLoadBot 이 서버 키트를 올려 connect × Mid, replication × Mid · LowEnd 가 통과했다(오류율 0, tick p99 약 15 ms).
  러너에는 기준선이 없어 알림만 나오므로 기준선 비교는 이 PC 에서만 본다.
- 첫 측정에서는 모든 프로필이 GT · RT · GPU p99 예산의 1/10 안팎이라 자동 조정의 이득이 보이지 않았다.

## 2. 단위

| 단위 | 내용 | 건드릴 폴더 | 선행 | 검증 | 측정 |
|---|---|---|---|---|---|
| Q1 | 쿠킹 팩을 올린 Release(`--cooked-dir`)로 부하 시나리오를 돌리는 길을 만들고, 그 기동에서 Shipping 에 없는 일 셋(섀도 복사 · 낱개 매니페스트 · 셰이더 원본 해시)을 빼고 잰다. 매니페스트는 한 파일로 묶는 안을 낸다 | `Scripts/`, `Test/`, `cmake/` | 없음 | `-L load` 의 LowEnd 기동 열기 수 전후 | 필요합니다. 이 PC 의 Release 에서 혼자 |
| Q2 | 13 ~ 20 ms 한 프레임이 어느 구간인지 Tracy(`gv_profileTracePath`)로 그 프레임을 본다 | `Source/Engine/Profiling/` | 없음 | 트레이스에서 구간 이름이 나옴 | 필요합니다. Release 에서 혼자 |
| Q4 | 실제 저사양 기기로 한 번 재서 흉내(LowEnd)와의 차이를 [검증과 측정](../08_Verification.md) 에 적는다 | `docs/` | 실기 | 같은 시나리오의 기기 값과 흉내 값 | 필요합니다. 실기 |

## 3. 착수 조건과 막힌 것

- Q3(자동 렌더 배율)은 들어갔다: `RenderScaleAdjuster`(순수 계산, `RenderScaleAdjusterTest`)를 `RenderPacketBuilder` 가 프레임 델타로 먹여 주 시점 배율에 곱한다. 기본은 꺼짐(`-gv_autoRenderScale=1` 로 켠다, 예산 `gv_autoRenderScaleBudgetMs`, 하한 `gv_autoRenderScaleMin`). 틀린 설정은 오류를 찍고 끈다. 실제 효과는 매니페스트 id `load-auto-render-scale` 이 잰다(모든 프로필이 예산의 1/10 안팎이면 전환이 없으니 예산을 낮춰 강제로 깨 본다). 기본을 켤지는 그 측정 뒤에 정한다.
- Q4 는 실제 저사양 기기가 있어야 한다(외부 조건).

## 4. 사용자 결정

없습니다.
