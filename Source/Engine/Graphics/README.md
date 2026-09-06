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
| **Shader/** | 컴파일 · 리플렉션 · 배리언트 · 핫리로드 |
| **Mesh/** | 메시 버퍼 |
| **RenderPass/** | FrameRenderer, RenderGraph, GpuScene, RenderThread |
| **Render/** | InstanceBuffer 등 보조 |
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
| ShaderVariant · Reflection · Compiler | 배리언트 키 · 바인딩 메타 · 컴파일 |
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
| `ShaderCompiler` · `ShaderReflection` · `ShaderVariant` · `ShaderCache` · `LiveShaderManager` | Shader/ | 컴파일·리플렉션·배리언트·캐시·핫리로드 |
| `Mesh` | Mesh/ | 메시 버퍼 |
| `FrameRenderer` · `RenderGraph` · `RenderThread` · `GpuScene` | RenderPass/ | 프레임 오케스트레이션 |
| `RenderPassManager` · `RenderPassResource` · `RenderPipelineResource` | RenderPass/ | XML/에셋 쪽 패스·파이프라인 |
| `RenderFramePacket` | RenderPass/ | 프레임 입력 패킷 |
| `InstanceBuffer` | Render/ | 인스턴싱 보조 |
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
| 한 프레임이 어떻게 도나 | `RenderPass/FrameRenderer.*` (+ `PassExecute` / `Draw` / `Pso` …) |
| 패스 순서·의존성 | `pipeline/*.xml` + `RenderGraph` |
| 머티리얼 파라미터 | `Material/Material.*` · `MaterialInstance.*` |
| GPU API 호출 | `RHI/IRHI*.h` → 활성 백엔드 `RHI/<Backend>/` |
| 셰이더 컴파일 | `Shader/ShaderCompiler.*` · `ShaderReflection.*` |

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
| Native bindless | 런타임 세트 기준 | 에뮬 | 에뮬 | `_bHeapDirectlyIndexed` |

**의도적 차이(패리티 예외):**

- DX11/GL `prepareTextureForShaderRead` — 상태리스라 no-op (DX12/VK는 barrier/layout)
- DX11 `transitionBuffer` no-op; GL은 `glMemoryBarrier` soft
- Exclusive graphics context thread — DX11/GL만
- Native bindless sampling — DX12/VK; DX11/GL은 bind-at-draw로 기능 동등
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
   - PassCB(b0)  : 리플렉션 멤버 오프셋에 값 기록 → 엔진 CB 슬롯 업로드 → bindConstantBuffer
   - MaterialCB(b1): Material 의 bindless 인덱스 → bindConstantBuffer
   - 텍스처       : g_<Name>Index 멤버는 registry 에서 자동 채움 (네이티브 bindless)
                    비네이티브(DX11/GL)는 bindShaderResource(srv, 리플렉션 t#)
```

| 파일 | 역할 |
|------|------|
| `Shader/ShaderBindingSlots.h` | 슬롯 용량 상수 (C++ 측). HLSL 미러 `Resource/engine/shaders/bindingslots.hlsli` |
| `Shader/ShaderBindingLayout.{h,cpp}` | 스테이지별 `ShaderReflectionData` 병합 → 이름/레지스터/CB멤버 조회 + 지문 |
| `Shader/ShaderBindingLayoutCache.{h,cpp}` | (경로+define+백엔드) 키 캐시. 핫리로드 시 `invalidateByShaderPath` |
| `RenderPass/FrameResourceRegistry.{h,cpp}` | 패스 스코프 이름→{텍스처/버퍼, bindless 인덱스} |
| `RenderPass/ShaderBindingBinder.{h,cpp}` | `bindGraphics` + `PassConstantValues` (대형 미러 struct 대체) |
| `Resource/engine/shaders/binding.hlsli` | `bindless.hlsli` 대체. PassCB 선언 + `SampleShadow/Source/...` 헬퍼 + GPUScene 인스턴스 (4백엔드) |

**셰이더 작성 규칙**: `#include "binding.hlsli"` → `g_ViewProj` 등 PassCB 필드와 `SampleXxx(uv)` 를 바로
쓴다. 새 엔진 텍스처가 필요하면 `binding.hlsli` PassCB 에 `uint g_<Name>Index;` 추가 + 엔진이
`FrameResourceRegistry` 에 `"<Name>"` 등록. `#if VULKAN/OPENGL` 분기 금지 — `binding.hlsli` 가 처리한다.

**`ShaderBindingLayoutCache::getOrBuild`는 반드시 실제 디바이스의 `backend`를 받는다** (전역 `gv_rhiBackend`
사용 금지) — 한 프로세스에 여러 `IRHIDevice` 가 공존하면(멀티 백엔드 파리티 테스트 등) 전역값이 실제
디바이스와 어긋나 엉뚱한 셰이더 변형을 리플렉션한다.

### GPUScene 인스턴스드 드로우 (언리얼 방식)

메시 드로우는 per-instance world/material 을 **영속 구조버퍼**(`SwInstanceData`, C++ `GpuInstance` 와 레이아웃 일치)
에서 읽고, 배치당 `drawInstanced` 한 번으로 그린다. VS 는 `SwLoadInstanceWorld( SV_InstanceID )` 로 월드 행렬을
얻는다. PassCB `g_InstanceBase` = 배치 시작 오프셋, `g_SwInstancesIndex` = 버퍼 bindless SRV 인덱스
(`SW_INVALID_INDEX` 면 `g_World` 폴백). **4백엔드 전부 지원**:

| 백엔드 | 인스턴스 버퍼 접근 |
|--------|--------------------|
| DX12   | `ResourceDescriptorHeap[g_SwInstancesIndex]` (힙 인덱싱, StructuredBuffer SRV — `registerBindlessResource` 가 구조버퍼면 CBV 대신 SRV 생성) |
| DX11   | `StructuredBuffer` SRV (t4) — `createStructuredBuffer` 가 SRV 생성, `VS/PSSetShaderResources` |
| OpenGL | SSBO `glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4)` |
| Vulkan | storage buffer descriptor set (space6/binding0) — 파이프라인 레이아웃 set 6+slot 에 바인딩. **set 4 는 정적 샘플러(`g_SwSamplerLinearWrap`, immutable) 전용** 레이아웃으로 매 드로우 set 0/1 과 함께 바인딩 (`bindGraphicsMaterialSets`) |

- GPU 컬(gpucull)은 `instanceCount` 만 줄이고 인스턴스 리스트를 compact 하지 않는다 (배치 앞 N개만 그림).
- **RHI ABI**: `bindConstantBuffer`/`bindStructuredBuffer`/`drawInstanced` 추가 (`RHIModuleAbi` stamp `rhi-cl-v4-2026-09`).

---

## 의존 · 레이어

- Graphics → Reflection / Object 참조 **허용**
- Graphics → Editor / GameFramework / Games **금지** (`CheckEngineLayers.py`)
- RHI 모듈 DLL (`SW_RHI_AS_MODULES`) 시 백엔드는 별도 모듈 엔트리 (`RHI/Modules/`)

---

## 자주 하는 실수

| 실수 | 결과 |
|------|------|
| Caps의 bindless = 실제 native | DX12는 `supportsNativeBindlessSampling()` → `_bHeapDirectlyIndexed` 확인 |
| DebugDrawQueue = GPU 즉시 드로우 | 큐 API; 화면 표시는 Editor GameView 등 소비 측 |
| Immediate Context = Immediate CommandList | Mode vs Context 혼동 — 위 표 참고 |
| gen/머티리얼 XML을 코드에 하드코딩 | `Resource/engine/` 파이프라인·머티리얼 에셋 사용 |
| DX11/GL prepareTexture가 “미구현 stub” | **의도적** 상태리스 no-op |

---

## 알려진 보강 후보 (우선순위)

문서화된 로드맵이지, 이 README가 기능을 새로 만들지 않습니다.

완료됨 (참고):
- **P0** — 전 백엔드 dual Immediate/Deferred Context + Mode 배선
- **P0** — SSAO/TAA/Tonemap/GpuCull 패스 실행 경로
- **P0** — DX11/GL `prepareTextureForShaderRead` 정의 (의도적 no-op)
- **P1** — DX12 `_bHeapDirectlyIndexed` ↔ `supportsNativeBindlessSampling()` / `getCapabilities()._bNativeBindless`
- **P1** — DebugDrawQueue 스피어 → `GameViewPanel` ImGui 원으로 소비
- **P2** — DX12 `HEAP_DIRECTLY_INDEXED` 실패 시 bind-at-draw (런타임 Caps, WARNING 제거)
- **P2** — Vulkan `createRenderPass(desc)` 소유 VkRenderPass 생성 + `destroy`/`shutdown`에서 해제 (빈 desc는 swapchain RP alias)
- **P2** — 전 백엔드(DX12/DX11/Vulkan/OpenGL) soft `Cmd` replay(`RHIDeferredCommandList`) 제거,
  즉시 호출하는 네이티브 `IRHICommandList`로 전환 완료 — DX11은 `FinishCommandList`, DX12는 자신만의
  `ID3D12GraphicsCommandList`, Vulkan/OpenGL은 기존 `*RHICommandContext`를 그대로 감싸 즉시 호출
- **P2** — `MaterialTypes.h` 분리, `ShaderReflection` 포맷별 TU + exhaustive switch, `IRHIDevice::executeOffscreenPipelineSmoke`, FrameRenderer `FrameRendererStatus`, GpuMaterialRetireQueue
- **Perf** — Transparent 연속 mesh/mat 머지, GpuScene 내용·카메라 핑거프린트 캐시, Deferred CL 기본·`_frameCmd` 재사용·Cmd reserve 256

---

## 더 볼 곳

- [ARCHITECTURE.md](../../../ARCHITECTURE.md) — RHI · RenderPass · Pipeline  
- `Resource/engine/pipeline/` · `Resource/engine/renderpass/`  
- [Engine/README.md](../README.md) — 레이어 규칙
