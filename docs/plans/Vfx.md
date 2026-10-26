# 파티클 · VFX 구현 단위 (`D2-11`)

> 상태: 측정대기 · 선행: 없음 · 영역: graphics · 옛번호: D2-11 · 진행: 12/12

**남은 확인(통합에서 `gpu-verifier` 로 한 번에):** 매니페스트 id `render-pass-gpu-tests`, `vfx-preview-scenario`(GPU 방출기 · 깊이 충돌 · 메시 입자 · 띠의 네 백엔드 화면, DX11 RAW 인자 버퍼 SRV — 아래 "V4 남은 확인"). 확인이 끝나면 통합 담당이 이 문서를 닫는다.

결정은 [결정 기록](../09_Decisions.md) 5-12 의 `D2-11` 행들과 옛 `D2-11` 본문의 사용자 결정이 원본입니다. 단위가 끝나면 절을 지우고 남는 교훈은 코드 헤더 주석으로 옮깁니다.

## 1. 정한 것

- 시뮬레이션은 CPU 와 GPU 를 둘 다 만들고 대상은 방출기마다 정합니다(Niagara 의 Sim Target). 모듈마다 돌 수 있는 곳을 선언하고 방출기의 대상은 그 교집합입니다(비면 로드 오류). 물리 충돌 · 라이트는 CPU, 깊이 충돌은 GPU, 나머지(빌보드 · 띠 · 메시)는 둘 다이고, 둘 다 되는 방출기만 Shipping 에서 GPU 로 고정, 개발 빌드에서 `gv_vfxSimulation` 으로 고릅니다(Shipping 에는 변수가 없다, 결정 기록 5-12 `D2-11`).
- 데이터 형식(`*.vfx.xml`)과 렌더 입력 형식은 하나이고 두 시뮬레이션이 같은 형식을 냅니다.
- 첫 모듈 형식은 고정 모듈과 값 커브(`FloatCurve`)입니다. 다형 목록과 그래프는 뒤로 미룹니다.
- 방출기 하나가 투명 정렬 후보 하나입니다(입자 단위 정렬 없음). 서버는 시뮬레이션하지 않습니다. 2D 픽셀 퍼펙트 입자는 P1 에 넣지 않습니다.

## 2. 단위 (모두 들어감, 빌드 · 화면 확인 전)

| 단위 | 내용 · 계약 |
|---|---|
| V0 | 데이터 형식 `VfxEffectDef` · `VfxEmitterDef`, 리플렉션 로드와 검증(필수 값이 없으면 로드 실패) — `VfxEffectDefTest` · 리소스 종류 `vfx` |
| V1 | CPU 시뮬레이션 `VfxCPUSimulation`: 고정 크기 풀, 결정적 난수(방출기 씨앗), 단계 = 생성 · 적분 · 수명 제거(뒤쪽과 바꾸기). 출력은 `GPUParticle`(float4 셋) |
| V2 | 방출기마다 입자 그리기 하나(`GPUParticleBatch`)를 투명 패스가 빌보드로 그림(`VfxParticleRenderer`, `particle.hlsl` — `SV_VertexID` 와 패스 전용 슬롯 t11 의 입자 버퍼) |
| V3 | `VfxComponent`와 에디터 미리보기 창(`VfxPreviewPanel` — 보기 전용), 표본 `engine/vfx/sparks.vfx.xml` |
| V4 | GPU 시뮬레이션 `vfxsimulate.hlsl`(그룹 하나 = 방출기 하나, 단계마다 생성 → 적분 → 안정 압축, 간접 드로우 인자). `VfxEmitterClock` 이 두 시뮬레이션의 생성 일정, 게임 스레드 `VfxGPUEmitter` → 요청 `VfxGPUEmitterRequest` → 렌더 스레드 `VfxGPUSimulation`, 패스 종류 `VfxSimulation`. 계약: 생존 수는 원자 카운터가 아니라 그룹 공유 메모리 접두사 합의 안정 압축(버퍼가 늘 태어난 순서라 띠가 정렬 없이 잇는다). 입자 상태 · 렌더 입력은 구조 버퍼, 간접 인자는 따로 둔 버퍼(DX11 은 RAW UAV, 셰이더가 `#if defined( DX11 )` 로 바이트 주소에 씀). GL `drawIndirect` 는 메시 없는 드로우를 장치 공용 VAO 로 그림. GPU 풀이 차서 버린 입자는 인자 버퍼에 센다(`SW_VFX_ARGS_WORD_DROPPED` — RHI 에 버퍼 읽기 길이 없어 아직 경고로 못 올림). 풀이 찰 수 있는 데이터(생성률 × 가장 긴 수명 + 버스트 > 풀)는 읽을 때 경고. 렌더 스레드가 늦으면 디스패치마다 `SW_VFX_MAX_STEPS_PER_DISPATCH` 까지만 따라잡음 |
| V5 | CPU · GPU 비교: `RenderPassGPUTest.CPUAndGPUParticleSimulationsDrawTheSameImage` 네 백엔드(Release hostgpu 통과, 2026-10-23) |
| V6 | 메시 입자(`VfxRenderMode::Mesh`), 리본(`Ribbon` — 태어난 순서의 띠), 충돌(`VfxCollisionDef` — 물리 레이캐스트, `IVfxCollisionScene`), 파티클 라이트(`VfxLightDef` — 씬 라이트 목록 · CPU 라이트 그리드), 표본 `trail.vfx.xml`. 띠의 셰이더 경로(`particle.hlsl` 플래그 칸)는 커밋 훅(`CheckShaderStamps`)이 쿠킹 바이너리와 함께 커밋하라고 막아 **아직 커밋되지 않았다** — 빌드 뒤 `App --cook-shaders` 와 함께 넣는다(그 전까지 띠는 더하기 빌보드로 잘못 그려진다). GPU 라이트 그리드는 `ClusteredLighting` S5 |
| V7 | 메시 투명과 입자의 교차 정렬: 방출기를 메시 투명 배치와 한 순서로 섞고(프리미티브 단위), 추가 뷰는 그 뷰의 순서로 그림(`GPUTransparentParticleSlot`, `FrameRenderer::applyViewTransparentOrder`). 패스 끝에 붙이는 다른 경로는 정렬 대상이 아님: 캔버스 패스는 HUD(마지막이 의도), 월드 UI 는 렌더 텍스처를 메시에 붙여 메시 투명 배치로 정렬, `MeshOutline` 은 투명 앞의 불투명 배치만, `Outline` · 디버그 도형은 선택 · 에디터 겹침 |
| VfxModuleTargets | 모듈마다 대상 집합(`VfxSimulationTargetSet`, `getSupportedTargets`)을 선언하고 방출기는 교집합(`VfxEmitterDef::computeSupportedTargets`, 비면 로드 오류). 충돌의 `_method`(`PhysicsRaycast` = CPU, `SceneDepth` = GPU)와 `_depthThickness`. "수명을 늘리는 모듈이 없다"(GPU 방출기의 "끝났다" 근거)도 모듈 표가 선언(`isLifetimeBoundedByMax`). `EveryEmitterFieldDeclaresItsTargets` 가 리플렉션 칸 목록과 표를 맞춘다 |
| VfxGPUMesh | GPU 메시 입자: 배치를 `MeshInstanceWriter::GPU` 형식(CPU `set*` 을 거절, 항목은 크기 0 자리표)으로 만들고 빌더가 풀 자리마다 인스턴스 칸(`GPUVfxMeshEmitter`, 불투명 구간만)을 싣는다. `vfxmeshinstance.hlsl` 이 시뮬레이션 뒤 · 인스턴스 애니메이션과 컬링 앞에 행렬 · 바운드 · 색을 쓰고 기존 `gpucull` 이 간접 인자를 만든다. 투명 머티리얼이면 오류로 그리지 않는다. 표본 `gpudebris.vfx.xml`, 패스 종류 `VfxMeshInstance` |
| DepthHistory | 뷰마다 지난 프레임 장면 깊이의 R32 실수 사본(RT · RW 텍스처)과 그 프레임의 뷰-투영 · 눈 위치(`FrameRenderer::ViewTarget::_depthHistory*`). 다음 프레임 컴퓨트 프리패스에서 풀스크린(`depthhistory.hlsl`, `DepthHistory`)으로 옮긴다(깊이 텍스처는 RW 가 될 수 없고 에뮬 백엔드 컴퓨트는 샘플 텍스처를 못 읽어 RW 슬롯 0 으로 읽는다) |
| VfxGPUDepthCollision | `SceneDepth` 충돌: 입자 월드 점을 히스토리 뷰-투영으로 투영해 그 텍셀 깊이보다 뒤이고 깊이 면에서 `_depthThickness` 안쪽이면 닿은 것. 법선은 이웃 텍셀 둘의 깊이 점이 이루는 면, 튀기는 CPU 와 같은 식(띄움 `kSurfaceSkin`). 한계: 화면 밖 · 카메라 뒤는 보지 않고, 가려진 면은 두께로만 가르며(두꺼운 가림막 뒤는 닿지 않고 얇은 면은 빨리 지나가면 뚫음), 한 프레임 늦은 깊이라 빠른 면은 어긋나고, 주 시점만 본다. 첫 프레임 · 풀 크기가 바뀐 프레임은 히스토리가 없다. GL 은 텍스처 행 방향이 다르면 위아래가 뒤집힐 수 있다(확인 전) |

### V4 남은 확인

- **셰이더 커밋은 쿠킹과 함께.** `vfxsimulate.hlsl` · `vfxsimulate.hlsli` · `vfxmeshinstance.hlsl` · `depthhistory.hlsl` 은 `CheckShaderStamps` 가 네 백엔드 쿠킹 바이너리와 함께 커밋하라고 막는다. 빌드를 허락받은 작업자가 `App --cook-shaders` 뒤 함께 넣는다(그 전에는 GPU 방출기와 깊이 히스토리가 없고, `VfxGPUSimulation.cpp` 가 `vfxsimulate.hlsli` 를 include 해 셰이더 패치 없이는 빌드되지 않는다).
- FXC(DX11 cs_5_0)가 동기화를 낀 반복(덩어리 · 접두사 합)을 받는지, 네 백엔드에서 `ParticlesDrawInTheTransparentPass` 와 `editor/vfxpreview` 를 본다(`gv_vfxSimulation=CPU` 와 그림이 같아야 한다).
- **V5 의 SPIR-V 크기 · 색 커브 오류(2026-10)**: GPU 크기 · 색 커브(`evaluateCurve` — 반복과 이른 반환을 다섯 번 인라인)가 Vulkan · GL 에서 첫 키 값을 냈다. `evaluateCurve` 를 이른 반환 없이 끝에서 한 번에 고르는 꼴로 고쳐 네 백엔드 통과했고 우회는 그대로 둔다. 처음 "SPIR-V 최적화 탓"으로 봤으나 반증됐다(검증기 통과, SPIR-V 해석기 실행이 -Od 와 비트까지 같음) — 최적화된 모양을 컴파일하는 NVIDIA 드라이버 쪽으로 본다(확신 약 65 %). 같은 모양(반복 + 이른 반환 여러 번 인라인)이 다른 셰이더에도 생길 수 있어 백엔드끼리 그림을 견주는 GPU 테스트가 마지막 그물이다.
- GL 의 R32 실수 RW 텍스처 읽기(형식 없는 이미지 읽기)는 고쳤다(`e3f348bd5`): 컴퓨트 RW 슬롯 형식이 `rgba32f` 로 고정이던 것을 슬롯마다 적게 했고(`SW_RW_SLOT<n>_GL_FORMAT`), 깊이 히스토리 슬롯은 `r32f` 다. `vfxgpulights` 의 gl 라이트 없음의 주원인은 형식이 아니라 행 순서(GL 장면 깊이는 0 행이 아래)였고 깊이 히스토리를 D3D 순서로 뒤집어 고쳤다 — [GpuVerification](GpuVerification.md) 3.1 절. DX11 RAW 인자 버퍼의 SRV(메시 인스턴스 쓰기가 살아 있는 수를 읽는다)는 확인이 남았다.

### GPU 본체 + 라이트 CPU 방출기 (VfxGpuLights 전의 패턴)

라이트는 CPU 라이트 목록에 붙어 CPU 전용이라 깊이 충돌 같은 GPU 전용 모듈과 한 방출기에 둘 수 없다(로드 오류). 빛이 필요한 GPU 효과는 방출기를 둘로 나눈다 — 본체(GPU)와 빛만 맡는 작은 CPU 방출기(빌보드 몇 개, `_light._maxLights` 몇 개, 같은 생성 일정 · 속도 범위). 씨앗 규칙이 방출기 번호를 섞어 두 방출기의 입자는 같지 않으므로 빛은 본체의 대표 몇 개를 흉내 낼 뿐이다. GPU 입자에서 바로 빛을 내는 일은 [VfxGpuLights](VfxGpuLights.md).

### 다중 그룹 압축 (측정 뒤 정한다)

지금 방출기 하나를 그룹 하나(256 스레드)가 돌아 풀이 수만 개면 느리다. 풀이 크면 디스패치 셋으로 나눈다: (1) 그룹마다 생성 · 적분 · 생존 표시와 개수, (2) 개수의 접두사 합(그룹 하나), (3) 그룹마다 자기 시작 위치로 안정 압축. 단계마다 셋이라 여러 단계를 한 디스패치에 못 넣는다 — 단계 수 × 3 디스패치가 그룹 하나보다 빨라지는 풀 크기를 Release 로 잰 뒤 풀 크기로 두 경로를 고를지(같은 결과여야 한다) 정한다.
