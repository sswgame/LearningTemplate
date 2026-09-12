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
RHI (IRHIDevice / IRHICommandContext / Resources)
        ↓
DX11 · DX12 · OpenGL · Vulkan
```

| 폴더 | 역할 |
|------|------|
| **RHI/** | 백엔드 추상화·구현·(옵션) RHI DLL 모듈 |
| **Material/** | XML 머티리얼, 인스턴스, 패킹, 캐시 |
| **Shader/** | `Compile/` 컴파일·캐시·베이크·핫리로드 · `Reflection/` 리플렉션과 매니페스트 · `Binding/` 슬롯 계약 |
| **Mesh/** | 메시 버퍼 |
| **Renderer/** | `Frame/` FrameRenderer · `Graph/` RenderGraph · `Pipeline/` 패스·파이프라인 리소스 · `Scene/` GpuScene · RenderThread |
| **Debug/** | DebugDrawQueue (라인/스피어 큐 → Editor GameView ImGui 소비) |

---

## 용어집 (Glossary)

| 용어 | 뜻 |
|------|-----|
| RHI | Rendering Hardware Interface — GPU API(DX/VK/GL) 앞의 엔진 추상화 |
| Backend | RHI의 구체 구현 (DX11/DX12/Vulkan/OpenGL). **구현 수준은 네 백엔드 패리티** |
| Device (`IRHIDevice`) | GPU 장치·리소스 생성·커맨드 제출의 진입점 |
| Context (`IRHICommandContext`) | 실제 draw/dispatch/barrier 등을 내는 제출/기록 슬롯 |
| CommandList / CL | 프레임에서 쌓는 명령 묶음(이 엔진은 소프트웨어 `Cmd` 벡터) |
| Immediate / Deferred | **Mode**(언제 재생) 또는 **Context**(어느 슬롯) — 반드시 구분 (아래) |
| Replay | 기록된 `Cmd`를 Context API로 순서 재생 (네이티브 Execute ≠) |
| Resource / Handle | GPU 버퍼·텍스처 등 + 엔진 쪽 핸들 테이블 식별자 |
| PSO / PipelineState | 셰이더+고정 상태 묶음 (그래픽스/컴퓨트) |
| SwapChain | 화면 present용 백버퍼 체인 |
| Material / MaterialInstance | 셰이더·파라미터 정의 / 인스턴스 값 |
| Compiler · Reflection · BindingLayout | HLSL 컴파일 · 바이트코드 바인딩 메타 · 병합된 조회 레이아웃 |
| RenderPass (개념) | 한 번의 begin/end 렌더 타깃 구간 (XML 패스와 혼동 주의) |
| RenderGraph | 패스·리소스 의존성 그래프로 프레임 순서 결정 |
| FrameRenderer | 한 프레임: 패킷 → 그래프 → CL 기록/실행 |
| RenderThread | 렌더 스레드 루프 (offscreen/present 등) |
| GpuScene | 씬 → GPU 업로드용 데이터 |
| Frame packet | 프레임에 넘기는 CPU 측 렌더 입력 묶음 |
| Bindless / Descriptor | 리소스 인덱스로 셰이더 접근; 디스크립터 테이블/힙 |
| DebugDrawQueue | 디버그 라인/스피어 **큐** (즉시 GPU 드로우 아님) |

## 주요 클래스 역할

| 클래스 | 폴더 | 한 줄 역할 |
|--------|------|------------|
| `RHI` | RHI/ | 백엔드 선택·디바이스 수명·`gv_rhiCommandListMode` |
| `IRHIDevice` (+ 백엔드 `*RHIDevice`) | RHI/ | 리소스·PSO·CL 생성, Context 소유, execute |
| `IRHICommandContext` (+ `*RHICommandContext`) | RHI/ | draw/dispatch/barrier/blit 실행면 |
| `IRHICommandList` (+ 백엔드 `*RHICommandList`) | RHI/ | 명령 기록 — 모든 백엔드가 소프트웨어 replay 없이 즉시 `*RHICommandContext`를 호출 |
| `IRHIResource` | RHI/ | 리소스(버퍼·텍스처·PSO) 추상 |
| `RHIHandleTable` · `FrameResourceRing` · `RHIReleaseQueue` | RHI/ | 핸들·프레임링·지연 해제 |
| `Material` · `MaterialInstance` · `MaterialCache` | Material/ | 정의·인스턴스·캐시 |
| `ShaderCompiler` · `ShaderCache` · `ShaderBaker` · `LiveShaderManager` | Shader/Compile/ | HLSL → 바이트코드, 디스크 캐시, 오프라인 베이크, 수동 리로드 |
| `ShaderReflection` · `ShaderReflectionLibrary` | Shader/Reflection/ | 바이트코드 리플렉션과 구운 매니페스트 |
| `ShaderBindingSlots` · `ShaderBindingLayout` · `ShaderBindingContract` | Shader/Binding/ | 슬롯 정본, 병합 레이아웃, 구운 바이너리 대조 |
| `Mesh` | Mesh/ | 메시 버퍼 |
| `FrameRenderer` · `RenderView` | Renderer/Frame/ | 한 프레임 오케스트레이션과 뷰 |
| `RenderGraph` | Renderer/Graph/ | 패스 의존성 정렬·배리어 추론 |
| `RenderThread` · `GpuScene` | Renderer/ · Renderer/Scene/ | 렌더 스레드 루프 · 씬 GPU 스냅샷 |
| `RenderPassManager` · `RenderPassResource` · `RenderPipelineResource` | Renderer/Pipeline/ | XML/에셋 쪽 패스·파이프라인 |
| `RenderFramePacket` | Renderer/Frame/ | 프레임 입력 패킷 |
| `DebugDrawQueue` | Debug/ | 디버그 드로우 큐 |

---

## CommandList vs Context (헷갈리기 쉬운 용어)

**Context는 “어느 제출/기록 슬롯이냐”, CommandList는 “FrameRenderer가 기록에 쓰는 핸들”.**
모든 백엔드가 네이티브(또는 즉시 호출) `IRHICommandList`를 반환하므로 `RHICommandListMode`는
`createCommandList()`에서 사실상 무시된다 — Context 쌍은 지금도 디바이스가 항상 들고 있다.

| 용어 | 무엇인가 | 역할 |
|------|----------|------|
| Immediate **Context** (`getImmediateContext`) | GPU에 바로/최종 제출하는 컨텍스트 | present, offscreen 경로, ImGui 오버레이가 직접 사용 |
| Deferred **Context** (`getDeferredCommandContext`) | Immediate와 같은 기록 대상을 감싸는 별개 인스턴스 | 레거시 슬롯 구분 유지(현재는 present 대상 아님 정도의 의미만 남음) |
| `IRHICommandList` (`createCommandList()`) | FrameRenderer가 그래프를 기록하는 핸들 | `beginCommandList`/각 draw·bind 호출/`endCommandList`가 그 자리에서 바로 백엔드 API로 나간다 |

### 기록이 실제로 나가는 방식 (모든 백엔드 공통, replay 없음)

과거엔 CPU `vector<Cmd>`에 op를 쌓았다가(`RHIDeferredCommandList`) 나중에 `IRHICommandContext`
가상 호출로 재생(replay)했지만, 지금은 모든 백엔드가 그 중간 계층 없이 즉시 호출한다.

- DX11/DX12: `*RHICommandList`가 자신만의 네이티브 Deferred Context/CommandList를 소유하고
  그 자리에서 바로 API를 호출, `executeCommandList()`가 실제 제출(`ExecuteCommandLists`)을 한다.
- Vulkan/OpenGL: `*RHICommandList`는 새 네이티브 자원을 만들지 않고 `*RHICommandContext`를 그대로
  감싸 즉시 호출만 전달한다 — 버퍼 open/close·제출은 지금도 `beginFrame`/`endFrame`(Vulkan) 또는
  GL 컨텍스트(스레드 종속, begin/end 없음)가 그대로 소유하므로 `executeCommandList()`는 no-op이다.

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

`FrameRenderer` 는 **한 클래스·여러 .cpp** 로 이미 나뉘어 있습니다. 클래스를 더 쪼개기보다 파일 역할만 익히면 됩니다.

---

## 백엔드 성숙도 (패리티)

Windows에서 **DX11 · DX12 · Vulkan · OpenGL**은 Device / Context / SwapChain·Resource facade / dual Context+Mode / CommandList execute 경로에서 **같은 구현 수준**을 목표로 한다.

| | Vulkan | DX11 | GL | DX12 |
|--|:------:|:----:|:--:|:----:|
| Device / Resource | ● | ● | ● | ● |
| Immediate + Deferred Context | ● | ● | ● | ● |
| CommandContext (draw/barrier/…) | ● | ● | ● | ● |
| SwapChain | Device 로직 + thin facade | 동상 | 동상 | SwapChain TU에 로직 |
| Bindless 텍스처 배열 | set 1 텍스처 배열 | 에뮬(t 슬롯) | 에뮬(t 슬롯) | `_bBindlessRootSignature`(t0 space1 테이블) |

**의도적 차이(패리티 예외):**

- DX11/GL `prepareTextureForShaderRead` — 상태리스라 no-op (DX12/VK는 barrier/layout)
- DX11 `transitionBuffer` no-op; GL은 `glMemoryBarrier` soft
- Exclusive graphics context thread — DX11/GL만
- Native bindless sampling — DX12/VK 는 무제한 텍스처 배열 + 인덱스; DX11/GL 은 bind-at-draw 로 기능 동등. 버퍼(인스턴스·머티리얼 데이터)는 4 백엔드가 같은 StructuredBuffer 슬롯을 쓴다
- Vulkan `createRenderPass(desc)` — 비어 있으면 swapchain RP alias; 어태치먼트가 있으면 **소유** VkRenderPass 생성

프레임 수명주기(`beginFrame`/`endFrame`/`resize`)는 `IRHIDevice` 에 있고, 창의 백버퍼는
`<백엔드>RHISwapChain` 이 소유합니다 — 백버퍼·이미지 인덱스·리소스 상태·동기화 객체·present 가
한 객체에 모여 있습니다. 예전에는 `IRHISwapChain` 이라는 **가상 인터페이스**가 있었지만 상태를 하나도
갖지 않아 구현 넷 중 셋이 Device 로 그대로 넘기기만 했고, DX12 만 내용이 있었는데 그마저 Device 의
private 멤버를 만지느라 `friend` 가 필요했습니다. 지금 것은 가상 인터페이스가 아니라 **백엔드 내부의
구체 클래스**입니다 — 백엔드 밖에서 스왑체인을 다형적으로 다룰 이유가 없어서, 그렇게 만들면 없앴던
껍데기가 그대로 돌아옵니다. GL 에는 없습니다(`SwapBuffers(HDC)` 가 present 의 전부이고 그 HDC 는
스레드 바인딩에도 쓰이므로 스왑체인이 아니라 컨텍스트입니다).

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
드로우 직전 ShaderBindingBinder::bindGraphics(layout, registry, values, ...)
   - PassCB(b0)  : 리플렉션 멤버 오프셋에 값 기록 → 엔진 CB 슬롯 업로드 → bindConstantBuffer (패스마다 한 번)
   - g_SwMaterials(t9): 셰이더 타입별 StructuredBuffer<SwMaterialData_t> — GpuScene 이 Material/MaterialInstance 버퍼를
                    리플렉션 stride 로 채워 배치 전에 bindStructuredBuffer. PS 는 인스턴스의 _materialIndex 로 원소를 읽는다
   - 텍스처       : g_<Name>Index 멤버는 registry 에서 자동 채움 (DX12/VK 텍스처 배열)
                    비네이티브(DX11/GL)는 bindShaderResource(srv, 리플렉션 t#)
   - MaterialCB(b1): 인스턴스 버퍼가 없는 픽스처(fullscreentriangle) 만 — Material 버퍼를 상수버퍼로 건다
   - 샘플러       : 정적 세트 s0..s7 (SW_SAMPLER_*, `SW_SampleIndexWith`) — DX12 정적 샘플러 / Vulkan immutable / DX11 s9..s15 샘플러 상태 / GL 은 결합 샘플러라 samplerId 무시
   - RW 텍스처    : 컴퓨트 전용 `SW_StoreTex2D( index, coord, v )` — DX12/VK 배열(registerBindlessTextureUAV 인덱스), DX11/GL u4..u7 서수
   - 루트 상수    : `SW_ROOT_CONSTANTS_BEGIN … SW_ROOT_CONSTANTS_END` + `SW_ROOT( field )` ← setComputeRootConstants (16 dword)
```

| 파일 | 역할 |
|------|------|
| `Shader/Binding/ShaderBindingSlots.h` | 슬롯·공간·Vulkan 시프트 상수 (C++ 측). `Resource/engine/shaders/bindingslots.hlsli` 를 include 해 정본을 공유 |
| `Shader/Binding/ShaderBindingLayout.{h,cpp}` | 스테이지별 `ShaderReflectionData` 병합 → 이름/레지스터/CB멤버 조회 + 지문 |
| `Shader/Binding/ShaderBindingLayoutCache.{h,cpp}` | (경로+define+백엔드) 키 캐시. 핫리로드 시 `invalidateByShaderPath` |
| `Renderer/Frame/FrameResourceRegistry.{h,cpp}` | 패스 스코프 이름→{텍스처/버퍼, bindless 인덱스} |
| `Renderer/Frame/ShaderBindingBinder.{h,cpp}` | `bindGraphics` + `PassConstantValues` (대형 미러 struct 대체) |
| `Resource/engine/shaders/binding.hlsli` | PassCB(b0) + `g_SwInstances`(t4) + `SW_MATERIAL_BEGIN/END`(→ `g_SwMaterials` t9) + 텍스처 배열/슬롯 분기 + `SampleShadow/Source/...` 헬퍼 (4백엔드) |

**셰이더 작성 규칙**: `#include "binding.hlsli"` → `g_ViewProj` 등 PassCB 필드와 `SampleXxx(uv)` 를 바로
쓴다. 새 엔진 텍스처가 필요하면 `binding.hlsli` PassCB 에 `uint g_<Name>Index;` 추가 + 엔진이
`FrameResourceRegistry` 에 `"<Name>"` 등록. `#if VULKAN/OPENGL` 분기 금지 — `binding.hlsli` 가 처리한다.

**`ShaderBindingLayoutCache::getOrBuild`는 반드시 실제 디바이스의 `backend`를 받는다** (전역 `gv_rhiBackend`
사용 금지) — 한 프로세스에 여러 `IRHIDevice` 가 공존하면(멀티 백엔드 파리티 테스트 등) 전역값이 실제
디바이스와 어긋나 엉뚱한 셰이더 변형을 리플렉션한다.

### GPUScene 인스턴스드 드로우 (언리얼 방식)

메시 드로우는 per-instance world/material 을 **영속 구조버퍼**(`SwInstanceData`, C++ `GpuInstance` 와 레이아웃 일치)
에서 읽고, 배치당 `drawInstanced` 한 번으로 그린다. VS 는 `SwLoadInstance( SV_InstanceID )` 로 월드 행렬과
`materialIndex` 를 얻어 PS 에 넘기고, PS 는 `SW_MATERIAL( materialIndex )` 로 셰이더 타입별 머티리얼 버퍼
`g_SwMaterials`(t9) 의 원소를 읽는다. PassCB `g_InstanceBase` = 배치 시작 오프셋, `g_SwInstancesIndex` 가
`SW_INVALID_INDEX` 면 `g_World`/`g_MaterialIndex` 폴백(레거시 드로우). 인스턴스·머티리얼 버퍼는 **4백엔드가
같은 슬롯(t4/t9)** 을 쓰고 백엔드는 그 슬롯을 어떻게 거는지만 다르다:

| 백엔드 | t4/t9 구조버퍼를 거는 방법 |
|--------|--------------------|
| DX12   | t/u 슬롯 **디스크립터 테이블** — 등록 때 오프라인(CPU) 힙에 만든 뷰를 드로우/디스패치 직전 `flushSlotTables` 가 온라인 힙 블록에 `CopyDescriptors` 해 루트 테이블로 건다(언리얼 `FD3D12DescriptorCache`). CB 만 루트 CBV. 텍스처 배열은 힙 시작 테이블(t0 space1). 루트 예산 25/64 dword (`shaderslot::dx12`). SM6.6 힙 인덱싱은 쓰지 않는다 |
| DX11   | `StructuredBuffer` SRV — `createStructuredBuffer` 가 SRV 생성, `VS/PSSetShaderResources` |
| OpenGL | SSBO `glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 슬롯)` |
| Vulkan | 슬롯 세트(set 0, binding 16+슬롯 STORAGE_BUFFER). 바인딩이 바뀐 드로우 직전 **커맨드 버퍼 자신의 풀 묶음**(`VulkanDescriptorPoolSet`, 언리얼 `FVulkanDescriptorPoolSetContainer`)에서 세트를 할당해 쓴다(`flushSlotSet`) — 락 없음, 버퍼가 펜스를 지나 재사용될 때 통째로 리셋. 텍스처 배열·immutable sampler 는 set 1 |

- GPU 컬(gpucull)은 `instanceCount` 만 줄이고 인스턴스 리스트를 compact 하지 않는다 (배치 앞 N개만 그림).
- **RHI ABI**: `bindConstantBuffer`/`bindStructuredBuffer`/`drawInstanced` 추가 (`RHIModuleAbi` stamp `rhi-cl-v4-2026-09`).

---

## 소유와 수명 — 누가 만들고, 누가 놓고, 누가 빌리는가

세 버그가 한 뿌리에서 나왔다(2026-09-12): 카메라를 움직일 때만 메시가 보이던 것(RT 가 만든 값을 GT 의 0 이
매 프레임 덮음), 머티리얼 해제 후 사용(스냅샷이 생포인터), 벤치 종료 세그폴트(게임 모듈이 만든 객체를 엔진이
모듈 사후에 놓음). 전부 "누가 소유하고 누가 빌리는지가 타입에 없어서" 다. 지금은 아래가 규칙이고 타입과 린트가
지킨다 — 문서만 믿지 말 것.

| 객체 | 만드는 곳 · 소유 | 렌더 스레드가 보는 방식 |
|---|---|---|
| `Mesh` | `Mesh::create*()` (Engine) · `MeshComponent` 의 `shared_ptr` | 스냅샷 배치가 `shared_ptr` 로 **함께 소유**, `upload()` 에서 역참조 |
| `Material` | `Material::create()` (Engine) · `MaterialCache` 의 `shared_ptr` + 참조 수 | 스냅샷 배치·원소가 `shared_ptr` 로 함께 소유 |
| `MaterialInstance` | `MaterialInstance::create()` (Engine) · `MeshComponent` 의 `shared_ptr` | 위와 같음. `updateRhi` 를 RT 가 부른다 |
| `Texture2D` | `TextureCache` 의 `unique_ptr` + 참조 수 | 보지 않는다 — 머티리얼이 **SRV 인덱스(값)** 로 실어 준다 |
| GPU 핸들(버퍼·텍스처) | `IRHIResource` | 파괴는 디바이스 해제 큐가 **펜스 뒤로 미룬다**(DX12·Vulkan). CPU 객체가 먼저 죽어도 된다 |
| `GpuSceneSnapshot` | GT 가 프레임마다 만들어 `RenderFramePacket` 에 싣는다 | RT `GpuScene::adoptCpuSnapshot` 이 통째로 받는다 |
| GPU 슬롯·컬 뷰·간접 개수 | RT `GpuScene` 이 `upload()` 에서 만든다 | 스냅샷 타입에 없으므로 **옮겨질 수 없다** |

규칙 일곱:

1. **스레드를 넘어 역참조하는 것은 소유를 함께 싣는다.** 스냅샷·패킷의 멤버는 `shared_ptr` 이거나 값이다.
   생포인터는 키(`GpuMaterialElementKey`) 같은 **정체성**에만 쓴다 — 키는 비교만 하고 역참조하지 않는다.
2. **한쪽만 만드는 값은 그쪽 타입에만 있다.** 옮겨지는 것은 `GpuSceneSnapshot` 하나로 묶고, `export`/`adopt` 는 그
   타입을 통째로 옮긴다. 필드를 손으로 골라 복사하는 함수를 다시 만들지 말 것 — 그것이 첫 번째 버그였다.
3. **모듈 경계를 넘어 소유될 수 있는 객체는 Engine 의 `create()` 로만 태어난다 — 컴파일러가 지킨다.**
   `shared_ptr` 의 제어 블록은 `make_shared` 를 부른 DLL 에 산다. 그래서 Material · MaterialInstance · Mesh 의
   생성자는 `create()` 만 만들 수 있는 열쇠(`CreateKey`)를 요구한다: 모듈에서 `make_shared` 해도, 스택에 값으로
   두어도 **컴파일되지 않는다.** 덤으로 "shared 로 소유되지 않은 머티리얼" 이 존재할 수 없어 스냅샷이 언제나
   `shared_from_this` 로 소유를 빌릴 수 있다. (`Material*` 인자는 ADL 로 `std::make_shared` 를 끌어오므로
   Engine 안에서도 `sw::make_shared` 로 한정한다.)
4. **놓는 순서는 디바이스보다 먼저.** `FrameRenderer::shutdown` · `EngineLoop::shutdown` 이 스냅샷 소유를 놓은 뒤
   디바이스를 내린다. 소멸자에 맡기면 디바이스 사후에 GPU 자원을 돌려주려 한다.

5. **핸들 값은 디바이스 안에서만 정체성이다.** 새 디바이스의 첫 PSO·버퍼·디스크립터는 옛 디바이스와 **같은 번호**를
   받는다(할당 순서가 결정적이다). 핸들 값으로 "그대로인가" 를 판단하는 캐시 — `FramePassContext` 의 마지막 바인딩,
   `RenderGraphExecutionContext` 의 리소스 상태 — 는 디바이스를 내릴 때 함께 잊는다(`resetBindingCache` · `reset`).
   GPU 버퍼를 드는 객체는 핸들을 맨몸으로 들지 말고 **`RHIResidentBuffer`**(핸들 · 디바이스)로 든다 — `isResident()` 가
   "올라가 있다" 를, `getLiveDevice()` 가 해제해도 되는 디바이스만 돌려준다. 값이 남아 있다는 것만으로 "살아 있는
   디바이스의 것" 임이 보장되는 이유는 아래 5-1 이다.

5-1. **GPU 자원을 드는 객체는 `RHIRenderResource` 를 상속한다 — 예외 없이.** 언리얼 `FRenderResource` 와 같은 자리다.
   태어날 때 전역 등록부에 자기를 넣고, 디바이스 수명 이벤트가 목록 전체에 밀어 넣는다:
   `IRHIDevice::shutdown()` 이 **자원을 내리기 전에** `releaseAllFor( this )`, `~IRHIDevice` 가 안전망으로
   `forgetAllFor( this )`, 새 디바이스가 선 직후 `EngineLoop` 이 `initAllFor( device )`.
   지금 상속하는 것은 `Mesh` · `Material` · `MaterialInstance` · `Texture2D` 넷이다.
   **바깥에서 캐시를 훑어 일괄 해제·일괄 재생성하는 함수를 다시 만들지 말 것** — 그것이 `MaterialCache::shutdownAllGpu`
   · `reinitializeAll` · `TextureCache::shutdownAllGpu` 였고, 목록에서 빠진 것은 조용히 틀렸다.
   통보 중에 남이 파괴될 수 있으므로(머티리얼이 텍스처 참조를 놓으면 그 자리에서 `Texture2D` 가 죽는다) 등록부는
   부르기 직전에 "아직 있나" 를 잠금 아래에서 다시 묻는다.
6. **게임 모듈이 씬 오브젝트를 들 때는 핸들이다.** 상태 복원(모듈 리로드 · RHI 교체)은 씬을 통째로 지우고 다시
   만든다. 생포인터는 죽은 주소가 되고 `ComponentHandle` 은 nullptr 로 끝난다. 절차 생성물은 스냅샷에 싣지 말고
   `onBeforeStateSerialize` 에서 걷고 `onAfterStateDeserialize` 에서 다시 만든다(`BenchScene`).

7. **GPU 리소스는 그리기 전에 만든다.** 렌더 스레드는 그리기만 한다. 게임 스레드가 "이번 프레임에 그릴 것" 을
   알고 있으므로 스냅샷을 내보내기 전에 `GpuUploadQueue` 로 넘겨 워커가 병렬로 만든다(`-gv_gpuUploadQueue=0` 으로
   끌 수 있다). 워커 생성 가능 여부는 백엔드가 답한다(`_bThreadSafeResourceCreation`) — OpenGL 은 컨텍스트가
   스레드에 묶여 인라인으로 돈다. 큐는 **앞당기는 장치**이지 유일한 통로가 아니다: 큐가 못 다룬 것은 렌더
   스레드가 예전처럼 그 자리에서 만든다(`Mesh::initRhi` 는 멱등이다). **Release** 실측으로 400개 메시를 다시 올리는
   프레임의 `RT.GpuScene.uploadMeshes` 가 80.8ms → 0.03ms 였다(3회 반복, 79.9~82.7ms → 26~41us).

무엇이 무엇을 지키는가: 옮겨지는 값의 집합은 `GpuSceneSnapshot` **타입**이, 생성·소유 방식은 **패스키 생성자**가
컴파일 시점에 지킨다. C++ 가 못 막는 것은 "옮겨지는 구조체에 원시 포인터 필드를 추가하는 것" 하나이고, 그것만
`Scripts/lint/CheckRenderOwnership.py` 가 본다(CTest `lint` 라벨 · pre-commit 6/6).
재현·회귀 테스트: `RenderPassGpuTest.MaterialLifetimeFollowsPacket` (ASAN 프리셋에서 수정 전 UAF 를 잡았다),
`RenderPassGpuTest.RendererSurvivesDeviceRecreate` (디바이스 재생성 뒤 유리 큐브 — 수정 전 빈 화면).
헤드리스 재현: `-gv_rhiSwapAtFrame=30 -gv_rhiSwapTo=<0..3>` (DX11=0 · DX12=1 · Vulkan=2 · GL=3) 과 `-gv_screenshotFrame=100`.

## 의존 · 레이어

- Graphics → Reflection / Object 참조 **허용**
- Graphics → Editor / GameFramework / Games **금지** (`CheckEngineLayers.py`)
- RHI 모듈 DLL (`SW_RHI_AS_MODULES`) 시 백엔드는 별도 모듈 엔트리 (`RHI/Modules/`)

---

## 자주 하는 실수

| 실수 | 결과 |
|------|------|
| Caps의 bindless = 실제 native | DX12는 `supportsNativeBindlessSampling()` → `_bBindlessRootSignature` 확인 |
| DebugDrawQueue = GPU 즉시 드로우 | 큐 API; 화면 표시는 Editor GameView 등 소비 측 |
| Immediate Context = Immediate CommandList | Mode vs Context 혼동 — 위 표 참고 |
| gen/머티리얼 XML을 코드에 하드코딩 | `Resource/engine/` 파이프라인·머티리얼 에셋 사용 |
| DX11/GL prepareTexture가 “미구현 stub” | **의도적** 상태리스 no-op |
| `-gv_rhiBackend=Vulkan` 으로 백엔드 선택 | 무시된다 — EngineConfig `_defaultRHI` 가 덮어쓴다. `-dx11` / `-dx12` / `-vk` / `-gl` 플래그를 쓴다 (스모크가 이 실수로 네 번 다 DX12 를 돌렸다) |
| 백엔드 패리티를 "실행 성공" 으로 판정 | `RenderPassGpuTest.FrameRendererParityAllBackends` 는 SceneColor 를 읽어 큐브 픽셀과 평균을 비교한다 — 픽셀을 보지 않는 스모크는 아무것도 증명하지 않는다 |

---

## 알려진 보강 후보 (우선순위)

문서화된 로드맵이지, 이 README가 기능을 새로 만들지 않습니다.

완료됨 (참고):
- **P0** — 전 백엔드 dual Immediate/Deferred Context + Mode 배선
- **P0** — SSAO/TAA/Tonemap/GpuCull 패스 실행 경로
- **P0** — DX11/GL `prepareTextureForShaderRead` 정의 (의도적 no-op)
- **P1** — DX12 `_bBindlessRootSignature` ↔ `supportsNativeBindlessSampling()` / `getCapabilities()._bNativeBindless`
- **P1** — DebugDrawQueue 스피어 → `GameViewPanel` ImGui 원으로 소비
- **P2** — DX12 `HEAP_DIRECTLY_INDEXED` 실패 시 bind-at-draw (런타임 Caps, WARNING 제거)
- **P2** — Vulkan `createRenderPass(desc)` 소유 VkRenderPass 생성 + `destroy`/`shutdown`에서 해제 (빈 desc는 swapchain RP alias)
- **P2** — 전 백엔드(DX12/DX11/Vulkan/OpenGL) soft `Cmd` replay(`RHIDeferredCommandList`) 제거,
  즉시 호출하는 네이티브 `IRHICommandList`로 전환 완료 — DX11은 `FinishCommandList`, DX12는 자신만의
  `ID3D12GraphicsCommandList`, Vulkan/OpenGL은 기존 `*RHICommandContext`를 그대로 감싸 즉시 호출
- **P2** — `MaterialTypes.h` 분리, `ShaderReflection` 포맷별 TU + exhaustive switch, 오프스크린 파이프라인 스모크(2026-09-11 에 `TestRHI.cpp` 로 내렸다 — 부르는 곳이 테스트뿐이었다), FrameRenderer `FrameRendererStatus`, GpuMaterialRetireQueue
- **Perf** — Transparent 연속 mesh/mat 머지, GpuScene 내용·카메라 핑거프린트 캐시, Deferred CL 기본·`_frameCmd` 재사용·Cmd reserve 256

2026-09-07 바인딩 리워크 이후 남았던 것 — 상용 엔진(언리얼)이 같은 문제를 어떻게 푸는지에 맞춰 닫았다:
- **P1 해결** — Vulkan Present 렌더패스 포맷 불일치. 원인은 Present PSO 가 `R8G8B8A8` 상수로 만들어지고 스왑체인은 서피스와
  협상한 포맷(B8G8R8A8 일 수 있음)을 쓴 것. 언리얼은 PSO 초기화자의 `RenderTargetFormats` 를 **바인딩된 타깃의 실제 포맷**
  (`FRHITexture::GetFormat`, 스왑체인은 `FVulkanSwapChain` 이 되돌려 준 포맷)에서 뽑고 그것이 PSO 캐시 키다. 같은 구조로:
  `IRHIResource::getTextureFormat( handle )` + `IRHIDevice::getBackBufferFormat()` 을 정본으로 두고 `FrameRenderer::ensurePresentPso( format )`
  가 대상 포맷별 PSO 를 캐시한다(백버퍼 vs GameView RT). `-gv_rhiBackBufferFormat=1`(언리얼 `r.DefaultBackBufferPixelFormat` 자리)로
  B8G8R8A8 백버퍼를 실제로 돌려 검증 에러 0 을 확인했다.
- **P2 해결** — Vulkan 슬롯 세트 풀의 전역 뮤텍스 제거. 언리얼 `FVulkanDescriptorPoolSetContainer` 처럼 **커맨드 버퍼 쌍이 자기 풀 묶음**
  (`VulkanDescriptorPoolSet`)을 들고 다니고, 쌍이 GPU 펜스를 지나 재사용 풀로 돌아온 뒤 `beginCommandList` 가 통째로 리셋한다.
  디바이스 프레임 스트림은 링 슬롯별 묶음. 할당 경로에 락이 없다.
- **P2 해결** — DX12 루트 예산 51 → 25 dword. 언리얼 `FD3D12RootSignature` 배치대로 CB 는 루트 CBV, t/u 슬롯은 **디스크립터 테이블**.
  등록이 뷰를 오프라인 힙에도 만들고(`_offlineCpuHandle`), 드로우/디스패치 직전 `flushSlotTables` 가 바뀐 테이블만 온라인 힙 블록에
  `CopyDescriptors` 해 건다(`FD3D12DescriptorCache` + 서브할당 온라인 힙). 블록은 리스트가 닫힐 때 펜스 뒤 반납. 안 걸린 슬롯은 null 뷰.
  `ShaderBindingContractTest.Dx12RootSignatureFitsBudget` 이 계약에서 예산을 계산한다 — 테이블 안의 슬롯 수는 예산에 들지 않는다.
- **P3 해결(DX11)** — `SW_SampleIndexWith` 의 samplerId 를 DX11 도 존중한다: 정적 샘플러 세트를 s9..s15 샘플러 상태로 걸고(`bindStaticSamplers`,
  즉시/지연 컨텍스트 모두) 셰이더가 리터럴 분기로 고른다. GL 은 결합 샘플러뿐(ARB_gl_spirv 는 분리 샘플러 불가)이라 슬롯의 샘플러를
  엔진이 정한다 — 언리얼 OpenGL RHI 도 같은 제약(`glBindSampler` 유닛 단위).
- **P3 설계 결정(구현 안 함)** — 텍스처 배열 용량은 언리얼도 고정(cvar 로 정한 힙 크기) + 펜스 뒤 인덱스 재사용이며, 스트리밍·축출은
  디스크립터가 아니라 텍스처 스트리밍 시스템의 일이다(이미 지연 해제는 4 백엔드 구현됨). 큐브맵/3D 는 같은 배열 바인딩에 타입만 다른
  선언을 겹쳐 두는 방식(Vulkan: set 1 binding 0 에 `TextureCube[]` 별칭, DX12: 같은 힙 시작을 가리키는 space3 범위)이 언리얼식이지만,
  엔진에 큐브/3D 텍스처 리소스 자체가 없어 바인딩만 먼저 열 이유가 없다. DX11/GL 의 머티리얼 단위 배치도 언리얼과 같다 —
  `FMeshDrawCommand` 병합은 셰이더 바인딩이 같을 때만 일어나고, 텍스처를 슬롯에 거는 플랫폼에서는 머티리얼 경계가 곧 바인딩 경계다.

추가로 닫은 것 (2026-09-08, 위 검증 중에 드러난 것):
- **머티리얼 폴백 원소 stride** — 머티리얼 없는 배치에 걸던 폴백이 256 바이트 원소 **하나를 공용**으로 썼다. 셰이더의
  `SwMaterialData_t` 는 24 바이트라 DX11 디버그 레이어가 드로우마다 "structure stride 256 vs 24" 를 냈다(SRV 의 구조 stride 는
  셰이더 선언과 같아야 한다). 언리얼이 RDG 더미 버퍼를 `CreateStructuredDesc( sizeof( FElement ), 1 )` 로 만드는 것과 같게
  **stride 마다 하나**씩 만든다(`ensureMaterialFallbackBuffers`, PSO 를 다 등록한 뒤 셋업에서 — 기록 중에는 bindless 레지스트리를
  못 바꾼다). 필요한 stride 는 `ShaderBindingSlot::_elementStride`(리플렉션의 구조버퍼 원소 레이아웃)가 준다.
- **인스턴스 원소 레이아웃 테스트** — `g_SwInstances`(t4)는 C++ 이 쓰고 셰이더가 읽는 유일한 구조체인데 둘을 대조하는 것이
  없었다. `ShaderBindingContractTest.InstanceElementLayoutMatchesCpuStruct`(nogpu)가 구운 바이너리의 stride·필드 오프셋을
  `GpuInstance` 와 대조한다 — 오프셋을 일부러 4 틀리게 넣어 실패 메시지(파일 이름 + 숫자)까지 확인했다.

OpenGL 이 상하 반전으로 그리고 있었다 (2026-09-08):
- `glClipControl( GL_UPPER_LEFT, GL_ZERO_TO_ONE )` 이 **한 번도 불리지 않았다.** 호출부가 `#ifdef GL_CLIP_CONTROL` 로 감싸여
  있었는데 그런 GL 토큰은 없다(실제 토큰은 `GL_CLIP_ORIGIN` / `GL_CLIP_DEPTH_MODE`, `GL_CLIP_CONTROL` 은 함수 이름일 뿐이다).
  4.6 컨텍스트에서도 블록이 통째로 컴파일에서 빠졌고, 안에 있던 로그까지 같이 빠져 아무 흔적이 없었다.
- 결과 둘: (1) 프레임버퍼 원점이 좌하단으로 남아 GL 만 SceneColor 행 순서가 반대로 쌓였다 — 풀스크린 블릿은 DX 규약
  (NDC 위쪽 = uv.y 0)을 백엔드 분기 없이 쓰므로 **화면과 스크린샷이 통째로 상하 반전**. (2) 깊이 NDC 가 [-1,1] 로 남아
  [0,1] 을 내보내는 투영이 깊이 버퍼의 절반만 썼다(대소는 유지돼 그림자·깊이 테스트는 정상, 정밀도만 절반).
- 이제 함수 포인터로 판단하고 `glGetIntegerv( GL_CLIP_ORIGIN / GL_CLIP_DEPTH_MODE )` 로 **실제로 걸렸는지 GL 에 되묻는다.**
  못 걸면 에러 로그를 남긴다 — 조용히 지나가는 것이 이 버그의 본체였다.
- **왜 안 잡혔나**: `RenderPassGpuTest.FrameRendererParityAllBackends` 가 채널 평균과 그려진 픽셀 수만 비교했다 — 둘 다 상하
  반전에 무관하다. 게다가 큐브를 원점(= 카메라가 보는 지점)에 두어 그림이 세로로 대칭이라 어떤 지표로도 잡을 수 없었다.
  이제 큐브를 원점 위로 올려 비대칭하게 만들고 **그려진 픽셀의 무게중심이 이미지 위쪽인지** 단언한다. 수정을 되돌려
  OpenGL 만 실패하는 것(`무게중심 y=1055 가 중앙 640 보다 아래`)을 확인한 뒤 되살렸다.

Graphics 감사 후 고친 것 (2026-09-08):
- **패스 상수버퍼가 드로우마다 덮어써졌다.** `acquirePassCb` 는 패스당 슬롯 하나만 잡는데 `bindGraphics` 는
  드로우마다 그 버퍼에 `updateConstantBuffer` 를 했다. 백엔드는 버퍼의 **프레임 슬롯 하나**에 쓰고 GPU 는 제출
  뒤에 읽으므로, 한 패스의 모든 드로우가 마지막 배치의 `g_InstanceBase`·`g_SwMaterialCount` 를 봤다. 배치 키에
  메시가 들어가므로 메시가 둘이면 바로 재현된다 — 앞 배치의 메시가 뒤 배치의 인스턴스 자리에 그려졌다.
  이제 `bindForDraw` 가 드로우마다 슬롯을 잡고(언리얼의 드로우별 유니폼 버퍼와 같은 자리), 슬롯 수는 배치 수에
  맞춰 **기록 시작 전에** 늘린다(`ensurePassCbCapacity` — 기록 중에는 버퍼 생성·bindless 등록을 못 한다).
- **검증 공백이 그걸 가리고 있었다.** 벤치 씬도 패리티 테스트도 메시를 하나만 써서 배치가 늘 하나였다.
  `RenderPassGpuTest.MultiBatchPassKeepsPerBatchConstants` 가 같은 큐브를 두 번 따로 만들어 배치를 가르고 좌우
  양쪽에 그려졌는지 본다. 수정 전 DX11 은 왼쪽 0 픽셀, DX12 는 오른쪽 0 픽셀이었다.
- **Present PSO 를 기록 중에 만들고 있었다.** 포맷별 캐시를 넣으면서 없으면 그 자리에서 만들게 뒀는데, 그 자리가
  태스크 워커였다. PSO 생성은 `RHIHandleTable`(락 없음)과 Vulkan 렌더패스 캐시(락 없음)를 건드린다.
  `buildPresentPsoVariants` 가 셋업에서 백버퍼·오프스크린 두 포맷을 미리 만들고, `ensurePresentPso` 는 조회만 한다.
- **머티리얼 원소를 언리얼 GPUScene 식 영속 ID 로.** 예전엔 빌드마다 그룹을 지우고 인스턴스마다 선형 탐색으로
  인덱스를 다시 부여했다(O(인스턴스 x 머티리얼)). 이제 처음 본 쌍에만 자리를 주고, 안 쓰이면 지연 회수하되
  **자리를 옮기지 않는다**(옮기면 인스턴스에 적힌 materialIndex 가 엉뚱한 머티리얼을 가리킨다).
  `GpuSceneTest.MaterialElementIdsPersistAcrossBuildsAndRetire` 가 영속·회수·자리 재사용을 고정한다.
  측정: 큐브 2000·머티리얼 1 종에서 `build.batches` 665us → 564us. 맵만 넣었을 땐 901us 로 더 느렸다 —
  머티리얼이 하나뿐인 흔한 경우엔 해시가 1 원소 선형 탐색보다 비싸서, 직전 결과 메모를 앞에 뒀다.

아직 언리얼과 다른 것 (의도적, 또는 다음 과제):
- **트랜지언트 메모리 앨리어싱 없음.** 언리얼 RDG 는 생명주기가 겹치지 않는 트랜지언트끼리 메모리를 공유한다.
  여기는 각 트랜지언트를 개별 텍스처로 프레임 내내 들고 있다. 정확성 문제는 아니고 메모리 차이다.
- **DX12 에 PSO 디스크 캐시가 없다.** Vulkan 은 종료 시 파이프라인 캐시를 저장하는데 DX12 에는 대응물이 없다
  (`ID3D12PipelineLibrary`). 언리얼은 PSO 프리캐싱과 번들 캐시를 양쪽 다 쓴다.
- **배리어는 웨이브 프롤로그가 한꺼번에 발행한다.** 언리얼 RDG 는 그래프에서 뽑고 스플릿 배리어까지 쓴다.
  더 단순하고 병렬 기록에 안전한 대신, 세밀한 겹침은 포기한 설계다.

드로우 경로 최적화 (2026-09-08, 언리얼 구조로) — 다중 배치 워크로드에서 잰 것:

| 구간 | 이전 | 이후 |
|------|------|------|
| `RT.Draw.gpuBatches` | 5774us | 1924us |
| `RT.Graph.executeParallel` | 6890us | 2735us |
| 상수버퍼 구성 | 프레임당 403회 | 4.7회 |

측정 조건은 `-gv_benchMeshes=2000 -gv_benchMeshVariants=200`(배치 200, 프레임당 드로우 400), DX12 Debug 다.
**메시 종류가 곧 배치 수다** — 예전 벤치는 큐브 하나를 모두가 공유해 배치가 늘 하나였고, 그래서 드로우 경로를
전혀 재지 못했다(다중 배치 버그 둘이 그 뒤에 숨어 있었다).

무엇을 했나 — 전부 "값이 실제로 바뀔 때만 일한다" 는 한 가지 원칙이다:
- **드로우별 상수를 루트/푸시 상수로.** 배치마다 바뀌는 `g_InstanceBase`·`g_SwMaterialCount` 를 PassCB 에서
  빼내 `setGraphicsRootConstants` 로 싣는다(DX12 루트 상수 / Vulkan 푸시 상수 / DX11·GL 은 b2 에뮬).
  그래야 PassCB 가 진짜 패스 단위가 되어 드로우마다 다시 만들지 않는다. 언리얼이 `FMeshDrawCommand` 의
  느슨한 파라미터를 드로우별로 싣는 자리와 같다.
- **상수버퍼는 값·레지스트리가 바뀔 때만 다시 만든다.** 둘 다 **내용이 실제로 달라질 때만** 버전을 올린다 —
  같은 값을 다시 넣는 호출이 흔하다(드로우마다 같은 항등 행렬, 배치마다 같은 머티리얼 버퍼).
- **바인딩 상태도 같은 규칙.** DX12 슬롯 테이블과 Vulkan 슬롯 세트가 값과 무관하게 더럽혀져 드로우마다
  디스크립터를 복사하거나 세트를 새로 할당했다(언리얼 `FD3D12DescriptorCache` 는 내용이 같으면 그대로 쓴다).
- **CBV 갱신은 프레임당 한 번.** DX12 `updateConstantBuffer` 가 드로우마다 레지스트리 전체를 훑고 있었다.

남은 것은 대부분 API 호출 자체다 — `ExecuteIndirect` 867us, 정점버퍼 바인딩 407us(배치마다 메시가 달라
줄일 수 없다). 더 줄이려면 메시들이 정점 버퍼를 공유해 `multiDrawIndirect` 한 번으로 묶어야 한다
(언리얼의 통합 정점 버퍼 풀) — 별개의 구조 변경이다.

## 성능을 잴 때 — 먼저 VSync 를 확인한다 (2026-09-13)

**프레임 시간이 주사율에 붙어 있으면 CPU 측정은 전부 무의미하다.** 2026-09-13 까지 이 저장소는
`RenderThread` 가 `endFrame( true )` 를 못박고 있어서 `EngineConfig._window._bVSync` 도 CLI 도 효과가
없었고, 모든 프레임이 165Hz(6061us)에 정확히 붙어 있었다. 지금은 설정·CLI 가 실제로 먹는다.

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
build/Ninja-Debug/Bin/App.exe --bake-shaders                                   # 구운 바이너리 + reflection.manifest 갱신 (계약 테스트가 이걸 읽는다)
build/Ninja-Debug/Bin/EngineTest.exe --test_filter=ShaderBindingContractTest.*   # 계약 + 네 백엔드 리플렉션 레이아웃 일치
build/Ninja-Debug/Bin/EngineTest.exe --test_filter=RHITest.*                     # 컴퓨트 RW 텍스처 쓰기→읽기(4 백엔드) 포함
build/Ninja-Debug/Bin/EngineTest.exe --test_filter=RenderPassTest.*,RenderPassGpuTest.*   # FrameRendererParityAllBackends 가 SceneColor 픽셀을 비교
py -3 Scripts/dev/BackendSmoke.py                                                # 실제 앱 경로: 네 백엔드 PPM 평균·큐브 픽셀 수
```

- 백엔드는 `-dx11 / -dx12 / -vk / -gl` 플래그로 고른다. `-gv_rhiBackend=X` 는 무시된다.
- `-gv_rhiBackBufferFormat=1` 은 B8G8R8A8 백버퍼를 요청한다 — 백버퍼 PSO 가 `getBackBufferFormat()` 을 따르는지(Vulkan 렌더패스 호환) 이걸로 본다.
  로그의 `백버퍼 포맷: 요청 → 채택` 줄이 실제 채택값이다.
- DX11/DX12 디버그 레이어 메시지는 프레임 끝에 `[Error]` 로 로그에 나온다(`flushDebugMessages`). 스모크 로그의 `[Error]` 수가 0 이 아니면 읽어라.
- 셰이더 .hlsli 를 고쳤으면 반드시 `--bake-shaders` 를 다시 돌린다 — 런타임은 매니페스트가 소스보다 오래되면 런타임 리플렉션으로 폴백하지만 테스트는 구운 바이너리를 본다.
- **스왑체인·프레젠트를 건드렸으면 창을 실제로 흔들어야 한다.** `ResizeBuffers` 의 플래그가 생성 때와
  어긋나면 그 뒤의 Present 가 `INVALID_CALL` 이 되는데, 리사이즈를 안 하면 영원히 드러나지 않는다.
  확인은 스크린샷 크기로 한다 — 창을 700×520 으로 바꾸고 `-gv_screenshot` 을 찍으면 PPM 헤더가
  `684 481`(클라이언트 영역)로 따라와야 하고, 로그의 `[Error]` 는 0 이어야 한다.
- **빌드 경고는 종료 코드를 바꾸지 않는다.** `cmake --build --preset <preset> 2>&1 | grep -i "warning:"` 가
  0 이어야 한다 — Debug · Release · Shipping 각각.

---

## 더 볼 곳

- [ARCHITECTURE.md](../../../ARCHITECTURE.md) — RHI · RenderPass · Pipeline  
- `Resource/engine/pipeline/` · `Resource/engine/renderpass/`  
- [Engine/README.md](../README.md) — 레이어 규칙
