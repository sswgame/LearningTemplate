# RenderPipelineXml

> 질문: 렌더 패스를 코드 없이 데이터로 어떻게 바꾸는가?
> 독자: 파이프라인 · 렌더 패스 XML 을 쓰거나 패스 코드를 고치는 사람과 에이전트.
> 결론: 파이프라인 XML 에 적힌 값이 곧 실행되는 값이다. 패스와 머티리얼은 같은 렌더 상태 칸을 쓰고 실행은 패스 → 머티리얼 순서로 합친다. 실행이 받아 줄 수 없는 조합은 무시되지 않고 읽기 오류다. 표(`RenderPassTypeInfo`)는 XML 이 없는 내부 슬롯, XML 이 비운 기본값, 머티리얼에게 내주는 범위만 정한다.

## 파일

| 파일 | 하는 일 |
|---|---|
| `<도메인>/pipeline/*.xml` (`RenderPipelineDesc`) | 첨부, 패스 그래프, 패스별 PSO 설정. 고르는 법은 `-gv_renderPipeline=` 또는 팩 설정 |
| `engine/renderpass/*.xml` (`RenderPassDesc`) | 여러 파이프라인이 함께 쓰는 묶음(첨부 · 프레임 자원 · 프레임 패스). `_listRenderPassRef` 로 가져오면 읽을 때 같은 목록 앞에 합쳐진다. 프레임 패스 한 벌은 `framecomputerenderpass.xml` |
| `engine/renderstate/blendpresets.xml` (`RenderBlendPresetTable`) | 블렌드 프리셋(Opaque · Transparent · Additive · Multiply · Premultiplied). 줄을 더하면 코드 없이 새 프리셋이다 |
| `*.material` (`MaterialDef`) | 머티리얼. 렌더 상태 칸은 패스와 같은 형식이다(아래 머티리얼 칸) |

이름이 겹치는 첨부는 오류다 — 파이프라인이 덮어쓰지 않는다.

비슷한 파이프라인은 `_base="engine/pipeline/forwardpipeline.xml"` 로 부모를 가리키고 차이만 적는다. 패스 · 첨부 목록은 `_name` 으로 합치고(`_op="Remove"` · `_op="Replace"` · `_before` · `_after`), 쿠킹이 펼친다. 규칙은 [DataInheritance](DataInheritance.md).

## 첨부 칸

| 칸 | 뜻 | 받아 주지 않는 조합(읽기 오류) |
|---|---|---|
| `_format` | `RHIFormat` 이름 | 그 밖의 글. 렌더 타깃이 될 수 없는 형식 |
| `_loadOp` | 프레임에 처음 여는 패스의 로드 동작. `Clear` · `Load` · `DontCare`. 뒤의 패스는 언제나 `Load` 다 | 그 밖의 글 |
| `_storeOp` | 여는 패스의 저장 동작. `Store` · `DontCare`(DX12 · Vulkan 은 버리고 DX11 · GL 은 그대로 둔다) | `DontCare` 인데 읽는 패스가 있거나 여는 패스가 둘 이상일 때 |
| `_clearColor` · `_clearDepth` · `_clearStencil` | 지우는 값 | |

## 패스 칸

칸은 리플렉션 enum 이다. 받는 철자는 enum 하나에서 나오고 오타는 읽기 오류다.

| 칸 | 뜻 | 받아 주지 않는 조합(읽기 오류) |
|---|---|---|
| `_type` | `RenderPassType` | 그 밖의 글. 내부 슬롯 |
| `_depthAttachment` | 붙일 깊이 첨부 | 깊이 테스트를 끄고 적었을 때 |
| `_blendPreset` | 모든 렌더 타깃의 블렌드 프리셋. 비면 불투명. 곧은 알파인지 프리멀티플라이인지는 셰이더의 출력 규약에 맞춘다(캔버스 · 안개는 `Premultiplied`) | 없는 이름. `_listBlendTarget` 과 함께. 컴퓨트 패스. 반투명 패스에서 끄면 경고만 한다 |
| `_listBlendTarget` | 렌더 타깃마다의 `BlendTargetState`. 하나면 모든 타깃에 같다 | 둘 이상인데 컬러 출력 수와 다를 때. 알파 계수 자리의 색 계수 |
| `_shaderPath` · `_listPermutation` | 셰이더와 define. 비면 표의 기본 셰이더 | 컴퓨트 패스의 빈 `_shaderPath` |
| `_vertexEntryPoint` · `_pixelEntryPoint` | 진입점. 비면 `VSMain` · `PSMain` | |
| `_geometryEntryPoint` | 적으면 지오메트리 스테이지가 붙는다 | 머티리얼 셰이더 패스, 컴퓨트 패스 |
| `_hullEntryPoint` · `_domainEntryPoint` | 함께 적으면 테셀레이션이 붙고 제어점 셋의 패치로 그린다 | 한쪽만 적었을 때, 머티리얼 셰이더 패스, 컴퓨트 패스 |
| `_computeEntryPoint` · `_threadGroupSize` | 컴퓨트 패스의 진입점과 그룹 크기(셰이더의 `numthreads` 와 같게) | 컴퓨트 패스 밖의 진입점 |

메시 · 증폭 스테이지는 지원하지 않는다(적으면 모르는 칸이라 오류다).

## 렌더 상태 칸

패스(`RenderGraphPassDesc`)와 머티리얼 루트가 같은 칸을 쓴다. 정본은 `FixedRenderState` · `BlendTargetState` 이고 패스 XML 은 C++ 이름 그대로(`_cullMode`), 머티리얼 XML 은 앞의 `_` 와 불리언 `b` 를 뗀 이름(`cullMode`, `enableStencil`)이다.

| 묶음 | 칸 | 뜻 · 조합 규칙 |
|---|---|---|
| 깊이 | `_bEnableDepthTest` | 깊이 첨부를 붙이는지이기도 하다. 켜면 `_depthAttachment` 가 있어야 하고 그 패스 종류가 깊이를 붙여야 한다 |
| | `_bEnableDepthWrite` · `_depthCompare` · `_depthBias` · `_slopeScaledDepthBias` | 쓰기 · 바이어스는 깊이 테스트가 있어야 한다. 비교의 기본은 `LessEqual` |
| | `_bEnableDepthClip` | 끄면 평면 밖 기하를 눌러 그린다. Vulkan 은 장치 기능 `depthClamp` 이 없으면 PSO 를 만들지 않는다 |
| 래스터 | `_cullMode` · `_fillMode` · `_frontFace` | 풀스크린 패스의 컬은 `None` 이어야 한다 |
| 스텐실 | `_bEnableStencil` · `_stencilRef` · `_stencilReadMask` · `_stencilWriteMask` | 깊이 테스트가 있어야 첨부가 있다(모두 통과시키려면 `_depthCompare="Always"`). 마스크 · 참조는 면마다 있지 않다(D3D 와 같다) |
| | `_stencilFront*` · `_stencilBack*`(`Fail` · `DepthFail` · `Pass` · `Compare`) | 앞면 · 뒷면의 연산과 비교. 첨부의 스텐실은 깊이와 같은 로드 · 저장 동작이다 |
| 블렌드 | `_srcColor` · `_dstColor` · `_colorOp` · `_srcAlpha` · `_dstAlpha` · `_alphaOp` · `_colorWriteMask` · `_bEnableBlend` | 렌더 타깃 하나의 값(`BlendTargetState`) |

## 머티리얼 칸

머티리얼은 위 렌더 상태 칸 중 **적은 칸만** 패스 위에 덮는다. 적지 않은 칸은 패스의 값이다. 실행은 패스 → 머티리얼 → 보기 모드 → 컬 반전 순서로 합쳐 변형 PSO 를 만든다(`FramePSOManager::createMaterialPSOVariant`, `RenderStateOverride::applyTo`).

| 칸 | 뜻 | 받아 주지 않는 조합(읽기 오류) |
|---|---|---|
| `alphaMode` | `Opaque` · `Mask`(불투명 목록, 문턱 아래 알파를 버린다 — `MATERIAL_ALPHA_CUTOFF`) · `Blend`(반투명 목록) | 그 밖의 글. define `MATERIAL_ALPHA_CUTOFF` 를 직접 적었을 때 |
| `alphaCutoff` | `Mask` 의 문턱. `_alphaCutoff` 프로퍼티로 간다 | `Mask` 가 아닌데 기본값(0.5)이 아닐 때. 프로퍼티가 없는 셰이더의 기본값 아닌 값 |
| `renderQueue` | 그리기 순서(작은 것부터, 기본 0). 반투명 정렬의 첫 키, 불투명은 띠 안의 순서다. 불투명의 띠는 렌더 상태가 정한다(`OpaqueDrawBand`) — 보통 → 스텐실 쓰기(마스크) → 그려진 것을 읽거나 일부만 덮는 것(채널 일부 쓰기 · 블렌드 · 스텐실 비교 · 깊이 Less/LessEqual 밖 · 깊이 쓰기 끔). 순서에 기대는 머티리얼에 차례를 적지 않는다 | |
| `twoSidedLighting` | 뒷면에서 노멀을 뒤집는다(`MATERIAL_TWO_SIDED`) | `cullMode="None"` 없이 적었을 때. define 을 직접 적었을 때 |
| `blendPreset` · `_listBlendTarget` | 패스의 블렌드를 덮는다 | 함께 적었을 때. 없는 이름. 블렌드가 켜진 값인데 `alphaMode` 가 `Blend` 가 아닐 때(쓰기 마스크만 바꾸는 값은 불투명도 받는다) |
| `enableDepthTest` | 없음 — 첨부와 함께 움직여 패스의 것이다 | 적었을 때 |

무엇을 덮을 수 있는지는 패스 종류의 표가 정한다(`RenderPassTraitFlag::kMaterialFixedState` · `kMaterialBlend`, `FrameRendererUtil::getMaterialStateScope`).

| 패스 | 컬 · 앞면 방향 | 깊이 · 스텐실 · 채우기 | 블렌드 · 쓰기 마스크 | 까닭 |
|---|---|---|---|---|
| ForwardOpaque · ForwardOnlyOpaque · Transparent | 받는다 | 받는다 | 받는다 | |
| GBuffer | 받는다 | 받는다 | 받지 않는다 | 첨부 셋의 의미가 정해져 있다 |
| Shadow · DepthPrepass · 그 밖의 씬 메시 패스 | 받는다 | 받지 않는다 | 받지 않는다 | 깊이의 모양이 색 패스와 같아야 한다 |

- 패스의 컬이 `Back` 이 아니면 컬은 패스의 것이다. 뒤집은 껍질 외곽선은 양면 머티리얼도 앞면을 잘라야 외곽선이다.
- 깊이 첨부가 없는 패스는 깊이 쓰기 · 바이어스 · 스텐실을 받지 않는다. 깊이 테스트는 언제나 패스의 값이다.
- `MaterialInstance` 는 값 · 텍스처 · 키워드만 덮는다(문턱은 프로퍼티라 덮을 수 있다). 렌더 상태 · `alphaMode` · `renderQueue` 가 다르면 `parent=` 로 자식 머티리얼을 만든다. 드로우마다 PSO 를 가르면 인스턴스를 머티리얼 원소로 합치는 이득을 잃는다(언리얼 Dynamic Instance · 유니티 MaterialPropertyBlock 과 같다, 기억에 의존한 비교).
- 렌더 칸은 퍼뮤테이션 해시에 섞여 배치 키와 PSO 변형을 가른다(`Material::getRenderFieldsHash`). 칸을 적지 않은 머티리얼의 해시는 define 만의 값이다.

## 화면 속도

TAA · 모션 블러가 읽는 물체별 화면 속도(이번 UV - 지난 UV)는 파이프라인이 첨부로 선언한다. 첨부는 `SceneVelocity`(역할 Velocity, `R16G16_FLOAT`, 클리어 0 — G버퍼 명세 `GBufferLayout` 판 2 의 넷째 칸과 같은 값)다.

| 누가 | 어떻게 | 받아 주지 않는 조합(읽기 오류) |
|---|---|---|
| 쓰는 패스 | 깊이를 쓰는 불투명 지오메트리 패스(`RenderPassTraitFlag::kWritesVelocity` — GBuffer · ForwardOpaque · ForwardOnlyOpaque)가 `_listOutput` 에 적는다. G버퍼는 넷째 MRT, 포워드는 [색, 속도] 둘째 출력이다. 포워드는 선언이 있을 때만 `SW_PASS_VELOCITY` 로 쓴다 — 깊이 프리패스 뒤의 내부 슬롯도 ForwardOpaque 의 선언을 따른다 | 다른 패스가 적을 때. 포맷 · 클리어 값이 다를 때. 포워드에서 둘째 출력이 아닐 때 |
| 읽는 패스 | TAA · MotionBlur 의 필수 입력이다(`_listInput`) | 적지 않았을 때 |

그리지 않은 픽셀(하늘)은 속도가 0 이고, 읽는 쪽은 깊이 1 인 픽셀을 지난 뷰-투영으로 카메라 재투영한다. 반투명 · 외곽선 껍질은 속도를 쓰지 않는다(언리얼 기본값과 같다). 속도를 읽는 패스가 없는 파이프라인(2D)은 첨부도 쓰기도 두지 않는다.

## 화면 왜곡

충격파 · 아지랑이 · 유리 굴절처럼 뒤의 장면을 비껴 보이게 하는 효과는 두 패스다(언리얼 Distortion 패스 · 유니티 왜곡과 같은 자리).

| 패스 | 하는 일 | 선언 |
|---|---|---|
| `Distortion` | 머티리얼에 `MATERIAL_DISTORTION` 이 있는 반투명 배치만 왜곡 훅(`computeMaterialDistortion`)으로 화면 UV 오프셋을 더한다(가산 블렌드, 깊이는 테스트만) | 출력 `SceneDistortion`(역할 Distortion, `R16G16_FLOAT`, 클리어 0), `_depthAttachment`, `_blendPreset="Additive"` |
| `DistortionApply` | 장면 색 사본을 오프셋만큼 비껴 읽어 장면 색에 쓴다. 비껴 읽을 자리가 이 픽셀보다 가까우면 끌어오지 않는다 | 입력 SceneColorCopy · SceneDistortion · SceneDepth(필수), 출력 장면 색 |

- 엔진 파이프라인은 장면 색 사본 뒤 · 반투명 앞에 둔다 — 반투명은 깊이를 쓰지 않아 뒤에 두면 앞의 반투명이 끌려 들어온다(언리얼은 반투명 뒤).
- 씬에 왜곡 배치가 없으면 두 패스 모두 기록하지 않는다(`GPUScene::hasDistortionBatches`). 장면 색 사본이 없는 파이프라인(2D)은 두 패스를 지운다.
- 머티리얼: `_alwaysDefines` 에 `MATERIAL_DISTORTION` · `MATERIAL_BLEND_TRANSLUCENT`, 셰이더는 `SW_MATERIAL_CUSTOM_DISTORTION` 과 훅을 정의한다(본보기 `engine/materials/distortion.material` · `engine/shaders/distortion.hlsl`). VFX 메시 입자는 방출기 메시 머티리얼을 왜곡 머티리얼로 두면 같은 길로 그린다. 물의 굴절은 자기 길(반투명 패스가 사본을 읽는다) 그대로다.

## 컴퓨트 패스

`_type="Compute"` 패스는 셰이더 하나로 효과를 더한다. 선언: 컬러 출력 하나, 입력 최대 둘(깊이 형식 제외 — 깊이는 `SceneDepthCopy` 를 읽는다), 입력과 출력은 겹치지 않는다. 첨부는 RW 텍스처로 묶이고 값은 `compute_pass.hlsli` 의 루트 상수로 받는다. GL 은 묶은 포맷과 셰이더 선언이 같아야 하므로 include 전에 `SW_RW_SLOT0_GL_FORMAT` 을 적는다. 예: `game/empty/pipeline/computepasspipeline.xml`.

## 후처리 값 · 볼륨

뷰마다의 후처리 값은 `PostProcessValues` 한 벌이다(`Engine/Graphics/View`). 카메라 값이 바탕이고, 카메라가 선 자리의 `PostProcessVolumeComponent` 들이 우선순위가 낮은 것부터 **덮어쓴 칸만** 섞는다(`PostProcessVolumeComponent::resolveAt`, 뷰 수집 `RenderViewCollector`). 언리얼 Post Process Volume 과 같은 칸이다 — 모양(Box · Sphere · Unbound), Priority, Blend Weight, Blend Radius(모양 밖으로 1 → 0), 칸마다 `bOverride_*`(여기서는 `PostProcessOverrides::_b<칸>`). 언리얼과 달리 카메라 값이 볼륨 아래다 — 카메라 값은 프리셋 · 포즈 블렌드가 쓰는 샷의 룩이다.

| 할 일 | 고칠 곳 |
|---|---|
| 값 칸을 더한다 | `PostProcessValues` 에 칸, `PostProcessOverrides` 에 같은 차례로 `_b<칸>` 비트와 생성자 줄. 섞기 · 포즈 블렌드는 리플렉션으로 따라온다(테스트 `PostProcessVolumeTest.OverrideFlagsPairEveryValue` 가 짝을 본다). 섞을 수 없는 형식(경로)은 `blendOverrides` 에 주소로 고르는 줄을 더한다(절반에서 바꾼다) |
| 셰이더가 읽는다 | 기본값은 지금 그림을 바꾸지 않는 중립이어야 한다. 패스 상수는 `FramePassExecutor::applyViewPassConstants` 가 뷰마다 채운다 |
| 텍스처를 가리킨다(LUT · 먼지 마스크) | 칸은 `DataPath`, 로드는 `FramePostProcessTextureSet::findReady`(비동기 — 읽는 동안 그 효과는 빠진다), 셰이더 칸은 엔진 텍스처 표(`shaderslot::emulated::kEngineTextureNames`)의 이름이다. 에뮬(DX11 · GL)의 엔진 텍스처 칸은 GL 유닛 0..15 안이라 둘이 더 남는다 |

컬러 그레이딩은 톤맵하는 셰이더(Tonemap 패스 · 후처리 체인 `SW_POST_TONEMAP`) 안의 함수다 — 노출 → 화이트 밸런스 · 범위 보정(선형 HDR) → 톤맵 곡선 → LUT → 채도 · 대비 · 비네트 · 레터박스. 언리얼은 그레이딩을 LUT 하나로 구워(PostProcessCombineLUTs) 톤맵이 읽지만, 여기서는 뷰마다 값이 적어 굽는 패스(왕복 하나)를 두지 않고 픽셀마다 푼다. 값이 모두 중립이면 C++ 가 켬 상수를 0 으로 두어 셰이더가 건너뛴다. 후처리를 끈 뷰(`SW_PASS_FLAG_SKIP_POST`)는 톤맵만 한다.

렌즈 왜곡 · 색수차 · 선명화 · 필름 그레인도 같은 셰이더 안이다(언리얼도 색수차 · 그레인 · 선명화를 톤맵 패스에서 한다). 앞의 셋은 원본을 다시 읽어야 해서 원본을 읽는 자리(`CameraGrade::applyLens`), 그레인은 LUT 뒤다. 패스를 따로 두지 않는 까닭은 왕복 하나(720p 약 57 us)다. 세기가 모두 0 이면 원본을 다시 읽지 않는다. 후처리 체인(`post_chain.hlsl`)은 렌즈를 블룸 앞에서, 단계 파이프라인의 Tonemap 패스는 블룸 뒤 원본에서 하므로 렌즈를 켠 두 파이프라인의 그림은 조금 다르다.

블룸 먼지 마스크 · 렌즈 플레어는 블룸을 더하는 함수(`post_bloom.hlsli`) 안이다 — 전용 Bloom 패스와 후처리 체인이 같은 코드를 쓴다. 플레어는 밝은 부분을 화면 가운데에 대해 뒤집은 선 위의 고스트 넷과 헤일로 고리에서 다시 읽는다(반해상도 밝은 부분 `BloomBright` 가 있으면 그것을, 없으면 원본에서 구한다). 언리얼처럼 블룸 다운샘플 사슬의 여러 단을 쓰지 않는다 — 단이 하나라 고스트가 덜 흐리다.

### 후처리 체인과 디퍼드

포워드 파이프라인은 블룸 · 외곽선 · 톤맵을 Present 한 패스(`post_chain.hlsl`, `SW_POST_*`)로 합쳐 왕복 둘(720p 약 114 us)을 아낀다. 디퍼드 기본 파이프라인은 합치지 않는다 — 합치면 그림이 같지 않아서다(골든이 바뀐다).

| 차이 | 디퍼드 단계 파이프라인 | 후처리 체인으로 합치면 |
|---|---|---|
| 순서 | 블룸 · 외곽선 → TAA → 피사계 심도 → 모션 블러 → 톤맵 | 체인은 한 패스라 TAA · 피사계 심도 · 모션 블러 **뒤**에서만 돈다 — 외곽선이 흐림 위에 선명하게 남고, 블룸이 TAA 누적 · 모션 블러를 받지 않는다 |
| AO | PostBloom 이 AOColor 를 곱한다 | 체인에 AO 입력이 없다 |
| 품질 단계 | PostBloom 은 `_minPostQuality="1"` 이라 low 에서 원본을 넘긴다 | 체인의 효과는 퍼뮤테이션이라 품질로 끌 수 없다 |
| 렌즈 | 톤맵 패스가 블룸 뒤 원본에서 | 체인은 블룸 앞에서(위 렌즈 문단) |

합치려면 TAA · 피사계 심도 · 모션 블러를 블룸 앞에 두는 파이프라인을 따로 두고(포워드와 같은 순서) 골든을 새로 찍는다. 기본을 바꾸지 않는 까닭은 위 넷이 보이는 차이라서다. 언리얼도 블룸을 TAA 뒤, 톤맵(후처리 합성) 앞에 두므로 포워드 쪽 순서가 언리얼에 가깝다(기억에 의존한 비교).

화면 공간 반사(`ScreenSpaceReflection`, 디퍼드)는 안개와 같은 모양이다 — 장면 색 사본 · 깊이 · G버퍼 노멀을 읽어 장면 색 **위에 블렌드**하고, 세기 0 · 후처리를 끈 뷰에서는 그리지 않는다(전용 실행). 꺼진 효과의 다른 길(`kBypassesToSource` — 출력 이름이 원본을 가리킴)은 출력을 그 패스만 써야 해서(로드 검증) 반투명이 같은 장면 색에 그리는 자리에는 쓸 수 없다. HZB 가 없어 광선은 같은 걸음으로 나아간다(언리얼은 HZB 로 빈 칸을 건너뛴다) — HZB 를 더하면 `classifyRayPoint` 의 걸음을 밉 단으로 바꾼다.
## 디퍼드 데칼

`DecalComponent`(컴포넌트 변환 = 상자, 로컬 -Y 로 투영)가 G버퍼를 덮는다(언리얼 Deferred Decal · 유니티 HDRP Decal Projector). 길 · 흙 · 손상처럼 지형 · 벽 위에 얹는 그림이다.

| 칸 | 뜻 | 받아 주지 않는 조합(읽기 오류) |
|---|---|---|
| `_type="Decal"` | 데칼마다 풀스크린 하나로 장면 깊이에서 위치를 복원해 상자 안 픽셀만 섞는다(decal.hlsl). 입력은 장면 깊이 | 출력이 G버퍼 알베도 · 노멀 · 머티리얼 셋(이 차례)이 아님(3-9) |
| `_listBlendTarget` | 곧은 알파 셋 — 쓰기 마스크가 알베도 알파(셰이딩 모델 번호) · 머티리얼 b · a(반사율 · 가림)를 지킨다 | |

- 칸마다의 덮임은 셰이더 출력 알파다(데칼 `_channels` 비트 × 불투명도 × 텍스처 알파 × 각도 · 끝 페이드). 노멀 텍스처가 없으면 노멀 칸을 끈다.
- 텍스처는 렌더 스레드가 경로로 읽는다(후처리 텍스처와 같은 묶음) — 읽는 동안 그 데칼은 빠진다. G버퍼 뒤 · 조명 · SSAO 앞에 둔다(엔진 디퍼드).
- 포워드 파이프라인은 덮을 G버퍼가 없어 그리지 않는다(렌더러가 한 번 경고). DBuffer(언리얼)는 모든 포워드 머티리얼 셰이더가 데칼 버퍼 셋을 읽어야 해 두지 않았다.
- shortcut: 데칼마다 화면 전체를 돈다 — 수십 개를 넘으면 상자 래스터 · 타일 목록으로 바꾼다.

## 후처리 머티리얼

카메라 · 볼륨의 블렌더블 목록(`PostProcessValues::_listBlendable` — 경로 · 무게 · 자리)이 그리는 머티리얼이다(언리얼 Post Process Material · Blendable Location). 볼륨은 목록 칸(`_bListBlendable`)을 켜면 자기 항목을 볼륨 무게로 **더한다**(같은 경로 · 자리는 무게를 섞는다 — 아래 항목은 남는다). 포즈 블렌드는 앞 카메라에만 있던 항목을 넘어가는 만큼 줄인다.

| 칸 | 뜻 | 받아 주지 않는 조합(읽기 오류) |
|---|---|---|
| `_type="PostProcessMaterial"` | 그 자리의 블렌더블을 목록 차례로 원본 위에 무게만큼 그린다(프리멀티플라이). 입력은 원본(필수)과 깊이 · G버퍼(선택 — 그래프 "Post Process" 노드가 읽는다) | `_shaderPath`(셰이더는 머티리얼의 것) |
| `_blendableLocation` | `BeforeTonemap` · `AfterTonemap` · `ReplacingTonemap`. 그 패스만 적는다 | 그 패스의 None · 다른 패스의 값 · 같은 자리 둘 · 톤맵 대신 자리의 출력을 앞의 패스가 쓰지 않음(3-8) |

- 톤맵 앞 · 뒤: 원본을 출력에 복사하고 그 위에 그린다. 그릴 머티리얼이 없는 뷰는 출력이 원본을 가리킨다(`kBypassesToSource` — 출력은 원본과 같은 형식, 3-6). 후처리를 끈 뷰도 같다.
- 톤맵 대신: HDR 원본을 읽어 톤맵 출력 위에 무게만큼 덮는다 — 출력을 톤맵과 함께 써서 늘 돌고, 그릴 것이 없으면 아무것도 하지 않는다.
- 머티리얼은 PostProcess 영역이다(`_alwaysDefines` 의 `MATERIAL_DOMAIN_POST_PROCESS`, 셰이더 뼈대 `post_process_material.hlsli` — 본보기 `engine/materials/postprocess/colortint.material`). 아니면 게임 스레드가 오류로 거절한다(`PostProcessMaterialSet`).
- 렌더 스레드는 머티리얼마다 원소 버퍼 하나와 확장 프로그램 PSO(출력 형식)를 셋업에서 만든다(`FramePostProcessMaterials`). 무게는 그리기마다의 패스 상수 `g_PostProcessMaterial.x` 다.
- shortcut: 같은 자리의 머티리얼은 모두 패스의 원본을 읽는다(앞 머티리얼의 결과를 읽지 않는다 — 언리얼은 이어 읽는다).
- 엔진 디퍼드 파이프라인이 세 자리를 모두 둔다. 포워드 파이프라인은 톤맵이 Present(후처리 체인) 안이라 톤맵 뒤 · 대신 자리가 없다 — 쓰려면 자식 파이프라인이 Present 앞에 톤맵 앞 자리 패스를 더한다.

## 프레임 그래프

뷰 그래프(`_listPass`)는 뷰(주 시점 · 추가 뷰)마다 다시 돈다. 프레임에 한 번 모든 뷰의 몫을 기록하는 일(깊이 히스토리 · 가림 타일 · 입자 · 인스턴스 애니메이션 · 모프 · 스킨 · 컬링 · 라이트 그리드 · 볼륨 안개 · 프로브 · 마커)은 프레임 그래프다 — 뷰 그래프보다 먼저 큐에 낸다(`FrameGraphExecutor`). 언리얼 RDG 는 한 그래프에 둘을 넣지만 이 엔진의 뷰 그래프는 뷰마다 다시 돌아 범위를 가른다.

| 칸 | 뜻 | 받아 주지 않는 조합(읽기 오류) |
|---|---|---|
| `_listResource` 의 `RenderGraphResourceDesc` | 프레임 패스가 읽고 쓰는 자원 이름과 `_kind`. 그래프는 버퍼를 만들지 않고 순서만 정한다(주인은 서브시스템) | 이름이 겹침 · 첨부와 겹침. `_kind="Transient"`(첨부의 몫) |
| `_kind="Persistent"` | 프레임을 넘는 히스토리(깊이 히스토리 · 가림 타일 · 입자 상태 · 프로브). 앞선 쓰기 없이 읽으면 지난 프레임 값이고, 이번 프레임 첫 쓰기보다 앞에 온다 | |
| `_kind="External"` | 서브시스템이 프레임마다 채우는 버퍼(인스턴스 · 라이트 · 컬링 결과 · 그리드). "쓰지 않고 읽는다" 경고가 없다 | |
| `_listFramePass` 의 패스 | `_type` 은 프레임 패스 종류(`isFramePassType`). 입출력은 `_listResource` 이름 | 뷰 패스 종류 · 같은 종류 둘 · 자원이 아닌 이름 · 셰이더 · 진입점 · 퍼뮤테이션 칸(셰이더는 표의 것) · `_depthAttachment` · `_minPostQuality` · 뷰 패스와 같은 이름 |

- 선언하지 않은 종류는 PSO 도 없고 돌지 않는다. 그 종류가 채우는 버퍼를 준비하는 쪽(간접 인자를 컬링에 맡김 · GPU 라이트 그리드 · 볼륨 안개 · 프로브 · 스킨 풀)도 PSO 가 없으니 준비하지 않는다(`FramePSOManager::getEnginePSO` 가 선언을 본다).
- 각 패스는 자기 버퍼의 배리어를 스스로 건다. 깊이 히스토리 패스는 끝에 히스토리를 RW 텍스처 상태로 넘겨 둔다(읽는 쪽 둘 다 RW 텍스처로 읽고, 비동기 컴퓨트 큐는 렌더 타깃 상태를 옮길 수 없다).
- 뷰 그래프의 패스는 프레임 그래프 결과(컬링 결과 · 그리드)를 입력으로 적지 않는다 — 프레임 그래프가 통째로 앞이다.

### 비동기 컴퓨트 큐(`_queue`)

| 칸 | 뜻 | 받아 주지 않는 조합(읽기 오류) |
|---|---|---|
| `_queue` | `Graphics`(기본) · `AsyncCompute`. 언리얼 `ERDGPassFlags::AsyncCompute` 자리 | 뷰 패스의 `AsyncCompute`. 컴퓨트가 아닌 프레임 패스(깊이 히스토리)의 `AsyncCompute` |

- 규칙: 능력 `RHICapabilities::_bAsyncCompute` 가 참인 백엔드(DX12 — COMPUTE 큐와 펜스 둘, Vulkan — 그래픽스 패밀리에 큐가 둘인 장치의 두 번째 큐와 이진 세마포어)에서만 따로 돈다. DX11 · GL · WebGPU · 큐가 하나인 Vulkan 장치는 그래픽스 큐에서 같은 순서로 돈다 — 능력 표가 정한 길이지 실패의 대체가 아니다. `-gv_asyncCompute=0` 은 측정용으로 모두 그래픽스 큐에 둔다.
- 포크 · 조인은 그래프가 간선으로 정한다(`RenderGraph::getQueueSteps`): 실행 순서에서 이어진 AsyncCompute 패스가 한 묶음(컴퓨트 리스트 하나)이고 그 앞의 그래픽스 일 뒤에 시작한다. 그래픽스 패스는 자기가 읽는 묶음만 앞에서 기다리고, 아무도 읽지 않은 묶음은 프레임 그래프 끝에서 기다린다. 프레임 끝(`endFrame`)은 남은 컴퓨트 일을 모두 기다린 뒤 프레임 펜스를 신호한다.
- 큐 사이 자원: 버퍼는 D3D12 에서 큐 경계마다 COMMON 으로 감쇠해 이전이 따로 없고, Vulkan 은 같은 패밀리라 소유권 이전이 없다. 텍스처는 생산자가 컴퓨트가 아는 상태로 넘겨 둔다(깊이 히스토리 → RW). 컴퓨트 큐 리스트의 배리어는 픽셀 셰이더 상태를 쓰지 않는다(`D3D12RHICommandContext::toQueueState`).
- 후보: 가림 타일 · 입자 · 라이트 그리드 · 프로브(`framecomputerenderpass.xml`). 이득이 없으면 그 패스의 `_queue` 한 줄을 지운다.

## 컴퓨트 구현(`_implementation`)

| 칸 | 뜻 | 받아 주지 않는 조합(읽기 오류) |
|---|---|---|
| `_implementation` | `Pixel`(기본, 풀스크린 삼각형) · `Compute`(같은 셰이더 파일의 `CSMain` — define `SW_PASS_COMPUTE`, 8x8 그룹 공유 타일) | 표에 컴퓨트 구현이 없는 종류(`kComputeImplementation` — 지금은 BloomDownsample · DepthOfField 만) |

- 규칙: 컴퓨트 구현은 입력을 bindless SRV 번호로 읽어 네이티브 bindless(DX12 · Vulkan)에서만 돈다. DX11 · GL · WebGPU 는 픽셀 구현이 돈다(능력이 정한 길 — 쿠커는 두 구현을 다 굽는다).
- 효과의 값(블룸 문턱 · 렌즈 · 역뷰-투영 4 열)은 루트 상수로 간다(`post_compute.hlsli`) — 컴퓨트 PSO 에는 PassCB 가 묶이지 않는다.
- 어느 쪽을 낼지는 측정이 정한다. 바꾸는 법은 한 줄이다: `deferredpipeline.xml` 의 `BloomDownsample` 패스, `deferredpipeline.xml` · `forwardpipeline.xml` 의 `DepthOfField` 패스에 `_implementation="Compute"` 를 적는다(자식 파이프라인은 물려받는다).

## 트랜지언트 앨리어싱

뷰 그래프의 첨부 중 레벨 수명이 겹치지 않고 서술(형식 · 나눗수 · RW 용도 · 클리어 값)이 같은 것은 텍스처 하나를 나눠 쓴다(`RenderGraphAliasPlan::build`, 계획은 `FramePassExecutor::buildTransientAliasPlan`). XML 에 적을 칸은 없다 — 수명은 패스의 입출력 선언이 정한다.

- 나누지 않는 것: 깊이 형식(장면 깊이는 다음 프레임의 깊이 히스토리가 읽는다 · 그림자 맵), `_loadOp="Load"`(프레임을 넘어 잇는다), TAA 출력, 처음 쓸 때 만드는 효과 출력(그 주인 · 별칭은 자기 텍스처다).
- 꺼지면 원본을 가리키는 효과(`kBypassesToSource`)는 원본과 출력을 한 구간으로 묶는다. NaN 감시(`-gv_nanCheck`)를 켠 실행은 나누지 않는다. 비교는 `-gv_transientAliasing=0`.
- 서술이 다른 첨부도 능력 `_bPlacedTextures`(DX12) 가 있으면 힙 하나에 겹쳐 놓는다 — 첫 쓰기가 지우기(`_loadOp="Clear"`)이고 RW 가 아닌 칸만, 백엔드의 크기 · 정렬로 수명이 겹치지 않는 바이트를 나눈다(`RenderGraphMemoryPlan`, `FrameViewResources::placeTransients`). 놓인 텍스처의 쓰기 전이 앞에 앨리어싱 배리어가 든다. Vulkan · DX11 · GL · WebGPU 는 같은 서술끼리만 나눈다(Vulkan 은 해제 큐의 메모리 참조 세기가 들 때까지).

## 선택 스테이지를 쓰는 셰이더

- 소스는 패스 셰이더 한 파일이다. 머티리얼 변형은 머티리얼 셰이더의 정점 출력으로 만들어져 신호가 맞지 않아 머티리얼 셰이더 패스에는 쓸 수 없다.
- 헐 · 도메인의 제어점 위치는 `SV_POSITION` 이 아니라 일반 값으로 넘긴다. 내장 위치로 넘기면 GL 에서 오류 없이 아무것도 그리지 않는다. 예: `game/empty/shaders/optional_stages.hlsl`.
- 이 스테이지는 패스 상수 · 루트 상수를 받지 않는다고 보고 쓴다. 필요하면 정점 스테이지에서 값으로 넘긴다.

## 머티리얼 변위(테셀레이션)

패스 XML 의 헐 · 도메인과 별개로, 머티리얼이 `_alwaysDefines` 에 `MATERIAL_DISPLACEMENT` 를 적으면 렌더러가 그 머티리얼의 변형 PSO 에 머티리얼 셰이더의 `HSMain` · `DSMain` 과 패치 토폴로지를 붙인다(`FrameRendererUtil::isMaterialTessellated` — 쿠커와 런타임이 같은 판정). 셰이더는 `material_displacement.hlsli`(뼈대가 보간 칸 VSMain 대신 include)다.

- 값은 머티리얼 칸이다: `_displacementMap`(R 높이) · `_displacementScale`(m) · `_displacementMidLevel` · `_tessellationDensity`(각 1 rad 당 분할 수) · `_tessellationMaxFactor`(64 이하). `pbrmaterial.material` 이 칸을 가진다.
- 변 계수는 변의 두 끝점(월드 위치 · 카메라 뷰 깊이)만으로 정해 틈이 없다. 식의 C++ 짝 `MaterialDisplacement` 를 단위 테스트가 지킨다.
- 그림자 · 깊이 프리패스도 머티리얼 셰이더로 같은 변위를 그린다(`usesMaterialShaderForDepth`). 뷰 깊이는 그림자 패스에서도 카메라의 값이라 계수까지 같다.
- 테셀레이션이 없는 백엔드는 변형을 만들지 않고 `FramePSOManager.DisplacementUnsupported` 오류를 남긴다 — 평면으로 대신 그리지 않는다. 능력은 `RHICapabilities::_bMaterialTessellation`: DX11 · DX12 참, Vulkan 은 장치 기능 `tessellationShader`, OpenGL 거짓(헐 · 도메인이 머티리얼 구조 버퍼를 읽으면 링크 거절 — binding.hlsli 9 절), WebGPU 거짓. 시나리오는 `<SkipUnless watch="RHI.MaterialTessellation">` 로 그 백엔드를 건너뛴다.
- 받지 않는 조합(셰이더 `#error`): 머티리얼 없는 셰이더, 사용자 보간 칸(`SW_MATERIAL_CUSTOM_VARYINGS`), 외곽선 패스.
- 언리얼 Nanite 변위 · 옛 테셀레이션과 같은 자리다 — 머티리얼이 높이 · 기준 높이를 주고 렌더러가 분할을 정한다(기억에 의존한 비교). 언리얼은 화면 크기 기준 분할이고 여기는 각 크기 × 밀도다.

## 화면 공간 GI · GTAO

- GTAO 는 SSAO 패스의 `_shaderPath="engine/shaders/gtao.hlsl"` 로 고른다(같은 AOColor — r 가림, gba 굽은 노멀). 값은 퍼뮤테이션 `SW_GTAO_RADIUS` · `SW_GTAO_SLICES` · `SW_GTAO_STEPS` · `SW_GTAO_INTENSITY` 이고 셰이더에 기본값이 없다.
- `_type="ScreenSpaceGI"`(ssgi.hlsl)는 입력 넷(SceneColorCopy · SceneDepth · GBufferNormal · GBufferAlbedo)을 읽어 조명 첨부에 `_blendPreset="Additive"` 로 더한다. 출력을 여러 패스가 쓰므로 뷰마다 건너뛰지 않는다(`_minPostQuality="0"`) — 켜고 끄는 것은 파이프라인이다. 값은 `SW_SSGI_RADIUS` · `SW_SSGI_DIRECTIONS` · `SW_SSGI_STEPS` · `SW_SSGI_INTENSITY`.
- 예: `engine/pipeline/deferredpipeline_gi.xml`(기본 디퍼드는 그대로).

## 렌즈 플레어

- `_type="LensFlare"`(lens_flare.hlsl)는 입력 SceneDepth 하나와 HDR 컬러 출력 하나, `_blendPreset="Additive"` 다. 기본 포워드 · 디퍼드 파이프라인이 안개 뒤 · 블룸(포워드는 피사계 심도) 앞에 둔다. 2D 파이프라인은 뺀다.
- 그릴 것은 데이터다: 빛 컴포넌트의 `_lensFlarePath` 가 `*.lensflare.xml`(`LensFlareAsset` — 원소의 축 위 자리 · 크기 · 색 · 모양, 가장자리 감쇠 폭, 가림 표본 반지름)을 가리킨다. 렌더 스레드가 주 시점에 투영해 스프라이트 버퍼(패스 버퍼 슬롯)를 채우고(`LensFlareSpriteBuffer`), 픽셀 셰이더가 빛 자리 둘레 깊이 표본 아홉으로 가린다. 플레어를 단 빛이 없으면 그리지 않는다.
- 블룸에서 고스트를 뽑는 화면 효과와 별개의 패스다(빛마다의 데이터 기반 — 유니티 SRP Lens Flare 자리).

## 고칠 때

렌더 상태 칸을 더할 곳은 `Engine/Graphics/RenderState/RenderState.h` 하나와 네 백엔드의 읽기다 — XML 읽기 · 쓰기는 리플렉션이, 패스 · 머티리얼 합치기와 해시 · 비교는 바이트가 따라온다(패딩이 생기면 `static_assert` 가 멈춘다). 조합 규칙은 `findInvalidReason`, 머티리얼에게 내주지 않을 칸은 `RenderStateOverride::applyTo` 에 적는다. RHI ABI 번호를 올린다.

그 밖의 패스 칸은 PSO(`FramePSOManager::makePassPSODesc`) · 쿠커(`ShaderCookRequestCollector`) · 검증(`RenderPipelineAsset::validate`)을 함께 고친다. 칸만 두고 실행이 읽지 않으면 XML 이 거짓말을 한다. 값이 그림을 바꾸는지는 끄고 켠 두 장을 같은 실행에서 견주는 시나리오로 확인한다(`<ExpectImagesDiffer>`, 예: `game/empty/automation/renderstate_stencil`).

쿠커 코드를 고칠 때 번호를 올릴 일이 없다 — 쿠킹 키는 소스 지문에서 산출한다(`Config/Dev/CookCodeInputs.json`). 새 쿠커는 이 표에 폴더를 적는다.
