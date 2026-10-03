# Shader — HLSL 한 파일이 GPU 에 걸리기까지

여기서 하는 일은 셋입니다. **컴파일**해서 바이트코드를 만들고, 그 바이트코드에서 **리플렉션**으로
무엇이 어디에 걸리는지 읽고, 그 결과가 우리가 정한 **바인딩 계약**과 맞는지 확인합니다.

## 폴더 = 그 세 가지

```
Shader/
  Compile/      소스 → 바이트코드 (컴파일 · 디스크 캐시 · 오프라인 베이크 · 핫리로드)
  Reflection/   바이트코드 → 바인딩 메타 (백엔드별 리플렉션 + 구운 매니페스트)
  Binding/      메타 → 계약 (슬롯 정본 · 병합 레이아웃 · 구운 바이너리 대조 · 셰이더가 읽는 꼴 그대로의 GPU 값 묶음)
```

### Compile/ — 소스에서 바이트코드로

- `ShaderCompiler` — DXC / D3DCompiler 로 HLSL 을 DXIL · SPIR-V · DXBC 로 만듭니다. Vulkan SPIR-V 타깃 판은
  `RHI/Vulkan/VulkanRHIApiVersion.h`(1.3 → SPIR-V 1.6)이 정합니다 — 디바이스 최소 판과 같은 값입니다. GL 용 SPIR-V 는 `vulkan1.1` 타깃입니다.
- `ShaderCache` — (경로 + define + 타깃) → 컴파일 결과. 사전 베이크 바이너리 · 로컬 라이브 캐시(`Saved/ShaderCache/`) · 실시간 컴파일
  세 갈래가 같은 항목을 만들고, 구운 파일 이름에는 퍼뮤테이션 해시가 들어갑니다. `ResourceManager` 의 에셋 캐시가 아닙니다 —
  셰이더 바이트코드는 RHI/컴파일러 수명입니다. `shutdown` 이 리플렉션 매니페스트 캐시도 비웁니다.
- `ShaderBaker` — 오프라인 베이크의 **메커니즘**. 한 장을 굽고 이름을 짓는다. "무엇을 구울지" 와
  "전부 굽기"(`App.exe --bake-shaders` 가 부르는 것)는 파이프라인 XML 과 패스 종류를 아는 렌더러의 정책이라
  `Renderer/Bake/ShaderBakeDriver` 에 있다 — 그래서 `Shader/` 는 `Renderer/` 를 include 하지 않는다.
  바이너리와 함께 **리플렉션 매니페스트**(`Reflection/ShaderReflectionLibrary`)도 굽습니다.
  세 조각으로 나뉘고 각자 입력이 다릅니다:
  - `Renderer/Bake/ShaderBakeRequest.cpp` — **무엇을 구울지**. `ShaderBakeRequest` 목록을 네 단계로 모은다:
    (1) 파이프라인 XML 의 패스, (2) 패스 종류 표(`RenderPassTypeTraits`)의 **모든** 엔진 셰이더 — 런타임은 로드한 파이프라인과 무관하게
    표 전체로 엔진 PSO 를 만든다, (3) 머티리얼 에셋, (4) 씬 메시 패스 × (머티리얼 없음 + 머티리얼) × `RenderViewMode` — 뷰 모드 define 은
    런타임 PSO 와 같은 `FrameRendererUtil::findViewModeDefine` 에서 얻는다.
    런타임이 만드는 퍼뮤테이션과 어긋나면 Shipping 이 매니페스트 미스로 떨어지므로, define 합치기 · 패스 기본 셰이더 · 뷰 모드 define 을
    런타임과 같은 자리에서 읽는다. `ShaderBakeRequestTest.EveryViewModeVariantOfEveryMeshPassTypeIsRequested`(빈 리소스 루트로도 표 × 뷰 모드가 다 나온다)와
    `ShaderBakeRequestTest.BakedManifestHoldsEveryRequest`(커밋된 매니페스트가 요청을 모두 담는다)가 고정한다.
  - `ShaderBakeStamp` — **이미 최신인가**. 판정은 파일 시간이 아니라 **내용 해시**다(`bake.stamp`).
    주의: 구운 바이너리를 커밋하는 저장소라 mtime 은 `git pull` 이 임의 순서로 덮어쓴다 — 파일 시간으로 판정하면 낡은 바이너리가 최신으로 보인다.
  - `ShaderBaker` — **굽고 이름 짓기**. 요청 하나를 받아 컴파일하고, 구운 파일 이름(스템·스테이지·퍼뮤테이션 해시)을 정한다.
- `LiveShaderManager` — `ShaderCache` 가 든 셰이더를 **요청 시** 다시 컴파일합니다. `ReloadShaders`(Ctrl+F8) 가
  `triggerReloadAll` → `update` 를 돌립니다. 파일 감시로 자동 재컴파일하지 않습니다.

### Reflection/ — 바이트코드에서 바인딩 메타로

- `ShaderReflection` — 진입점. 포맷을 보고 아래 둘 중 하나로 보냅니다.
- `ShaderReflectionDx` / `ShaderReflectionSpirv` — 백엔드별 구현.
  `ShaderReflectionUtil.h` 가 이 둘의 TU 공유 선언입니다.
- `ShaderReflectionLibrary` — **런타임이 아니라 쿠킹 시점에** 뽑아 둔 매니페스트를 읽습니다.
  DXIL 리플렉션은 `dxcompiler.dll` 을 필요로 해서, 그걸 런타임에 하면 배포물에 셰이더 컴파일러를
  같이 넣어야 합니다. 없으면 바인딩이 조용히 어긋나 DEVICE_HUNG 으로 갑니다. 파일은 RHI 폴더마다 하나
  (`<domain>/shaders/bin/<rhi>/reflection.manifest`)이고, 개발 빌드는 매니페스트가 지금 소스에서 나온 것이 아니면(`bake.stamp`)
  런타임 리플렉션으로 폴백합니다. 배포본에는 폴백이 없습니다.

### Binding/ — 메타가 계약과 맞는지

- `ShaderBindingSlots.h` — 슬롯 번호의 **정본은 여기가 아닙니다**.
  `Resource/engine/shaders/bindingslots.hlsli` 를 그대로 `#include` 합니다. HLSL 과 C++ 가 같은
  파일을 읽으므로 "수동 동기" 가 없습니다. 바인딩 숫자 리터럴을 코드에 적지 마세요.
- `ShaderBindingLayout` — 여러 스테이지의 `ShaderReflectionData` 를 병합해 이름·레지스터로
  조회할 수 있게 만듭니다. C++ 미러 struct 없이 리플렉션만 신뢰하는 구조의 핵심입니다.
- `ShaderBindingLayoutCache` — (경로 + define + 백엔드) → 레이아웃. PSO 생성이 여기서 얻습니다.
  핫리로드 시 `invalidateByShaderPath` 로 무효화합니다.
- `ShaderBindingContract` — **구운 바이너리의 리플렉션이 계약과 맞는지** 검사합니다.
  `EngineTest --test_filter=ShaderBindingContractTest.*` 가 nogpu 로 이걸 돌립니다.
- `GpuLight.h` · `GpuSpriteInstanceData.h` — 셰이더가 읽는 꼴 그대로 묶은 값(라이트 64 바이트 · 스프라이트 인스턴스 12 바이트).
  컴포넌트(Object 층)가 직접 채우므로 Object 가 include 할 수 있는 자리여야 합니다 — `Graphics/Renderer` 는 Object 위 티어라 거기 둘 수 없고,
  셰이더 계약을 두는 이 폴더가 Object 아래의 가장 가까운 자리입니다.

## 함정

- **`.hlsli` 를 고쳤으면 `App.exe --bake-shaders` 를 다시 돌립니다.** 빌드는 HLSL 을 굽지 않습니다. 개발 빌드 런타임은
  낡은 매니페스트를 버리고 런타임 리플렉션으로 폴백하지만, 테스트와 배포본은 구운 바이너리를 봅니다. 스테일 함정은
  세 겹입니다 — 베이크된 바이너리, 리플렉션 매니페스트, `ShaderCompiler` 의 디스크 캐시.
- **결과가 안 바뀌면 실제로 로드된 바이트부터 확인합니다.** 셰이더를 고쳤는데 화면이 그대로면
  거의 항상 위 셋 중 하나가 옛것입니다.
- **백엔드 하나만 예외를 두지 않습니다.** 리플렉션이 기준이면 네 백엔드가 같은 규칙을 따릅니다.

---

## 더 볼 곳

- [Graphics/README.md](../README.md) — 바인딩 계약 표와 셰이더 작성 규칙
- `Resource/engine/shaders/bindingslots.hlsli` — 슬롯 번호 정본
- `Resource/engine/shaders/binding.hlsli` — 셰이더가 include 하는 바인딩 선언
