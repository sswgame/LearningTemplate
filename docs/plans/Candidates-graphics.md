# 그래픽스 후보

> 상태: 후보 · 선행: 없음 · 영역: graphics · 옛번호: C-55, D2-1, D2-2, D2-5, D2-6, D2-7, D2-9, D2-10, D2-14, E-3, E-13 · 진행: 해당없음

이 문서는 그래픽스 · RHI · 셰이더 영역에서 착수 전인 후보의 목록입니다. 착수하면 그 절을 계획 문서로 옮기고 여기서 지웁니다.

## 정리

### SpanAllocatorVatSkinPools
제목: 정점 애니메이션 풀과 스킨 원본 버퍼를 구간 할당기로
옛번호: C-55
왜 필요한가: `feat16`(옛 C-43)이 씬 정점 풀과 모프 풀을 "바뀐 메시의 구간만 올린다"로 바꿨다. 같은 모양(메시 집합이 바뀌면 통째로 다시 올림)이 정점 애니메이션(VAT) 풀과 스킨 원본에 남았다.
착수 조건 / 남은 것: 재고(`RT.GPUScene.vertexPool` 처럼) 이득이 있을 때 `SpanAllocator` 구조로 옮긴다.

## 기능

### TwoDLightingExtras
제목: 2D 조명의 남은 것
옛번호: D2-1
왜 필요한가: 2D 빛 텍스처 · 자유 모양 빛 · 부드러운 그림자 · 빛 블렌드 스타일이 없다.
착수 조건 / 남은 것: 넷 모두 빛 컴포넌트(`Source/Engine/Object/Component/2D/Light2DComponent`)의 데이터부터 바꿔야 해 렌더러만 맡은 작업자는 시작하지 못했다(2026-10-17).

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

### ToonShadingCompletion
제목: 툰 머티리얼(MToon 1.0)의 남은 것
옛번호: D2-6
왜 필요한가: `toon.hlsl` 은 MToon 1.0 체계이고 아래가 빠졌다.
착수 조건 / 남은 것:
- 포워드 전용 지킴: toon.hlsl 의 `SW_PASS_GBUFFER` 분기를 `#error "toon.hlsl is forward-only ..."` 로 바꾼다 — `MATERIAL_FORWARD_ONLY` 를 빠뜨린 머티리얼이 G버퍼에서 램버트로 조용히 칠해지지 않고 쿠킹에서 멈춘다. 커밋 훅(`CheckShaderStamps`)이 다시 굽기를 요구하므로 App 빌드가 있는 통합 때 `--cook-shaders` 와 함께 한다(G버퍼 분기의 "디퍼드 조명은 램버트로 칠한다" 주석도 그때 지운다).
- UV 스크롤 애니메이션(`uvAnimation*`): VRM 머티리얼 임포터(`Source/Editor`)의 매핑과 같이 바꾼다.
- 스킨 · 모프 메시의 탄젠트: 스키닝 · 모프 컴퓨트가 위치 · 노멀만 돌려 노멀 맵이 레스트 탄젠트를 쓴다. `GPUMorphVertex` 에 탄젠트 칸이 필요하다.

### VrmRuntimeConsumption
제목: VRM 임포트 결과를 런타임이 읽기
옛번호: D2-7
왜 필요한가: humanoid 본 표 · 표정 · 스프링 본 · 1인칭은 `<이름>.avatar.json` 으로 나오지만 데이터뿐이다. 런타임이 읽는 타입이 없다(표정 → 모프 가중치, 스프링 본 시뮬레이션, 1인칭 메시 숨김). 스키마 테스트는 JSON 객체와 `_version` · `_mapHumanBone` 만 본다.
착수 조건 / 남은 것:
- 런타임 타입은 `Engine/Character` · `Engine/Animation` 에, BC7 재임포트는 Editor 임포터에 있어 그래픽 작업자의 파일 밖이다.
- 본 메시(`<이름>.mesh`)는 구간들을 다시 합친 것이라 디스크에 두 벌이다(VRoid 34k 삼각형 7 MB × 2). 엔진 메시에 머티리얼 구간이 생기면 하나로 줄인다.
- VRoid 텍스처는 BC3 이다. BC7 은 Release 의 띠 병렬 압축으로 512² 한 장이 8 초라([검증과 측정](../08_Verification.md) 2절, 커밋 `e71f3b8ae`) 예전의 "Debug 10 분" 은 근거가 아니다. Release 로 BC7 임포트를 다시 한다.
함정: `ModelImporterTest.SkinnedModelImportsSkeletonClipsAndAttachments` 는 Debug 에서 혼자 31 초라 EditorTest 한도를 30 → 120 초로 올려 두었다. 임포트를 줄이면 되돌린다.

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
- Shaded Wireframe: 패스가 둘 필요하다(셰이딩한 면 위에 같은 배치를 선으로 한 번 더). 착수 조건은 RHI 다 — `RHIPipelineStateDesc` 에 깊이 비교 함수(LessEqual)와 깊이 바이어스가 없어(네 백엔드) 겹쳐 그린 선이 면과 z-fighting 한다. 선 색은 보기 모드 define 하나(`swStoreSurface` 가 단색을 낸다)이고 G버퍼에는 셰이딩 모델 Unlit 으로 적으면 디퍼드도 그대로 된다. 디퍼드에서 툰은 `ForwardOnlyOpaque` 패스가 그리므로 그 패스에도 같은 겹침이 필요하다.
함정: `swStoreSurface` 가 셰이더마다 명시한 거칠기 · 금속도를 받는다. `toon` · `sprite2dlit` 포워드 경로도 LightingOnly 에서는 회색으로 바뀐다.

### RealtimeGlobalIllumination
제목: 실시간 전역 조명과 볼륨 표현
옛번호: D2-14
왜 필요한가: 실시간 전역 조명과 볼륨 표현이 없다. [IndirectLighting](IndirectLighting.md)의 프로브 뒤 단계다.
착수 조건 / 남은 것: 라이트맵 또는 실시간 GI(Lumen · SDFGI · APV 갱신 방식 비교), 볼륨 안개 · 구름 · 대기 산란(`TerrainWaterVegetationRemainders` 의 안개 항목과 합침). 설계 문서가 먼저다. [ClusteredLighting](ClusteredLighting.md)이 선행이다.

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

## 조건부
