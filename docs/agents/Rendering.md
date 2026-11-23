# Rendering

> 결론: 렌더 파이프라인 · 패스 XML, 머티리얼 · 머티리얼 그래프, 셰이더 · RHI 를 고칠 때의 규칙. 파이프라인 XML 에 적힌 값이 곧 실행되는 값이다 — 실행이 받아 줄 수 없는 조합은 읽기 오류다(`RenderPipelineAsset::validate` · `findInvalidReason` — 로드 강제). 표(`RenderPassKindInfo`)는 XML 이 없는 내부 슬롯, XML 이 비운 기본값, 머티리얼에게 내주는 범위만 정한다. 새 린트 대신 형식 · 로드 오류로 막는다. 셰이더 이름은 [NamingCpp](NamingCpp.md) HLSL, 플랫폼 차이는 [Platform](Platform.md).

## 파이프라인 XML

### 파일

| 파일 | 하는 일 |
|---|---|
| `<도메인>/pipeline/*.xml` (`RenderPipelineDesc`) | 첨부, 패스 그래프, 패스별 PSO 설정(`-gv_renderPipeline=` 또는 팩 설정) |
| `engine/renderpass/*.xml` (`RenderPassDesc`) | 여러 파이프라인이 함께 쓰는 묶음(`_listRenderPassRef` 로 가져온다). 프레임 패스 한 벌은 `framecomputerenderpass.xml` |
| `engine/renderstate/blendpresets.xml` | 블렌드 프리셋 — 줄을 더하면 코드 없이 새 프리셋 |
| `*.material` (`MaterialDef`) | 렌더 상태 칸은 패스와 같은 형식(머티리얼 XML 은 앞 `_` 와 불리언 `b` 를 뗀 이름) |

- 비슷한 파이프라인은 `_base` 로 부모를 가리키고 차이만 적는다([DataInheritance](DataInheritance.md)). 이름이 겹치는 첨부는 오류다.
- 패스 칸은 리플렉션 enum 이다 — 철자 오타는 읽기 오류. 메시 · 증폭 스테이지는 없다.

### 머티리얼 칸

- 머티리얼은 **적은 칸만** 패스 위에 덮는다. 합치는 차례는 패스 → 머티리얼 → 보기 모드 → 컬 반전(`RenderStateOverride::applyTo`). 덮을 수 있는 범위는 패스 종류 표(`getMaterialStateScope`)가 정한다: GBuffer 는 블렌드를, Shadow · DepthPrepass 는 깊이 · 스텐실 · 블렌드를 받지 않는다. 깊이 테스트는 언제나 패스의 값.
- `surfaceModel` 은 필수(`_base` 로 물려받는다). `MATERIAL_ALPHA_CUTOFF` · `MATERIAL_FORWARD_ONLY` · `MATERIAL_TWO_SIDED` define 은 직접 적지 않는다 — `alphaMode` · `surfaceModel` · `twoSidedLighting` 칸이 낸다.
- 순서에 기대는 머티리얼에 `renderQueue` 차례를 적지 않는다 — 불투명 띠는 렌더 상태가 정한다(`OpaqueDrawBand`).
- 인스턴스 모델은 둘이다: 에셋 변형은 자식 머티리얼(`_base` — 렌더 상태 · `alphaMode` · `renderQueue` · 정적 스위치 · 멀티 컴파일 · 값), 실행 중 값은 `MaterialInstance::create`(값 · 텍스처 · 런타임 스위치만 — 드로우마다 PSO 를 가르지 않는다).
- 자식의 `_permutations` 는 부모와 이름으로 합친다 — 더할 define · 바꿀 스위치만 적는다.
- 모든 머티리얼이 같은 값을 읽고 자주 바뀌는 것(날씨 · 환경 시간)은 머티리얼 칸에 묶지 않고 전역 값(`MaterialGlobals` → PassCB `g_MaterialGlobal*`)으로 둔다. 파라미터 묶음의 값은 `_global` 로 먹이고, 전역 값에는 프로퍼티를 묶지 않는다(오류).
- 퍼뮤테이션을 바꾸는 값(정적 스위치 · 키워드 칸)은 자식 머티리얼에 적거나, 부모가 선언한 런타임 스위치(`bShaderFeature="0"`)를 `enableKeyword` 로 켠다. 값 API(`setParameter` 등)는 키워드 칸을 오류로 거절한다 — 런타임 값은 쿠킹하지 않은 조합이다(언리얼 MID 와 같은 경계).

### 화면 속도 · 왜곡

- 속도 첨부는 `SceneVelocity`(`R16G16_FLOAT`, 클리어 0). 쓰는 것은 깊이를 쓰는 불투명 패스(`kWritesVelocity`)만, 포워드는 둘째 출력. TAA · MotionBlur 는 `_listInput` 에 꼭 적는다. 반투명 · 외곽선 껍질은 속도를 쓰지 않는다. 읽는 패스가 없는 파이프라인(2D)은 첨부도 두지 않는다.
- `Distortion` · `DistortionApply` 는 장면 색 사본 뒤 · 반투명 앞에 둔다(뒤에 두면 앞의 반투명이 끌려 들어온다). 머티리얼은 `_alwaysDefines` 에 `MATERIAL_DISTORTION` · `MATERIAL_BLEND_TRANSLUCENT`(본보기 `engine/materials/distortion.material`).

### 컴퓨트 패스

- `_type="Compute"`: 컬러 출력 하나, 입력 최대 둘(깊이는 `SceneDepthCopy` 를 읽는다), 입출력이 겹치지 않는다. 값은 `compute_pass.hlsli` 루트 상수. GL 은 include 전에 `SW_RW_SLOT0_GL_FORMAT` 을 적는다.
- `_implementation="Compute"`(표의 `kComputeImplementation` 종류만)는 측정이 이득을 보일 때 그 패스에 한 줄 적는다. 네이티브 bindless 백엔드만 컴퓨트로 돈다. 값은 루트 상수로(컴퓨트 PSO 에 PassCB 가 없다).

### 후처리

- 뷰마다 값은 `PostProcessValues` 한 벌. 볼륨은 **덮어쓴 칸만** 우선순위 차례로 섞는다. 카메라 값이 볼륨 아래.
- 값 칸을 더할 때: `PostProcessValues` 에 칸, `PostProcessOverrides` 에 같은 차례로 `_b<칸>`(테스트 `OverrideFlagsPairEveryValue`). 섞을 수 없는 형식은 `blendOverrides` 에 줄. 기본값은 지금 그림을 바꾸지 않는 중립.
- 텍스처를 가리키는 칸은 `DataPath`, 로드는 `PathTextureSet::requestReady`, 셰이더 쪽은 엔진 텍스처 표 이름.
- 그레이딩 · 렌즈 · 색수차 · 선명화 · 그레인은 톤맵 셰이더 안 함수, 블룸 먼지 · 고스트는 `post_bloom.hlsli` 안 — **이런 효과에 패스를 따로 두지 않는다**(왕복 하나). 값이 중립이면 C++ 가 켬 상수를 0 으로.
- 디퍼드 기본 파이프라인은 후처리 체인(`post_chain.hlsl`)으로 합치지 않는다 — 순서 · AO · 품질 단계가 달라 골든이 바뀐다. 합치려면 TAA · DOF · 모션 블러를 블룸 앞에 둔 새 파이프라인과 새 골든.
- 후처리 머티리얼(`_type="PostProcessMaterial"`): `_shaderPath` 를 적지 않는다(머티리얼의 것), `_blendableLocation` 은 그 패스만 적고 자리마다 하나. 머티리얼은 `MATERIAL_DOMAIN_POST_PROCESS`(본보기 `engine/materials/postprocess/colortint.material`).
- 데칼(`_type="Decal"`)은 G버퍼 뒤 · 조명 · SSAO 앞, 출력은 G버퍼 알베도 · 노멀 · 머티리얼 차례. 포워드 파이프라인에는 두지 않는다. shortcut: 데칼마다 화면 전체 — 수십 개를 넘으면 상자 래스터 · 타일 목록으로.

### 프레임 그래프

- 프레임에 한 번 모든 뷰 몫을 기록하는 일은 `_listFramePass` 에, 뷰마다 도는 일은 `_listPass` 에. 프레임 그래프가 통째로 앞이므로 뷰 패스는 그 결과를 입력으로 적지 않는다.
- `_listResource` 는 순서만 정한다(버퍼 주인은 서브시스템). `Persistent` = 프레임을 넘는 히스토리, `External` = 서브시스템이 채우는 버퍼. `Transient` 는 적지 않는다(첨부의 몫).
- 선언하지 않은 종류는 PSO 도, 준비 작업도 없다(`FramePSOManager::getEnginePSO`). 각 패스는 자기 버퍼 배리어를 스스로 건다.
- `_queue="AsyncCompute"` 는 컴퓨트 프레임 패스에만. 능력 `_bAsyncCompute` 가 없으면 그래픽스 큐에서 같은 차례로 돈다(비교 `-gv_asyncCompute=0`). 이득이 없으면 그 줄을 지운다.
- 트랜지언트 앨리어싱은 적을 칸이 없다 — 수명은 패스 입출력 선언이 정한다(비교 `-gv_transientAliasing=0`).

### 선택 스테이지 · 변위

- 지오메트리 · 헐 · 도메인은 패스 셰이더 한 파일에서만(머티리얼 셰이더 패스 불가). 제어점 위치는 `SV_POSITION` 이 아니라 일반 값으로 넘긴다 — GL 에서 오류 없이 아무것도 안 그린다. 패스 상수 · 루트 상수를 받지 않는다고 보고, 필요하면 정점 스테이지에서 값으로.
- 머티리얼 변위는 `_alwaysDefines` 의 `MATERIAL_DISPLACEMENT` + `material_displacement.hlsli`. 테셀레이션 능력(`_bMaterialTessellation`)이 없는 백엔드는 오류(`DisplacementUnsupported`)로 남기고 평면으로 대신 그리지 않는다. 시나리오는 `<SkipUnless watch="RHI.MaterialTessellation">`.

### 고칠 때

- 렌더 상태 칸은 `Engine/Graphics/RenderState/RenderState.h` 하나와 각 백엔드 읽기에 더한다. 조합 규칙은 `findInvalidReason`, 머티리얼에 내주지 않을 칸은 `RenderStateOverride::applyTo`. RHI ABI 는 손으로 올리지 않는다(빌드가 스탬프를 다시 만든다).
- 그 밖의 패스 칸은 PSO(`makePassPSODesc`) · 쿠커(`ShaderCookRequestCollector`) · 검증(`validate`)을 함께 고친다 — 실행이 읽지 않는 칸은 XML 이 거짓말을 한다. 그림을 바꾸는지는 `<ExpectImagesDiffer>` 시나리오로 확인한다.
- 쿠커 코드를 고쳐도 번호를 올리지 않는다(쿠킹 키 = 소스 지문). 새 쿠커는 `Config/Dev/CookCodeInputs.json` 에 폴더를 적는다.

## 머티리얼 그래프

새 노드는 `MaterialGraphNodes.cpp` 에 `SW_MATERIAL_GRAPH_NODE` 한 줄(컴파일러 · 메뉴 · 검사는 고치지 않는다). 그래프는 `.hlsl` 과 `.gen.material` 을 생성하고 그 뒤는 손으로 쓴 머티리얼과 같다. 그래프 코드는 DevTools 의 `Material/`(배포에 없음), 편집기는 에디터 `Panels/MaterialGraph/`.

### 생성물

- 생성물(`shaders/matgraph/<줄기>_<해시>.hlsl` · `materials/…/<이름>.gen.material`)은 **커밋하고 손으로 고치지 않는다.** 그래프를 고쳤으면 `App --cook-shaders` 나 편집기 저장으로 다시 쓴다(테스트 `SampleGeneratedAssetsAreCurrent` 가 낡음을 잡는다).
- 엔진은 컴파일러를 모른다 — 셰이더 쿠커는 `IShaderSourceGenerator` 창구만 부른다. 엔진 · 런타임에서 그래프 코드를 include 하지 않는다.

### 파일에 적는 법

- 그래프 경로에 `/materials/` 칸이 있어야 한다(생성물 자리가 거기서 정해진다).
- 연결은 받는 노드의 `_listLink` 에 적는다. 입력 핀 하나에 링크 하나. `_listValue` 는 설정이거나 잇지 않은 핀의 상수.
- 값 · 렌더 상태(`alphaMode` · `cullMode`) 변형은 `.gen.material` 의 자식 `.material`(`_base`). 셰이더가 달라지는 변형만 그래프 `_base`(열쇠 `_name` · `_pin`).

### 새 노드 더하기

`MaterialGraphNodes.cpp` 에 한 줄만 더한다(컴파일러 · 메뉴 · 검사는 고치지 않는다).

```cpp
//                    이름     메뉴 묶음  입력                           출력                            설정
SW_MATERIAL_GRAPH_NODE( Lerp, "Math", "A:any!|B:any!|T:float=0.5", "Out:any=lerp( $A, $B, $T )", "" );
// 머티리얼 칸이 되는 노드: 마지막 인자 `HLSL 형식:프로퍼티 형식`, 설정 `Parameter` 가 칸 이름
SW_MATERIAL_GRAPH_PARAMETER_NODE( ScalarParameter, "Parameter", "", "Out:float=%Parameter", "Parameter:string=_value|Default:float=0|Min:float=0|Max:float=1", "float:Range" );
```

| 자리 | 문법 |
|---|---|
| 입력 | `이름:형식`, 필수 `!`, 기본값 `=수치` 또는 `=HLSL 식`. `\|` 로 나눈다 |
| 출력 | `이름:형식=HLSL 식`. `$이름` 입력 · 앞선 출력, `%이름` 설정 |
| 설정 | `이름:형식=기본값`(잇지 못하는 값) |
| 형식 | `float` · `float2` · `float3` · `float4` · `any`(이어진 입력 크기, `float` 만 넓어진다) · `string`(설정만) |
| 머티리얼 칸 형식 | `float` · `float4` · `uint`(텍스처 번호)만 |

- 식에는 `PixelContext` 칸(`context._uv` 등)과 `SurfaceFunction::*`(`material_functions.hlsli`) 같은 공용 함수만 쓴다. 식 안에 `|` 를 쓰지 않는다 — 비트 연산은 함수로 감싼 `.hlsli` 를 더한다.
- **등록은 엔진 안에만.** 팩 쿠킹은 게임 모듈을 올리기 전에 돌아 키트 · 게임 등록을 보지 못한다. 게임 전용 재사용은 하위 그래프로(테스트 `EveryRegisteredKindParses` · `EverySnippetExpands`).

### 하위 그래프 · 층

- 하위 그래프는 `GraphInput` · `GraphOutput` 을 가진 그래프, `Subgraph` 노드의 `_assetPath` 로 가리킨다. 스스로를 부르면 오류.
- 층 파일의 `GraphOutput` `Name` 은 `BaseColor` · `Metallic` · `Roughness` · `Normal` · `Emissive` 가운데 하나. **층은 알파를 바꾸지 않는다** — 렌더 상태라 패스가 달라진다.
- `Layer` 노드는 머티리얼 그래프에만, 노드 이름은 칸 머리(글자 · 숫자 · `_`). 상한은 `Config/Dev/MaterialLayers.json`(`_maxLayerCount` · `_maxMaskChannelCount`).
- 층을 끄는 변형은 자식 `.material` 의 `_<층>Weight=0`(같은 셰이더). 층 파일을 바꾸거나 더하는 변형만 그래프 `_base`.

### 후처리 영역

- 출력 노드 `PostProcessOutput` 이면 후처리 머티리얼. 화면 입력 노드(`SceneColor` · `SceneDepth` · `GBuffer*`)는 후처리 그래프에만, `Layer` 는 받지 않는다(`DomainMismatch`).

### 오류

컴파일 문제는 `MaterialGraphIssue` 값(노드 이름 포함)으로 돌아오고 쿠킹은 `MaterialGraph.CompileFailed` 로 실패한다 — 컴파일 강제. 편집기 저장은 컴파일이 통과할 때만 쓴다.

## 렌더 · RHI 결정

- **G버퍼:** 속성 추가는 남는 채널 · 정밀도 검사 뒤 판(`SW_GBUFFER_LAYOUT_VERSION`)을 올린다. 겹친 엽(박막 · 털 · 스타킹 · 비단)은 포워드 전용(`MATERIAL_FORWARD_ONLY`) — 칸을 늘리면 모든 디퍼드 픽셀이 대역폭을 치른다. 모바일 · 웹은 측정 전까지 포워드. 포워드 · 디퍼드 비교는 같은 장면 · 해상도 · 플랫폼에서 GPU 시간 · G버퍼 대역폭 · PSO 수 · 라이트 수로.
- **메시:** 모든 메시는 인덱스를 가진다(색인 없는 이중 모드 금지). 스킨 결과는 칸(`AnimationSkinSlot`)마다 — 메시 사본을 만들지 않고, 프레임마다 바뀌는 번호(팔레트 순서)를 인스턴스에 싣지 않는다. 변형이 필요한데 칸이 무효인 인스턴스는 그리지 않는다(레스트 포즈로 대신하지 않는다). GPU 인스턴스 LOD 와 CPU 선택은 한 규칙(`MeshLODSystem::isGPUSelectable`).
- **구조 버퍼 범위:** 원소 수 상수(`g_Sw<Name>Count`)와 함께 넘기고 읽기 함수 한 곳에서 범위 밖이면 0 을 돌려준다(`if` 로 — 삼항은 양쪽을 평가). 셰이더 `GetDimensions` 로 원소 수를 읽지 않는다(GLSL 변환 · DX12 루트 SRV 에서 뜻이 갈린다).
- **선택 스테이지:** 스트림 아웃은 RHI 에 더하지 않는다. 지오메트리 · 테셀레이션은 `RHICapabilities` 가 참인 곳만, WebGPU 대체와 함께. 지형 · 풀 전용 경로는 테셀레이션보다 compute 배치 + 간접.
- **RHI 형식 지원:** 형식 · 용도 조합은 실제 자원 · 렌더 상태로 질의하고, 미지원 경로는 백엔드별 시나리오로 본다.
- **bindless:** 힙 · 세트 하나 제자리 덮어쓰기, 갱신마다 힙 전체 복사, 셰이더 간접 표, 스파스 텍스처, 텍스처별 세대는 하지 않는다(`rebindBindlessTexture` · `RHIDescriptorRingMirror` 를 쓴다).
- **텍스처 스트리밍:** 밉 범위 읽기(`TextureMipLayout` · `readRangeAsync` · `_maxBytesInFlight`)를 쓴다. 파일 꼬리 · 벌크 분리, KTX2, 메모리 맵은 하지 않는다.
- **자동 렌더 배율:** GPU 타임스탬프(`GPUFrameClock`)로만 — 벽시계 · CPU `RT.Frame` 으로 대신하지 않는다. 타임스탬프 없는 장치는 오류 한 번 뒤 끈다.
- **입자 정렬:** 뜻 없는 조합(더하기 블렌드 · 띠 · 불투명 메시 + 정렬)과 GPU `ViewDepth` 에서 `_maxParticles` > 512 는 로드 오류(조용히 건너뛰지 않는다). CPU 입자 방출기는 기능 차이로 남긴다.
- **업로드 · 스레드:** 전용 업로드 스레드는 넣지 않는다 — 메시 GPU 생성 대기를 없애려면 "보이는 메시는 그 프레임에 있다" 계약부터 바꾼다. 기록 경로의 잎 락(`_onlineBlockMutex` · `_cmdListPoolMutex`)은 두고, 줄무늬 락은 쓰지 않는다. 병렬 구간 임시 멈춤은 넣지 않는다.
- **확장:** 기본 씬 수집과 `RenderExtensionRegistry` 는 합치지 않는다 — 같은 기능을 두 번 구현하는 사례가 나올 때만 공통 기여 API. `IEngineRenderDriver` 경계 · 프로브 revision 캐시 · 머티리얼 변경 감지는 유지한다.
- **디버그:** 새 보기 모드(가림 · 스트리밍)는 만들지 않는다 — 창 · HUD 에 같은 정보가 있다. 손 포인터 병렬 본문(셰이더 쿠킹 사슬 · GPUSceneBuilder 트랜스폼 복사 · `PathModifierUtil` 정합)은 형식으로 담을 수 없어 둔다.
