# 클러스터드 라이팅 구현 단위 (`D2-13`)

> 상태: 구현중 · 선행: 없음 · 영역: graphics · 옛번호: D2-13 · 진행: 4/6

결정은 [결정 기록](../09_Decisions.md) 5-12 의 `D2-13` 행이 원본입니다. 이 문서는 그 결정을 구현 단위로 나눈 계획과 단위마다의 계약을 적습니다.
단위가 끝나면 그 단위의 절을 지우고, 남는 교훈은 코드의 헤더 주석으로 옮깁니다.

## 1. 지금 상태

디퍼드 라이팅과 포워드 셰이더는 화면의 모든 픽셀에서 씬의 모든 라이트를 돕니다(`lighting.hlsli` `swShadeLights`, `toon.hlsl`).
라이트 수에 비례해 비용이 커지고, 간접 조명 프로브(`D2-12`)와 파티클(`D2-11`)이 같은 루프에 들어오면 더 커집니다.
언리얼의 클러스터드 디퍼드, 유니티 Forward+ 와 Clustered 렌더러가 같은 문제를 같은 방법으로 풉니다.

## 2. 단위

| 단위 | 내용 | 선행 |
|---|---|---|
| S0 | 키 라이트 폴백 제거, 라이트 순회 커서 API(`swBeginLights`, `swNextLight`) — **들어감**(`9c24255b6`) | 없음 |
| S1 | 빛 목록 형식 `LightGrid`, CPU 생산자, 셰이더 소비(커서 안쪽), `gv_lightCulling` — **들어감**(`9c24255b6`, `LightGridTest` 있음). 골든 바이트 비교(`gv_lightCulling` 0 · 1 · 2)는 S2 의 `RenderPassGPUTest.LightCullingProducersDrawTheSameImage` 로 갈음한다 — **확인 필요**(재베이크 뒤) | S0 |
| S2 | GPU 컴퓨트 생산자(`lightgrid.hlsl`, 그룹 하나 = 뷰 하나의 단일 디스패치), 전용 SRV 슬롯 t12(`SW_SLOT_LIGHT_GRID_SRV`, 머티리얼 에뮬 슬롯은 t16..t31 로), Shipping 은 GPU 고정 — **들어감**. 셰이더 재베이크 · `LightCullingProducersDrawTheSameImage` · `ShaderBindingValidatorTest` 는 **확인 필요** | S1 |
| S3 | Release 측정과 클러스터 크기 조정(16×9×24 에서 시작), 라이트 1024 개 벤치 | S1 |
| S4 | Light Complexity 보기 모드(`RenderViewMode::LightComplexity`, `-gv_viewMode=10`) — **들어감**. 표면(`swComputeViewModeColor`)에서 그리드를 세므로 디퍼드도 G버퍼가 색을 넘긴다. 스크린샷 확인 · `ViewModesProduceDistinctPictures` 는 **확인 필요**(재베이크 뒤) | S1 |
| S5 | 프로브(`D2-12`)와 파티클 라이트(`D2-11`)를 같은 그리드에 넣습니다 | S1, 각 항목의 P1 |

형식 · 위치 · 범위 식의 정본은 `bindingslots.hlsli` 10절의 `SW_LIGHT_GRID_*` 이고, 생산자 둘의 계약은 `LightGrid.h` · `lightgrid.hlsl` 헤더 주석에 있습니다.

### S3 — 측정

- 측정 씬은 `py -3 Scripts/dev/MakeStressScene.py --count 4000 --point-lights <N>` 이 만듭니다(점광 수 매개변수는 들어감). 라이트 16 · 128 · 1024 개에서 `GPU.DeferredLighting` p50 · p99 를 `gv_lightCulling` 0 · 1 · 2 로 측정합니다(Release) — **남음**.
- 클러스터 크기(X, Y, 깊이 조각)는 측정 뒤에 정하고 결정 기록에 측정값과 함께 남깁니다.

### S5 — 프로브와 파티클 라이트

- 반사 프로브 볼륨과 라이트 프로브 격자는 클러스터 목록에 같은 형식의 항목으로 들어갑니다. 항목 종류는 라이트 원소의 타입 값으로 가릅니다.
