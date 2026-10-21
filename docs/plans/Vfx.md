# 파티클 · VFX 구현 단위 (`D2-11`)

> 상태: 구현중 · 선행: 없음 · 영역: graphics · 옛번호: D2-11 · 진행: 7/8

결정은 [결정 기록](../09_Decisions.md) 5-12 의 `D2-11` 행들과 옛 `D2-11` 본문의 사용자 결정이 원본입니다. 이 문서는 그 결정을 구현 단위로 나눈 계획과 단위마다의 계약을 적습니다.
단위가 끝나면 그 단위의 절을 지우고, 남는 교훈은 코드의 헤더 주석으로 옮깁니다.

## 1. 정한 것

- 시뮬레이션은 CPU 와 GPU 를 둘 다 만듭니다. 개발 빌드는 `gv_vfxSimulation` 으로 고르고, Shipping 은 GPU 로 고정하며 CPU 경로와 변수를 두지 않습니다.
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
| V4 | GPU 시뮬레이션 `vfxsimulate.hlsl`: 같은 모듈 의미를 컴퓨트로, 생존 수는 원자적 카운터, 간접 드로우 인자. DX11 은 구조 버퍼와 간접 인자를 한 버퍼로 못 쓰므로 복사 단계 하나를 더합니다. 이 단위에서 Shipping 의 CPU 경로를 뺍니다(지금은 CPU 가 유일한 경로라 Shipping 도 CPU 다) | V2, `D2-6.3` 슬롯 지도 |
| V5 | CPU 와 GPU 비교 테스트 — **들어감**(`RenderPassGPUTest.CPUAndGPUParticleSimulationsDrawTheSameImage`, 빌드 · 실행 전 — V4(`wt/vfx4`)의 `gv_vfxSimulation` · `VfxEmitterClock` 을 쓰므로 V4 가 들어간 뒤 빌드된다). 표본 효과를 CPU 와 GPU 로 같은 씨앗 · 같은 틱 수만큼 돌려 네 백엔드의 Present 를 견준다 — 다른 칸 1% 이하(차이의 크기는 보지 않는다 — FMA 순서가 빌보드 가장자리를 한두 칸 바꿀 수 있다). 입자 수를 직접 세지 않는 이유: RHI 에 버퍼 되읽기가 없다 — 그림이 수와 위치를 함께 본다. 버퍼 되읽기가 생기면 살아 있는 수(간접 인자 `SW_VFX_ARGS_WORD_ALIVE`)와 위치를 수로 견주는 케이스를 더한다 | V4 |
| V6 | 메시 입자(`VfxRenderMode::Mesh` — 메시 인스턴스 배치), 리본(`Ribbon` — 태어난 순서의 띠, `particle.hlsl`), 충돌(`VfxCollisionDef` — 물리 레이캐스트, `IVfxCollisionScene`), 파티클 라이트(`VfxLightDef` — 씬 라이트 목록 · CPU 라이트 그리드) — **들어감**(`VfxEffectDefTest.RenderModeFieldsAreChecked` · `VfxCPUSimulationTest.RibbonParticlesAreWrittenInBirthOrder` · `CollisionBouncesOrKills` · `VfxComponentTest.TrailSampleDrivesRibbonMeshAndLights`, 표본 `engine/vfx/trail.vfx.xml`, 빌드 · 화면 확인 전). 띠의 셰이더 경로(`particle.hlsl` 플래그 칸)는 커밋 훅(`CheckShaderStamps`)이 쿠킹한 바이너리와 함께 커밋하라고 막아 **아직 커밋되지 않았다** — 빌드 뒤 `App --cook-shaders` 와 함께 넣는다(그 전까지 띠는 더하기 빌보드로 잘못 그려진다). GPU 라이트 그리드에 넣는 일은 `ClusteredLighting` S5 | V3 |
| V7 | 메시 투명과 입자의 교차 정렬: 방출기를 메시 투명 배치와 한 순서로 섞고(프리미티브 단위, 결정 기록 5-12 `D2-11`), 추가 뷰는 그 뷰의 순서로 그립니다(`GPUTransparentParticleSlot`) — **들어감**(`GPUTransparentSorterTest.ParticlePlacementFollowsLayerAndDepth` · `VfxComponentTest.ParticlesInterleaveWithTransparentMeshes`, 빌드 · 화면 확인 전). 추가 뷰는 배치 순서와 입자 자리를 한 쌍으로만 쓴다(`FrameRenderer::applyViewTransparentOrder` — 뷰 순서에 주 시점 자리를 섞지 않는다). 패스 끝에 붙여 그리는 다른 경로는 정렬 대상이 아니다: 캔버스 패스는 화면 HUD(마지막이 의도), 월드 UI(`WidgetSpace::World`)는 렌더 텍스처를 메시에 붙여 메시 투명 배치로 정렬되고, `MeshOutline` 은 모든 파이프라인에서 투명 앞이며 불투명 배치만, `Outline` · 디버그 도형은 선택 · 에디터 겹침이다 | V2 |

### V4 계약

- **착수 조건: 빌드.** 새 컴퓨트 셰이더(`vfxsimulate.hlsl`)와 셰이더 수정은 커밋 훅(`CheckShaderStamps`)이 네 백엔드의 쿠킹 바이너리와 함께 커밋하라고 막는다. 쿠킹(`App --cook-shaders`)은 빌드가 있어야 하므로, 빌드를 허락받은 작업자가 맡는다.
- **CPU 전용 모듈과 Shipping 의 GPU 고정이 부딪힌다.** V6 의 물리 레이캐스트 충돌 · 파티클 라이트 · 메시 입자는 CPU 데이터(물리 씬, CPU 라이트 목록, 메시 인스턴스 배치 항목)가 필요해 GPU 시뮬레이션으로 옮길 수 없다(언리얼 Niagara 도 라이트 렌더러와 CPU 레이 트레이스 충돌은 CPU 시뮬레이션 전용이고, GPU 충돌은 깊이 버퍼로 한다).
  권고안: Niagara 처럼 **방출기마다 시뮬레이션 대상**을 정한다 — GPU 가 표현할 수 있는 방출기(빌보드 · 띠, 충돌 없음 · 라이트 없음)만 `gv_vfxSimulation` · Shipping 고정의 대상이고, CPU 전용 모듈을 쓰는 방출기는 모든 빌드에서 CPU 로 돈다(검증이 대상을 데이터로 정해 로그에 남긴다). Shipping 에서 CPU 경로를 통째로 빼는 원래 결정(결정 기록 5-12 `D2-11`)과 다르므로 사용자 확인이 필요하다. GPU 깊이 버퍼 충돌은 그다음 단위로 둔다.
- 결정성: 난수는 CPU 와 같은 splitmix64 수열이다. DX11(SM5)에는 64 비트 정수가 없어 uint2 로 흉내 낸다. 커브는 키와 접선을 올려 같은 식으로 계산한다(표로 굽지 않는다 — V5 가 같은 값을 기대한다).
- DX11: 구조 버퍼와 간접 인자를 한 버퍼로 못 쓰므로 RAW UAV(`RHIBufferUsage::Raw | IndirectArgs`)에 인자를 쓰는 작은 컴퓨트 단계를 더한다(RHI 에 버퍼 복사가 없다).
