# GPU 로 시뮬레이션하는 방출기의 파티클 라이트 (2026-10-24)

> 상태: 측정대기 · 선행: ClusteredLighting · 영역: graphics · 옛번호: 없음 · 진행: 5/5

후보 `VfxGPULights` 를 계획으로 올린 문서입니다. 코드(L1 ~ L4)와 시나리오(L5)는 들어갔고, 셰이더 소스(L3)는 패치 파일로 남았습니다(3절). 네 백엔드 화면 확인과 Release 측정은 통합이 합니다.

## 1. 지금 상태

- 파티클 라이트(`VfxLightDef`)는 CPU 방출기와 GPU 방출기 모두 낸다. CPU 방출기는 게임 스레드가 씬 라이트 목록에 점광을 붙이고(`collectSceneLights`), GPU 방출기는 시뮬레이션 컴퓨트(`vfxsimulate.hlsl`)가 라이트 버퍼의 **GPU 구간**에 쓴다.
  옛 규칙(빛을 내면 CPU 전용)은 깊이 충돌(GPU 전용)과 한 방출기에 둘 수 없었다. 이제 둘이 함께 있으면 GPU 로 돈다.
- 언리얼 Niagara 의 라이트 렌더러는 CPU 시뮬레이션 전용입니다. GPU 라이트는 이 엔진의 확장입니다.

## 2. 구현에서 정한 것

처음 제안(라이트 버퍼의 고정 위치 구간 + 그리드 생산자가 카운터를 읽는다)에서 구현이 더 단순한 형태로 바뀐 곳이 있습니다. 바꾼 이유까지 적습니다.

1. **라이트 버퍼는 [CPU 라이트][GPU 구간][그림자 타일] 순서다.** GPU 구간은 CPU 라이트 바로 뒤에 프레임마다 정해진 위치(`GPULightBuffer::getGpuLightBase`)에서 시작하고 길이는 방출기들이 요청한 슬롯의 합이다
   (`VfxGPUSimulation::assignLightRanges`, 상한 `GPULightBuffer::kMaxGpuLight` = 프레임 라이트 상한의 4분의 1, 측정 뒤 확정). 고정 위치로 두면 버퍼가 늘 상한 크기여야 하고 조명 커서의 범위 검사(`lightIndex < g_SwLightCount`)에도 구간을 따로 알려야 했다.
   구간이 CPU 라이트 바로 뒤라서 셰이더가 보는 라이트 수(`g_SwLightCount`)가 CPU + GPU 구간이다 — 조명 커서와 그림자 타일 번호(`g_SwLightCount` + 상대 번호)는 그대로 맞는다. CPU 라이트 수는 구간만큼 줄여 합이 `kMaxFrameLight` 를 넘지 않게 한다.
2. **그리드 생산자(`lightgrid.hlsl`)는 바뀌지 않는다.** 구간의 슬롯은 항상 유효한 `LightData` 다: 컴퓨트가 살아 있는 입자의 빛을 쓰고, 입자가 모자란 슬롯과 건너뛴 방출기의 슬롯은 **죽은 라이트**(점광, 반경 0)다.
   죽은 라이트는 `classifyLight` 에서 범위 밖이라 그리드에 들지 않고, 그리드 없는 경로에서도 감쇠 0 이라 기여가 없다. 그래서 살아 있는 수 카운터를 생산자에 넘길 필요가 없다 — 옛 L2(`lightgrid.hlsl` 이 카운터를 읽는다)는 코드 변경 없이 풀렸다.
   `GPULightBuffer::update` 가 구간을 죽은 라이트로 채워 올리므로, 컴퓨트가 쓰지 못한 슬롯이 지난 프레임의 낡은 빛으로 남지 않는다.
3. **GPU 라이트는 GPU 생산자(`gv_lightCulling` 2, Shipping 은 고정)만 받는다.** CPU 생산자는 CPU 가 가진 목록만 그리드에 넣으므로 GPU 구간을 보지 못한다. 그래서 `GPULightBuffer::selectGpuLightSlotCount` 가 다른 생산자에는 0 을 주고 오류를 한 번 남긴 뒤 빛을 그리지 않는다.
   **조용히 GPU 생산자로 바꾸지 않는다**(폴백 금지) — 생산자는 데이터와 빌드가 정했다.
4. **라이트는 점광만, 그림자 없음.** 슬롯 i 는 살아 있는 입자 i 의 빛이다(앞의 `_maxLights` 개 — CPU 방출기와 같은 규칙). 색은 입자 색, 세기는 방출기 세기 × 입자 알파, 반경은 방출기 배율을 곱해 올린다.
5. **빛이 있는 방출기는 밀린 단계가 없어도 매 프레임 디스패치한다.** 라이트는 월드 좌표로 쓰이고 구간의 위치(CPU 라이트 수)도 프레임마다 바뀌어 다시 써야 한다.
   요청이 상한(`kMaxGpuLight`)을 넘는 방출기는 빛이 없고 오류를 한 번 남긴다(`VfxGPUSimulation::prepareFrame`).

## 3. 단위

| 단위 | 내용 | 상태 |
|---|---|---|
| L1 | 결정 확정과 라이트 버퍼 구간 형식(`GPULightBuffer` 의 구간 상수 · 생산자 선택 규칙과 오류 — C++) | **들어감** |
| L2 | 그리드 생산자가 GPU 구간을 보게 한다 | **들어감** — 2절 2번: 구간이 라이트 수에 들어 코드 변경이 필요 없다 |
| L3 | `vfxsimulate` 컴퓨트가 살아 있는 입자에서 `LightData` 를 GPU 구간에 쓴다(셰이더, u3 와 라이트 행) | **들어감** — 셰이더 소스는 통합(integE)이 패치를 적용하고 네 백엔드로 구웠다 |
| L4 | `VfxLightDef::getSupportedTargets` 가 GPU 를 허용하고, 깊이 충돌 + 라이트 방출기 로드 테스트 · 생산자 규칙 테스트 · 슬롯 나누기 테스트 | **들어감** |
| L5 | 시나리오(깊이 충돌 + 빛나는 입자)로 네 백엔드 화면 확인과 Release 측정(라이트 그리드 GPU 시간) | 시나리오 `game/empty/automation/vfxgpulights` 있음 · 실행 대기(매니페스트 id `vfx-gpu-lights-scenario`) |

셰이더 소스를 바꾸는 단위(L3)는 워크트리에서 커밋하지 못한다(커밋 훅이 쿠킹 바이너리와 함께 커밋하라고 막는다). 패치 파일 [ShaderPatches](ShaderPatches/README.md) 로 남겼고 통합(integE)이 적용해 `App --cook-shaders` 로 구웠다.
C++(`VfxGPUSimulation` 의 라이트 행 · `SW_VFX_PARAM_ROW_LIGHT`)는 패치의 `vfxsimulate.hlsli` 를 읽으므로 **C++ 와 패치를 함께** 적용해야 빌드된다.
`ClusteredLighting` S5 의 "파티클 라이트는 Vfx V6 가 점광 원소로 라이트 목록에 넣는다" 와 이 문서의 구간은 같은 라이트 목록 형식을 건드린다.

## 4. 통합에서 돌릴 확인

**남은 확인(통합에서 확인):** 매니페스트 id `vfx-gpu-lights-tests`, `vfx-gpu-lights-scenario`, `vfx-gpu-lights-measure` — 시나리오는 네 백엔드이고 문턱 `atLeast` 를 실측으로 정한다, 측정은 슬롯 32 / 128 / 256 의 컴퓨트 프리패스 GPU 시간. 확인이 끝나면 통합 담당이 이 문서를 닫는다.
