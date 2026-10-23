# GPU 로 시뮬레이션하는 방출기의 파티클 라이트 (2026-10-24)

> 상태: 구현대기 · 선행: ClusteredLighting · 영역: graphics · 옛번호: 없음 · 진행: 0/5

후보 `VfxGPULights` 를 계획으로 올린 문서입니다. 수요(라이트를 내는 대량 입자)가 생기면 L1 부터 착수합니다. 그때까지는 [Vfx](Vfx.md) 의 "GPU 본체 + 라이트 CPU 방출기" 패턴으로 풉니다.

## 1. 지금 상태

- 파티클 라이트(`VfxLightDef`)는 게임 스레드의 씬 라이트 목록에 점광을 붙입니다. 그래서 CPU 시뮬레이션 전용이고(`VfxLightDef::getSupportedTargets`), 깊이 충돌(GPU 전용)과 한 방출기에 둘 수 없어 로드가 실패합니다.
- 라이트 버퍼(`g_SwLights`)는 프레임당 `SW_MAX_FRAME_LIGHT`(1024)개이고 개수를 CPU 가 압니다. 라이트 그리드 생산자 둘은 그 개수를 뷰 입력(`GridView::lightCount`)으로 받습니다. CPU 생산자(`gv_lightCulling=1`)는 CPU 가 만든 목록만 보고, GPU 생산자(`lightgrid.hlsl`, `gv_lightCulling=2`, Shipping 은 GPU 고정)는 라이트 상한을 그룹 하나가 돕니다.
- 언리얼 Niagara 의 라이트 렌더러도 CPU 시뮬레이션 전용입니다. GPU 라이트는 이 엔진의 확장입니다.

## 2. 정할 것 (제안)

1. **라이트 버퍼를 두 구간으로 나눈다.** CPU 구간은 지금과 같고, GPU 구간은 고정 위치(`kGpuLightBase`)에서 시작해 최대 `kMaxGpuLight` 개다. 입자 수는 시뮬레이션이 이미 가진 살아 있는 수 카운터(`MeshInstanceWriter::GPU` 가 읽는 것과 같은 형식)에 있어 CPU 가 모른다.
2. **그리드 생산자가 카운터를 읽는다.** `GridView::lightCount` 는 CPU 구간 수만 주고, GPU 구간 수는 카운터에서 읽는다. 라이트 상한 순회는 그룹 하나가 도는 그대로 둔다(`kGpuThreadCount * 4 >= SW_MAX_FRAME_LIGHT` 정적 검사에 GPU 구간을 포함시킨다).
3. **GPU 라이트가 있으면 그리드가 필수다.** 그리드 없는 경로(조명 식이 라이트를 처음부터 끝까지 도는 경로)는 GPU 구간을 보지 못한다. CPU 생산자(`gv_lightCulling=1`)와 그리드 끔(`0`)을 명시한 채 GPU 라이트가 켜지면 **조용히 GPU 생산자로 바꾸지 않고 오류를 남기고 GPU 라이트를 그리지 않는다**(폴백 금지). 기본값(`gv_lightCulling` 2)이 GPU 생산자라 기본은 그대로 동작한다.
4. **라이트는 점광만, 그림자 없음.** 입자당 라이트 하나, 방출기 `_light._maxLights` 가 상한이다. 라이트를 낼 입자는 생성 번호의 간격(stride)으로 고른다 — 압축이 안정이라 같은 입자가 같은 라이트를 유지한다.
5. **라이트 수 상한의 몫을 나눈다.** CPU 구간 + GPU 구간이 `SW_MAX_FRAME_LIGHT` 를 넘지 않게 `kMaxGpuLight` 를 정한다(1024 의 4분의 1 안팎, 측정 뒤 확정).

## 3. 단위

| 단위 | 내용 | 선행 |
|---|---|---|
| L1 | 결정 확정과 라이트 버퍼 구간 형식(`GPULightBuffer` 의 구간 상수 · 정적 검사 · 생산자 선택 규칙과 오류 — C++) | 없음 |
| L2 | `lightgrid.hlsl` 이 GPU 구간 카운터를 읽도록(뷰 입력에 카운터 위치 추가, 셰이더) — 셰이더 소스라 패치로 넘긴다 | L1 |
| L3 | `vfxsimulate` 컴퓨트가 살아 있는 입자에서 `LightData` 를 GPU 구간에 쓴다(셰이더) | L1 |
| L4 | `VfxLightDef::getSupportedTargets` 가 GPU 를 허용하고, 깊이 충돌 + 라이트 방출기 로드 테스트 · CPU 생산자 조합 오류 테스트 | L2, L3 |
| L5 | 시나리오(깊이 충돌 + 빛나는 입자 대량)로 네 백엔드 화면 확인과 Release 측정(라이트 그리드 GPU 시간) | L4 |

셰이더 소스를 바꾸는 단위(L2 · L3)는 워크트리에서 커밋하지 못하고(`CheckShaderStamps` 가 쿠킹 바이너리와 함께 커밋하라고 막는다) 패치로 넘겨 통합에서 적용 · 쿠킹한다.
`ClusteredLighting` S5 의 "파티클 라이트는 Vfx V6 가 점광 원소로 라이트 목록에 넣는다" 와 이 문서의 L1 은 같은 라이트 목록 형식을 건드리므로 함께 본다.
