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
- 10-16 이후 야간 잡은 한 번도 통과하지 못했다(2026-10-27 정리). 원인 넷: ① 구성 · 빌드(10-16 구성, 10-24 리눅스 컴파일 — `f1a19dd17` 이 고침),
  ② `EditorTest_Load` 합성 리그 벤치(10-22 · 23 — `d0559bf93` 이 고침), ③ connect × Mid 의 OnlineLoadBot(10-19 ~ 23 SIGSEGV, 10-25 매치메이킹 키트의 `mode 'solo:2x1'` 거절 — NEXT 2번 작업자 몫),
  ④ replication × Mid · LowEnd 의 `tick_p99_us` 가 러너에서 17.2 ~ 17.9 ms 로 예산 16.7 ms 를 넘음(10-22 부터, 첫 실행은 약 15 ms, 이 PC 기준선은 약 10 ms).
  ④ 는 제품 목표(이 PC 기준)를 측정 기계가 아닌 러너에서 판정한 구조 결함이라, 야간 묶음은 `--report-time-budgets` 로 시간 예산(`_us` · `_ms` · `hitch.*`)을 알림(`::notice`)으로만 올리고
  정확성(수렴 · 오류율) · 메모리 예산과 실행 실패만 판정한다. 시간 회귀는 이 PC 의 기준선(`load suite baseline`)이 본다.
- 첫 측정에서는 모든 프로필이 GT · RT · GPU p99 예산의 1/10 안팎이라 자동 조정의 이득이 보이지 않았다.

## 2. 단위

| 단위 | 내용 | 건드릴 폴더 | 선행 | 검증 | 측정 |
|---|---|---|---|---|---|
| Q1 | 쿠킹 팩을 올린 Release(`--cooked-dir`)로 부하 시나리오를 돌리는 길을 만들고, 그 기동에서 Shipping 에 없는 일 셋(섀도 복사 · 낱개 매니페스트 · 셰이더 원본 해시)을 빼고 잰다. 매니페스트는 한 파일로 묶는 안을 낸다 | `Scripts/`, `Test/`, `cmake/` | 없음 | `-L load` 의 LowEnd 기동 열기 수 전후 | 필요합니다. 이 PC 의 Release 에서 혼자 |
| Q2 | 13 ~ 20 ms 한 프레임이 어느 구간인지 Tracy(`gv_profileTracePath`)로 그 프레임을 본다 | `Source/Engine/Profiling/` | 없음 | 트레이스에서 구간 이름이 나옴 | 필요합니다. Release 에서 혼자 |
| Q4 | 실제 저사양 기기로 한 번 재서 흉내(LowEnd)와의 차이를 [검증과 측정](../08_Verification.md) 에 적는다 | `docs/` | 실기 | 같은 시나리오의 기기 값과 흉내 값 | 필요합니다. 실기 |

## 3. 착수 조건과 막힌 것

- Q3(자동 렌더 배율)은 들어갔다: `RenderScaleAdjuster`(순수 계산, `RenderScaleAdjusterTest`)를 `RenderPacketBuilder` 가 프레임 델타로 먹여 화면에 나가는 뷰(주 시점 · 화면 사각형, `RenderViewCollector` 의 `autoScale`)의 배율에 곱한다. 렌더 텍스처 뷰는 사용자 렌더 스케일처럼 자기 크기가 데이터라 곱하지 않는다. 씬 직접 경로(`FrameRenderer::execute`, 테스트 · 초상화 베이크)는 프레임 시계가 없고 출력이 결정적이어야 해 일부러 1 을 넘긴다. 호스트 타깃 뷰(`appendHostView`, 에디터 씬 뷰)도 같은 규칙을 어기지 않는다: 이미지를 타깃 크기 그대로 보여 주므로 `gv_renderScale` · 자동 배율을 곱하지 않는다(헤더에 적었고 `DirectPathAndHostViewSkipAutoScale` 이 지킨다), 직접 경로의 1 은 `kDirectPathAutoScale` 상수다. Present 업샘플이 없는 뷰(호스트 타깃 · 렌더 텍스처)는 카메라 출력 배율이 1 이 아니면 풀만 작아지고 이미지가 늘어나므로, `makeSettings` 가 배율을 아예 옮기지 않고 두 입구가 `hasUnitResolutionScale` 로 오류를 남기며 그 뷰를 거절한다(조용히 1 로 바꾸지 않는다, `UnscaledViewKindsRejectResolutionScale` 이 지킨다). 프레임 예산 때문에 해상도를 조용히 낮추는 다른 길은 `Source/Engine/Renderer` 에 없다(뷰 예산은 뷰를 미룰 뿐 해상도를 안 낮춘다). 기본은 꺼짐(`-gv_autoRenderScale=1` 로 켠다, 예산 `gv_autoRenderScaleBudgetMs`, 하한 `gv_autoRenderScaleMin`). 틀린 설정은 오류를 찍고 끈다. 실제 효과는 매니페스트 id `load-auto-render-scale` 이 잰다(모든 프로필이 예산의 1/10 안팎이면 전환이 없으니 예산을 낮춰 강제로 깨 본다). 기본을 켤지는 그 측정 뒤에 정한다.
- Q4 는 실제 저사양 기기가 있어야 한다(외부 조건).

## 4. 사용자 결정

없습니다.
