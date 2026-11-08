# RenderPipelineXml

> 질문: 렌더 패스를 코드 없이 데이터로 어떻게 바꾸는가?
> 독자: 파이프라인 · 렌더 패스 XML 을 쓰거나 패스 코드를 고치는 사람과 에이전트.
> 결론: 파이프라인 XML 에 적힌 값이 곧 실행되는 값이다. 패스와 머티리얼은 같은 렌더 상태 칸을 쓰고 실행은 패스 → 머티리얼 순서로 합친다. 실행이 받아 줄 수 없는 조합은 무시되지 않고 읽기 오류다. 표(`RenderPassTypeInfo`)는 XML 이 없는 내부 슬롯, XML 이 비운 기본값, 머티리얼에게 내주는 범위만 정한다.

## 파일

| 파일 | 하는 일 |
|---|---|
| `<도메인>/pipeline/*.xml` (`RenderPipelineDesc`) | 첨부, 패스 그래프, 패스별 PSO 설정. 고르는 법은 `-gv_renderPipeline=` 또는 팩 설정 |
| `engine/renderpass/*.xml` (`RenderPassDesc`) | 여러 파이프라인이 함께 쓰는 첨부 묶음. `_listRenderPassRef` 로 가져오면 읽을 때 `_listAttachment` 앞에 합쳐진다 |
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
| `_listBlendTarget` | 렌더 타깃마다의 `RHIBlendTargetState`. 하나면 모든 타깃에 같다 | 둘 이상인데 컬러 출력 수와 다를 때. 알파 계수 자리의 색 계수 |
| `_shaderPath` · `_listPermutation` | 셰이더와 define. 비면 표의 기본 셰이더 | 컴퓨트 패스의 빈 `_shaderPath` |
| `_vertexEntryPoint` · `_pixelEntryPoint` | 진입점. 비면 `VSMain` · `PSMain` | |
| `_geometryEntryPoint` | 적으면 지오메트리 스테이지가 붙는다 | 머티리얼 셰이더 패스, 컴퓨트 패스 |
| `_hullEntryPoint` · `_domainEntryPoint` | 함께 적으면 테셀레이션이 붙고 제어점 셋의 패치로 그린다 | 한쪽만 적었을 때, 머티리얼 셰이더 패스, 컴퓨트 패스 |
| `_computeEntryPoint` · `_threadGroupSize` | 컴퓨트 패스의 진입점과 그룹 크기(셰이더의 `numthreads` 와 같게) | 컴퓨트 패스 밖의 진입점 |

메시 · 증폭 스테이지는 지원하지 않는다(적으면 모르는 칸이라 오류다).

## 렌더 상태 칸

패스(`RenderGraphPassDesc`)와 머티리얼 루트가 같은 칸을 쓴다. 정본은 `RHIFixedRenderState` · `RHIBlendTargetState` 이고 패스 XML 은 C++ 이름 그대로(`_cullMode`), 머티리얼 XML 은 앞의 `_` 와 불리언 `b` 를 뗀 이름(`cullMode`, `enableStencil`)이다.

| 묶음 | 칸 | 뜻 · 조합 규칙 |
|---|---|---|
| 깊이 | `_bEnableDepthTest` | 깊이 첨부를 붙이는지이기도 하다. 켜면 `_depthAttachment` 가 있어야 하고 그 패스 종류가 깊이를 붙여야 한다 |
| | `_bEnableDepthWrite` · `_depthCompare` · `_depthBias` · `_slopeScaledDepthBias` | 쓰기 · 바이어스는 깊이 테스트가 있어야 한다. 비교의 기본은 `LessEqual` |
| | `_bEnableDepthClip` | 끄면 평면 밖 기하를 눌러 그린다. Vulkan 은 장치 기능 `depthClamp` 이 없으면 PSO 를 만들지 않는다 |
| 래스터 | `_cullMode` · `_fillMode` · `_frontFace` | 풀스크린 패스의 컬은 `None` 이어야 한다 |
| 스텐실 | `_bEnableStencil` · `_stencilRef` · `_stencilReadMask` · `_stencilWriteMask` | 깊이 테스트가 있어야 첨부가 있다(모두 통과시키려면 `_depthCompare="Always"`). 마스크 · 참조는 면마다 있지 않다(D3D 와 같다) |
| | `_stencilFront*` · `_stencilBack*`(`Fail` · `DepthFail` · `Pass` · `Compare`) | 앞면 · 뒷면의 연산과 비교. 첨부의 스텐실은 깊이와 같은 로드 · 저장 동작이다 |
| 블렌드 | `_srcColor` · `_dstColor` · `_colorOp` · `_srcAlpha` · `_dstAlpha` · `_alphaOp` · `_colorWriteMask` · `_bEnableBlend` | 렌더 타깃 하나의 값(`RHIBlendTargetState`) |

## 머티리얼 칸

머티리얼은 위 렌더 상태 칸 중 **적은 칸만** 패스 위에 덮는다. 적지 않은 칸은 패스의 값이다. 실행은 패스 → 머티리얼 → 보기 모드 → 컬 반전 순서로 합쳐 변형 PSO 를 만든다(`FramePSOManager::createMaterialPSOVariant`, `RHIRenderStateOverride::applyTo`).

| 칸 | 뜻 | 받아 주지 않는 조합(읽기 오류) |
|---|---|---|
| `alphaMode` | `Opaque` · `Mask`(불투명 목록, 문턱 아래 알파를 버린다 — `MATERIAL_ALPHA_CUTOFF`) · `Blend`(반투명 목록) | 그 밖의 글. define `MATERIAL_ALPHA_CUTOFF` 를 직접 적었을 때 |
| `alphaCutoff` | `Mask` 의 문턱. `_alphaCutoff` 프로퍼티로 간다 | `Mask` 가 아닌데 기본값(0.5)이 아닐 때. 프로퍼티가 없는 셰이더의 기본값 아닌 값 |
| `renderQueue` | 그리기 순서(작은 것부터, 기본 0). 불투명 · 반투명 정렬의 첫 키 — 스텐실 마스크는 받는 쪽보다 작게 준다 | |
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

## 선택 스테이지를 쓰는 셰이더

- 소스는 패스 셰이더 한 파일이다. 머티리얼 변형은 머티리얼 셰이더의 정점 출력으로 만들어져 신호가 맞지 않아 머티리얼 셰이더 패스에는 쓸 수 없다.
- 헐 · 도메인의 제어점 위치는 `SV_POSITION` 이 아니라 일반 값으로 넘긴다. 내장 위치로 넘기면 GL 에서 오류 없이 아무것도 그리지 않는다. 예: `game/empty/shaders/optional_stages.hlsl`.
- 이 스테이지는 패스 상수 · 루트 상수를 받지 않는다고 보고 쓴다. 필요하면 정점 스테이지에서 값으로 넘긴다.

## 고칠 때

렌더 상태 칸을 더할 곳은 `RHIRenderState.h` 하나와 네 백엔드의 읽기다 — XML 읽기 · 쓰기는 리플렉션이, 패스 · 머티리얼 합치기와 해시 · 비교는 바이트가 따라온다(패딩이 생기면 `static_assert` 가 멈춘다). 조합 규칙은 `findInvalidReason`, 머티리얼에게 내주지 않을 칸은 `RHIRenderStateOverride::applyTo` 에 적는다. RHI ABI 번호를 올린다.

그 밖의 패스 칸은 PSO(`FramePSOManager::makePassPSODesc`) · 쿠커(`ShaderCookRequestCollector`) · 검증(`RenderPipelineAsset::validate`)을 함께 고친다. 칸만 두고 실행이 읽지 않으면 XML 이 거짓말을 한다. 값이 그림을 바꾸는지는 끄고 켠 두 장을 같은 실행에서 견주는 시나리오로 확인한다(`<ExpectImagesDiffer>`, 예: `game/empty/automation/renderstate_stencil`).

쿠커 코드를 고칠 때 번호를 올릴 일이 없다 — 쿠킹 키는 소스 지문에서 산출한다(`Config/Dev/CookCodeInputs.json`). 새 쿠커는 이 표에 폴더를 적는다.
