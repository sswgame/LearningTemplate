# 클러스터드 라이팅 구현 단위

> 상태: 구현중 · 선행: 없음 · 영역: graphics · 진행: 5/6

화면 픽셀마다 씬의 모든 라이트를 도는 라이팅을, 화면을 격자(클러스터)로 나눠 격자 칸마다 영향을 주는 라이트만 도는 방식으로 바꾼다. 단위 다섯은 코드가 들어갔고 **셰이더 재베이크 · 빌드 확인 전**이며, 남은 것은 Release 측정(S3)이다.

| 단위 | 내용 | 상태 |
|---|---|---|
| S0 | 키 라이트 폴백 제거, 라이트 순회 커서 `LightCursor::begin` · `next` | 들어감 |
| S1 | 빛 목록 형식 `LightGrid`, CPU 생산자, 셰이더 소비, `gv_lightCulling`(`LightGridTest`) | 들어감, 골든 비교는 S2 테스트로 갈음 · 재베이크 뒤 확인 |
| S2 | GPU 컴퓨트 생산자 `lightgrid.hlsl`(그룹 하나 = 뷰 하나), 전용 SRV 슬롯 t12, Shipping 은 GPU 고정 | 들어감, 재베이크 · `LightCullingProducersDrawTheSameImage` · `ShaderBindingValidatorTest` 확인 전 |
| S3 | Release 측정, 클러스터 크기 조정(16×9×24 에서 시작) | 측정 남음 |
| S4 | Light Complexity 보기 모드(`-gv_viewMode=LightComplexity`) | 들어감, 재베이크 뒤 `ViewModesProduceDistinctPictures` 확인 |
| S5 | 라이트 프로브 볼륨을 라이트 목록 원소(`SW_LIGHT_TYPE_PROBE_VOLUME`)로 넣어 경계 구로 클러스터에 배치 | 들어감 |

## 배경

디퍼드 라이팅과 포워드 셰이더(`lighting.hlsli` `Lighting::shadeLights`, `toon.hlsl`)는 모든 픽셀에서 모든 라이트를 돈다. 비용이 라이트 수에 비례하고 간접 조명 프로브와 파티클이 같은 루프에 들어오면 더 커진다. 언리얼의 클러스터드 디퍼드, 유니티 Forward+ 가 같은 방법으로 푼다.

## 설계 메모

- 형식 · 위치 · 범위 식의 정본은 `bindingslots.hlsli` 10절의 `SW_LIGHT_GRID_*`, 생산자 둘의 계약은 `LightGrid.h` · `lightgrid.hlsl` 헤더 주석이다.
- 머티리얼 에뮬 슬롯은 t12 를 비키려고 t16..t31 로 옮겼다.
- Light Complexity 는 표면(`ViewMode::computeColor`)에서 그리드를 세므로 디퍼드도 G버퍼가 색을 넘긴다.
- 파티클 라이트는 V6 이 점광 원소로 라이트 목록에 넣으면 그리드가 그대로 받고, 반사 프로브는 P4 가 같은 방식으로 넣는다.

## S3 — 남은 일

- 첫 일: Release 에서 혼자 `py -3 -m Scripts perf-compare --app build/Ninja-Release/Bin/App.exe --cases lightCullingCpu16 lightCullingGpu16 lightCullingCpu128 lightCullingGpu128 lightCullingCpu1024 lightCullingGpu1024 --runs 5` 를 돌린다.
- 케이스는 디퍼드 + 큐브 4000 + 점광 · 스폿 N 개이고 A 는 그리드 없음, B 는 CPU · GPU 생산자다. 구간은 `GPU.Frame` · `GPU.Shading` · `GPU.Compute` · `RT.Frame`. 큰 손 씬은 `MakeStressScene.py --count 4000 --point-lights <N>`.
- 클러스터 크기(X, Y, 깊이 조각)는 측정 뒤에 정한다.
