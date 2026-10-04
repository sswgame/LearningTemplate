# Graphics (RHI · Material · RenderPass)

렌더링 스택입니다. **백엔드(RHI)** 위에서 **머티리얼/셰이더**를 올리고, **파이프라인 XML → RenderGraph → FrameRenderer** 로 한 프레임을 그립니다.

경로: `Source/Engine/Graphics/`  
상위 레이어: [Engine/README.md](../README.md)  
개념 개요: [ARCHITECTURE.md](../../../ARCHITECTURE.md) (RHI · RenderPass · Pipeline)

---

## 한 줄로 이해하기

```text
Asset (Material / Shader / Mesh)
        ↓
RenderPass (pipeline XML → RenderGraph → FrameRenderer)
        ↓
RHI (IRHIDevice / IRHICommandList / IRHIResourceFactory)
        ↓
DX11 · DX12 · OpenGL · Vulkan
```

| 폴더 | 역할 |
|------|------|
| **RHI/** | 백엔드 추상화·구현·(옵션) RHI DLL 모듈. 자세한 것은 [RHI/README.md](RHI/README.md) |
| **Material/** | 머티리얼 정의·인스턴스·캐시. 파일이 곧 주제다 — `MaterialXml`(XML 읽기/쓰기) · `MaterialPacking`(타입 표·CB 패킹) · `MaterialPermutation`(define 조립·세대) |
| **Shader/** | [Shader/README.md](Shader/README.md). `Compile/` 컴파일·캐시·쿠킹·핫리로드 · `Reflection/` 리플렉션과 매니페스트 · `Binding/` 슬롯 계약과 셰이더가 읽는 꼴 그대로의 값 묶음(`GpuLight` · `GpuSpriteInstanceData` — 컴포넌트가 채우므로 Renderer 아래에 둔다) |
| **Mesh/** | CPU 메시 에셋(`Mesh`)과 기본 도형 생성기(`MeshUtil`), 메시 에셋 파일(`.mesh` — `MeshAssetFormat`)과 경로 캐시(`MeshCache`). GPU 풀은 여기 없다 — Renderer/Scene/ |
| **Texture/** | `Texture2D` 에셋과 `TextureCache`(참조 수 + unique_ptr) |
| **Upload/** | `GpuUploadQueue` — 게임 스레드가 스냅샷을 내보내기 **전에** 워커가 GPU 리소스를 만든다 |
| **2D/** | [2D/README.md](2D/README.md). 2D 렌더 데이터 — `Render2DSettings`(`render2d.xml` 정렬 레이어 표 · 정렬 키 · 투명 정렬 축) |
| **Renderer/** | [Renderer/README.md](Renderer/README.md). `Frame/` FrameRenderer 와 그 소유물 셋(PassConstantRing · RenderPsoCache · TransientAttachmentPool) · `Graph/` RenderGraph · `Pipeline/` 패스·파이프라인 리소스·입력 계약 · `Scene/` GpuSceneBuilder(GT) → GpuSceneSnapshot → GpuScene(RT) + GPU 정점/모프 풀 · `Light/` 라이트 버퍼 · `Debug/` 에디터가 읽는 통로(RenderTargetRegistry · DebugDrawQueue) · RenderThread |

---

## 용어집 (Glossary)

| 용어 | 뜻 |
|------|-----|
| RHI | Rendering Hardware Interface — GPU API(DX/VK/GL) 앞의 엔진 추상화 |
| Backend | RHI의 구체 구현 (DX11/DX12/Vulkan/OpenGL). **구현 수준은 네 백엔드 패리티** |
| Device (`IRHIDevice`) | GPU 장치·리소스 생성·커맨드 제출의 진입점 |
| Frame stream / Context (`IRHICommandContext`) | 디바이스가 소유한 프레임 기록 스트림(`beginFrame`~`endFrame`)에 바로 기록하는 면 |
| CommandList / CL (`IRHICommandList`) | 패스마다 만드는 독립 기록 단위. 네 백엔드 모두 네이티브(또는 즉시 호출)이고 CPU 재생 계층은 없다 |
| Resource / Handle | GPU 버퍼·텍스처 등 + 엔진 쪽 핸들 테이블 식별자 |
| PSO / PipelineState | 셰이더+고정 상태 묶음 (그래픽스/컴퓨트) |
| SwapChain | 화면 present용 백버퍼 체인 |
| Material / MaterialInstance | 셰이더·파라미터 정의 / 인스턴스 값 |
| Compiler · Reflection · BindingLayout | HLSL 컴파일 · 바이트코드 바인딩 메타 · 병합된 조회 레이아웃 |
| RenderPass (개념) | 한 번의 begin/end 렌더 타깃 구간 (XML 패스와 혼동 주의) |
| RenderGraph | 패스·리소스 의존성 그래프로 프레임 순서 결정 |
| FrameRenderer | 한 프레임: 패킷 → 그래프 → CL 기록/실행 |
| RenderThread | 렌더 스레드 루프 (offscreen/present 등) |
| GpuSceneBuilder / GpuSceneSnapshot / GpuScene | 씬을 훑어 스냅샷을 만드는 쪽(GT) / 두 스레드가 공유하는 **유일한** 타입 / 스냅샷을 GPU 에 올리는 쪽(RT) |
| Frame packet | 프레임에 넘기는 CPU 측 렌더 입력 묶음 |
| Bindless / Descriptor | 리소스 인덱스로 셰이더 접근; 디스크립터 테이블/힙 |
| DebugDrawQueue | 디버그 라인/스피어 **큐** (즉시 GPU 드로우 아님) |

## 주요 클래스 역할

| 클래스 | 폴더 | 한 줄 역할 |
|--------|------|------------|
| `RHI` | RHI/ | 백엔드 선택(`gv_rhiBackend` · `-dx11/-dx12/-vk/-gl`)·디바이스 수명·백엔드 교체(`recreateDevice`) |
| `IRHIDevice` (+ 백엔드 `*RHIDevice`) | RHI/ | 프레임 수명주기·CL 생성과 제출·능력 조회·종료 순서(`shutdown` 템플릿 메서드)·GPU 완료 뒤 해제(`enqueueGpuRelease`) |
| `RHINativeHandles` | RHI/IRHIDevice.h | 엔진 밖 모듈(에디터 ImGui 백엔드)에 넘기는 네이티브 핸들 POD — 판 번호로 대조(`queryNativeHandles`) |
| `IRHICommandContext` (+ `*RHICommandContext`) | RHI/ | 프레임 스트림에 바로 기록하는 draw/dispatch/barrier/blit 면 |
| `IRHICommandList` (+ 백엔드 `*RHICommandList`) | RHI/ | 독립 기록 단위 — `RHICommandListForwarder` 가 자기 컨텍스트로 즉시 넘긴다 |
| `IRHIResourceFactory` | RHI/ | 리소스(버퍼·텍스처·PSO) 생성·파괴 |
| `RHIHandleTable` · `FrameResourceRing` · `RHIReleaseQueue` | RHI/Support/ | 핸들·프레임링·지연 해제 |
| `RHIShaderRequest` · `RHIGpuTimestamp` · `RHIBufferSize` | RHI/Support/ | 넷이 각자 갖던 규칙 하나 — 서술체 → 컴파일 요청 · 타임스탬프 칸 → 마이크로초 · 32비트 버퍼 크기 |
| `RHIRenderResource` · `RHIResidentBuffer` | RHI/ | GPU 자원 소유자의 등록부(디바이스 수명 통보) · 핸들+디바이스 |
| `RHIStructuredBufferSlot` · `RHIConstantBufferSlot` | RHI/ | 버퍼 + 뷰/인덱스 한 벌 — 만들기·갱신·해제 순서를 타입이 안다. 구조버퍼는 용량이 변하고(`ensureCapacity`) 상수버퍼는 안 변한다(`create`) |
| `Material` · `MaterialInstance` · `MaterialCache` | Material/ | 정의·인스턴스·캐시 |
| `ShaderCompiler` · `ShaderCache` · `ShaderRecompiler` | Shader/Compile/ | HLSL → 바이트코드, 디스크 캐시, 수동 리로드 |
| `ShaderCookStamp` · `ShaderCooker` | Shader/Compile/ | 오프라인 쿠킹의 **메커니즘** — 이미 최신인지(내용 해시) · 한 장 쿠킹하고 이름 짓기 |
| `ShaderCookDriver` (+ `ShaderCookRequest.cpp`) | Renderer/Cook/ | 오프라인 쿠킹의 **정책** — 무엇을 쿠킹할지(파이프라인 XML · 패스 종류 표 × 뷰 모드 · 머티리얼 → 요청) · 전부 쿠킹. 패스 종류를 아는 렌더러의 지식이라 여기 있다 |
| `ShaderReflection` · `ShaderReflectionLibrary` | Shader/Reflection/ | 바이트코드 리플렉션과 쿠킹된 매니페스트 |
| `ShaderBindingSlots` · `ShaderBindingLayout` · `ShaderBindingValidator` | Shader/Binding/ | 슬롯 정본, 병합 레이아웃, 쿠킹된 바이너리 대조 |
| `Mesh` · `MeshUtil` | Mesh/ | 메시 버퍼 · 기본 도형 생성 |
| `MeshAssetFormat` · `MeshCache` | Mesh/ | `.mesh` 읽기 · 쓰기(쓰기는 에디터 모델 임포터), 경로당 `Mesh` 하나(약한 참조) · 제자리 핫 리로드. `MeshComponent::_meshId` 가 `.mesh` 경로면 여기서 받는다 |
| `Texture2D` · `TextureCache` | Texture/ | 텍스처 에셋 · 캐시 |
| `GpuUploadQueue` | Upload/ | GPU 리소스를 그리기 전에 워커로 만든다 |
| `FrameRenderer` · `RenderView` | Renderer/Frame/ | 한 프레임 오케스트레이션과 뷰 |
| `PassConstantRing` · `RenderPsoCache` · `TransientAttachmentPool` | Renderer/Frame/ | FrameRenderer 가 소유하는 셋 — 드로우별 상수버퍼 슬롯 링 · PSO 저장소(해제 순서) · 이름으로 찾는 첨부 풀 |
| `FrameRendererCompute` | Renderer/Frame/ | 컴퓨트 프리패스 넷 — 인스턴스 애니메이션 · 메시 모프 · GPU 컬링 · 인스턴스 정렬. 그래프 패스가 아니라 그리기 전에 직접 걸린다 |
| `RenderGraph` | Renderer/Graph/ | 패스 의존성 정렬·배리어 추론 |
| `RenderThread` | Renderer/ | 렌더 스레드 루프 |
| `GpuSceneBuilder` → `GpuSceneSnapshot` → `GpuScene` | Renderer/Scene/ | GT 가 씬을 훑어 스냅샷을 만들고 RT 가 받아 GPU 버퍼로 올린다 — 두 클래스는 스냅샷 타입으로만 만난다 |
| `GpuMeshVertexPool` · `GpuMeshMorphPool` | Renderer/Scene/ | RT 소유 GPU 풀 — 씬 정점을 한 버퍼에(멀티 드로우) · 모프 결과 |
| `RenderPipelineAssetCache` · `RenderPassAsset` · `RenderPipelineAsset` | Renderer/Pipeline/ | XML/에셋 쪽 패스·파이프라인 |
| `RenderFramePacket` | Renderer/Frame/ | 프레임 입력 패킷 |
| `DebugDrawQueue` · `RenderTargetRegistry` | Renderer/Debug/ | 에디터가 읽는 디버그 통로 — 라인/스피어 큐 · 프레임 렌더타깃 목록 |

---

## 프레임 스트림 vs 커맨드 리스트 (헷갈리기 쉬운 용어)

기록 대상은 둘입니다. **디바이스가 소유한 프레임 스트림**(`getFrameStreamContext`, `beginFrame` 이 열고 `endFrame` 이 제출)과
**패스마다 만드는 커맨드 리스트**(`createCommandList`)입니다. `IRHICommandContext` 는 `IRHICommandList` 와 같은 기록 API 를 쓰되
기록 범위가 없는 쪽(`beginCommandList`/`endCommandList` 는 no-op)입니다.

| 무엇 | 얻는 곳 | 쓰는 곳 |
|------|---------|---------|
| 프레임 스트림 컨텍스트 | `IRHIDevice::getFrameStreamContext()` | Present · 오프스크린 경로 · 프레임 단위 작업 |
| 커맨드 리스트 | `IRHIDevice::createCommandList()` → `executeCommandList` | FrameRenderer 가 그래프 패스를 기록(병렬 레벨이면 여러 스레드) |
| 즉시 제출 | `executeCommandListImmediate` | 프레임 밖 일회성 작업(스모크 · 업로드 · 썸네일) |

`executeCommandList` 는 프레임 스트림을 그 지점에서 잘라 [앞 세그먼트][이 리스트][새 세그먼트] 순서로 잇고 `endFrame` 에서 한 번에
제출합니다 — 같은 큐의 제출 순서가 곧 실행 순서입니다. 백엔드별 리스트의 실체:

- DX11/DX12: 리스트가 자기 네이티브 Deferred Context(`FinishCommandList`) · `ID3D12GraphicsCommandList` + 얼로케이터를 소유한다.
- Vulkan: 리스트가 커맨드 풀 + 커맨드 버퍼 쌍을 디바이스 풀에서 빌리고 GPU 펜스를 지난 뒤 돌려준다(풀은 외부 동기화 대상이라 리스트마다 따로).
- OpenGL: 커맨드 버퍼 개념이 없는 스레드 종속 상태 머신이라 리스트가 컨텍스트를 감싸 즉시 GL 을 부를 뿐이다(병렬 기록 없음).

기록 중의 상수버퍼 갱신은 리스트로 갑니다(`IRHICommandList::updateConstantBuffer`). `IRHIResourceFactory::updateConstantBuffer` 는 기록 밖 전용입니다.

---

## 초심자: 어디부터 읽나?

| 궁금한 것 | 열 곳 |
|-----------|--------|
| 용어·클래스 역할 | 위 Glossary / 클래스 표 |
| 한 프레임이 어떻게 도나 | `Renderer/Frame/FrameRenderer.*` (+ `PassExecute` / `Draw` / `Pso` …) |
| 패스 순서·의존성 | `pipeline/*.xml` + `RenderGraph` |
| 머티리얼 파라미터 | `Material/Material.*` · `MaterialInstance.*` |
| GPU API 호출 | `RHI/IRHI*.h` → 활성 백엔드 `RHI/<Backend>/` |
| 셰이더 컴파일 | `Shader/Compile/ShaderCompiler.*` · `Shader/Reflection/ShaderReflection.*` |

`FrameRenderer` 는 **한 클래스·여러 .cpp** 이고, 자기 뮤텍스와 수명을 따로 가진 상태 셋은 클래스로 떼어 소유합니다
(`PassConstantRing` · `RenderPsoCache` · `TransientAttachmentPool`). 파일 역할부터 익히고, 상태가 어디 사는지는 그 셋을 보면 됩니다.

---

## 백엔드 성숙도 (패리티)

Windows에서 **DX11 · DX12 · Vulkan · OpenGL**은 Device / 프레임 스트림 / 커맨드 리스트 / Resource / SwapChain 경로에서 **같은 구현 수준**이다.

| | Vulkan | DX11 | GL | DX12 |
|--|:------:|:----:|:--:|:----:|
| Device / Resource | ● | ● | ● | ● |
| 프레임 스트림 + 커맨드 리스트 | ● | ● | ● | ● |
| CommandContext (draw/barrier/…) | ● | ● | ● | ● |
| SwapChain | `VulkanRHISwapChain` | `D3D11RHISwapChain` | 없음(컨텍스트) | `D3D12RHISwapChain` |
| Bindless 텍스처 배열 | set 1 텍스처 배열 | 에뮬(t 슬롯) | 에뮬(t 슬롯) | `_bBindlessRootSignature`(t0 space1 테이블) |

**의도적 차이(패리티 예외):**

- `prepareTextureForShaderRead` · `transitionBuffer` — DX12/VK 는 배리어 · 레이아웃 전이. DX11 은 상태 전이가 없는 대신 **해저드를 푼다**
  (읽기 전에 PS SRV 슬롯을 비우고, 읽기 상태로 돌리는 버퍼를 CS UAV 슬롯에서 뗀다 — 안 떼면 런타임이 SRV 를 NULL 로 강제하고 경고만 낸다).
  GL 은 `glMemoryBarrier`
- Exclusive graphics context thread — DX11/GL만
- Native bindless sampling — DX12/VK 는 무제한 텍스처 배열 + 인덱스; DX11/GL 은 bind-at-draw 로 기능 동등. 버퍼(인스턴스·머티리얼 데이터)는 4 백엔드가 같은 StructuredBuffer 슬롯을 쓴다
- Vulkan 만 렌더패스 객체를 미리 만들어 캐시한다(`VulkanRHIRenderPassCache`). `createRenderPass(desc)` 는 첨부가 없으면 거절한다

프레임 수명주기(`beginFrame`/`endFrame`/`resize`)는 `IRHIDevice` 에 있고, 창의 백버퍼는
`<백엔드>RHISwapChain` 이 소유합니다 — 백버퍼·이미지 인덱스·리소스 상태·동기화 객체·present 가
한 객체에 모여 있습니다. 이것은 가상 인터페이스가 아니라 **백엔드 내부의 구체 클래스**입니다 — 백엔드 밖에서
스왑체인을 다형적으로 다룰 이유가 없고, 가상 인터페이스로 만들면 상태 없는 껍데기가 됩니다. GL 에는 없습니다
(`SwapBuffers(HDC)` 가 present 의 전부이고 그 HDC 는 스레드 바인딩에도 쓰이므로 스왑체인이 아니라 컨텍스트입니다).

---

## 리플렉션 구동 셰이더 바인딩

**셰이더(.hlsl)만 고치면 된다.** C++ 에 미러 struct 없음 — 엔진이 `ShaderReflection` 으로 셰이더가
선언한 CB 멤버/텍스처 이름·레지스터를 읽어 바인딩한다.

```text
PSO desc → ShaderBindingLayoutCache.getOrBuild(desc, backend)   (컴파일 → 리플렉션 → 레이아웃, 캐시)
                    ↓
FrameRenderer: 패스마다 FrameResourceRegistry 에 "ShadowMap"/"SceneColor"/... 등록,
               PassConstantValues 에 g_ViewProj/g_World/... 값 채움
                    ↓
드로우 직전 ShaderParameterBinder::bindGraphics(layout, registry, values, ...)
   - PassCB(b0)  : 리플렉션 멤버 오프셋에 값 기록 → 엔진 CB 슬롯 업로드 → bindConstantBuffer (패스마다 한 번)
   - g_SwMaterials(t9): 셰이더 타입별 StructuredBuffer<SwMaterialData> — GpuScene 이 Material/MaterialInstance 버퍼를
                    리플렉션 stride 로 채워 배치 전에 bindStructuredBuffer. PS 는 인스턴스의 _materialIndex 로 원소를 읽는다
   - 텍스처       : g_<Name>Index 멤버는 registry 에서 자동 채움 (DX12/VK 텍스처 배열)
                    비네이티브(DX11/GL)는 bindShaderResource(srv, 리플렉션 t#)
   - MaterialCB(b1): 인스턴스 버퍼가 없는 픽스처(fullscreentriangle) 만 — Material 버퍼를 상수버퍼로 건다
   - 샘플러       : 정적 세트 s0..s7 (SW_SAMPLER_*, `swSampleIndexWith`) — DX12 정적 샘플러 / Vulkan immutable / DX11 s9..s15 샘플러 상태 / GL 은 결합 샘플러라 samplerId 무시.
                    엔진 텍스처 슬롯(t0..t3)은 네 백엔드가 계약 샘플러 하나(`SW_ENGINE_TEXTURE_SAMPLER` = 선형 · 클램프)로 읽는다
   - RW 텍스처    : 컴퓨트 전용 `swStoreRwTexture2D( index, texelPosition, value )` — DX12/VK 배열(registerBindlessTextureUav 인덱스), DX11/GL u4..u7 서수
   - 루트 상수    : `SW_ROOT_CONSTANTS_BEGIN … SW_ROOT_CONSTANTS_END` + `SW_ROOT( field )` ← setComputeRootConstants (16 dword)
```

| 파일 | 역할 |
|------|------|
| `Shader/Binding/ShaderBindingSlots.h` | 슬롯·공간·Vulkan 시프트 상수 (C++ 측). `Resource/engine/shaders/bindingslots.hlsli` 를 include 해 정본을 공유 |
| `Shader/Binding/ShaderBindingLayout.{h,cpp}` | 스테이지별 `ShaderReflectionData` 병합 → 이름/레지스터/CB멤버 조회 + 지문 |
| `Shader/Binding/ShaderBindingLayoutCache.{h,cpp}` | (경로+define+백엔드) 키 캐시. 핫리로드 시 `invalidateByShaderPath` |
| `Renderer/Frame/FrameResourceRegistry.{h,cpp}` | 패스 스코프 이름→{텍스처/버퍼, bindless 인덱스} |
| `Renderer/Frame/ShaderParameterBinder.{h,cpp}` | `bindGraphics` + `PassConstantValues` (대형 미러 struct 대체) |
| `Resource/engine/shaders/binding.hlsli` | PassCB(b0) + `g_SwInstances`(t4) + `SW_MATERIAL_BEGIN/END`(→ `g_SwMaterials` t9) + 텍스처 배열/슬롯 분기 + `swSampleShadow/Source/...` 헬퍼 (4백엔드) |

**셰이더 작성 규칙**: `#include "binding.hlsli"` → `g_ViewProj` 등 PassCB 필드와 `SampleXxx(uv)` 를 바로
쓴다. 새 엔진 텍스처가 필요하면 `binding.hlsli` PassCB 에 `uint g_<Name>Index;` 추가 + 엔진이
`FrameResourceRegistry` 에 `"<Name>"` 등록. `#if VULKAN/OPENGL` 분기 금지 — `binding.hlsli` 가 처리한다.

**정점을 받는 셰이더는 `SwVertexInput`(common.hlsli) 하나만 쓴다.** DX 는 시맨틱 이름으로 묶지만 Vulkan·GL 은
**선언 순서로 location** 을 매긴다 — `struct VSInput { pos; col }` 처럼 중간 속성을 빼면 col 이 노멀을 읽는다.
리플렉션이 정점 입력(시맨틱·location)을 읽고
`ShaderBindingValidator` 5번 규칙이 `constant::arrVertexAttribute` 와 대조하므로, 어긋난 바이너리는 nogpu 테스트에서
이름과 숫자로 떨어진다.

### 패스 입력 역할 계약 — 파이프라인 XML 의 선언이 곧 바인딩이다

풀스크린 패스(Lighting · SSAO · Bloom · Outline · TAA · Tonemap · Present)는 XML 의 `_listInput` 을 **역할 이름으로
전부 건다**(`FrameRenderer::registerDeclaredInputs`). 첨부 이름·포맷 → 역할은 `resolveRenderPassInputRole` 하나가
정한다: 고정 역할 이름(GBufferAlbedo · GBufferNormal · ShadowMap · AOColor)은 그 역할, 그 밖의 깊이는 SceneDepth,
나머지 컬러는 SourceColor. 셰이더는 역할 이름으로 읽는다(`g_SourceColorIndex` · `g_AmbientOcclusionIndex` …).
타깃은 선언한 출력 중 첫 번째로 존재하는 것이다.

`RenderPassInputSignature`(Pipeline/, 타입마다의 목록은 `RenderPassTypeInfo` 표의 칸)가 타입마다 읽는 역할의 필수/선택 목록이고, `RenderPipelineAsset::validate` 4번
검사가 로드 시점에 대조한다 — 계약에 없는 역할을 선언하면 "선언만 있고 바인딩되지 않는 입력", 필수 역할이 빠지면
"셰이더가 kInvalidIndex 를 읽습니다", SourceColor 가 둘이면 오류. 새 역할이 필요하면 (1) enum 과
이름표, (2) 패스 종류 표(`RenderPassTypeInfo`)의 계약 칸, (3) PassCB 의 `g_<Role>Index`, (4) 에뮬 슬롯 표(`swSampleIndex` · `commitBindlessTextureBindings`)
네 곳이다. `FrameRenderer::setInputRoleEnabled( role, false )` 는 그 역할을 걸지 않는 쇼 플래그다(테스트가 켬/끔을 비교한다).

**`ShaderBindingLayoutCache::getOrBuild`는 반드시 실제 디바이스의 `backend`를 받는다** (전역 `gv_rhiBackend`
사용 금지) — 한 프로세스에 여러 `IRHIDevice` 가 공존하면(멀티 백엔드 파리티 테스트 등) 전역값이 실제
디바이스와 어긋나 엉뚱한 셰이더 변형을 리플렉션한다.

### GPUScene 인스턴스드 드로우 (언리얼 방식)

메시 드로우는 per-instance world/material 을 **영속 구조버퍼**(`SwInstanceData` — 정의는 `instancedata.hlsli` 하나로 그래픽스와 컴퓨트
셋이 함께 쓴다, C++ `GpuInstance` 와 112 바이트 레이아웃 일치)에서 읽고, 배치당 간접 드로우 하나로 그린다(같은 PSO 의 배치들은 멀티 드로우 하나). VS 는 입력 어셈블러가 주는
인스턴스 슬롯(`SW_INSTANCESLOT` — 간접 인자의 startInstance(배치 시작) + 서수)으로 `swLoadInstance( input.instanceSlot )`
를 불러 월드 행렬과 `materialIndex` 를 얻어 PS 에 넘기고, PS 는 `SW_MATERIAL( materialIndex )` 로 셰이더 타입별 머티리얼
버퍼 `g_SwMaterials`(t9) 의 원소를 읽는다.
`g_SwInstancesIndex` 가 `kInvalidIndex` 면 `g_World`/`g_MaterialIndex` 폴백(풀스크린 · 픽스처 드로우). 인스턴스·머티리얼 버퍼는 **4백엔드가
같은 슬롯(t4/t9)** 을 쓰고 백엔드는 그 슬롯을 어떻게 거는지만 다르다:

| 백엔드 | t4/t9 구조버퍼를 거는 방법 |
|--------|--------------------|
| DX12   | t/u 슬롯 **디스크립터 테이블** — 등록 때 오프라인(CPU) 힙에 만든 뷰를 드로우/디스패치 직전 `flushSlotTables` 가 온라인 힙 블록에 `CopyDescriptors` 해 루트 테이블로 건다(언리얼 `FD3D12DescriptorCache`). CB 만 루트 CBV. 텍스처 배열은 힙 시작 테이블(t0 space1). 루트 예산 25/64 dword (`shaderslot::dx12`). SM6.6 힙 인덱싱은 쓰지 않는다 |
| DX11   | `StructuredBuffer` SRV — `createStructuredBuffer` 가 SRV 생성, `VS/PSSetShaderResources` |
| OpenGL | SSBO `glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 슬롯)` |
| Vulkan | 슬롯 세트(set 0, binding 16+슬롯 STORAGE_BUFFER). 바인딩이 바뀐 드로우 직전 **커맨드 버퍼 자신의 풀 묶음**(`VulkanDescriptorPoolSet`, 언리얼 `FVulkanDescriptorPoolSetContainer`)에서 세트를 할당해 쓴다(`flushSlotSet`) — 락 없음, 버퍼가 펜스를 지나 재사용될 때 통째로 리셋. 텍스처 배열·immutable sampler 는 set 1 |

- 드로우가 인스턴스를 찾는 길은 아래 "씬 드로우 경로" 절(인스턴스 슬롯 스트림 → 가시 목록 → 인스턴스 버퍼)이다.
- **RHI ABI**: 기록 표면(`IRHIDevice` · `IRHIResourceFactory` · `IRHICommandList`)을 바꾸면 `RHIModuleAbi.h` 의 버전과 도장을 함께 올리고 Engine 과 `RHI_*` 를 함께 다시 짓는다.

---

## 소유와 수명 — 누가 만들고, 누가 놓고, 누가 빌리는가

"누가 소유하고 누가 빌리는지" 를 타입에 둔다. 그렇지 않으면 RT 가 만든 값을 GT 가 덮어쓰고, 스냅샷이 해제된
머티리얼을 읽고, 게임 모듈이 만든 객체를 엔진이 모듈 사후에 놓는다. 아래가 규칙이고 타입과 린트가 지킨다 — 문서만 믿지 말 것.

| 객체 | 만드는 곳 · 소유 | 렌더 스레드가 보는 방식 |
|---|---|---|
| `Mesh` | `Mesh::create*()` (Engine) · `MeshComponent` 의 `shared_ptr` | 스냅샷 배치가 `shared_ptr` 로 **함께 소유**, `upload()` 에서 역참조 |
| `Material` | `Material::create()` (Engine) · `MaterialCache` 의 `shared_ptr` + 참조 수 | 스냅샷 배치·원소가 `shared_ptr` 로 함께 소유 |
| `MaterialInstance` | `MaterialInstance::create()` (Engine) · `MeshComponent` 의 `shared_ptr` | 위와 같음. `updateRhi` 를 RT 가 부른다 |
| `Texture2D` | `TextureCache` 의 `unique_ptr` + 참조 수 | 보지 않는다 — 머티리얼이 **SRV 인덱스(값)** 로 실어 준다 |
| GPU 핸들(버퍼·텍스처) | `IRHIResourceFactory` | 파괴는 디바이스 해제 큐가 **펜스 뒤로 미룬다**(DX12·Vulkan). CPU 객체가 먼저 죽어도 된다 |
| `GpuSceneSnapshot` | GT `GpuSceneBuilder::exportCpuSnapshot` 이 프레임마다 만들어 `RenderFramePacket` 에 싣는다 | RT `GpuScene::adoptCpuSnapshot` 이 통째로 받는다 |
| GPU 슬롯·컬 뷰·간접 개수 | RT `GpuScene` 이 `upload()` 에서 만든다 | 스냅샷 타입에 없으므로 **옮겨질 수 없다** |

규칙 일곱:

1. **스레드를 넘어 역참조하는 것은 소유를 함께 싣는다.** 스냅샷·패킷의 멤버는 `shared_ptr` 이거나 값이다.
   생포인터는 키(`GpuMaterialElementKey`) 같은 **정체성**에만 쓴다 — 키는 비교만 하고 역참조하지 않는다.
2. **한쪽만 만드는 값은 그쪽 타입에만 있다.** 옮겨지는 것은 `GpuSceneSnapshot` 하나로 묶고, `export`/`adopt` 는 그
   타입을 통째로 옮긴다. 필드를 손으로 골라 복사하는 함수를 다시 만들지 말 것.
   만드는 쪽(`GpuSceneBuilder`)과 받는 쪽(`GpuScene`)은 **다른 클래스**다 — RT 가 씬을 읽거나 GT 가 GPU 핸들을
   만지는 코드는 컴파일되지 않는다. 씬 직접 경로(`FrameRenderer::execute( pScene )`)도 자기 빌더로 같은 길을 탄다.
3. **모듈 경계를 넘어 소유될 수 있는 객체는 Engine 의 `create()` 로만 태어난다 — 컴파일러가 지킨다.**
   `shared_ptr` 의 제어 블록은 `make_shared` 를 부른 DLL 에 산다. 그래서 Material · MaterialInstance · Mesh 의
   생성자는 `create()` 만 만들 수 있는 열쇠(`CreateKey`)를 요구한다: 모듈에서 `make_shared` 해도, 스택에 값으로
   두어도 **컴파일되지 않는다.** 덤으로 "shared 로 소유되지 않은 머티리얼" 이 존재할 수 없어 스냅샷이 언제나
   `shared_from_this` 로 소유를 빌릴 수 있다. (`Material*` 인자는 ADL 로 `std::make_shared` 를 끌어오므로
   Engine 안에서도 `sw::make_shared` 로 한정한다.)
4. **놓는 순서는 디바이스보다 먼저.** 기동 표에서 FrameRenderer 단계가 RHI 단계 뒤에 서므로, 역순 종료에서 스냅샷 소유를
   먼저 놓고 디바이스를 내린다. 소멸자에 맡기면 디바이스 사후에 GPU 자원을 돌려주려 한다.

5. **핸들 값은 디바이스 안에서만 정체성이다.** 새 디바이스의 첫 PSO·버퍼·디스크립터는 옛 디바이스와 **같은 번호**를
   받는다(할당 순서가 결정적이다). 핸들 값으로 "그대로인가" 를 판단하는 캐시 — `FramePassContext` 의 마지막 바인딩,
   `RenderGraphExecutionContext` 의 리소스 상태 — 는 디바이스를 내릴 때 함께 잊는다(`resetBindingCache` · `reset`).
   GPU 버퍼를 드는 객체는 핸들을 맨몸으로 들지 말고 **`RHIResidentBuffer`**(핸들 · 디바이스)로 든다 — `isResident()` 가
   "올라가 있다" 를, `getLiveDevice()` 가 해제해도 되는 디바이스만 돌려준다. 값이 남아 있다는 것만으로 "살아 있는
   디바이스의 것" 임이 보장되는 이유는 아래 5-1 이다.

5-1. **GPU 자원을 드는 객체는 `RHIRenderResource` 를 상속한다 — 예외 없이.** 언리얼 `FRenderResource` 와 같은 자리다.
   태어날 때 전역 등록부에 자기를 넣고, 디바이스 수명 이벤트가 목록 전체에 밀어 넣는다:
   `IRHIDevice::shutdown()` 이 **자원을 내리기 전에**(종료 1 단계) `releaseAllFor( this )`, `~IRHIDevice` 가 안전망으로
   `forgetAllFor( this )`, 새 디바이스가 선 직후 `EngineLoop` 이 `initAllFor( device )`.
   지금 상속하는 것은 `Mesh` · `Material` · `MaterialInstance` · `Texture2D` 넷이다.
   **바깥에서 캐시를 훑어 일괄 해제·일괄 재생성하는 함수를 다시 만들지 말 것** — 목록에서 빠진 것이 조용히 틀린다.
   통보 중에 남이 파괴될 수 있으므로(머티리얼이 텍스처 참조를 놓으면 그 자리에서 `Texture2D` 가 죽는다) 등록부는
   부르기 직전에 "아직 있나" 를 잠금 아래에서 다시 묻는다.
6. **게임 모듈이 씬 오브젝트를 들 때는 핸들이다.** 상태 복원(모듈 리로드 · RHI 교체)은 씬을 통째로 지우고 다시
   만든다. 생포인터는 죽은 주소가 되고 `ComponentHandle` 은 nullptr 로 끝난다. 절차 생성물은 스냅샷에 싣지 말고
   `onBeforeStateSerialize` 에서 걷고 `onAfterStateDeserialize` 에서 다시 만든다(`BenchScene`).

7. **GPU 리소스는 그리기 전에 만든다.** 렌더 스레드는 그리기만 한다. 게임 스레드가 "이번 프레임에 그릴 것" 을
   알고 있으므로 스냅샷을 내보내기 전에 `GpuUploadQueue` 로 넘겨 워커가 병렬로 만든다(`-gv_gpuUploadQueue=0` 으로
   끌 수 있다). 워커 생성 가능 여부는 백엔드가 답한다(`_bThreadSafeResourceCreation`) — OpenGL 은 컨텍스트가
   스레드에 묶여 인라인으로 돈다. 큐는 **앞당기는 장치**이지 유일한 통로가 아니다: 큐가 못 다룬 것은 렌더
   스레드가 그 자리에서 만든다(`Mesh::initRhi` 는 멱등이다).

무엇이 무엇을 지키는가: 옮겨지는 값의 집합은 `GpuSceneSnapshot` **타입**이, 생성·소유 방식은 **패스키 생성자**가
컴파일 시점에 지킨다. C++ 가 못 막는 것은 "옮겨지는 구조체에 원시 포인터 필드를 추가하는 것" 하나이고, 그것만
`Scripts/lint/gate/CheckRenderOwnership.py` 가 본다(CTest `lint` 라벨 · pre-commit).
재현·회귀 테스트: `RenderPassGpuTest.MaterialLifetimeFollowsPacket` (ASAN 프리셋에서 해제 후 사용을 잡는다),
`RenderPassGpuTest.RendererSurvivesDeviceRecreate` (디바이스 재생성 뒤 유리 큐브).
헤드리스 재현: `-gv_rhiSwapAtFrame=30 -gv_rhiSwapTo=<0..3>` (DX11=0 · DX12=1 · Vulkan=2 · GL=3) 과 `-gv_screenshotFrame=100`.

## 의존 · 레이어

- `Graphics`(Renderer 제외)는 티어 5, `Graphics/Renderer` 는 티어 8 이다 — RHI · Shader · Material 이 Renderer 나 Object · Scene 을
  include 하면 실패한다. 컴포넌트가 채우는 GPU 값 묶음(`GpuLight` · `GpuSpriteInstanceData`)이 `Shader/Binding/` 에 있는 이유다.
- Graphics → Editor / GameFramework / Games **금지**. 둘 다 `CheckEngineLayers.py` 가 강제한다([Engine/README.md](../README.md) 티어 표).
- Dev 의 RHI 백엔드는 모듈 DLL 이고 엔트리는 `RHI/Modules/` 에 있다(Shipping 은 하나를 Engine 에 정적 링크)

---

## 자주 하는 실수

| 실수 | 결과 |
|------|------|
| Caps의 bindless = 실제 native | DX12는 `supportsNativeBindlessSampling()` → `_bBindlessRootSignature` 확인 |
| DebugDrawQueue = GPU 즉시 드로우 | 큐 API; 화면 표시는 Editor GameView 등 소비 측 |
| 프레임 스트림 컨텍스트 = 커맨드 리스트 | 프레임 스트림은 디바이스가 소유하고 `endFrame` 에서 제출되며, 리스트는 패스마다 만들어 `executeCommandList` 로 잇는다 — 위 표 참고 |
| gen/머티리얼 XML을 코드에 하드코딩 | `Resource/engine/` 파이프라인·머티리얼 에셋 사용 |
| DX11/GL `prepareTextureForShaderRead` · `transitionBuffer` 를 빈 함수로 두기 | DX11 은 슬롯 해저드(SRV ↔ RTV · UAV)를 여기서 풀고 GL 은 메모리 배리어를 낸다 — 비우면 그 백엔드만 0 을 읽는다 |
| 명령줄에 백엔드를 안 주고 원하는 백엔드로 돌았다고 믿기 | 명령줄이 고르지 않으면 `EngineConfig` 의 `_defaultRHI` 가 이긴다. `-dx11` / `-dx12` / `-vk` / `-gl`(또는 `-gv_rhiBackend=Vulkan` 처럼 열거자 이름 · 숫자 — 모르는 이름이면 기동이 멈춘다)로 명시하고, 로그의 백엔드 이름으로 확인한다 |
| 백엔드 패리티를 "실행 성공" 으로 판정 | `RenderPassGpuTest.FrameRendererParityAllBackends` 는 SceneColor 를 읽어 큐브 픽셀과 평균을 비교한다 — 픽셀을 보지 않는 스모크는 아무것도 증명하지 않는다 |

---

## 설계 메모 — 언리얼과 같은 것, 다른 것

남은 일은 [docs/06_Backlog.md](../../../docs/06_Backlog.md) 의 그래픽스 절이 정본입니다. 여기에는 구조와 주의만 둡니다.

**언리얼과 같은 자리:**

- **Present PSO 는 대상 포맷마다 하나.** PSO 의 렌더 타깃 포맷은 바인딩된 타깃의 실제 포맷(`IRHIResourceFactory::getTextureFormat( handle )` ·
  `IRHIDevice::getBackBufferFormat()`)에서 뽑는다. `buildPresentPsoVariants` 가 셋업에서 백버퍼 · 오프스크린 포맷을 미리 만들고,
  기록 중의 `ensurePresentPso` 는 조회만 한다 — PSO 생성은 락 없는 핸들 표 · Vulkan 렌더패스 캐시를 건드리므로 태스크 워커에서 만들면 안 된다.
- **Vulkan 슬롯 세트 풀은 커맨드 버퍼 쌍이 들고 다닌다**(`VulkanDescriptorPoolSet`, 언리얼 `FVulkanDescriptorPoolSetContainer`). 쌍이 GPU 펜스를
  지나 돌아온 뒤 `beginCommandList` 가 통째로 리셋한다 — 할당 경로에 락이 없다.
- **DX12 루트 시그니처 25/64 dword**: CB 는 루트 CBV, t/u 슬롯은 디스크립터 테이블(`flushSlotTables` 가 바뀐 테이블만 온라인 힙 블록에 복사).
  `ShaderBindingValidatorTest.Dx12RootSignatureFitsBudget` 가 계약에서 예산을 계산한다.
- **패스 상수버퍼는 드로우마다 슬롯을 받는다**(`PassConstantRing`, 기록 전에 `PassConstantRing::ensureCapacity` 로 배치 수만큼). 한 버퍼를 드로우들이
  나눠 쓰면 GPU 는 제출 뒤에 읽으므로 모두 마지막 값을 본다 — `RenderPassGpuTest.MultiBatchPassKeepsPerBatchConstants` 가 메시 둘로 고정한다.
- **머티리얼 원소는 영속 ID**(GPUScene 식): 처음 본 쌍에만 자리를 주고, 안 쓰이면 지연 회수하되 **자리를 옮기지 않는다**(인스턴스에 적힌
  materialIndex 가 엉뚱한 원소를 가리키게 된다). `GpuSceneTest.MaterialElementIdsPersistAcrossBuildsAndAreFreed`.
- **머티리얼 폴백 버퍼는 stride 마다 하나**(`ensureMaterialFallbackBuffers`, stride 는 `ShaderBindingSlot::_elementStride`) — SRV 의 구조 stride 는
  셰이더 선언과 같아야 한다(RDG 더미 버퍼와 같은 규칙).
- **인스턴스 원소 레이아웃은 시험이 대조한다.** `ShaderBindingValidatorTest.InstanceElementLayoutMatchesCpuStruct`(nogpu)가 쿠킹된 바이너리의
  stride · 필드 오프셋을 `GpuInstance` 와, 컴퓨트 쪽 이름(`g_Instances` · `g_InstancesRW`)까지 같은 표로 본다.
- **투명 순서의 정본은 CPU 한 곳**(`GpuSceneBuilder::sortTransparent` — 정렬 레이어 키 → 깊이 → 후보 번호). GPU 컬링이 투명 배치를 압축한 뒤
  `instancesort.hlsl` 은 깊이를 다시 재지 않고 **인스턴스 번호 오름차순**으로 되돌린다(배치 안의 인스턴스가 CPU 순서로 놓이므로). 깊이는 직교 카메라에서
  시선 축, 원근에서 거리다(`Render2DSettings::computeTransparentSortAxis`) — [2D/README.md](2D/README.md).
- **스프라이트 프레임 · 색은 인스턴스 칸**(`GpuInstance::_sprite` = `GpuSpriteInstanceData` 16 바이트 — 프레임 · 색 · 픽셀 스냅, Custom Primitive Data 자리). 배치 키를
  건드리지 않아 같은 텍스처의 스프라이트는 한 드로우다. 스프라이트 메시는 양면 사각형(`MeshUtil::createSpriteQuad`)이고 UV 는 메시의 것이다
  (`RenderPassGpuTest.SpriteFramesAndTintsArePerInstance`).
- **값이 실제로 바뀔 때만 일한다.** 상수버퍼 · 바인딩 상태(DX12 슬롯 테이블 · Vulkan 슬롯 세트)는 내용이 달라질 때만 버전을 올리고 다시 만든다 —
  같은 값을 다시 넣는 호출이 흔하다.

**의도적으로 다른 것:**

- **트랜지언트 메모리 앨리어싱 없음.** 각 트랜지언트를 개별 텍스처로 프레임 내내 든다(정확성이 아니라 메모리 차이).
- **DX12 에 PSO 디스크 캐시가 없다**(`ID3D12PipelineLibrary`). Vulkan 은 종료 때 파이프라인 캐시를 저장한다.
- **배리어는 레벨 프롤로그가 한꺼번에 발행한다**(`RenderGraph::setLevelPrologue`). 그래프에서 스플릿 배리어를 뽑지 않는다 — 단순하고 병렬 기록에
  안전한 대신 세밀한 겹침을 포기했다.
- **텍스처 배열 용량은 고정** + 펜스 뒤 인덱스 재사용. 스트리밍 · 축출은 디스크립터가 아니라 텍스처 스트리밍의 일이다.
- **GL 은 결합 샘플러뿐**(ARB_gl_spirv 는 분리 샘플러 불가)이라 슬롯의 샘플러를 엔진이 정한다. DX11/GL 은 텍스처를 슬롯에 걸므로 머티리얼 경계가 곧 배치 경계다.

**주의 — OpenGL 클립 규약.** GL 은 `glClipControl`(오프스크린 `GL_UPPER_LEFT` · 기본 프레임버퍼 `GL_LOWER_LEFT`, `GL_ZERO_TO_ONE`)로 DX 규약에 맞춘다.
있는지는 함수 포인터로 판단한다 — `#ifdef GL_CLIP_CONTROL` 같은 토큰은 없어서 그렇게 감싸면 블록이 통째로 빠진다. 안 걸리면 화면이 상하 반전되고
깊이 정밀도가 절반이 된다. 패리티 시험은 비대칭 장면에서 **그려진 픽셀의 무게중심이 위쪽인지**를 단언한다(평균 · 픽셀 수는 반전에 무관하다).

## 씬 드로우 경로 — 정점 풀 · 배치 표 · 인스턴스 슬롯 스트림

같은 PSO·머티리얼(버퍼·CB·텍스처·원소 수)의 연속 배치는 `drawIndirect( args, offset, count )` 한 번(멀티 드로우)이다.
배치마다 다른 값은 드로우 호출이 아니라 **데이터**가 준다:
- `GpuMeshVertexPool` — 씬 메시 정점을 한 정점 버퍼에 이어 붙인다. 간접 인자의 `startVertex` 가 풀 오프셋. 메시 집합이 같으면
  다시 만들지 않는다. 못 든 메시는 자기 버퍼(멀티 드로우엔 못 묶인다).
- `g_SwBatches`(t13, `GpuBatchInfo` 32바이트) — 배치의 인스턴스 시작·모프 풀 시작·정점 풀 시작. 패스당 한 번 건다. 컬링 t1 과 같은 버퍼.
- 인스턴스 슬롯 스트림(정점 슬롯 1, `SW_INSTANCESLOT`, uint, 인스턴스 스텝) — `0,1,2,…`. 간접 인자의 `startInstance` 가 배치 시작이라
  입력 어셈블러가 네 API 모두 `startInstance + i` 를 준다. 정점 셰이더는 `swLoadInstance( input.instanceSlot )` 로 자기 인스턴스를,
  `inst.meshBatchIndex` 로 배치 표를 읽는다. **SV_InstanceID 는 쓰지 않는다**(startInstance 포함 여부가 API 마다 달라서).
- 루트 상수는 그룹당 하나(`g_SwMaterialCount`, `setGraphicsRootConstants` — DX12 루트 상수 / Vulkan 푸시 상수 / DX11·GL 은 b2 에뮬).

**API 차이 하나는 남는다 — SV_VertexID.** Vulkan·GL 은 startVertex 를 포함하고 D3D 는 드로우 안의 0 기반 번호다.
`binding.hlsli` 의 `swComputeMorphElement` 가 흡수하고 `RHIDeviceTest.SceneDrawVertexIdStartsAtZeroOnlyOnD3D` 가 네 백엔드의 기대를 고정한다.
**버린 설계**: DX12 커맨드 시그니처의 루트 상수 주입 + Vulkan/GL DrawIndex — 그림은 맞지만 DX12 ExecuteIndirect 가 호출당 두 배
느려진다(런타임 패치). 비용은 상태 변경이 아니라 호출 수라 정렬 순서로는 줄지 않는다.
진단: `-gv_drawMerge=0`(배치마다 호출) · `-gv_vertexPool=0`(메시마다 정점 버퍼).

## 성능을 잴 때 — 먼저 VSync 를 확인한다

**프레임 시간이 주사율에 붙어 있으면 CPU 측정은 전부 무의미하다.** VSync 는 설정(`EngineConfig._window._bVSync`) → CLI 순으로
정해지고, 프레젠트 경로는 `IRHIDevice::isVSyncEnabled()` 를 읽는다.

- 기본은 **꺼짐**(`Config/Engine/EngineConfig.json` 의 `"_bVSync": false`). 켜서 재려면 `-vsync`.
- DX11·DX12 는 `Present( 0, 0 )` 만으로는 안 꺼진다 — 스왑체인 `ALLOW_TEARING` 플래그와 Present 플래그가
  **짝**이어야 하고, `ResizeBuffers` 도 같은 플래그를 다시 넘겨야 한다(`RHI/DX/RHIDxgiTearing.h`).
- Vulkan 은 present 호출에 동기화 인자가 없다 — **스왑체인 present 모드**(FIFO/MAILBOX/IMMEDIATE)가 그 자리다.
- **의심되면 숫자를 나눠 보라.** `1 / RT.Frame` 이 모니터 주사율과 같으면 vsync 에 붙어 있는 것이다.

프로파일 구간은 게임 스레드와 렌더 스레드를 **각각 통째로** 잡는다 — 그래야 "이게 프레임의 몇 %인가" 에
답할 수 있다. 읽는 법은 두 뺄셈이다:

| 구간 | 뜻 |
|------|-----|
| `GT.Frame` | 게임 스레드 프레임 전체 (`EngineLoop::tick`) |
| `GT.Packet.submit` | GT 가 **RT 를 기다린** 시간. 크면 렌더 스레드가 밀린 것이다(링이 차면 submit 이 막는다) |
| `RT.Frame` | 렌더 스레드 프레임 전체 |
| `RT.Present` | 제출·표시 대기. `RT.Frame - RT.Present` 가 순수 기록 시간이다 |

```powershell
build/Ninja-Release/Bin/App.exe -gv_benchMeshes=2000 -gv_benchMeshVariants=200 -gv_profileFrames=200 -dx12
```

**Debug 로 재지 말 것** — 컨테이너의 레이스 검출기가 결과를 바꾼다(`measure-in-release`).

## 검증 절차 (바인딩·백엔드를 건드렸다면 전부)

```powershell
cmake --build --preset Ninja-Debug
build/Ninja-Debug/Bin/App.exe --cook-shaders                                   # 쿠킹된 바이너리 + reflection.manifest 갱신 (계약 테스트가 이걸 읽는다)
build/Ninja-Debug/Bin/EngineTest.exe --test_filter=ShaderBindingValidatorTest.*   # 계약 + 네 백엔드 리플렉션 레이아웃 일치
build/Ninja-Debug/Bin/EngineTest.exe --test_filter=RHIDeviceTest.*               # 컴퓨트 RW 텍스처 쓰기→읽기(4 백엔드) 포함
build/Ninja-Debug/Bin/EngineTest.exe --test_filter=GpuSceneTest.*,RenderPassTest.*,RenderPassGpuTest.*   # 스냅샷 규칙 · 그래프 · 픽셀 패리티(FrameRendererParityAllBackends)
py -3 Scripts/dev/BackendSmoke.py                                                # 실제 앱 경로: 네 백엔드 PPM 평균·큐브 픽셀 수
```

- 백엔드는 `-dx11 / -dx12 / -vk / -gl` 플래그로 고른다(안 주면 `EngineConfig` 의 `_defaultRHI`).
- `-gv_rhiBackBufferFormat=1` 은 B8G8R8A8 백버퍼를 요청한다 — 백버퍼 PSO 가 `getBackBufferFormat()` 을 따르는지(Vulkan 렌더패스 호환) 이걸로 본다.
  로그의 `백버퍼 포맷: 요청 → 채택` 줄이 실제 채택값이다.
- DX11/DX12 디버그 레이어 메시지는 프레임 끝에 `[Error]` 로 로그에 나온다(`flushDebugMessages`). 스모크 로그의 `[Error]` 수가 0 이 아니면 읽어라.
- **OpenGL 도 같다.** 비-Shipping 은 디버그 컨텍스트(`WGL_CONTEXT_DEBUG_BIT_ARB`) + KHR_debug 콜백이라 GL 오류가
  `[Error]`, 중간 심각도가 `[Warning]` 으로 나온다(알림은 끈다).
- Vulkan 렌더패스는 `VulkanRHIRenderPassCache::RenderPassSpec` + `createRenderPassFromSpec` 한 자리에서만 만든다(스왑체인 CLEAR/LOAD · 오프스크린 ·
  PSO 호환 · 합성 · desc 여섯 자리가 그것을 채운다). 첨부/의존성을 손으로 적는 자리를 다시 만들지 말 것.
- 셰이더 .hlsli 를 고쳤으면 반드시 `--cook-shaders` 를 다시 돌린다 — 개발 빌드 런타임은 매니페스트가 지금 소스에서 나온 것이 아니면(`cook.stamp` 내용 해시)
  런타임 리플렉션으로 폴백하지만, 테스트와 배포본은 쿠킹된 바이너리 · 매니페스트를 본다.
- **스왑체인·프레젠트를 건드렸으면 창을 실제로 흔들어야 한다.** `ResizeBuffers` 의 플래그가 생성 때와
  어긋나면 그 뒤의 Present 가 `INVALID_CALL` 이 되는데, 리사이즈를 안 하면 영원히 드러나지 않는다.
  확인은 스크린샷 크기로 한다 — 창을 700×520 으로 바꾸고 `-gv_screenshot` 을 찍으면 PPM 헤더가
  `684 481`(클라이언트 영역)로 따라와야 하고, 로그의 `[Error]` 는 0 이어야 한다.
- **빌드 로그를 grep 해서는 경고를 셀 수 없다.** 경고는 그 TU 가 컴파일되는 순간에만 나오고, ninja 는
  바뀌지 않은 파일을 다시 컴파일하지 않는다 — 경고를 들여온 그 빌드 이후로는 영원히 안 보인다.
  `py -3 Scripts/lint/report/RunBuildWarnings.py` 가 트리 전체에 다시 물어본다(기본이 Debug · Release · Shipping
  셋이다 — 구성마다 경고 집합이 다르다). **0 이 정답이다.**

---

## 더 볼 곳

- [ARCHITECTURE.md](../../../ARCHITECTURE.md) — RHI · RenderPass · Pipeline  
- `Resource/engine/pipeline/` · `Resource/engine/renderpass/`  
- [Engine/README.md](../README.md) — 레이어 규칙
