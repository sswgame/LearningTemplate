# 파티클 · VFX 구현 단위 (`D2-11`)

> 상태: 구현중 · 선행: 없음 · 영역: graphics · 옛번호: D2-11 · 진행: 7/8

결정은 [결정 기록](../09_Decisions.md) 5-12 의 `D2-11` 행들과 옛 `D2-11` 본문의 사용자 결정이 원본입니다. 이 문서는 그 결정을 구현 단위로 나눈 계획과 단위마다의 계약을 적습니다.
단위가 끝나면 그 단위의 절을 지우고, 남는 교훈은 코드의 헤더 주석으로 옮깁니다.

## 1. 정한 것

- 시뮬레이션은 CPU 와 GPU 를 둘 다 만들고, 대상은 방출기마다 정합니다(Niagara 의 Sim Target). CPU 전용 모듈(메시 · 충돌 · 라이트)을 쓰는 방출기는 모든 빌드에서 CPU 이고, 빌보드 · 띠는 Shipping 에서 GPU 로 고정, 개발 빌드에서 `gv_vfxSimulation` 으로 고릅니다. Shipping 에는 변수가 없습니다(결정 기록 5-12 `D2-11`, 2026-10-22 에 "Shipping 은 CPU 경로 없음" 을 바꿨다).
- 데이터 형식(`*.vfx.xml`)과 렌더 입력 형식은 하나이고, 두 시뮬레이션이 같은 형식을 냅니다.
- 첫 모듈 형식은 고정 모듈과 값 커브(`FloatCurve`)입니다. 다형 목록과 그래프는 뒤로 미룹니다.
- 방출기 하나가 투명 정렬 후보 하나입니다. 입자 단위 정렬은 하지 않습니다.
- 서버는 시뮬레이션하지 않습니다(외형 전용). 2D 픽셀 퍼펙트 입자는 P1 에 넣지 않습니다.

## 2. 단위

| 단위 | 내용 | 선행 |
|---|---|---|
| V0 | 데이터 형식: `VfxEffectDef`(방출기 목록), `VfxEmitterDef`(생성률, 버스트, 수명, 초기 속도 범위, 중력, 크기와 색의 `FloatCurve`, 텍스처, 블렌드), 리플렉션 로드와 검증(필수 값이 없으면 로드 실패) — **들어감**(`VfxEffectDefTest` · 리소스 종류 `vfx`) | 없음 |
| V1 | CPU 시뮬레이션 `VfxCPUSimulation`: 고정 크기 풀, 결정적 난수(방출기 씨앗), 단계 하나 = 생성, 적분, 수명 제거(뒤쪽과 바꾸기). 출력은 렌더 입력 `GPUParticle`(float4 셋: 위치와 크기, 색, 회전과 수명 비율) — **들어감**(`VfxCPUSimulationTest`) | V0 |
| V2 | 렌더 경로: 방출기마다 입자 그리기 하나(`GPUParticleBatch`, 스냅샷의 `_listParticleBatch`)를 투명 패스가 빌보드로 그립니다(`VfxParticleRenderer`, `particle.hlsl` — `SV_VertexID` 와 패스 전용 슬롯 t11 의 입자 버퍼) — **들어감**(`VfxComponentTest`, 시나리오 `editor/vfxpreview` 네 백엔드) | V1 |
| V3 | `VfxComponent`(효과 경로, 재생, 정지, 씨앗)와 에디터 미리보기 창(`VfxPreviewPanel` — 보기 전용, 효과 편집은 카탈로그 편집기) — **들어감**(표본 효과 `engine/vfx/sparks.vfx.xml`) | V2 |
| V4 | GPU 시뮬레이션 `vfxsimulate.hlsl`(그룹 하나 = 방출기 하나, 단계마다 생성 → 적분 → 안정 압축, 간접 드로우 인자)과 방출기마다의 시뮬레이션 대상(`VfxSimulationTarget` — CPU 전용 모듈이면 모든 빌드에서 CPU, 결정 기록 5-12 `D2-11` 2026-10-22) — **들어감**(`VfxEmitterClock` 이 두 시뮬레이션의 생성 일정, 게임 스레드 `VfxGPUEmitter` → 요청 `VfxGPUEmitterRequest` → 렌더 스레드 `VfxGPUSimulation`, 패스 종류 `VfxSimulation`; `VfxEffectDefTest.CPUOnlyModulesPinTheCPU` · `VfxEmitterClockTest.ScheduleMatchesTheCPUSimulation` · `VfxGPUEmitterTest.FinishesAfterTheLongestLifetimeAndRestartsWithANewRun` · `VfxComponentTest.EmittersPickTheirSimulationTarget`, `RenderPassGPUTest.ParticlesDrawInTheTransparentPass` 가 기본 GPU 경로를 그린다. **빌드 · 셰이더 컴파일 · 쿠킹 · 화면 확인 전**). 계약에서 바꾼 것: 생존 수는 원자적 카운터가 아니라 그룹 공유 메모리 접두사 합의 안정 압축이다 — 버퍼가 늘 태어난 순서라 띠가 정렬 없이 잇는다. DX11 의 "복사 단계" 는 필요 없다 — 입자 상태 · 렌더 입력은 구조 버퍼, 간접 인자는 따로 둔 버퍼(DX11 은 RAW UAV, 셰이더가 `#if defined( DX11 )` 로 바이트 주소에 쓴다)라 같은 디스패치가 쓴다. GL `drawIndirect` 는 메시 없는 드로우를 장치 공용 VAO 로 그리게 고쳤다. 남은 한계: GPU 풀이 차서 버린 입자는 경고가 없다(CPU 는 경고). 렌더 스레드가 늦으면 디스패치마다 단계 상한(`SW_VFX_MAX_STEPS_PER_DISPATCH`)까지만 따라잡는다 | V2, `D2-6.3` 슬롯 지도 |
| V5 | CPU 와 GPU 비교 테스트: 같은 씨앗, 같은 단계 수에서 입자 수가 같고 위치가 허용 오차 안입니다(hostgpu) | V4 |
| V6 | 메시 입자(`VfxRenderMode::Mesh` — 메시 인스턴스 배치), 리본(`Ribbon` — 태어난 순서의 띠, `particle.hlsl`), 충돌(`VfxCollisionDef` — 물리 레이캐스트, `IVfxCollisionScene`), 파티클 라이트(`VfxLightDef` — 씬 라이트 목록 · CPU 라이트 그리드) — **들어감**(`VfxEffectDefTest.RenderModeFieldsAreChecked` · `VfxCPUSimulationTest.RibbonParticlesAreWrittenInBirthOrder` · `CollisionBouncesOrKills` · `VfxComponentTest.TrailSampleDrivesRibbonMeshAndLights`, 표본 `engine/vfx/trail.vfx.xml`, 빌드 · 화면 확인 전). 띠의 셰이더 경로(`particle.hlsl` 플래그 칸)는 커밋 훅(`CheckShaderStamps`)이 쿠킹한 바이너리와 함께 커밋하라고 막아 **아직 커밋되지 않았다** — 빌드 뒤 `App --cook-shaders` 와 함께 넣는다(그 전까지 띠는 더하기 빌보드로 잘못 그려진다). GPU 라이트 그리드에 넣는 일은 `ClusteredLighting` S5 | V3 |
| V7 | 메시 투명과 입자의 교차 정렬: 방출기를 메시 투명 배치와 한 순서로 섞고(프리미티브 단위, 결정 기록 5-12 `D2-11`), 추가 뷰는 그 뷰의 순서로 그립니다(`GPUTransparentParticleSlot`) — **들어감**(`GPUTransparentSorterTest.ParticlePlacementFollowsLayerAndDepth` · `VfxComponentTest.ParticlesInterleaveWithTransparentMeshes`, 빌드 · 화면 확인 전). 추가 뷰는 배치 순서와 입자 자리를 한 쌍으로만 쓴다(`FrameRenderer::applyViewTransparentOrder` — 뷰 순서에 주 시점 자리를 섞지 않는다). 패스 끝에 붙여 그리는 다른 경로는 정렬 대상이 아니다: 캔버스 패스는 화면 HUD(마지막이 의도), 월드 UI(`WidgetSpace::World`)는 렌더 텍스처를 메시에 붙여 메시 투명 배치로 정렬되고, `MeshOutline` 은 모든 파이프라인에서 투명 앞이며 불투명 배치만, `Outline` · 디버그 도형은 선택 · 에디터 겹침이다 | V2 |

### V4 남은 확인

- **셰이더 커밋은 쿠킹과 함께.** `vfxsimulate.hlsl` · `vfxsimulate.hlsli` 는 커밋 훅(`CheckShaderStamps`)이 네 백엔드의 쿠킹 바이너리와 함께 커밋하라고 막는다. 빌드를 허락받은 작업자가 `App --cook-shaders` 뒤 함께 넣는다. 그 전에는 엔진 기본 자산의 `_shaderVfxSimulation` 이 가리키는 파일이 없어 GPU 방출기가 그려지지 않는다(오류 한 번).
- FXC(DX11 cs_5_0)가 동기화를 낀 반복(덩어리 · 접두사 합)을 받는지, 네 백엔드에서 `ParticlesDrawInTheTransparentPass` 와 `editor/vfxpreview` 시나리오를 본다. `gv_vfxSimulation=CPU` 와 그림이 같아야 한다(V5 가 수로 견준다).
