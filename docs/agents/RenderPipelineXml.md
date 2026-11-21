# RenderPipelineXml

> 결론: 파이프라인 XML 에 적힌 값이 곧 실행되는 값이다. 실행이 받아 줄 수 없는 조합은 무시하지 않고 읽기 오류로 만든다(`RenderPipelineAsset::validate` · `findInvalidReason` — 로드 강제). 표(`RenderPassKindInfo`)는 XML 이 없는 내부 슬롯, XML 이 비운 기본값, 머티리얼에게 내주는 범위만 정한다.

## 파일

| 파일 | 하는 일 |
|---|---|
| `<도메인>/pipeline/*.xml` (`RenderPipelineDesc`) | 첨부, 패스 그래프, 패스별 PSO 설정(`-gv_renderPipeline=` 또는 팩 설정) |
| `engine/renderpass/*.xml` (`RenderPassDesc`) | 여러 파이프라인이 함께 쓰는 묶음(`_listRenderPassRef` 로 가져온다). 프레임 패스 한 벌은 `framecomputerenderpass.xml` |
| `engine/renderstate/blendpresets.xml` | 블렌드 프리셋 — 줄을 더하면 코드 없이 새 프리셋 |
| `*.material` (`MaterialDef`) | 렌더 상태 칸은 패스와 같은 형식(머티리얼 XML 은 앞 `_` 와 불리언 `b` 를 뗀 이름) |

- 비슷한 파이프라인은 `_base` 로 부모를 가리키고 차이만 적는다([DataInheritance](DataInheritance.md)). 이름이 겹치는 첨부는 오류다.
- 패스 칸은 리플렉션 enum 이다 — 철자 오타는 읽기 오류. 메시 · 증폭 스테이지는 없다.

## 머티리얼 칸

- 머티리얼은 **적은 칸만** 패스 위에 덮는다. 합치는 차례는 패스 → 머티리얼 → 보기 모드 → 컬 반전(`RenderStateOverride::applyTo`). 덮을 수 있는 범위는 패스 종류 표(`getMaterialStateScope`)가 정한다: GBuffer 는 블렌드를, Shadow · DepthPrepass 는 깊이 · 스텐실 · 블렌드를 받지 않는다. 깊이 테스트는 언제나 패스의 값.
- `surfaceModel` 은 필수(`_base` 로 물려받는다). `MATERIAL_ALPHA_CUTOFF` · `MATERIAL_FORWARD_ONLY` · `MATERIAL_TWO_SIDED` define 은 직접 적지 않는다 — `alphaMode` · `surfaceModel` · `twoSidedLighting` 칸이 낸다.
- 순서에 기대는 머티리얼에 `renderQueue` 차례를 적지 않는다 — 불투명 띠는 렌더 상태가 정한다(`OpaqueDrawBand`).
- `MaterialInstance` 는 값 · 텍스처 · 키워드만 덮는다. 렌더 상태 · `alphaMode` · `renderQueue` 가 다르면 `parent=` 로 자식 머티리얼을 만든다 — 드로우마다 PSO 를 가르지 않는다.

## 화면 속도 · 왜곡

- 속도 첨부는 `SceneVelocity`(`R16G16_FLOAT`, 클리어 0). 쓰는 것은 깊이를 쓰는 불투명 패스(`kWritesVelocity`)만, 포워드는 둘째 출력. TAA · MotionBlur 는 `_listInput` 에 꼭 적는다. 반투명 · 외곽선 껍질은 속도를 쓰지 않는다. 읽는 패스가 없는 파이프라인(2D)은 첨부도 두지 않는다.
- `Distortion` · `DistortionApply` 는 장면 색 사본 뒤 · 반투명 앞에 둔다(뒤에 두면 앞의 반투명이 끌려 들어온다). 머티리얼은 `_alwaysDefines` 에 `MATERIAL_DISTORTION` · `MATERIAL_BLEND_TRANSLUCENT`(본보기 `engine/materials/distortion.material`).

## 컴퓨트 패스

- `_type="Compute"`: 컬러 출력 하나, 입력 최대 둘(깊이는 `SceneDepthCopy` 를 읽는다), 입출력이 겹치지 않는다. 값은 `compute_pass.hlsli` 루트 상수. GL 은 include 전에 `SW_RW_SLOT0_GL_FORMAT` 을 적는다.
- `_implementation="Compute"`(표의 `kComputeImplementation` 종류만)는 측정이 이득을 보일 때 그 패스에 한 줄 적는다. 네이티브 bindless 백엔드만 컴퓨트로 돈다. 값은 루트 상수로(컴퓨트 PSO 에 PassCB 가 없다).

## 후처리

- 뷰마다 값은 `PostProcessValues` 한 벌. 볼륨은 **덮어쓴 칸만** 우선순위 차례로 섞는다. 카메라 값이 볼륨 아래.
- 값 칸을 더할 때: `PostProcessValues` 에 칸, `PostProcessOverrides` 에 같은 차례로 `_b<칸>`(테스트 `OverrideFlagsPairEveryValue`). 섞을 수 없는 형식은 `blendOverrides` 에 줄. 기본값은 지금 그림을 바꾸지 않는 중립.
- 텍스처를 가리키는 칸은 `DataPath`, 로드는 `PathTextureSet::requestReady`, 셰이더 쪽은 엔진 텍스처 표 이름.
- 그레이딩 · 렌즈 · 색수차 · 선명화 · 그레인은 톤맵 셰이더 안 함수, 블룸 먼지 · 고스트는 `post_bloom.hlsli` 안 — **이런 효과에 패스를 따로 두지 않는다**(왕복 하나). 값이 중립이면 C++ 가 켬 상수를 0 으로.
- 디퍼드 기본 파이프라인은 후처리 체인(`post_chain.hlsl`)으로 합치지 않는다 — 순서 · AO · 품질 단계가 달라 골든이 바뀐다. 합치려면 TAA · DOF · 모션 블러를 블룸 앞에 둔 새 파이프라인과 새 골든.
- 후처리 머티리얼(`_type="PostProcessMaterial"`): `_shaderPath` 를 적지 않는다(머티리얼의 것), `_blendableLocation` 은 그 패스만 적고 자리마다 하나. 머티리얼은 `MATERIAL_DOMAIN_POST_PROCESS`(본보기 `engine/materials/postprocess/colortint.material`).
- 데칼(`_type="Decal"`)은 G버퍼 뒤 · 조명 · SSAO 앞, 출력은 G버퍼 알베도 · 노멀 · 머티리얼 차례. 포워드 파이프라인에는 두지 않는다. shortcut: 데칼마다 화면 전체 — 수십 개를 넘으면 상자 래스터 · 타일 목록으로.

## 프레임 그래프

- 프레임에 한 번 모든 뷰 몫을 기록하는 일은 `_listFramePass` 에, 뷰마다 도는 일은 `_listPass` 에. 프레임 그래프가 통째로 앞이므로 뷰 패스는 그 결과를 입력으로 적지 않는다.
- `_listResource` 는 순서만 정한다(버퍼 주인은 서브시스템). `Persistent` = 프레임을 넘는 히스토리, `External` = 서브시스템이 채우는 버퍼. `Transient` 는 적지 않는다(첨부의 몫).
- 선언하지 않은 종류는 PSO 도, 준비 작업도 없다(`FramePSOManager::getEnginePSO`). 각 패스는 자기 버퍼 배리어를 스스로 건다.
- `_queue="AsyncCompute"` 는 컴퓨트 프레임 패스에만. 능력 `_bAsyncCompute` 가 없으면 그래픽스 큐에서 같은 차례로 돈다(비교 `-gv_asyncCompute=0`). 이득이 없으면 그 줄을 지운다.
- 트랜지언트 앨리어싱은 적을 칸이 없다 — 수명은 패스 입출력 선언이 정한다(비교 `-gv_transientAliasing=0`).

## 선택 스테이지 · 변위

- 지오메트리 · 헐 · 도메인은 패스 셰이더 한 파일에서만(머티리얼 셰이더 패스 불가). 제어점 위치는 `SV_POSITION` 이 아니라 일반 값으로 넘긴다 — GL 에서 오류 없이 아무것도 안 그린다. 패스 상수 · 루트 상수를 받지 않는다고 보고, 필요하면 정점 스테이지에서 값으로.
- 머티리얼 변위는 `_alwaysDefines` 의 `MATERIAL_DISPLACEMENT` + `material_displacement.hlsli`. 테셀레이션 능력(`_bMaterialTessellation`)이 없는 백엔드는 오류(`DisplacementUnsupported`)로 남기고 평면으로 대신 그리지 않는다. 시나리오는 `<SkipUnless watch="RHI.MaterialTessellation">`.

## 고칠 때

- 렌더 상태 칸은 `Engine/Graphics/RenderState/RenderState.h` 하나와 각 백엔드 읽기에 더한다. 조합 규칙은 `findInvalidReason`, 머티리얼에 내주지 않을 칸은 `RenderStateOverride::applyTo`. RHI ABI 는 손으로 올리지 않는다(빌드가 스탬프를 다시 만든다).
- 그 밖의 패스 칸은 PSO(`makePassPSODesc`) · 쿠커(`ShaderCookRequestCollector`) · 검증(`validate`)을 함께 고친다 — 실행이 읽지 않는 칸은 XML 이 거짓말을 한다. 그림을 바꾸는지는 `<ExpectImagesDiffer>` 시나리오로 확인한다.
- 쿠커 코드를 고쳐도 번호를 올리지 않는다(쿠킹 키 = 소스 지문). 새 쿠커는 `Config/Dev/CookCodeInputs.json` 에 폴더를 적는다.
