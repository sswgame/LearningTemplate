# 플랫폼 대상: Android · 웹(WebGPU)

> 질문: Android 와 웹(WebGPU)에서 엔진이 요구하는 판 · 기능은 무엇이고, 대상이 못 주는 것은 어떻게 가르는가?
> 독자: 플랫폼 백엔드(WebGPU · Android Vulkan) · Emscripten · NDK 빌드를 잇는 세션(에이전트와 사람).
> 결론: Android 는 Vulkan 1.2 + 디스크립터 인덱싱이 필수다(없으면 장치를 고르지 않는다). 웹은 WebGPU 하나이고 bindless 가 없어 DX11 과 같은 고정 슬롯 모델로 그린다. 대상이 못 주는 것은 능력 깃발(`RHICapabilities`) · 형식 대체 · 쿠킹 변형으로 가르고, 필수인데 없는 것은 오류로 멈춘다.

사용자 결정(2026-11-07): Android 지원, iOS 제외, 웹은 최신 브라우저의 WebGPU 만(WebGL2 없음), 콘솔은 대기 — 콘솔 갈래를 만들지도 막지도 않는다. 판 · 한도 수치 중 "(기억)" 은 기억에 의존한 값이라 붙이기 전에 그 판의 명세 · 브라우저 문서로 다시 확인한다.

## 요구 판

| 대상 | 요구 | 근거 |
|---|---|---|
| Android | Vulkan 1.2(`constant::render::kVulkanRequiredMinor`), API 29(Android 10) 이상 | R4 가 1.3 → 1.2 로 낮췄다(demote 대신 clip 순서). Android 의 1.2 장치는 대체로 Android 11+ 출하분(기억). NDK 의 `libvulkan.so` 를 링크한다(로더 포트 없음) |
| 웹 | WebGPU(`navigator.gpu`), Chrome · Edge 121+(Android 포함), Firefox 141+(Windows), Safari 26+(기억) | WebGL2 판은 없다. 같은 API 를 데스크톱에서 Dawn 으로 돌려 본다(Dawn 포트가 붙으면) |

## 엔진 요구와 대상 대조

| 항목 | 엔진이 쓰는 것 | Android Vulkan 1.2 | WebGPU | 가르는 법 |
|---|---|---|---|---|
| 바인딩 모델 | DX12 · Vulkan 은 네이티브 bindless(텍스처 4096 · RW 텍스처 1024 칸 update-after-bind 배열), DX11 · GL 은 고정 슬롯 에뮬(엔진 텍스처 12 + 머티리얼 16) | 1.2 코어지만 기능 비트는 선택이다(`runtimeDescriptorArray` · `descriptorBindingPartiallyBound` · `…SampledImageUpdateAfterBind` · `…StorageImageUpdateAfterBind` · 비균일 인덱싱). 한도(`maxPerStageDescriptorUpdateAfterBind…`)도 장치마다다 | bindless 없음(`binding_array` 는 제안 단계, 기억) | Android: 필수. 장치 고르기가 기능 · 한도를 대조해 못 미치는 장치는 건너뛴다(`VulkanRHIDevice::meetsBindlessRequirements`). 웹: DX11 과 같은 고정 슬롯 모델(`SW_BINDLESS` 없음, 머티리얼은 서수) — 셰이더 갈래 `WEBGPU` |
| 바인딩 수 | 패스마다 다르다(쓰는 것만 셰이더에 남는다) | 위 한도 | 기본 한도: 그룹 4, 단계마다 텍스처 16 · 샘플러 16 · 저장 버퍼 8 · 상수 버퍼 12(기억) | WebGPU 는 장치를 만들 때 어댑터 한도를 그대로 요구하고(`requiredLimits`), PSO 의 바인딩 표(쿠킹 때 SPIR-V 리플렉션으로 셰이더 옆에 적는다)가 한도를 넘으면 PSO 를 만들지 않고 오류다 |
| 루트 상수 | 16 dword(`SW_ROOT_DWORD_COUNT`) — DX12 루트 상수, Vulkan 푸시 상수 | 푸시 상수 최소 128 B — 64 B 로 맞다 | 푸시 상수 없음(immediates 는 실험, 기억) | WebGPU 는 DX11 · GL 처럼 상수 버퍼 `b2`(`SW_SLOT_ROOT_CB_EMUL`) 에뮬. 셰이더 갈래 `SW_ROOT_CONSTANTS_BEGIN` 의 `WEBGPU` |
| 아토믹 | 32 비트 `InterlockedAdd`(버퍼 · groupshared)만 — 컬링 · 라이트 그리드 · NaN 검사 · GPU assert | 필수 | `atomic<u32>`(저장 버퍼 · workgroup) | 그대로. 64 비트 · 실수 · 텍스처 아토믹은 쓰지 않는다 — 새로 쓰면 이 표에 줄을 더한다 |
| 웨이브 연산 | `gpu_cull.hlsl` 의 `WaveReadLaneFirst` · `WaveActiveCountBits` · `WavePrefixCountBits` | 서브그룹 기본 연산은 1.1 코어, ballot 은 선택 | `subgroups` 기능은 선택(기억) | WebGPU 대상은 `SW_NO_WAVE_OPS` 로 쿠킹한다 — DX11 과 같은 스레드마다 원자 연산 길(`gpu_cull.hlsl`). 결과는 같고 경합만 늘어난다 |
| `R16G16_SNORM` 첨부 · 블렌드 | G버퍼 법선(`RenderPassTypeInfo` 의 고정 포맷) | 첨부는 선택 형식이다 — 장치 생성이 조회한다(`verifyRequiredColorAttachmentFormats`). 블렌드도 선택(못 섞는 첨부는 덮어쓴다 — 이미 있다) | 코어에 `rg16snorm` 없음 — `texture-formats-tier1` 기능(기억) | Vulkan: 못 쓰면 장치를 고르지 않는다(지금). WebGPU: 기능이 있으면 `rg16snorm`, 없으면 백엔드가 `rg16float` 으로 만든다 — 셰이더는 `.rg` 를 [-1, 1] 실수로 읽어 둘이 같다(`WebGPURHIUtil::toTextureFormat`) |
| `R32G32B32_FLOAT` 텍스처 | DDS(dxgi 6)로 들어올 수 있다. 정점 형식은 별개 | 샘플 형식은 선택(모바일에서 흔히 없다) | 텍스처 형식이 없다 | `Texture2D` 가 장치가 못 쓰면 RGBA32F 로 펴서 올린다(`Texture2D::expandToFourChannels`) — 알파 1 |
| 블록 압축 | 쿠킹 변형(`Config/Engine/EngineConfig.json` `_textureFormats`) | BC 는 대개 없다 → ASTC · ETC2 | `texture-compression-bc` · `-astc` · `-etc2` 모두 선택 | 쿠킹이 대상마다 목록대로 굽고 실행 때 장치가 지원하는 첫 묶음을 고른다(끝남). 웹 목록은 BC · ASTC · ETC2(셋 다 없는 장치는 오류) |
| `R32_FLOAT` 선형 샘플 | 깊이 히스토리 · 자동 노출을 선형 샘플러로 읽는 패스가 있다 | 선형 필터는 선택 | `float32-filterable` 기능(기억) | WebGPU 필수 기능 — 없으면 장치 생성이 `DriverUnsupported` 로 멈춘다 |
| 깊이 형식 · 되읽기 | `D24_UNORM_S8_UINT`, 깊이 되읽기는 float32 | 장치마다 D24S8 이 없을 수 있다 — 장치 생성이 고른다(`selectDepthFormat`) | `depth24plus-stencil8` 은 복사 원본이 될 수 없다 | WebGPU: `depth32float-stencil8` 기능이 있으면 그것, 없으면 `depth24plus-stencil8` 이고 깊이 되읽기는 `Rejected`(로그) |
| 멀티 드로우 · count 버퍼 | `_bMultiDrawIndirect`, `drawIndirectCount` | 선택(장치 값으로 덮는다 — 이미 있다) | 없음(`multi-draw-indirect` 는 실험, 기억) | 능력 깃발 거짓 — 엔진이 배치마다 간접 드로우를 낸다(DX11 길). 배치 번호는 루트 상수(DX11 과 같다) |
| 간접 첫 인스턴스 | 간접 인자의 `firstInstance` | 1.2 코어 기능 `drawIndirectFirstInstance` | `indirect-first-instance` 기능 | WebGPU: 기능이 있으면 켜고 없으면 장치 생성이 멈춘다(GPUScene 이 인자에 첫 인스턴스를 적는다) |
| 와이어프레임 | `RHIFillMode::Wireframe` | `fillModeNonSolid` 선택(장치 값 — 이미 있다) | 폴리곤 모드 없음 | `_bWireframeFill` 거짓 — 보기 모드를 받지 않는다(이미 있는 길) |
| 지오메트리 · 테셀레이션 | 파이프라인 XML 이 고르는 선택 스테이지 | 선택(장치 값) | 없음 | 그런 PSO 는 오류로 만들지 않는다(Vulkan 과 같은 규칙) |
| 스레드 | 렌더 스레드 · 병렬 기록 · 워커 생성 | 모두 된다 | 장치는 만든 스레드에서만(Emscripten 에서 JS 객체는 워커를 넘지 못한다) | `_bParallelCommandRecording` · `_bThreadSafeResourceCreation` 거짓, 웹은 렌더 스레드를 띄우지 않는다(`gv_useRenderThread` 기본값을 플랫폼 사슬로) |
| GPU 대기 | `waitIdle`(종료 · 백엔드 교체 · 크기 변경 · 셰이더 핫 리로드 · 파이프라인 바꿈) | 된다 | 브라우저는 GPU 를 동기로 기다릴 수 없다 | 씬 활성화 · 에셋 핫 리로드 · 캡처는 이미 `flushRenderThread` + 펜스 지연 해제다(`RHI.GPUIdleWaits` 시나리오). WebGPU 의 `waitIdleInternal` 은 기다리지 않고 해제 큐를 비운다 — WebGPU 는 GPU 가 쓰는 객체를 놓아도 구현이 끝날 때까지 살린다(명세). 되읽기는 늘 `mapAsync` 콜백이다 |
| 타임스탬프 | `IRHITimestampProvider` | 된다(`timestampPeriod`) | `timestamp-query` 선택 | WebGPU: 아직 제공자가 없다(nullptr — 이미 있는 선택 능력 모양). `timestamp-query` 로 다는 것은 남은 일 |
| 표면 | 창 핸들(`IRenderSurface`) | `ANativeWindow*`(`NativeSurfaceKind::AndroidWindow`), 앱 수명 안에서 사라졌다 생긴다 | canvas 선택자(`NativeSurfaceKind::WebCanvas`) | Android: `VK_KHR_android_surface`. 웹: `WGPUSurfaceSourceCanvasHTMLSelector` |

## 단계별 상태(코드와 대조, 2026-11-07)

1. **`waitIdle` 을 펜스 지연 해제로** — 끝남. 씬 활성화(`SceneManager` → `flushRenderThread`) · 에셋 핫 리로드(`TextureCache` · `MaterialCache` · `MeshCache`) · 캡처는 GPU 를 기다리지 않는다. 남은 `waitIdle` 은 이유가 붙은 도구 · 수명 길(`RHIWaitIdleReason`)이고, 시나리오 `empty/noidlewait` · `editor/assetcascade` 가 `RHI.GPUIdleWaits` 를 본다.
2. **텍스처 압축을 쿠킹으로** — 끝남. 임포터가 대상 묶음마다 ASTC · ETC2 변형을 굽고(`TextureBlockEncoder`), 실행 때 `TextureFormatSelection` 이 장치가 받는 첫 묶음을 고른다. 이번에 웹 목록에 ETC2 를 더했다.
3. **WebGPU RHI 백엔드** — `Source/Engine/Graphics/RHI/WebGPU/`. 셰이더 대상 `WGSL_WebGPU`(DXC SPIR-V → Tint → WGSL, 바인딩 표를 앞에 붙인 바이너리), 고정 슬롯 모델.
4. **Emscripten 빌드** — `cmake/Modules/Platform/Web.cmake`(링크 옵션 · 포트), 메인 루프는 브라우저 프레임 콜백(`emscripten_set_main_loop_arg`), canvas 창.
5. **Android Vulkan** — `cmake/Modules/Platform/Android.cmake`(NDK 라이브러리), `android_main` 진입 · `ANativeWindow` 창, `VK_KHR_android_surface`.

## 화면 대응(모바일 · 데스크톱)

- 안전 여백: 플랫폼 창이 `IWindow::getSafeAreaInsets`(물리 픽셀)를 준다 — Android 는 JNI `getRootWindowInsets`, 웹은 셸 HTML 의 CSS 변수 `--sw-safe-left/top/right/bottom`(= `env(safe-area-inset-*)`). `UIScaleSettings::makeViewport` 가 변마다 디버그 흉내(`gv_uiDebugSafeZone`)와 큰 쪽을 고른다.
- 크기 · 회전 · 수명: `CallbackInputWindow::applyPlatformResize` 하나로 모은다. Android `APP_CMD_TERM_WINDOW` → `SurfaceLost`(렌더러가 `IRHIDevice::releaseSurface`), `INIT_WINDOW` → `SurfaceRestored`(`restoreSurface`), `PAUSE` · `RESUME` · 웹 `visibilitychange` → 앱 멈춤 · 돌아옴. Vulkan 스왑체인은 `IDENTITY` 변환이라 합성기가 돌린다(pre-rotation 은 남은 일).
- 좁은 화면 · 방향: 리소스 태그 `screen.compact`(dp 너비 < `uiscale.xml` `_compactMaxWidth`) · `orientation.portrait` — UI 문서가 `_when` 변형으로 다시 배치한다.
- 손가락: 터치 장치의 첫 손가락이 UI 포인터가 되고, 조작을 비켜 짚으면 `_minTouchTargetSize`(48)까지 넓혀 고른다(`UIEventRouter::hitTestTouch`).
- 품질 등급: `engine.settings.xml` 의 `<AutoDetect platform=… requireNativeBindless=…>` — 플랫폼 기본 RHI 의 정적 능력과 시스템 메모리로 고른다.
- 에디터: UI 미리보기 견본(`uiresolutions.xml`)이 기기 여백 · density 를 싣고 "돌리기" 로 가로를 본다. 같은 조건의 시나리오는 `engine/automation/screenadapt`(단계 `Touch`).

## 남은 일(코드를 붙여 보지 못한 것)

- 빌드: Emscripten · NDK 로 한 번도 짓지 못했다 — Core · Engine 의 `#error "Unsupported platform"` 사슬 중 Android · Web 갈래가 없는 곳이 컴파일에서 드러난다(설계대로).
- WebGPU: 깊이를 색으로 샘플하는 패스(깊이 텍스처는 `texture_depth_2d` 로만 읽힌다 — 자동 레이아웃이 `float` 로 보면 검증 오류) — 언리얼 모바일의 SceneDepthAux(R32F 사본)처럼 깊이 사본 패스와 능력 깃발이 필요하다. 그리기마다 짓는 바인드 그룹의 캐시, 타임스탬프 제공자, 브라우저 Gamepad API.
- Android: APK 패키징(매니페스트 · Gradle), 인텐트로 받는 명령줄, 패드 연결, pre-rotation, 미리보기 문맥의 `screen` 태그(지금 태그는 전역이라 에디터 미리보기에 compact 변형이 보이지 않는다).

## 받아야 할 의존성(추가하지 않았다 — 붙일 때 사용자에게 라이선스를 받는다)

| 무엇 | 쓰는 곳 | 판(기억) | 라이선스 |
|---|---|---|---|
| Emscripten SDK(emsdk) | 웹 빌드 도구 사슬(`emcmake`), `emdawnwebgpu` 포트(`--use-port=emdawnwebgpu` — `webgpu/webgpu.h`) | 4.0.10 이상(emdawnwebgpu 포트가 든 판) | MIT / University of Illinois NCSA(emscripten), BSD-3(Dawn) |
| Dawn | 데스크톱에서 WebGPU 백엔드를 돌려 보는 네이티브 구현(`webgpu_dawn`), Tint(SPIR-V → WGSL) | 크로미움 분기 판을 고정(vcpkg `dawn` 포트는 없다 — 오버레이 포트로) | BSD-3-Clause |
| Tint 실행 파일 | 셰이더 쿠커의 SPIR-V → WGSL 단계(`Config/Engine/ShaderTools.json` 의 경로) | Dawn 과 같은 판 | BSD-3-Clause |
| Android NDK | `libvulkan.so` · `libandroid.so` · `liblog.so`, `android_native_app_glue`(NDK 소스) | r27 이상(API 29 대상) | Apache-2.0(NDK), 앱 글루 Apache-2.0 |
| Android SDK · Gradle | APK 패키징(NativeActivity 매니페스트) | AGP 8.x | Apache-2.0 |

## 엔진 비교(기억)

- 언리얼: 모바일은 Vulkan(ES3.1 대체), 웹은 공식 지원을 내렸다. 바인딩은 `FRHIUniformBuffer` + 고정 슬롯이고 bindless 는 SM6 데스크톱 전용 — 이 저장소의 "bindless 아니면 고정 슬롯" 두 모델과 같다.
- 유니티: WebGPU 백엔드(6000.x 실험)가 DX11 같은 슬롯 모델이고, 텍스처 압축은 빌드 대상마다 형식을 고른다(Android ASTC, 웹은 DXT + ASTC 두 벌).
- Godot: 웹은 GL Compatibility 만, Android 는 Vulkan Mobile 렌더러. `RenderingDevice::texture_is_format_supported_for_usage` 로 형식을 묻는다.
