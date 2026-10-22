# 그래픽스 후보

> 상태: 후보 · 선행: 없음 · 영역: graphics · 옛번호: C-55, D2-2, D2-5, D2-9, D2-10, D2-14, E-3, E-13 · 진행: 해당없음

이 문서는 그래픽스 · RHI · 셰이더 영역에서 착수 전인 후보의 목록입니다. 착수하면 그 절을 계획 문서로 옮기고 여기서 지웁니다.

## 정리

### SpanAllocatorVatSkinPools
제목: 정점 애니메이션 풀과 스킨 원본 버퍼를 구간 할당기로
옛번호: C-55
왜 필요한가: `feat16`(옛 C-43)이 씬 정점 풀과 모프 풀을 "바뀐 메시의 구간만 올린다"로 바꿨다. 같은 모양(메시 집합이 바뀌면 통째로 다시 올림)이 정점 애니메이션(VAT) 풀과 스킨 원본에 남았다.
착수 조건 / 남은 것: 재고(`RT.GPUScene.vertexPool` 처럼) 이득이 있을 때 `SpanAllocator` 구조로 옮긴다.

## 기능

### DirectionalShadowRefinement
제목: 방향광 그림자의 남은 것
옛번호: D2-2
왜 필요한가: 그림자 아틀라스(캐스케이드 · 뷰별 볼륨 · 점광 · 스폿 · 경계 구 맞춤)는 들어갔다(Renderer README "다중 뷰"). 아래 둘이 남았다.
착수 조건 / 남은 것:
- 캐스케이드별 컬링: 그림자 컬링 상자가 하나(방향광 볼륨 + 로컬 경계 구)라 캐스케이드마다 따로 거르지 않는다. 뷰마다 그림자 컬링 칸이 하나(`_shadowCullSlot`)라 타일마다 칸을 늘리는 GPU 컬링 구조 변경이다. 이득은 그림자 패스 드로우 수를 재 본 뒤에 판단한다(`GPU.Shadow`, 측정 전).
- 여드름 눈 확인: 바이어스가 텍셀 단위로 줄어 여섯 게임의 여드름을 아직 눈으로 안 봤다. 경계 구 맞춤 · envshowcase CSM 은 새 빌드로 게임별 스크린샷 전후를 다시 찍는다.
함정: 게임마다 `SW_ACTIVE_GAME` 빌드가 따로 필요해 통합 담당의 게임별 빌드 때 찍는다.

### TerrainWaterVegetationRemainders
제목: 환경(지형 · 식생 · 물)의 남은 것
옛번호: D2-5, D2-5.2, D2-5.5
왜 필요한가: 들어간 것과 계약(지형 충돌 · 부력 포함)은 `Source/Engine/Environment` 각 헤더 주석에 있다. 아래가 남았다.
착수 조건 / 남은 것:
- 강 경로: 경로가 점 목록이다. 스플라인 컴포넌트가 Engine 에 들어오면 그것을 경로로 받는다.
- 하늘 · 시간 · 높이 안개와 물속 안개 패스: 값은 `WaterBodyComponent::findUnderwaterFog` 가 이미 준다. 다중 뷰 병합 뒤. 볼륨 안개 · 대기 산란은 [RealtimeGlobalIllumination](Candidates-graphics.md)과 합친다(옛 D2-5.2).
- 물의 굴절 · 화면 공간 두께: 반투명 패스가 장면 색 사본 · 장면 깊이를 입력으로 받는 계약이 있어야 한다(지금은 지형 깊이를 정점에 굽는다).
- 지오모프(LOD 튐)와 레이어 다섯 이상: 머티리얼 텍스처 칸은 16 개로 늘었지만 `.layerweights` · 스플랫 업로드 · 브러시 · 셰이더가 레이어 넷이다. 두 번째 스플랫이 필요하다.
- 지형 LOD 교체 · 지형 쓰기의 비용과 식생 부분 계산은 계획 [TerrainEditCost](TerrainEditCost.md) 에 있다.
함정: 머티리얼 텍스처 칸 16 — 에뮬 슬롯은 Vulkan t 밴드 뒤 t16..t31 이라 SRV 띠(지금 t0..t12)가 밴드 끝까지 자라도 밀리지 않고 DX11 은 정적 머티리얼 샘플러로 읽는다(옛 D2-6.3, 정본은 `ShaderBindingSlots.h` 헤더).

### HalfResolutionBloomMeasurement
제목: 반해상도 블룸의 Release 측정
옛번호: D2-9
왜 필요한가: `deferredpipeline.xml` 블룸의 밝은 부분을 반해상도 패스(`BloomDownsample`)로 나눴다. 이득이 있는지 모른다.
착수 조건 / 남은 것: p50 · p99 를 `GPU.PostBloom`(전) 대 `GPU.BloomDownsample` + `GPU.PostBloom`(후)로 잰다. 지면 되돌린다.

### ViewModeCompletion
제목: 뷰포트 보기 모드의 남은 것
옛번호: D2-10, D2-10.2, D2-10.3
왜 필요한가: `RenderViewMode`(언리얼 View Mode · 유니티 Draw Mode 와 견준 것은 기억에 의존한 비교다)에서 `LightingOnly` · Roughness · Metallic · Mipmaps · LightComplexity 는 들어갔다. 아래가 남았다.
착수 조건 / 남은 것:
- Shipping 베이크: 쿠킹한 LightingOnly 변형으로 Shipping hostgpu 를 돌린다. Shipping 전체 빌드가 필요해 통합 담당의 Shipping 확인 때 돌린다.
- Metallic 금속도 칸은 PBR([IndirectLighting](IndirectLighting.md) P5)과 함께 생긴다.
함정: `swStoreSurface` 가 셰이더마다 명시한 거칠기 · 금속도를 받는다. `toon` · `sprite2dlit` 포워드 경로도 LightingOnly 에서는 회색으로 바뀐다.

### RealtimeGlobalIllumination
제목: 실시간 전역 조명과 볼륨 표현
옛번호: D2-14
왜 필요한가: 실시간 전역 조명과 볼륨 표현이 없다. [IndirectLighting](IndirectLighting.md)의 프로브 뒤 단계다.
착수 조건 / 남은 것: 라이트맵 또는 실시간 GI(Lumen · SDFGI · APV 갱신 방식 비교), 볼륨 안개 · 구름 · 대기 산란(`TerrainWaterVegetationRemainders` 의 안개 항목과 합침). 설계 문서가 먼저다. [ClusteredLighting](ClusteredLighting.md)이 선행이다.

### VfxGPULights
제목: GPU 로 시뮬레이션하는 방출기의 파티클 라이트
옛번호: 없음
왜 필요한가: 파티클 라이트(`VfxLightDef`)는 게임 스레드의 씬 라이트 목록에 점광을 붙여 라이트 그리드로 들어가므로 CPU 시뮬레이션 전용이다(`VfxLightDef::getSupportedTargets`). 그래서 라이트를 쓰는 방출기는 깊이 충돌(GPU 전용)과 함께 쓸 수 없고 로드가 실패한다. 지금은 GPU 본체 방출기와 빛만 맡는 작은 CPU 방출기를 같은 효과에 두는 패턴으로 푼다(계획 문서 `Vfx` 의 "GPU 본체 + 라이트 CPU 방출기"). 언리얼 Niagara 도 라이트 렌더러는 CPU 시뮬레이션 전용이다.
착수 조건 / 남은 것: GPU 라이트 그리드 생산자(`lightgrid.hlsl`)가 입자 라이트를 직접 읽게 하려면 라이트 버퍼에 GPU 가 쓰는 구간(`MeshInstanceWriter::GPU` 와 같은 형식)을 두고, 라이트 수를 CPU 가 모른 채 그리드를 만들 수 있어야 한다(`ClusteredLighting` S5 와 함께 본다). CPU 그리드 생산자(`gv_lightCulling=1`)는 GPU 가 쓴 라이트를 받을 수 없으므로 그 조합의 처리(로드 오류인지)를 함께 정한다. 수요(라이트를 내는 대량 입자)가 생기면 착수한다.

### ForwardHdrSceneColor
제목: 포워드 파이프라인의 HDR 장면 색
옛번호: 없음
왜 필요한가: 3D 포워드 파이프라인 넷(`forwardpipeline` · `forwardpipelinestaged` · `forwardprepasspipeline` · `forwardtoonpipeline`)은 `SceneColor` 가 `R8G8B8A8_UNORM` 이라 1.0 에서 잘린 값을 Present 가 Reinhard(`x / (x + 1)`)로 눌러 화면 최대가 0.5(128)다. 밝게 비친 바닥은 이미 128 이라 더하기 블렌드 입자(Vfx 띠 · 불꽃)가 그 위에서 보이지 않는다(2026-10-21 통합 GPU 검증 — main 도 같다, 미리보기 PPM 최댓값 128). 언리얼 · 유니티 URP · 고도는 포워드도 HDR 장면 색(RGBA16F · R11G11B10)이 기본이다.
착수 조건 / 남은 것: 의도한 룩 변경이다. `postchain.hlsl` 의 블룸 뒤 `saturate`(단계 파이프라인과 같은 그림을 내려고 둔 자름)를 지우고, 넷의 `SceneColor`(단계 파이프라인은 `BloomColor` 도)를 `R16G16B16A16_FLOAT` 로 바꾼다 — 형식만 바꾸면 그 자름 때문에 그대로 128 이다(확인함). 1.0 을 넘던 픽셀만 밝아지므로 골든 넷(Empty · NileCity · Shooter3D · StarSkirmish)을 `--record --runs 3` 로 다시 뜬다. 대역폭 차이를 Release 로 잰다. 2D 파이프라인은 톤맵을 안 하므로 그대로다.

## 관찰

### VulkanVsyncFrameMiss
제목: Vulkan VSync 에서 프레임이 주기를 놓침
옛번호: E-3
왜 필요한가: Vulkan(FIFO) + VSync 에서 가끔 한 프레임이 여러 주기를 놓친다(Release 큐브 100, 165 Hz). 조사 결과는 [검증과 측정](../08_Verification.md) 2절 "Vulkan VSync 프레임 놓침", 스왑체인 이미지 4 개는 결정 기록 3절에서 기각했다.
착수 조건 / 남은 것: 다시 보이면 `-gv_tracy=1` 로 acquire · 펜스 · present 를 시간축으로 보고, 전체 화면(독점) · NVIDIA "Vulkan/OpenGL 프레젠트 방식" 설정을 바꿔 가른다.

### DeviceLossHangRepro
제목: 실제 TDR 뒤 행을 재현하지 못함
옛번호: E-13, A-2
왜 필요한가: `rhi.removeDevice` 로 DX12(장치 제거) · Vulkan(손실 상태로 둠) 모두 프레임을 계속 돌고 종료 코드 0 으로 끝난다(2026-10-15 Vulkan 런타임 3/3 · 에디터 2/2, DX12 1/1). 남은 것은 드라이버 안의 대기(첫 `VK_ERROR_DEVICE_LOST` 를 돌려주기 전)뿐이다.
착수 조건 / 남은 것: 다시 보이면 `py -3 -m Scripts stacks <pid>` 로 매달린 스레드의 스택을 남긴다.

### ShaderPrefixAndFormat
제목: 셰이더 이름의 sw 접두를 없애고 clang-format 을 셰이더에도 건다
옛번호: 없음
왜 필요한가: (1) 공유 헤더 함수 · 타입의 `sw` / `Sw` 접두는 FXC(DX11 전용 — `ShaderCompiler.cpp`) 에 네임스페이스가 없어서 붙였다. 이름이 겹치면 셰이더 컴파일 오류로 멈추므로(오버로드만 조용히 공존) 접두의 이득이 작다. `SW_` 매크로 접두와 `g_` 전역은 매크로가 조용히 덮이고 C++ 이 이름으로 묶으므로 그대로 둔다. DX11 백엔드를 접으면 `sw::` 네임스페이스도 쓸 수 있다(결정 필요). (2) `.hlsl` / `.hlsli` 59 개는 어떤 포맷 도구에도 안 걸린다(고정판 20.1.8 로 59/59 가 다르다, 탭 들여쓰기). 그대로 `.clang-format` 을 걸면 cbuffer 멤버 들여쓰기가 사라지고 `[numthreads(...)]` 가 `void` 와 한 줄로 붙는다 — 셰이더용 설정(`Resource/` 아래 `.clang-format`)을 먼저 만들어 확인한다.
착수 조건 / 남은 것: MaterialShaderHooks H3~H6 병합 뒤(같은 파일을 고치는 중). 접두 제거는 `CheckShaderConventions.py` 와 [NamingScripts](../agents/NamingScripts.md) HLSL 절을 같은 커밋에서 고치고 C++ 이 이름으로 묶는 함수가 없는지 확인한다. 포맷은 설정 → `FormatClangFormat` 대상에 셰이더 추가 → 한 번에 포맷 + 쿠킹 스탬프 갱신 순서.
결정(사용자, 2026-10-23): 앞으로 셰이더에도 `.clang-format` 을 적용한다. 시험으로 찾은 설정(20.1.8) — 기존 `.clang-format` 에 `MacroBlockBegin: "^SW_ROOT_CONSTANTS_BEGIN$"` · `MacroBlockEnd: "^SW_ROOT_CONSTANTS_END$"`(`SW_MATERIAL_*` 는 넣지 않는다, 안의 `{ }` 가 이중 들여쓰기된다), `AlignConsecutiveDeclarations: {Enabled: true, AlignFunctionDeclarations: false}`, `StatementMacros: [SW_NUMTHREADS]` + 셰이더의 `[numthreads(…)]` 13곳(12 파일)을 `SW_NUMTHREADS(…)` 매크로로(`common.hlsli` 에 정의). 안 되는 것: `SW_ROOT_CONSTANTS_BEGIN` 한 줄 매크로가 여러 줄로 갈리고 `SW_DECLARE_*` 의 `\` 가 길게 정렬된다(cbuffer 의 `{` 를 함수 중괄호로 보기 때문 — `AfterFunction: false` 면 해결되나 모든 함수가 붙는다). 폐기한 안: `#endif` 주석 자동 달기, 매크로 구조 변경, 매크로 헤더만 `clang-format off`. 포맷한 뒤 포맷터를 다시 돌려 같은 결과인지(멱등) 확인한다.

## 조건부
