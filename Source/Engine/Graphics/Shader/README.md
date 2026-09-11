# Shader — HLSL 한 파일이 GPU 에 걸리기까지

여기서 하는 일은 셋입니다. **컴파일**해서 바이트코드를 만들고, 그 바이트코드에서 **리플렉션**으로
무엇이 어디에 걸리는지 읽고, 그 결과가 우리가 정한 **바인딩 계약**과 맞는지 확인합니다.

## 폴더 = 그 세 가지

```
Shader/
  Compile/      소스 → 바이트코드 (컴파일 · 디스크 캐시 · 오프라인 베이크 · 핫리로드)
  Reflection/   바이트코드 → 바인딩 메타 (백엔드별 리플렉션 + 구운 매니페스트)
  Binding/      메타 → 계약 (슬롯 정본 · 병합 레이아웃 · 구운 바이너리 대조)
```

### Compile/ — 소스에서 바이트코드로

- `ShaderCompiler` — DXC / D3DCompiler 로 HLSL 을 DXIL · SPIR-V · DXBC 로 만듭니다.
- `ShaderCache` — (경로 + define + 타깃) → 컴파일 결과. 파일이 바뀌면 다시 컴파일합니다.
  이름과 달리 `ResourceManager` 가 아닙니다 — 셰이더 바이트코드는 RHI/컴파일러 수명입니다.
- `ShaderBaker` — 오프라인 베이크. `App.exe --bake-shaders` 가 여기를 부릅니다.
  바이너리와 함께 **리플렉션 매니페스트**(`Reflection/ShaderReflectionLibrary`)도 굽습니다.
- `LiveShaderManager` — 등록된 셰이더를 **요청 시** 다시 컴파일합니다. `ReloadShaders`(Ctrl+F8) 가
  `triggerReloadAll` → `update` 를 돌립니다. 파일 감시로 자동 재컴파일하던 경로는 없앴습니다.

### Reflection/ — 바이트코드에서 바인딩 메타로

- `ShaderReflection` — 진입점. 포맷을 보고 아래 둘 중 하나로 보냅니다.
- `ShaderReflectionDx` / `ShaderReflectionSpirv` — 백엔드별 구현.
  `ShaderReflectionUtil.h` 가 이 둘의 TU 공유 선언입니다.
- `ShaderReflectionLibrary` — **런타임이 아니라 쿠킹 시점에** 뽑아 둔 매니페스트를 읽습니다.
  DXIL 리플렉션은 `dxcompiler.dll` 을 필요로 해서, 그걸 런타임에 하면 배포물에 셰이더 컴파일러를
  같이 넣어야 합니다. 없으면 바인딩이 조용히 어긋나 DEVICE_HUNG 으로 갑니다(실제로 그랬습니다).

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

## 함정

- **`.hlsli` 를 고쳤으면 `--bake-shaders` 를 다시 돌립니다.** 런타임은 매니페스트가 소스보다
  오래되면 런타임 리플렉션으로 폴백하지만, 테스트는 구운 바이너리를 봅니다. 스테일 함정은
  세 겹입니다 — 베이크된 바이너리, 리플렉션 매니페스트, `ShaderCompiler` 의 디스크 캐시.
- **결과가 안 바뀌면 실제로 로드된 바이트부터 확인합니다.** 셰이더를 고쳤는데 화면이 그대로면
  거의 항상 위 셋 중 하나가 옛것입니다.
- **백엔드 하나만 예외를 두지 않습니다.** 리플렉션이 기준이면 네 백엔드가 같은 규칙을 따릅니다.

---

## 더 볼 곳

- [Graphics/README.md](../README.md) — 바인딩 계약 표와 셰이더 작성 규칙
- `Resource/engine/shaders/bindingslots.hlsli` — 슬롯 번호 정본
- `Resource/engine/shaders/binding.hlsli` — 셰이더가 include 하는 바인딩 선언
