# Shader — HLSL 한 파일이 GPU 에 걸리기까지

여기서 하는 일은 셋입니다. **컴파일**해서 바이트코드를 만들고, 그 바이트코드에서 **리플렉션**으로
무엇이 어디에 걸리는지 읽고, 그 결과가 우리가 정한 **바인딩 계약**과 맞는지 확인합니다.

## 폴더 = 그 세 가지

```
Shader/
  Compile/      소스 → 바이트코드 (컴파일 · 디스크 캐시 · 오프라인 쿠킹 · 핫리로드)
  Reflection/   바이트코드 → 바인딩 메타 (백엔드별 리플렉션 + 쿠킹된 매니페스트)
  Binding/      메타 → 계약 (슬롯 정본 · 병합 레이아웃 · 쿠킹된 바이너리 대조 · 셰이더가 읽는 꼴 그대로의 GPU 값 묶음)
```

### Compile/ — 소스에서 바이트코드로

- `ShaderCompiler` — DXC / D3DCompiler 로 HLSL 을 DXIL · SPIR-V · DXBC 로 만듭니다. Vulkan SPIR-V 타깃 판은
  `RHI/Vulkan/VulkanRHIApiVersion.h`(1.3 → SPIR-V 1.6)이 정합니다 — 디바이스 최소 판과 같은 값입니다. GL 용 SPIR-V 는 `vulkan1.1` 타깃입니다.
- `ShaderCache` — (경로 + define + 타깃) → 컴파일 결과. 쿠킹된 바이너리 · 로컬 라이브 캐시(`Saved/ShaderCache/`) · 실시간 컴파일
  세 갈래가 같은 항목을 만들고, 쿠킹된 파일 이름에는 퍼뮤테이션 해시가 들어갑니다. `AssetManager` 의 에셋 캐시가 아닙니다 —
  셰이더 바이트코드는 RHI/컴파일러 수명입니다. `shutdown` 이 리플렉션 매니페스트 캐시도 비웁니다.
- `ShaderCooker` — 오프라인 쿠킹의 **메커니즘**. 한 장을 쿠킹하고 이름을 짓는다. "무엇을 쿠킹할지" 와
  "전부 쿠킹"(`App.exe --cook-shaders` 가 부르는 것)는 파이프라인 XML 과 패스 종류를 아는 렌더러의 정책이라
  `Renderer/Cook/ShaderCookDriver` 에 있다 — 그래서 `Shader/` 는 `Renderer/` 를 include 하지 않는다.
  바이너리와 함께 **리플렉션 매니페스트**(`Reflection/ShaderReflectionLibrary`)도 쿠킹합니다.
  세 조각으로 나뉘고 각자 입력이 다릅니다:
  - `Renderer/Cook/ShaderCookRequest.cpp` — **무엇을 쿠킹할지**. `ShaderCookRequest` 목록을 네 단계로 모은다:
    (1) 파이프라인 XML 의 패스, (2) 패스 종류 표(`RenderPassTypeInfo`)의 **모든** 엔진 셰이더 — 런타임은 로드한 파이프라인과 무관하게
    표 전체로 엔진 PSO 를 만든다, (3) 머티리얼 에셋, (4) 씬 메시 패스 × (머티리얼 없음 + 머티리얼) × `RenderViewMode` — 뷰 모드 define 은
    런타임 PSO 와 같은 `FrameRendererUtil::findViewModeDefine` 에서 얻는다.
    런타임이 만드는 퍼뮤테이션과 어긋나면 Shipping 이 매니페스트 미스로 떨어지므로, define 합치기 · 패스 기본 셰이더 · 뷰 모드 define 을
    런타임과 같은 자리에서 읽는다. `ShaderCookRequestTest.EveryViewModeVariantOfEveryMeshPassTypeIsRequested`(빈 리소스 루트로도 표 × 뷰 모드가 다 나온다)와
    `ShaderCookRequestTest.CookedManifestHoldsEveryRequest`(커밋된 매니페스트가 요청을 모두 담는다)가 고정한다.
  - `ShaderCookStamp` — **이미 최신인가**. 판정은 파일 시간이 아니라 **내용 해시**다(`cook.stamp`).
    주의: 쿠킹된 바이너리를 커밋하는 저장소라 mtime 은 `git pull` 이 임의 순서로 덮어쓴다 — 파일 시간으로 판정하면 낡은 바이너리가 최신으로 보인다.
  - `ShaderCooker` — **쿠킹하고 이름 짓기**. 요청 하나를 받아 컴파일하고, 쿠킹된 파일 이름(스템·스테이지·퍼뮤테이션 해시)을 정한다.
- `ShaderRecompiler` — `ShaderCache` 가 든 셰이더를 **요청 시** 다시 컴파일합니다. `ReloadShaders`(Ctrl+F8) 가
  `triggerReloadAll` → `update` 를 돌립니다. 파일 감시로 자동 재컴파일하지 않습니다.

### Reflection/ — 바이트코드에서 바인딩 메타로

- `ShaderReflection` — 진입점. 포맷을 보고 아래 둘 중 하나로 보냅니다.
- `ShaderReflectionDx` / `ShaderReflectionSpirv` — 백엔드별 구현.
  `ShaderReflectionUtil.h` 가 이 둘의 TU 공유 선언입니다.
- `ShaderReflectionLibrary` — **런타임이 아니라 쿠킹 시점에** 뽑아 둔 매니페스트를 읽습니다.
  DXIL 리플렉션은 `dxcompiler.dll` 을 필요로 해서, 그걸 런타임에 하면 배포물에 셰이더 컴파일러를
  같이 넣어야 합니다. 없으면 바인딩이 조용히 어긋나 DEVICE_HUNG 으로 갑니다. 파일은 RHI 폴더마다 하나
  (`<domain>/shaders/bin/<rhi>/reflection.manifest`)이고, 개발 빌드는 매니페스트가 지금 소스에서 나온 것이 아니면(`cook.stamp`)
  런타임 리플렉션으로 폴백합니다. 배포본에는 폴백이 없습니다.

### Binding/ — 메타가 계약과 맞는지

- `ShaderBindingSlots.h` — 슬롯 번호의 **정본은 여기가 아닙니다**.
  `Resource/engine/shaders/bindingslots.hlsli` 를 그대로 `#include` 합니다. HLSL 과 C++ 가 같은
  파일을 읽으므로 "수동 동기" 가 없습니다. 바인딩 숫자 리터럴을 코드에 적지 마세요.
- `ShaderBindingLayout` — 여러 스테이지의 `ShaderReflectionData` 를 병합해 이름·레지스터로
  조회할 수 있게 만듭니다. C++ 미러 struct 없이 리플렉션만 신뢰하는 구조의 핵심입니다.
- `ShaderBindingLayoutCache` — (경로 + define + 백엔드) → 레이아웃. PSO 생성이 여기서 얻습니다.
  핫리로드 시 `invalidateByShaderPath` 로 무효화합니다.
- `ShaderBindingValidator` — **쿠킹된 바이너리의 리플렉션이 계약과 맞는지** 검사합니다.
  `EngineTest --test_filter=ShaderBindingValidatorTest.*` 가 nogpu 로 이걸 돌립니다.
- `GpuLight.h` · `GpuSpriteInstanceData.h` — 셰이더가 읽는 꼴 그대로 묶은 값(라이트 64 바이트 · 스프라이트 인스턴스 16 바이트).
  컴포넌트(Object 층)가 직접 채우므로 Object 가 include 할 수 있는 자리여야 합니다 — `Graphics/Renderer` 는 Object 위 티어라 거기 둘 수 없고,
  셰이더 계약을 두는 이 폴더가 Object 아래의 가장 가까운 자리입니다.

## 함정

- **`.hlsli` 를 고쳤으면 `App.exe --cook-shaders` 를 다시 돌립니다.** 빌드는 HLSL 을 쿠킹하지 않습니다. 개발 빌드 런타임은
  낡은 매니페스트를 버리고 런타임 리플렉션으로 폴백하지만, 테스트와 배포본은 쿠킹된 바이너리를 봅니다. 스테일 함정은
  세 겹입니다 — 쿠킹된 바이너리, 리플렉션 매니페스트, `ShaderCompiler` 의 디스크 캐시.
- **결과가 안 바뀌면 실제로 로드된 바이트부터 확인합니다.** 셰이더를 고쳤는데 화면이 그대로면
  거의 항상 위 셋 중 하나가 옛것입니다.
- **백엔드 하나만 예외를 두지 않습니다.** 리플렉션이 기준이면 네 백엔드가 같은 규칙을 따릅니다.
- **머티리얼이 쓰는 새 셰이더는 `ShaderCookRequest.cpp` 의 엔진 셰이더 목록에도 넣는다** — 머티리얼 쿠킹은 퍼뮤테이션 해시가 붙은 변형만 굽는데 머티리얼은
  define 없는 변형의 리플렉션을 묻는다. Debug 는 런타임 리플렉션으로 넘어가 모르고, Shipping hostgpu 만 "매니페스트에 없다" 로 실패한다(sprite2dlit).
- **런타임에 바꾸는 정적 스위치는 `bShaderFeature="0"` 이어야 Shipping 에 바이너리가 있다** — 쿠커는 에셋 상태 + `bShaderFeature="0"` 스위치의 켬/끔 조합만 쿠킹한다(유니티 shader_feature / multi_compile, 런타임 스위치 넷까지). 코드가 `setStaticSwitch` 로 바꾸는 변형은 Dev 의 실시간 컴파일이 가려 Shipping hostgpu 에서만 진다 — `ShaderCookRequestTest.EveryRuntimeStaticSwitchCombinationIsRequested` 가 조합을 패스마다 대조한다. 멀티 컴파일(`_multiCompiles`)은 아직 고른 값만 쿠킹한다.
- **정적 스위치의 `keywordOff`(꺼지면 내는 define)를 쓰는 에셋은 지금 없다** — `MaterialTest.StaticSwitchOffKeywordWhenDisabled` 만 지킨다. 스위치를 지우거나 켜고 끄면
  퍼뮤테이션 해시가 바뀌므로 `--cook-shaders` 를 다시 한다.
- **셰이더 쿠킹은 패스 종류 표 전체 × (머티리얼 없음 + 머티리얼) × `RenderViewMode` 를 쿠킹한다** — 파이프라인 XML 에 나오는 패스만 곱하면 런타임
  (`ensurePassResources`)이 만드는 변형이 빠진다. 뷰 모드 define 의 정본은 `FrameRendererUtil::findViewModeDefine`, `ShaderCookRequestTest.CookedManifestHoldsEveryRequest` 가
  커밋된 매니페스트를 대조한다. Vulkan 최소 판과 쿠킹 타깃(`-fspv-target-env`)은 `VulkanRHIApiVersion.h` 하나 — 1.3 미만 디바이스는 고르지 않는다(SPIR-V 1.6).
- **텍스처 슬롯 샘플러의 정본은 `bindingslots.hlsli` 의 `SW_ENGINE_TEXTURE_SAMPLER`(t0..t3, 선형 · 클램프)와 `SW_MATERIAL_TEXTURE_SAMPLER`(t5..t8, 선형 · 랩)** —
  GL 은 유닛마다 샘플러 객체, DX11 은 정적 세트. 이로써 네 백엔드 벤치 프레임이 바이트까지 같다(골든 이미지 백엔드마다 같은 그림).
- **머티리얼 텍스처 슬롯(t5..t8)의 샘플러는 `shaderslot::kMaterialTextureSampler`(LINEAR_WRAP) 하나** — GL 은 샘플러 객체를 유닛에 `glBindSampler`, DX11 은 정적 세트.
- **셰이더 이름 규칙은 C++ 와 같다**(AGENTS.md "### HLSL", `CheckShaderConventions` 게이트): 함수 camelCase(공유 헤더는 `sw…`), 타입 PascalCase
  (공유 헤더는 `Sw…`, `_t` 없음), 필드는 C++ 멤버 이름에서 `_` 를 뺀 것. `g_*` · cbuffer · 시맨틱 · 진입점(`VSMain` · `PSMain` · `CSMain`)은 C++ 가
  문자열로 묶으므로 바꾸면 같은 커밋에서 C++ 도 바꾼다. **셰이더 쪽 개명은 계약 검사를 조용히 끌 수 있다**(`validate` 는 표에 없는 이름을 건너뛴다) —
  `ShaderBindingValidatorTest.EveryBoundNameIsInCookedReflection` 이 C++ 가 아는 이름이 쿠킹된 매니페스트에 있는지 본다.
- **셰이더 바인딩 계약의 정본은 `bindingslots.hlsli` 하나**(HLSL · C++ 같은 파일 include). 백엔드는 shaderslot 상수만 쓰고 바인딩 숫자 리터럴은 금지, `ShaderBindingValidator` 가 쿠킹된
  바이너리를 대조한다(nogpu). `draw()` 에 CB 인자는 없다. 백엔드 간 공유 상수는 `RHITypes.h` `constant` 블록 하나 — 지금 값이 같아도 바뀔 수 있으면 공유하고 별칭도 금지.
- **셰이더 산출물 스테일 판정은 내용 해시 하나다**(`ShaderCooker::computeEffectiveSourceHash`, mtime 금지 — 쿠킹된 바이너리를 커밋하므로 git 이 mtime 을 섞는다). 스탬프 헤더 `SWCOOK 3`,
  `cook.stamp` 는 CR 을 뗀 바이트로. 매니페스트가 바이너리보다 낡으면 바인더가 빈 레이아웃으로 그려 DX12 DEVICE_HUNG 이다 — `CookAssets.py --verify-shaders`. 백엔드 하나만 다른
  그림을 내면 ① 셰이더 산출물 ② 엔진 바깥을 다 되읽었는데 맞으면 셰이더 **코드 모양**(GL 드라이버가 DXC early-return 을 잘못 컴파일했다)을 의심한다. 프로브는 `nointerpolation`
  슬롯에 정수를 싣고, 모프 진단은 `-gv_morphDiag=1|2|3`.
- **쿠킹 바이너리 이름은 `ShaderCooker::computeBinaryFileName` 하나**(퍼뮤테이션 해시 포함 — 빠지면 define 이 GPU 에 안 닿는데 리플렉션은 맞아 보인다). 패스 define 의 정본은
  `FrameRendererUtil::getPassDefine`(런타임 PSO 와 쿠커가 같이 부른다 — 갈리면 Shipping 에서만 그 드로우가 사라진다). "컬러 출력 없는 패스에는 픽셀 스테이지가 없다" 는
  `hasPixelStage` 하나. 실시간 컴파일 요청은 `ShaderCache::makeLiveCompileDesc`(경로 `Saved/ShaderCache/<rhi>/<해시>-<opt|dbg>/`), 쿠커는 늘 최적화다. 셰이더 리로드 대상은
  `ShaderCache::collectCompiledDescs`, 바이트코드가 같으면 로컬 캐시에 쓰지 않는다.
- **정점 입력의 정본은 `constant::arrVertexAttribute`**(POSITION · NORMAL · TEXCOORD · COLOR, 48 B), 셰이더는 `common.hlsli` 의 `SwVertexInput` 만 쓴다(Vulkan · GL 은 선언 순서로
  location 을 매긴다). `SV_VertexID` 는 Vulkan · GL 에서 startVertex 를 포함하고 D3D 는 0 기반이다. 인스턴스 자리는 정점 슬롯 1(`SW_INSTANCESLOT`)로 넘기므로 `SV_InstanceID` 를 쓰지
  않는다. `GpuInstance` 원소 정의는 `instancedata.hlsli` 하나.
- **구조버퍼 원소는 `float4` 단위로 짠다**(float3 을 섞으면 std430 때문에 GL 만 어긋난다). GL(ARB_gl_spirv)은 구조버퍼를 정점 · 픽셀 두 단계에서 읽으면 링크를 거절하고 그 배치는
  조용히 물러난다 — 머티리얼은 픽셀 단계에서만 읽는다. GL 은 SPIR-V 라 bindless 텍스처가 불가, DX11 은 SM5.0 이라 버퍼로 텍스처를 못 넘긴다 — 머티리얼 텍스처는 t5..t8 고정 슬롯.


---

## 더 볼 곳

- [Graphics/README.md](../README.md) — 바인딩 계약 표와 셰이더 작성 규칙
- `Resource/engine/shaders/bindingslots.hlsli` — 슬롯 번호 정본
- `Resource/engine/shaders/binding.hlsli` — 셰이더가 include 하는 바인딩 선언
