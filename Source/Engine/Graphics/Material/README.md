# Material — 머티리얼과 머티리얼 인스턴스

## 이것은 무엇이고 왜 있나

머티리얼은 셰이더 하나와 그 셰이더에 넘길 값(색, 거칠기, 텍스처)을 묶은 에셋입니다. 메시는 머티리얼을 하나 지정받아 그 셰이더로 그려집니다.
머티리얼 인스턴스는 부모 머티리얼에서 일부 값만 바꾼 것입니다. 같은 셰이더와 기본값을 두고 오브젝트마다 색만 다르게 하고 싶을 때 씁니다.
언리얼의 Material과 Material Instance, 유니티의 Material과 MaterialPropertyBlock에 해당합니다.

머티리얼이 없으면 메시마다 셰이더 경로와 상수 값을 코드에 적어야 하고, 디자이너가 색 하나를 바꾸려 해도 빌드를 다시 해야 합니다.
머티리얼은 `.material` XML 파일이라 에디터나 텍스트 편집기에서 고치고, 엔진이 실행 중에 다시 로드합니다.

## 머릿속 그림

```text
engine/materials/toon.material   (MaterialDesc XML: 셰이더 경로, 블렌드 모드, 프로퍼티, 퍼뮤테이션)
        │  MaterialCache::acquire(경로) — 경로마다 Material 하나, 참조 수로 관리
        ▼
Material   ── 패킹된 머티리얼 원소, 퍼뮤테이션 define, 텍스처 SRV 인덱스
        │  MaterialInstance::create( Material* ) — 값 일부를 덮어씀
        ▼
MeshComponent  ──▶  GpuSceneBuilder 가 머티리얼 원소로 모아 셰이더의 g_SwMaterials 로 보냄
```

기억할 개념은 네 가지입니다.

**프로퍼티.** 머티리얼이 셰이더에 넘기는 값 하나입니다. `_properties` 에 이름, 타입(Color, Range, Texture2D 등), 셰이더 타입, 기본값을 적습니다.
이름은 셰이더의 머티리얼 구조체 멤버 이름과 같아야 합니다. 엔진은 셰이더 리플렉션에서 그 멤버의 오프셋을 읽어 값을 채웁니다.

**퍼뮤테이션.** 같은 셰이더 파일도 define 조합에 따라 다른 바이너리가 됩니다. 머티리얼의 `_permutations` 가 그 조합을 정합니다.
늘 켜는 define(`_alwaysDefines`), 켜고 끄는 **정적 스위치**(`_staticSwitches`), 여러 값 중 하나를 고르는 **멀티 컴파일**(`_multiCompiles`)이 있습니다.

**머티리얼 원소.** 렌더러는 머티리얼 값을 셰이더 종류마다의 구조체 버퍼(`g_SwMaterials`, t9)에 원소 하나로 올립니다. 인스턴스는 자기 머티리얼 원소 번호를 가지고 셰이더가 그 번호로 읽습니다.
머티리얼 인스턴스도 원소 하나를 따로 받습니다.

**머티리얼 캐시.** `MaterialCache` 는 리소스 경로를 키로 `Material` 을 소유하고 참조 수를 셉니다. 같은 경로를 여러 메시가 써도 머티리얼은 하나입니다.

## 따라 해 보기 — 새 머티리얼을 만들어 메시에 지정하기

[시작하기](../../../../docs/01_GettingStarted.md)의 도는 큐브에 빨간 머티리얼을 지정해 보겠습니다.

1. `Resource/engine/materials/defaultmaterial.material` 을 `Resource/game/empty/materials/` 폴더에 red.material 이라는 이름으로 복사합니다. 리소스 경로는 소문자여야 합니다.
2. 복사한 파일의 `name` 을 `Red` 로 바꾸고, `color` 프로퍼티의 `defaultValue` 를 `"1.0 0.1 0.1 1.0"` 으로 바꿉니다.
   `_permutations` 는 지우지 말고 그대로 둡니다. 퍼뮤테이션이 없으면 백엔드마다 다른 방식으로 그리기에 실패합니다.
3. 메시에 저장되는 머티리얼로 지정합니다. `EmptyGame::ensureTutorialSpinner` 에서 `setMeshID` 다음에 한 줄을 넣습니다.

   ```cpp
   pMesh->setMaterialPath( "game/empty/materials/red.material" );
   ```

4. 빌드하고 `-gv_tutorialSpinner=1` 로 실행하면 큐브가 빨갛게 그려집니다.
5. 에디터를 켜고(`-EnableEditor`) 실행한 채 `red.material` 의 `defaultValue` 를 다른 색으로 바꾸고 저장하면, 에디터의 에셋 핫 리로드(`AssetHotReload`)가 머티리얼을 다시 로드해 색이 바뀝니다.

`setMaterialPath` 는 씬에 저장되는 지정이고, `setMaterial( Material* )` 은 실행 중에만 유효합니다. 오브젝트마다 값을 바꾸는 머티리얼 인스턴스는 [Graphics 문서](../README.md)의 따라 해 보기에 있습니다.

## 작동 원리

### 에셋 파일과 로드

`.material` 파일의 루트는 `MaterialDesc` 이고, `shaderPath`, `blendMode`, `_properties`, `_permutations` 를 가집니다. 읽고 쓰는 코드는 `MaterialXml.cpp` 에 있습니다.
블렌드 모드는 `Opaque` 와 `Transparent` 두 가지입니다. 반투명 머티리얼은 `blendMode="Transparent"` 와 함께 `_alwaysDefines` 에 `MATERIAL_BLEND_TRANSLUCENT` 를 넣습니다.

에셋 형식이 바뀌었는지는 `AssetFormatRegistry::upgradeXmlWithActiveRegistry` 가 판단합니다. 씬, 프리팹과 같은 경로이고 `AssetManager` 없이 돕니다.
다만 본문의 enum 글을 해석하려면 `TypeRegistry` 가 필요합니다.

메시는 `MeshComponent::setMaterialPath` 로 경로를 받고, `MaterialCache::acquire` 로 머티리얼을 잡습니다. 컴포넌트는 디바이스를 모르므로 GPU 리소스는 바로 만들지 않습니다.
대신 `MaterialCache::requestInitialize` 로 표시해 두면, 게임 스레드의 `EngineLoop` 이 `initializePending` 으로 GPU 버퍼와 텍스처를 만듭니다.

### 프로퍼티 패킹과 셰이더 레이아웃

머티리얼 원소의 레이아웃 원본은 셰이더입니다. `Material::ensureShaderLayout` 이 디바이스 백엔드의 리플렉션으로 프로퍼티 오프셋과 원소 stride를 맞춥니다.
XML의 프로퍼티 순서가 아니라 셰이더 구조체가 기준이므로, 셰이더 멤버 순서를 바꿔도 머티리얼 파일은 그대로입니다.
머티리얼 스키마는 픽셀 셰이더에서 먼저 찾고, 없으면 정점 셰이더에서 찾습니다. 정점을 움직이는 셰이더는 정점 단계에서 머티리얼을 읽기 때문입니다.

### 퍼뮤테이션

`MaterialPermutation.cpp` 가 정적 스위치, 멀티 컴파일, 품질 설정을 모아 define 목록을 만듭니다. define이 바뀔 수 있는 설정이 바뀌면 전역 세대(`MaterialUtil::getPermutationGeneration`)가 오르고,
멈춘 씬도 그 세대를 보고 배치를 다시 모읍니다.

정적 스위치의 `bShaderFeature` 는 쿠킹 범위를 정합니다.

- `bShaderFeature="1"` 은 에셋에 저장된 상태만 쿠킹합니다. 유니티의 shader_feature에 해당합니다.
- `bShaderFeature="0"` 은 켬과 끔을 모두 쿠킹하므로 실행 중에 `Material::setStaticSwitch` 로 바꿀 수 있습니다. 유니티의 multi_compile에 해당합니다.

쿠킹 규칙의 자세한 내용은 [Shader 문서](../Shader/README.md)의 "쿠킹과 산출물"에 있습니다.

### 머티리얼 인스턴스

`MaterialInstance::create( Material* )` 로 만들고 `setScalarParameter`, `setVectorParameter`, `setTextureParameter`, `setParameter` 로 값을 덮어씁니다.
`updateRhi` 가 부모 기본값에 덮어쓴 값을 합쳐 인스턴스 버퍼를 만들거나 갱신하고, 렌더 스레드가 이것을 부릅니다.
텍스처는 텍스처 에셋 경로로 덮어씁니다(`setTextureParameter( name, "engine/textures/perlin.dds" )`). 언리얼의 `SetTextureParameterValue` 와 같습니다.

인스턴스가 다르면 머티리얼 원소가 달라지지만, DirectX 12와 Vulkan은 셰이더 종류 단위로 배치를 합치므로 드로우 콜이 늘지 않습니다.
DirectX 11과 OpenGL은 텍스처를 슬롯에 바인딩하므로 머티리얼 경계가 배치 경계입니다. 같은 색을 쓰는 오브젝트가 많으면 GameFramework의 `MaterialTintCache` 처럼 인스턴스 하나를 나눠 씁니다.

### 소유와 수명

`Material`, `MaterialInstance` 는 엔진의 `create()` 로만 만듭니다. 생성자가 `create()` 만 만들 수 있는 키(`CreateKey`)를 요구하므로 다른 방법은 컴파일되지 않습니다.
게임 모듈에서 `make_shared` 하면 `shared_ptr` 제어 블록이 모듈 DLL에 생겨 종료할 때 세그폴트가 나기 때문입니다. 엔진 안의 팩토리도 `sw::make_shared` 로 한정해 씁니다.

`MaterialCache` 와 `TextureCache` 는 경로로 참조를 세는 같은 구조지만 일부러 두 곳이 다릅니다. 헤더 주석에 그 이유가 있으니 한쪽에 맞추지 마세요.

- `MaterialCache` 는 `shared_ptr` 로 소유합니다. 렌더 스냅샷이 소유를 빌려 가므로 `release` 는 GPU 리소스를 해제하지 않고, 마지막 `shared_ptr` 이 놓일 때 해제됩니다.
  `TextureCache` 는 `unique_ptr` 이라 참조가 0이 되면 그 자리에서 해제합니다.
- `MaterialCache::acquire` 는 먼저 세고 실패하면 되돌립니다. `TextureCache` 는 성공한 뒤에 셉니다.

두 캐시의 `clear()` 는 GPU 리소스를 해제하지 않습니다. RHI 종료가 먼저 일어나 이미 해제된 뒤이기 때문입니다.
`Material` 은 `RHIRenderResource` 라 디바이스 수명 이벤트를 스스로 받습니다([RHI 문서](../RHI/README.md)의 "GPU 리소스를 놓는 세 가지 방법").

### 예: 셀 셰이딩(툰) 머티리얼

`engine/materials/toon.material` 과 `toon.hlsl` 은 머티리얼과 퍼뮤테이션만으로 새 셰이딩 모델을 만든 예입니다. 따로 도는 렌더 경로가 없습니다.
파라미터 체계는 VRM MToon 1.0(`VRMC_materials_mtoon`)을 따릅니다.

```text
shading = linearstep( -1 + shadingToony, 1 - shadingToony, dot( N, L ) + shadingShift ) × lerp( 1, 그림자, shadowReceive )
색      = Σ빛 lerp( 그림자색, 기본색, shading ) × 빛 색 × 세기 × 감쇠 + 기본색 × 환경광
        + ( 프레넬 림 + 맷캡 ) × lerp( 1, 직접광 + 환경광, rimLightingMix ) + 발광 × emissiveStrength
```

- **빛은 포워드 경로가 보는 목록 그대로입니다.** 방향광, 점광, 스폿 모두 같은 계단을 씁니다. 감쇠는 `lighting.hlsli` 의 `swComputeLightIncidence` 를 함께 씁니다.
  조명 식은 둘이어도 "빛이 어디서 얼마나 오는가"는 한 곳에서 계산하기 위해서입니다. 그림자 맵은 그림자를 드리우는 빛 하나에만 곱합니다.
- **카메라 위치는 PassCB에 없습니다.** 시선 방향은 그 화면 점의 가까운 평면과 먼 평면 위 두 점을 `g_InvViewProj` 로 되돌려 구합니다. 원근과 직교 카메라 모두에서 맞습니다.
- **텍스처는 네 개입니다**(기본색, 그림자, 발광, 맷캡). 머티리얼 텍스처 슬롯 t5..t8이 네 개이기 때문입니다. 맷캡은 텍스처가 있을 때만 더합니다. 없으면 흰색이라 화면이 바랩니다.
- **정적 스위치는 세 개입니다.** `Outline`(`MATERIAL_OUTLINE`)은 아래 메시 외곽선 패스가 이 머티리얼을 그리게 합니다. `AlphaCutoff`(`MATERIAL_ALPHA_CUTOFF`)는 `alphaCutoff` 아래 픽셀을 버립니다.
  `TwoSided`(`MATERIAL_TWO_SIDED`)는 양면으로 그립니다. `Outline` 과 `TwoSided` 는 `bShaderFeature="0"` 이라 실행 중에 바꿀 수 있고, `AlphaCutoff` 는 에셋 상태만 쿠킹합니다.
- **양면은 머티리얼의 속성입니다.** 언리얼의 Two Sided와 같습니다. 머티리얼 PSO 변형(`createMaterialPsoVariant`)이 퍼뮤테이션의 `MATERIAL_TWO_SIDED` 를 보고 후면 컬링을 끕니다.
  후면 컬링을 쓰는 패스에만 적용되고, 그림자 패스도 양면으로 드리웁니다. 셰이더는 뒷면의 노멀을 `SV_IsFrontFace` 로 뒤집습니다(`RenderPassGpuTest.TwoSidedMaterialDrawsBackFaces`).
- **디퍼드에서는 램버트로 그립니다.** G버퍼 패스는 표면(기본색, 노멀)만 기록하고, 계단 셰이딩은 포워드 경로에만 있습니다.
- **톤 매핑과 블룸이 없는 파이프라인 `engine/pipeline/forwardtoonpipeline.xml` 을 씁니다**(`-gv_renderPipeline`). 툰은 빛을 받는 면이 기본색 그대로 보이는 것이 목표입니다.
  기본 포워드의 톤 매핑(Reinhard)은 흰색을 0.5로 누르고, 블룸은 장면 색(R8G8B8A8)에서 잘린 밝은 면을 번지게 해 계단을 지웁니다. 2D의 `forward2dpipeline.xml` 과 같은 이유입니다.
  장면 색이 LDR이라 빛 세기와 환경광의 합이 1을 넘으면 기본색이 잘립니다. 툰 장면은 주 광원 세기를 1 근처로 둡니다(쇼케이스 `game/empty/maps/toonshowcase.scene.xml`).

밝기 단계 수는 `RenderPassGpuTest.ToonShadingHasFewerBrightnessLevelsThanLit` 가 픽셀로 확인합니다. 밝기 히스토그램에서 1%를 넘는 구간 수가 툰은 2, 램버트는 34입니다.

### 예: 메시 외곽선 — 인버티드 헐(inverted hull)

외곽선은 파이프라인의 `MeshOutline` 패스가 그립니다. 같은 `toon.hlsl` 의 다른 퍼뮤테이션(`SW_PASS_MESH_OUTLINE`)이 정점을 노멀 방향으로 밀고, 앞면을 컬링해 외곽선 색 하나를 냅니다.
뒤집어 그린 껍질이 원래 메시 밖으로 삐져나온 부분이 외곽선으로 보이는 방식입니다. 패스는 불투명 패스 뒤, 반투명 패스 앞에 선언합니다. 외곽선이 깊이를 써서 뒤의 반투명을 가리기 때문입니다.

- **머티리얼이 이 패스에 들어갈지를 정합니다.** 패스 종류 테이블의 `_pRequiredMaterialDefine`(`MATERIAL_OUTLINE`)이 없는 배치는 드로우, 머티리얼 PSO 변형, 셰이더 쿠킹에서 함께 빠집니다.
  판정은 `FrameRendererUtil::drawsMaterialInPass` 하나입니다. 그래서 외곽선을 모르는 셰이더가 이 패스의 앞면 컬링으로 그려지는 일이 없습니다([Renderer 문서](../../Renderer/README.md)).
- **컬은 테이블의 `kCullFront` 가 기본입니다.** XML의 "Back"은 이것을 바꾸지 않습니다. 양면 머티리얼도 껍질은 뒤집어 그립니다. 거울 배치는 Front와 Back이 바뀝니다.
- **두께**는 `outlineWidthMode` 로 고릅니다. 0은 월드 단위(미터, 노멀 방향으로 민다)이고 1은 화면 단위(화면 높이 비율)입니다.
  화면 단위는 클립 공간에서 투영한 노멀 방향으로 `width × 2 × w` 를 더해 거리와 무관한 두께를 냅니다. `outlineMaxDistance` 를 넘으면 가늘어지고, 가로세로 비는 `g_OutlineParams.yz` 로 맞춥니다.
- **색**은 `outlineColor × lerp( 1, 직접광 + 환경광, outlineLightingMix )` 입니다.
- **스키닝과 모프 결과를 밉니다.** 정점은 `swLoadMorphedVertex` 로 읽습니다(`RenderPassGpuTest.MeshOutlineFollowsSkinnedPose`).
- **외곽선 퍼뮤테이션은 정점 셰이더가 머티리얼을 읽습니다.** 두께가 정점 단계에 필요하기 때문입니다. 픽셀이 쓸 값(색, 알파 컷오프, 기본 텍스처)은 `nointerpolation` 필드로 넘깁니다.
  OpenGL은 두 단계가 구조체 버퍼를 읽으면 링크를 거부하기 때문입니다.
- 반투명 배치는 그리지 않고 불투명 목록만 그립니다. 와이어프레임 보기에서는 패스가 빠집니다.
- 큐브처럼 모서리가 각진 메시는 노멀이 갈라져 껍질에 틈이 생깁니다. 인버티드 헐 방식의 한계입니다.

`RenderPassGpuTest.MeshOutlineDrawsDarkRingAroundSilhouette` 가 포워드와 디퍼드에서 외곽선을 확인합니다. 외곽선을 끈 이미지의 배경 위치 중 켠 이미지에서 그려진 픽셀이 외곽선입니다.
이 고리가 실루엣 띠 안에 있고, 어둡고, 안쪽 픽셀은 그대로인지를 봅니다.

## 확장하는 법

### 새 머티리얼 셰이더 만들기

1. 셰이더에 `SW_MATERIAL_BEGIN … SW_MATERIAL_END` 로 머티리얼 구조체를 선언하고, 머티리얼은 한 스테이지에서만 읽습니다([Shader 문서](../Shader/README.md)의 "새 셰이더 쓰기").
2. 기존 `.material` 파일을 복사해 `shaderPath` 와 `_properties` 를 그 셰이더에 맞춥니다. 프로퍼티 이름과 `shaderType` 은 셰이더 멤버와 같아야 합니다.
3. 머티리얼이 쓰는 셰이더는 `Renderer/Cook/ShaderCookRequest.cpp` 의 엔진 셰이더 목록에도 넣습니다.
4. `App.exe --cook-shaders` 로 쿠킹하고 바이너리와 함께 커밋합니다.

### 정적 스위치 더하기

1. 셰이더에서 `#if MATERIAL_<이름>` 으로 분기합니다.
2. 머티리얼 파일의 `_staticSwitches` 에 `<item name="..." keyword="MATERIAL_<이름>" bEnabled="0" bShaderFeature="1"/>` 을 넣습니다. 실행 중에 바꿀 스위치면 `bShaderFeature="0"` 으로 둡니다.
3. 스위치를 더하거나 지우면 퍼뮤테이션 해시가 바뀌므로 `--cook-shaders` 를 다시 돌립니다.

## 함정과 주의

**머티리얼 파일을 처음부터 손으로 쓰지 마세요.** `_permutations` 를 빼먹으면 네 백엔드가 각자 다른 방식으로 그리기에 실패해 렌더러 버그로 보입니다. 기존 파일을 복사하거나 에디터에서 저장합니다.
테스트에서도 손으로 쓴 XML 대신 실제 에셋을 로드하고, 값은 `MaterialInstance` 의 `setVectorParameter` 같은 함수로 바꿉니다. 셰이더 경로나 머티리얼 XML을 C++ 코드에 하드코딩하지 않고 `Resource/engine/` 의 에셋을 씁니다.

**바인드리스 테이블을 바꾸는 일은 렌더 스레드의 병렬 기록과 겹치면 안 됩니다**(`IRHIDevice::setParallelRecording`).
게임 스레드의 `MaterialCache::initializePending`(씬 로드나 처음 쓰는 머티리얼의 스폰)은 렌더 스레드가 지난 프레임을 기록하는 동안 돕니다.
그래서 `EngineLoop` 은 올릴 것이 있는 프레임(`hasPendingInitialize`)에만 `RenderThread::waitIdle` 로 기다립니다.
어기면 `registerBindlessResource` 의 "병렬 패스 기록 중" 단언이나 크래시가 납니다. 단언은 플래그를 경합으로 읽으므로 같은 실행도 바이너리에 따라 0%와 100%를 오갑니다.
의심되면 `IRHIDevice::assertRegistryMutableNow` 에서 콜스택을 파일로 남겨 봅니다. 비동기 로거는 크래시 직전 줄을 잃습니다.

**텍스처를 다시 로드하면 같은 `Texture2D` 가 새 SRV 인덱스를 받습니다.** 머티리얼은 `TextureCache::getReloadGeneration` 이 바뀐 것을 보고 `Material::refreshTextureBindings` 로 인덱스를 다시 받고,
렌더러는 `GpuSceneBuilder::refreshReloadedTextures` 로 반영합니다. DirectX 11과 OpenGL은 인덱스를 바로 재사용해서 이 종류의 결함이 숨으므로 DirectX 12나 Vulkan으로 확인합니다.
디바이스 없이 잡은 머티리얼은 `MaterialCache::requestInitialize` 로 표시해야 GPU에 올라갑니다.

**`Material::forgetRhi` 는 `releaseRhi` 와 같은 상태를 남겨야 합니다.** 빌린 텍스처 목록이 남으면 DirectX 11과 OpenGL의 t5..t8 서수가 밀립니다.
디바이스 세대 번호나 "전체 GPU 해제", "전체 재초기화" 같은 함수를 다시 만들지 않습니다. 디바이스 수명은 `RHIRenderResource` 통보가 처리합니다.

**화면에 마젠타 · 검정 체커가 보이면 렌더러보다 데이터 경로를 먼저 봅니다.** 못 읽은 텍스처는 `EngineDefaultAssets::_missingTexture`, 못 읽은 머티리얼은 `_missingMaterial` 을 빌립니다(Shipping 도 같습니다). 머티리얼은 요청 경로(`_listAcquiredTexturePath`)와 실제로 빌린 경로(`_listBorrowedTexturePath`)를 따로 적고, 찾기 · 놓기는 빌린 경로로 합니다. 그 텍스처 파일을 나중에 만들어도 핫 리로드는 체커를 바꾸지 않습니다 — 캐시에 있는 경로만 다시 읽으므로 머티리얼을 다시 엽니다. `MeshComponent` 는 요청 경로(`_requestedMaterialPath`)를 기억해 같은 요청을 다시 시도 · 경고하지 않습니다.

**셰이더를 다시 쿠킹하면 머티리얼 상수 버퍼가 커질 수 있습니다.** `MaterialInstance` 는 `_constantByteSize` 로 버퍼를 다시 만듭니다. 상수 버퍼 필드 크기는 리플렉션, 쓰는 크기는 XML의 `shaderType` 에서 오므로 둘이 어긋나면 옆 프로퍼티의 색이 오염됩니다.

## 더 볼 곳

- [Graphics](../README.md): 머티리얼 인스턴스로 색 바꾸기 따라 해 보기
- [Shader](../Shader/README.md): 머티리얼 버퍼(t9)의 바인딩, 쿠킹 규칙
- [Renderer](../../Renderer/README.md): 머티리얼 원소의 영속 ID, 머티리얼로 배치를 거르는 패스
- [툰 쇼케이스](../../../../Resource/game/empty/README.md): 툰과 PBR 구를 나란히 보는 씬

| 파일 | 내용 |
|---|---|
| `Material.h` | 머티리얼 에셋과 정적 스위치 |
| `MaterialInstance.h` | 값 덮어쓰기 |
| `MaterialCache.h` | 경로별 소유와 지연 초기화 |
| `MaterialTypes.h` | 프로퍼티, 스위치, 퍼뮤테이션 설명 |
| `Resource/engine/materials/defaultmaterial.material` | 가장 단순한 머티리얼 |
| `Resource/engine/materials/toon.material` | 정적 스위치가 있는 예 |
