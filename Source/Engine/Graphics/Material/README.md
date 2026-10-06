# Material — 머티리얼과 머티리얼 인스턴스

## 옮겨 온 내용 — 다시 쓰기 전

아래는 Graphics README 에서 원문 그대로 옮겨 온 절이다. 이 문서를 다시 쓰는 단위가 본문에 녹여 없앤다.

## 셀 셰이딩(툰) 머티리얼 — `toon.hlsl` · `engine/materials/toon.material`

파라미터 체계는 VRM MToon 1.0(`VRMC_materials_mtoon`)이다. 머티리얼 · 퍼뮤테이션 체계 위에 얹은 머티리얼 셰이더 하나라 따로 도는 경로가 없다.

```text
shading = linearstep( -1 + shadingToony, 1 - shadingToony, dot( N, L ) + shadingShift ) × lerp( 1, 그림자, shadowReceive )
색      = Σ빛 lerp( 그림자색, 기본색, shading ) × 빛 색 · 세기 · 감쇠 + 기본색 × 환경광
        + ( 프레넬 림 + 맷캡 ) × lerp( 1, 직접광 + 환경광, rimLightingMix ) + 발광 × emissiveStrength
```

- **빛은 포워드 경로가 보는 목록 그대로다**(방향광 · 점광 · 스폿, 빛마다 같은 계단). 감쇠는 `lighting.hlsli` 의 `swComputeLightIncidence` 하나를 함께 쓴다
  (조명 식은 둘이어도 "빛이 어디서 얼마나 오는가" 는 한 벌). 그림자 맵은 그림자를 드리우는 빛 하나에만 곱한다.
- **카메라 위치는 PassCB 에 없다** — 시선은 그 화면 점의 가까운 · 먼 평면 두 점을 `g_InvViewProj` 로 되짚어 구한다(원근 · 직교 모두 맞다).
- 텍스처 칸은 넷(기본 · 그림자 · 발광 · 맷캡 — 머티리얼 텍스처 슬롯 t5..t8 이 넷이다). 맷캡은 텍스처가 있을 때만 더한다(없으면 흰색이라 화면이 바랜다).
- 정적 스위치: `Outline` · `TwoSided` 는 런타임에 바꿀 수 있다(`bShaderFeature="0"` — 켬 · 끔 둘 다 쿠킹, `AlphaCutoff` 는 에셋 상태만). `Outline`(`MATERIAL_OUTLINE` — 아래 메시 외곽선 패스가 그린다) · `AlphaCutoff`(`MATERIAL_ALPHA_CUTOFF` — `alphaCutoff` 아래를 버린다) · `TwoSided`(`MATERIAL_TWO_SIDED`). 반투명은 다른 머티리얼처럼 `blendMode="Transparent"` +
  `MATERIAL_BLEND_TRANSLUCENT`.
- **양면은 머티리얼의 성질이다**(언리얼 Two Sided): 머티리얼 변형 PSO(`createMaterialPsoVariant`)가 퍼뮤테이션의 `MATERIAL_TWO_SIDED` 를 보고 후면 컬링을 끈다(후면 컬링
  패스만 — 그림자 패스도 양면으로 드리운다). 셰이더는 뒷면의 노멀을 `SV_IsFrontFace` 로 뒤집는다(`RenderPassGpuTest.TwoSidedMaterialDrawsBackFaces`).
- **디퍼드에서는 램버트다** — G버퍼 패스는 표면(기본색 · 노멀)만 적고 계단 셰이딩은 포워드 경로의 것이다.
- **톤맵 · 블룸 없는 파이프라인 `engine/pipeline/forwardtoonpipeline.xml`** — 툰은 빛 쪽이 기본색 그대로가 목표인데, 기본 포워드의 톤맵(Reinhard)은 흰색을 0.5 로 누르고
  블룸은 장면색(R8G8B8A8)에서 잘린 밝은 면을 번지게 해 계단을 지운다(2D 의 `forward2dpipeline.xml` 과 같은 이유). `-gv_renderPipeline` 으로 고른다.
  장면색이 LDR 이라 빛 세기 + 환경광이 1 을 넘으면 기본색이 잘린다 — 툰 장면은 주광 세기 1 근처로 둔다(쇼케이스 `game/empty/maps/toonshowcase.scene.xml`).

**메시 외곽선 — 뒤집은 껍질(inverted hull), 파이프라인 패스 `MeshOutline`.** 같은 `toon.hlsl` 의 다른 퍼뮤테이션(`SW_PASS_MESH_OUTLINE`)이 정점을 노멀 방향으로
밀고 앞면을 컬링해 외곽선 색 하나를 낸다. 불투명 패스 뒤 · 반투명 앞에 선언한다(깊이를 써서 뒤의 반투명을 가린다).

- **머티리얼이 패스에 들어갈지를 정한다.** 패스 종류 표의 `_pRequiredMaterialDefine`(`MATERIAL_OUTLINE`, 툰 머티리얼의 정적 스위치 `Outline`)이 없는 배치는 드로우
  (`FrameRenderer::drawsBatchInPass`) · 머티리얼 PSO 변형(`ensureMaterialPsos`) · 셰이더 쿠킹(`ShaderCookRequest`)에서 함께 빠진다 — 판정은 `FrameRendererUtil::drawsMaterialInPass`
  하나다. 외곽선을 모르는 셰이더가 이 패스의 앞면 컬링으로 그려질 자리가 없다(언리얼 메시 패스 프로세서가 머티리얼 성질로 드로우를 거르는 자리).
- 컬은 표의 `kCullFront` 가 기본이다(XML 의 "Back" 은 그것을 바꾸지 않는다). 양면 머티리얼도 껍질은 뒤집어 그린다(양면은 후면 컬링 패스에만 적용). 거울 배치는 Front ↔ Back 이 바뀐다.
- 두께: `outlineWidthMode` 0 = 월드(미터, 노멀 방향으로 민다) · 1 = 화면(화면 높이 비율 — 클립 공간에서 투영한 노멀 방향으로 `width × 2 × w` 를 더해 거리와 무관,
  `outlineMaxDistance` 를 넘으면 가늘어진다, 가로세로 비는 `g_OutlineParams.yz` 로 맞춘다). 색은 `outlineColor × lerp( 1, 직접광 + 환경광, outlineLightingMix )`.
- **스키닝 · 모프 결과를 민다** — 정점은 `swLoadMorphedVertex` 로 읽는다(`RenderPassGpuTest.MeshOutlineFollowsSkinnedPose`).
- **외곽선 퍼뮤테이션은 정점이 머티리얼을 읽는다**(두께가 정점에 필요하다) — 픽셀이 쓸 값(색 · 알파 컷오프 · 기본 텍스처)은 `nointerpolation` 칸으로 넘긴다(GL 은 두 단계가 구조버퍼를 읽으면 링크를 거절한다).
- 반투명 배치는 그리지 않는다(불투명 목록만). 와이어프레임 보기에서는 패스가 빠진다. 굳은 모서리(큐브)는 노멀이 갈라져 껍질에 틈이 난다 — 뒤집은 껍질의 성질이다.
- 시험: `RenderPassGpuTest.MeshOutlineDrawsDarkRingAroundSilhouette`(포워드 · 디퍼드, 끈 그림의 배경이 켠 그림에서 그려진 픽셀 = 고리가 실루엣 띠 안 · 어둡고 · 안쪽은 그대로).
- 밝기 단계 수는 `RenderPassGpuTest.ToonShadingHasFewerBrightnessLevelsThanLit` 가 픽셀로 본다(밝기 히스토그램에서 1 % 넘는 칸 수 — 툰 2 · 램버트 34).

- **bindless 표를 바꾸는 일은 렌더 스레드의 병렬 기록과 겹치면 안 된다**(`IRHIDevice::setParallelRecording`). 게임 스레드의 `MaterialCache::initializePending`
  (씬 로드 · 처음 쓰는 머티리얼의 스폰)은 렌더 스레드가 지난 프레임을 기록하는 동안 돈다 — `EngineLoop` 는 올릴 것이 있는 프레임(`hasPendingInitialize`)만
  `RenderThread::waitIdle` 로 기다린다. 증상은 `registerBindlessResource 이(가) 병렬 패스 기록 중에…` 단언 · 크래시이고, 단언은 플래그를 경합으로 읽어 재현율이
  바이너리마다 다르다(같은 실행이 0 · 100 %). 의심되면 `assertRegistryMutableNow` 에 콜스택을 파일로 남겨 본다(로거는 크래시 직전 줄을 잃는다).
- **머티리얼 · 인스턴스 형식 판정은 `AssetFormatRegistry::upgradeXmlWithActiveRegistry`**(씬 · 프리팹과 같다) — `AssetManager` 없이 돈다. 본문의 enum 글은 `TypeRegistry` 가 필요하다.

- **텍스처 리로드는 같은 `Texture2D` 에 새 SRV 인덱스를 준다** — `getReloadGeneration` → `refreshTextureBindings` → `refreshReloadedTextures`. DX11 · GL 은 인덱스를 바로 다시 써서 이 종류가
  숨는다 — DX12 · Vulkan 으로 본다. 머티리얼 인스턴스 텍스처는 에셋 경로로 덮어쓴다(`setTextureParameter`). 디바이스 없이 잡은 머티리얼은 `MaterialCache::requestInitialize` 로 표시한다.
  `MaterialCache` · `TextureCache` 는 일부러 다르다(소유 · 디바이스 기억 · acquire 순서) — 맞추지 말 것. 두 캐시의 `clear()` 는 GPU 자원을 놓지 않는다(RHI shutdown 이 먼저라 안전).
- **`Material` · `MaterialInstance` · `Mesh` 는 Engine 의 `create()` 로만 만든다**(모듈이 `make_shared` 하면 제어 블록이 모듈 DLL 에 살아 종료 세그폴트). 팩토리 안에서는 `sw::make_shared`.
