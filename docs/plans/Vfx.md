# 파티클 · VFX 구현 단위 (`D2-11`)

> 상태: 구현중 · 선행: 없음 · 영역: graphics · 옛번호: D2-11 · 진행: 12/12

결정은 [결정 기록](../09_Decisions.md) 5-12 의 `D2-11` 행들과 옛 `D2-11` 본문의 사용자 결정이 원본입니다. 이 문서는 그 결정을 구현 단위로 나눈 계획과 단위마다의 계약을 적습니다.
단위가 끝나면 그 단위의 절을 지우고, 남는 교훈은 코드의 헤더 주석으로 옮깁니다.

## 1. 정한 것

- 시뮬레이션은 CPU 와 GPU 를 둘 다 만들고, 대상은 방출기마다 정합니다(Niagara 의 Sim Target). 모듈마다 돌 수 있는 곳을 선언하고 방출기의 대상은 그 교집합입니다(비면 로드 오류). 물리 충돌 · 라이트는 CPU, 깊이 충돌은 GPU, 나머지(빌보드 · 띠 · 메시)는 둘 다이고, 둘 다 되는 방출기만 Shipping 에서 GPU 로 고정, 개발 빌드에서 `gv_vfxSimulation` 으로 고릅니다. Shipping 에는 변수가 없습니다(결정 기록 5-12 `D2-11`, 2026-10-22 에 "Shipping 은 CPU 경로 없음" 과 "CPU 전용 모듈 목록" 을 바꿨다).
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
| V4 | GPU 시뮬레이션 `vfxsimulate.hlsl`(그룹 하나 = 방출기 하나, 단계마다 생성 → 적분 → 안정 압축, 간접 드로우 인자)과 방출기마다의 시뮬레이션 대상 — **들어감**(`VfxEmitterClock` 이 두 시뮬레이션의 생성 일정, 게임 스레드 `VfxGPUEmitter` → 요청 `VfxGPUEmitterRequest` → 렌더 스레드 `VfxGPUSimulation`, 패스 종류 `VfxSimulation`; `VfxEmitterClockTest.ScheduleMatchesTheCPUSimulation` · `VfxGPUEmitterTest.FinishesAfterTheLongestLifetimeAndRestartsWithANewRun` · `VfxComponentTest.EmittersPickTheirSimulationTarget`, `RenderPassGPUTest.ParticlesDrawInTheTransparentPass` 가 기본 GPU 경로를 그린다. **빌드 · 셰이더 컴파일 · 쿠킹 · 화면 확인 전**). 계약에서 바꾼 것: 생존 수는 원자적 카운터가 아니라 그룹 공유 메모리 접두사 합의 안정 압축이다 — 버퍼가 늘 태어난 순서라 띠가 정렬 없이 잇는다. DX11 의 "복사 단계" 는 필요 없다 — 입자 상태 · 렌더 입력은 구조 버퍼, 간접 인자는 따로 둔 버퍼(DX11 은 RAW UAV, 셰이더가 `#if defined( DX11 )` 로 바이트 주소에 쓴다)라 같은 디스패치가 쓴다. GL `drawIndirect` 는 메시 없는 드로우를 장치 공용 VAO 로 그리게 고쳤다. GPU 풀이 차서 버린 입자는 인자 버퍼에 센다(`SW_VFX_ARGS_WORD_DROPPED` — RHI 에 버퍼를 읽어 오는 길이 없어 아직 경고로 올리지 못한다). 풀이 찰 수 있는 데이터(생성률 × 가장 긴 수명 + 버스트 > 풀)는 읽을 때 경고한다. 렌더 스레드가 늦으면 디스패치마다 단계 상한(`SW_VFX_MAX_STEPS_PER_DISPATCH`)까지만 따라잡는다 | V2, `D2-6.3` 슬롯 지도 |
| V5 | CPU 와 GPU 비교 테스트: 같은 씨앗, 같은 단계 수에서 입자 수가 같고 위치가 허용 오차 안입니다(hostgpu) — **들어감**(`RenderPassGPUTest.CPUAndGPUParticleSimulationsDrawTheSameImage` 네 백엔드 — `evaluateCurve` 를 이른 반환 없는 꼴로 고쳐 Release hostgpu 통과, 2026-10-23) | V4 |
| V6 | 메시 입자(`VfxRenderMode::Mesh` — 메시 인스턴스 배치), 리본(`Ribbon` — 태어난 순서의 띠, `particle.hlsl`), 충돌(`VfxCollisionDef` — 물리 레이캐스트, `IVfxCollisionScene`), 파티클 라이트(`VfxLightDef` — 씬 라이트 목록 · CPU 라이트 그리드) — **들어감**(`VfxEffectDefTest.RenderModeFieldsAreChecked` · `VfxCPUSimulationTest.RibbonParticlesAreWrittenInBirthOrder` · `CollisionBouncesOrKills` · `VfxComponentTest.TrailSampleDrivesRibbonMeshAndLights`, 표본 `engine/vfx/trail.vfx.xml`, 빌드 · 화면 확인 전). 띠의 셰이더 경로(`particle.hlsl` 플래그 칸)는 커밋 훅(`CheckShaderStamps`)이 쿠킹한 바이너리와 함께 커밋하라고 막아 **아직 커밋되지 않았다** — 빌드 뒤 `App --cook-shaders` 와 함께 넣는다(그 전까지 띠는 더하기 빌보드로 잘못 그려진다). GPU 라이트 그리드에 넣는 일은 `ClusteredLighting` S5 | V3 |
| V7 | 메시 투명과 입자의 교차 정렬: 방출기를 메시 투명 배치와 한 순서로 섞고(프리미티브 단위, 결정 기록 5-12 `D2-11`), 추가 뷰는 그 뷰의 순서로 그립니다(`GPUTransparentParticleSlot`) — **들어감**(`GPUTransparentSorterTest.ParticlePlacementFollowsLayerAndDepth` · `VfxComponentTest.ParticlesInterleaveWithTransparentMeshes`, 빌드 · 화면 확인 전). 추가 뷰는 배치 순서와 입자 자리를 한 쌍으로만 쓴다(`FrameRenderer::applyViewTransparentOrder` — 뷰 순서에 주 시점 자리를 섞지 않는다). 패스 끝에 붙여 그리는 다른 경로는 정렬 대상이 아니다: 캔버스 패스는 화면 HUD(마지막이 의도), 월드 UI(`WidgetSpace::World`)는 렌더 텍스처를 메시에 붙여 메시 투명 배치로 정렬되고, `MeshOutline` 은 모든 파이프라인에서 투명 앞이며 불투명 배치만, `Outline` · 디버그 도형은 선택 · 에디터 겹침이다 | V2 |
| VfxModuleTargets | 손으로 적은 "CPU 전용 모듈 목록" 대신 모듈마다 대상 집합(`VfxSimulationTargetSet`, 모듈 구조체의 `getSupportedTargets`)을 선언하고 방출기는 교집합을 쓴다(`VfxEmitterDef::computeSupportedTargets` — 칸마다 한 줄인 모듈 표, 비면 로드 오류). 충돌에 `_method`(`PhysicsRaycast` = CPU, `SceneDepth` = GPU)와 `_depthThickness`. `selectSimulationTarget` 은 모듈 집합 ∩ 빌드가 원하는 곳(교집합이 비면 모듈이 허락한 곳 — `gv_vfxSimulation` 은 둘 다 되는 방출기만 고른다). "수명을 늘리는 모듈이 없다"(GPU 방출기의 "끝났다" 의 근거)도 모듈 표가 선언한다(`isLifetimeBoundedByMax`) — **들어감**(`VfxEffectDefTest.ModuleTargetsIntersect` · `EveryEmitterFieldDeclaresItsTargets` — 리플렉션의 칸 목록과 표를 맞춘다, 빌드 전) | V4 |
| VfxGPUMesh | GPU 메시 입자: 배치를 `MeshInstanceWriter::GPU`("GPU 가 쓰는 구간" 형식 — CPU 의 `set*` 을 거절하고 항목은 크기 0 자리표)로 만들고, 빌더가 풀 자리마다 그 프레임의 인스턴스 칸(`GPUVfxMeshEmitter`, 불투명 구간만)을 싣는다. `vfxmeshinstance.hlsl` 이 시뮬레이션 뒤 · 인스턴스 애니메이션과 컬링 **앞에** 인스턴스 칸(`SwInstanceData` 128 바이트)의 행렬 · 바운드 · 색을 쓰고 기존 `gpucull` 이 간접 인자를 만든다. 불투명 머티리얼만 — 투명 머티리얼이면 오류로 그리지 않는다(투명 정렬은 CPU 행렬로 한다) — **들어감**(`VfxComponentTest.GPUMeshEmitterPublishesInstanceSlots`, 표본 `engine/vfx/gpudebris.vfx.xml`, 패스 종류 `VfxMeshInstance`, 빌드 · 화면 확인 전). 배치 구성이 바뀌어 CPU 자리표를 통째로 올린 프레임도 같은 프레임의 컴퓨트가 그리기 전에 다시 쓴다(매 프레임 쓴다) | VfxModuleTargets |
| DepthHistory | 렌더러의 뷰마다 깊이 히스토리: 지난 프레임 장면 깊이의 R32 실수 사본(RT · RW 텍스처)과 그 프레임의 뷰-투영 · 눈 위치(`FrameRenderer::ViewTarget::_depthHistory*`, `ensureTaaHistory` 와 같은 셋업 단계 생성 · 풀과 함께 해제). 장면 깊이 첨부는 프레임 사이에 그대로 남아 있으므로 다음 프레임의 컴퓨트 프리패스에서 풀스크린(`depthhistory.hlsl`, 패스 종류 `DepthHistory`)으로 옮긴다 — 깊이 텍스처는 RW 텍스처가 될 수 없고 에뮬 백엔드의 컴퓨트는 샘플 텍스처를 못 읽어서, 읽는 컴퓨트는 RW 텍스처 슬롯 0(네이티브는 bindless 번호)으로 읽는다 — **들어감**(빌드 · 네 백엔드 확인 전) | — |
| VfxGPUDepthCollision | `SceneDepth` 충돌: 입자 월드 점을 히스토리 시점의 뷰-투영으로 투영해 그 텍셀의 깊이보다 뒤이고 깊이 면에서 `_depthThickness` 안쪽이면 닿은 것이다. 법선은 이웃 텍셀 둘의 깊이 점이 이루는 면(눈 쪽), 방출기 행렬과 그 역은 매개변수로 받는다. 튀기는 CPU 와 같은 식(법선 성분 × 반발, 접선 × (1 − 마찰), 띄움 `VfxCollisionDef::kSurfaceSkin`) — **들어감**(표본 `gpudebris.vfx.xml`, 빌드 · 화면 확인 전). 한계: 화면 밖 · 카메라 뒤는 보지 않는다. 다른 물체 뒤(가려진 면)는 두께로만 가른다 — 두께보다 두꺼운 가림막 뒤는 닿지 않고, 얇은 면은 두께보다 빨리 지나가면 뚫는다. 한 프레임 늦은 깊이라 빠르게 움직이는 면은 어긋난다. 주 시점의 깊이만 본다(추가 뷰는 히스토리를 갖지만 읽지 않는다). 첫 프레임 · 풀 크기가 바뀐 프레임은 히스토리가 없어 보지 않는다. GL 은 텍스처 행 방향이 다르면 위아래가 뒤집힐 수 있다(확인 전) | DepthHistory, VfxModuleTargets |

### V4 남은 확인

- **셰이더 커밋은 쿠킹과 함께.** `vfxsimulate.hlsl` · `vfxsimulate.hlsli` · `vfxmeshinstance.hlsl` · `depthhistory.hlsl` 은 커밋 훅(`CheckShaderStamps`)이 네 백엔드의 쿠킹 바이너리와 함께 커밋하라고 막는다. 빌드를 허락받은 작업자가 `App --cook-shaders` 뒤 함께 넣는다. 그 전에는 엔진 기본 자산이 가리키는 파일이 없어 GPU 방출기와 깊이 히스토리가 없다(오류 한 번). C++ 쪽 `VfxGPUSimulation.cpp` 가 `vfxsimulate.hlsli` 를 include 하므로 셰이더 패치 없이는 빌드되지 않는다.
- FXC(DX11 cs_5_0)가 동기화를 낀 반복(덩어리 · 접두사 합)을 받는지, 네 백엔드에서 `ParticlesDrawInTheTransparentPass` 와 `editor/vfxpreview` 시나리오를 본다. `gv_vfxSimulation=CPU` 와 그림이 같아야 한다(V5 가 수로 견준다).
- **V5 원인(2026-10-22)**: 그리기도 텍스처도 아니라 **SPIR-V 최적화**다. 렌더 입력에 값을 실어 좁히니 위치 · 나이 · 수명 · 수명 비율은 DXIL 과 같고, 크기 · 색 커브(`vfxsimulate.hlsl` `evaluateCurve` —
  반복과 이른 반환이 있는 함수를 다섯 번 인라인)만 첫 키 값을 냈다(불꽃이 태어날 때처럼 크고 노랗게, 연기가 작게). 같은 소스를 -Od SPIR-V 로 지으면 맞고, -O3 레시피를 `-Oconfig` 로
  한 단계씩 늘리면 첫 `--simplify-instructions` 에서 처음 틀린다(RTX 3070 Ti, DXC 2026-05-27 — GL 도 같은 SPIR-V 라 함께 진다). 그 단계를 레시피에서 모두 빼면 통과하지만 쓰지 않는
  정점 입력이 진입점에 남아(`ShaderBindingValidatorTest.ConsumedVertexAttributeMaskFollowsShaderInputs` 실패) 컴파일러 옵션으로는 풀지 않았다. 고침: `evaluateCurve` 를 이른 반환 없이
  끝에서 한 번에 고르는 꼴로 — 기본 -O3 그대로 네 백엔드 통과(`CPUAndGPUParticleSimulationsDrawTheSameImage` · `ParticlesDrawInTheTransparentPass` · `ShaderBindingValidatorTest` · `ShaderCookRequestTest` 26/26, Debug).
  통합 파동 C 에서 쿠킹한 네 백엔드 바이너리와 함께 들어갔다.
  같은 모양(반복 + 이른 반환을 여러 번 인라인)이 다른 셰이더에도 생길 수 있다 — 백엔드끼리 그림을 견주는 GPU 테스트가 마지막 그물이다. **정정(2026-10-23)**: 위 "SPIR-V 최적화가 틀리게 만든다"는 반증됐다 — 패치 직전 커밋의 SPIR-V 셋(최적화 전 · -Od · -O3)이 Vulkan 1.3 검증기를 통과하고, 첫 simplify 전후 차이는 자명한 phi 제거 · `LogicalNot(IEqual)`→`INotEqual` · `FDiv` 2^24→`FMul` 2^-24 · 추출 전달 · 삽입 체인→구성뿐이며, 스크래치 SPIR-V 해석기(순차 실행)로 256 스레드 그룹을 돌려 -O3 · GL 인자 · 새 셰이더 · 다시 최적화한 것이 -Od 와 비트까지 같았다. 그래서 틀린 값은 최적화된 모양을 컴파일하는 쪽(NVIDIA 드라이버)에서 나온다고 본다(확신 약 65%, GPU 없이는 확정 못 함). spirv-tools 보고는 하지 않는다. 이른 반환을 없앤 우회는 그대로 둔다.
- DX11 의 오류 두 줄(깊이 히스토리 RT 가 컴퓨트 UAV 로 걸림)은 고쳤다(`D3D11RHICommandContext::bindComputeUav`).
- GL 의 R32 실수 RW 텍스처 읽기(형식 없는 이미지 읽기)와 DX11 RAW 인자 버퍼의 SRV(메시 인스턴스 쓰기가 살아 있는 수를 읽는다)를 확인한다.

### GPU 본체 + 라이트 CPU 방출기 (VfxGPULights 전의 패턴)

라이트는 CPU 라이트 목록에 붙으므로 CPU 전용이고(`VfxLightDef::getSupportedTargets`), 깊이 충돌 같은 GPU 전용 모듈과 한 방출기에 둘 수 없다(로드 오류). 빛이 필요한 GPU 효과는 방출기를 둘로 나눈다 — 본체(GPU: 많은 입자, 깊이 충돌)와 빛만 맡는 작은 CPU 방출기(빌보드 몇 개, `_light._maxLights` 몇 개, 본체와 같은 생성 일정 · 속도 범위). 같은 씨앗 규칙(`_seed` 와 방출기 번호)이라 두 방출기의 입자는 같지 않다 — 빛은 본체의 대표 몇 개를 흉내 낼 뿐이다. GPU 가 쓴 입자에서 바로 빛을 내는 일은 후보 `VfxGPULights`(Candidates-graphics).

### 다중 그룹 압축 (측정 뒤 정한다)

지금 GPU 시뮬레이션은 방출기 하나를 그룹 하나(256 스레드)가 돈다 — 풀이 수만 개면 한 그룹이 덩어리를 차례로 돌아 느리다. 풀이 크면 디스패치 셋으로 나눈다: (1) 그룹마다 생성 · 적분 · 생존 표시와 그룹 안 개수, (2) 그룹 개수의 접두사 합(그룹 하나), (3) 그룹마다 자기 시작 위치로 안정 압축. 단계 사이에 디스패치가 끊기므로 여러 단계를 한 디스패치에 넣지 못한다(단계마다 셋) — 단계 수 × 3 디스패치가 그룹 하나보다 빨라지는 풀 크기를 Release 로 잰 뒤, 풀 크기로 두 경로를 고를지(같은 결과여야 한다 — 안정 압축이라 순서가 같다) 정한다.
