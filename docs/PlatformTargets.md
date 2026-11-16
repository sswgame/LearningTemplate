# 플랫폼 대상: Android · 웹(WebGPU)

> 질문: Android 와 웹(WebGPU)에서 엔진이 요구하는 판 · 기능은 무엇이고, 대상이 못 주는 것은 어떻게 가르는가?
> 독자: 플랫폼 백엔드(WebGPU · Android Vulkan) · Emscripten · NDK 빌드를 잇는 세션(에이전트와 사람).
> 결론: Android 는 Vulkan 1.2 + 디스크립터 인덱싱이 필수다(없으면 장치를 고르지 않는다). 웹은 WebGPU 하나이고 bindless 가 없어 DX11 과 같은 고정 슬롯 모델로 그린다. 대상이 못 주는 것은 능력 깃발(`RHICapabilities`) · 형식 대체 · 쿠킹 변형으로 가르고, 필수인데 없는 것은 오류로 멈춘다.

사용자 결정: Android 지원, iOS 제외, 웹은 최신 브라우저의 WebGPU 만(WebGL2 없음), 콘솔은 대기(갈래를 만들지도 막지도 않는다). "(기억)" 표시 값은 붙이기 전에 그 판의 명세로 다시 확인한다(context7).

## 요구 판

| 대상 | 요구 | 근거 |
|---|---|---|
| Android | Vulkan 1.2(`constant::render::kVulkanRequiredMinor`), API 29(Android 10) 이상 | 1.3 을 요구하면 Android 장치 다수가 빠진다. Android 의 1.2 장치는 대체로 Android 11+ 출하분(기억). NDK 의 `libvulkan.so` 를 링크한다(로더 포트 없음) |
| 웹 | WebGPU(`navigator.gpu`), Chrome · Edge 121+(Android 포함), Firefox 141+(Windows), Safari 26+(기억) | WebGL2 판은 없다. 같은 API 를 데스크톱에서 Dawn 으로 돌려 본다(vcpkg `dawn` — `ThirdParty/dawn`, Windows · Linux 플랫폼 순서 맨 끝) |

## 엔진 요구와 대상 대조

| 항목 | 엔진이 쓰는 것 | Android Vulkan 1.2 | WebGPU | 가르는 법 |
|---|---|---|---|---|
| 바인딩 모델 | DX12 · Vulkan 은 네이티브 bindless(텍스처 4096 · RW 텍스처 1024 칸 update-after-bind 배열), DX11 · GL 은 고정 슬롯 에뮬(엔진 텍스처 12 + 머티리얼 16) | 1.2 코어지만 기능 비트는 선택이다(`runtimeDescriptorArray` · `descriptorBindingPartiallyBound` · `…SampledImageUpdateAfterBind` · `…StorageImageUpdateAfterBind` · 비균일 인덱싱). 한도(`maxPerStageDescriptorUpdateAfterBind…`)도 장치마다다 | bindless 없음(`binding_array` 는 제안 단계, 기억) | Android: 필수. 장치 고르기가 기능 · 한도를 대조해 못 미치는 장치는 건너뛴다(`VulkanRHIDevice::isBindlessCapable`). 웹: DX11 과 같은 고정 슬롯 모델(`SW_BINDLESS` 없음, 머티리얼은 서수) — 셰이더 갈래 `WEBGPU` |
| 바인딩 수 | 패스마다 다르다(쓰는 것만 셰이더에 남는다) | 위 한도 | 기본 한도: 그룹 4, 단계마다 텍스처 16 · 샘플러 16 · 저장 버퍼 8(명세 https://www.w3.org/TR/webgpu/#limits), 상수 버퍼 12(기억) | WebGPU 는 장치를 만들 때 어댑터 한도를 그대로 요구한다(`requiredLimits`). 단계 텍스처는 계약 16(`CookContract.json` stage_texture_unit_limit = `shaderslot::wgpu::kStageTextureBudget`) — 쿠킹이 넘는 WGSL 을 멈추고, 장치 생성이 어댑터 `maxSampledTexturesPerShaderStage` 가 그보다 작으면 멈춘다. 그 밖의 한도는 PSO 의 바인딩 표(쿠킹 때 SPIR-V 리플렉션)가 넘으면 PSO 를 만들지 않고 오류다 |
| 루트 상수 | 16 dword(`SW_ROOT_DWORD_COUNT`) — DX12 루트 상수, Vulkan 푸시 상수 | 푸시 상수 최소 128 B — 64 B 로 맞다 | 푸시 상수 없음(immediates 는 실험, 기억) | WebGPU 는 DX11 · GL 처럼 상수 버퍼 `b2`(`SW_SLOT_ROOT_CB_EMUL`) 에뮬. 셰이더 갈래 `SW_ROOT_CONSTANTS_BEGIN` 의 `WEBGPU` |
| 아토믹 | 32 비트 `InterlockedAdd`(버퍼 · groupshared)만 — 컬링 · 라이트 그리드 · NaN 검사 · GPU assert | 필수 | `atomic<u32>`(저장 버퍼 · workgroup) | 그대로. 64 비트 · 실수 · 텍스처 아토믹은 쓰지 않는다 — 새로 쓰면 이 표에 줄을 더한다 |
| 웨이브 연산 | `gpu_cull.hlsl` 의 `WaveReadLaneFirst` · `WaveActiveCountBits` · `WavePrefixCountBits` | 서브그룹 기본 연산은 1.1 코어, ballot 은 선택 | `subgroups` 기능은 선택(기억) | WebGPU 대상은 `SW_NO_WAVE_OPS` 로 쿠킹한다 — DX11 과 같은 스레드마다 원자 연산 길(`gpu_cull.hlsl`). 결과는 같고 경합만 늘어난다 |
| `R16G16_SNORM` 첨부 · 블렌드 | G버퍼 법선(`RenderPassTypeInfo` 의 고정 포맷) | 첨부는 선택 형식이다 — 장치 생성이 조회한다(`verifyRequiredColorAttachmentFormats`). 블렌드도 선택(못 섞는 첨부는 덮어쓴다) | 코어에 `rg16snorm` 없음 — `texture-formats-tier1` 기능(기억) | Vulkan: 못 쓰면 장치를 고르지 않는다. WebGPU: 기능이 있으면 `rg16snorm`, 없으면 백엔드가 `rg16float` 으로 만든다 — 셰이더는 `.rg` 를 [-1, 1] 실수로 읽어 둘이 같다(`WebGPURHIUtil::toTextureFormat`) |
| `R32G32B32_FLOAT` 텍스처 | DDS(dxgi 6)로 들어올 수 있다. 정점 형식은 별개 | 샘플 형식은 선택(모바일에서 흔히 없다) | 텍스처 형식이 없다 | `Texture2D` 가 장치가 못 쓰면 RGBA32F 로 펴서 올린다(`Texture2D::expandToFourChannels`) — 알파 1 |
| 블록 압축 | 쿠킹 변형(`Config/Engine/EngineConfig.json` `_textureFormats`) | BC 는 대개 없다 → ASTC · ETC2 | `texture-compression-bc` · `-astc` · `-etc2` 모두 선택 | 쿠킹은 모든 대상의 접미사 묶음(ASTC · ETC2)을 한 번 굽고(`--cook-texture-variants`, `TextureFormatFamily::resolveCookFamilies`) 팩은 대상마다 같다. 실행은 빌드 플랫폼을 적은 대상(`_listPlatform`)에서 장치가 지원하는 첫 묶음을 고른다(`findPlatformTarget` · `selectFamily`). 웹 목록은 BC · ASTC · ETC2(셋 다 없는 장치는 오류) |
| `R32_FLOAT` 선형 샘플 | 깊이 히스토리 · 자동 노출을 선형 샘플러로 읽는 패스가 있다 | 선형 필터는 선택 | `float32-filterable` 기능(기억) | WebGPU 필수 기능 — 없으면 장치 생성이 `DriverUnsupported` 로 멈춘다 |
| 깊이 형식 · 되읽기 | `D24_UNORM_S8_UINT`, 깊이 되읽기는 float32 | 장치마다 D24S8 이 없을 수 있다 — 장치 생성이 고른다(`selectDepthFormat`) | `depth24plus-stencil8` 은 복사 원본이 될 수 없다 | WebGPU: `depth32float-stencil8` 기능이 있으면 그것, 없으면 `depth24plus-stencil8` 이고 깊이 되읽기는 `Rejected`(로그) |
| 깊이 샘플 | 셰이더가 깊이를 실수 텍스처(`Texture2D<float>`)로 읽는다 | 된다 | 깊이는 `texture_depth_*` 로만 읽힌다(자동 레이아웃이 실수로 보면 검증 오류) | WebGPU: 셰이더가 읽는 깊이 텍스처는 R32 실수 사본을 두고 샘플 뷰가 사본이다 — `prepareTextureForShaderRead` 가 내장 WGSL(`textureLoad`) 패스로 채운다(언리얼 모바일의 SceneDepthAux 자리, 기억). 그 프레임에 채우지 않은 사본을 읽으면 오류 로그 |
| 멀티 드로우 · count 버퍼 | `_bMultiDrawIndirect`, `drawIndirectCount` | 선택(장치 값으로 덮는다) | 없음(`multi-draw-indirect` 는 실험, 기억) | 능력 깃발 거짓 — 엔진이 배치마다 간접 드로우를 낸다(DX11 길). 배치 번호는 루트 상수(DX11 과 같다) |
| 간접 첫 인스턴스 | 간접 인자의 `firstInstance` | 1.2 코어 기능 `drawIndirectFirstInstance` | `indirect-first-instance` 기능 | WebGPU: 기능이 있으면 켜고 없으면 장치 생성이 멈춘다(GPUScene 이 인자에 첫 인스턴스를 적는다) |
| 와이어프레임 | `FillMode::Wireframe` | `fillModeNonSolid` 선택(장치 값) | 폴리곤 모드 없음 | `_bWireframeFill` 거짓 — 보기 모드를 받지 않는다 |
| 지오메트리 · 테셀레이션 | 파이프라인 XML 이 고르는 선택 스테이지 | 선택(장치 값) | 없음 | 그런 PSO 는 오류로 만들지 않는다(Vulkan 과 같은 규칙) |
| 스레드 | 렌더 스레드 · 병렬 기록 · 워커 생성 | 모두 된다 | 장치는 만든 스레드에서만(Emscripten 에서 JS 객체는 워커를 넘지 못한다) | `_bParallelCommandRecording` · `_bThreadSafeResourceCreation` 거짓, 웹은 렌더 스레드를 띄우지 않는다(`gv_useRenderThread` 기본값을 플랫폼 사슬로) |
| GPU 대기 | `waitIdle`(종료 · 백엔드 교체 · 크기 변경 · 셰이더 핫 리로드 · 파이프라인 바꿈) | 된다 | 브라우저는 GPU 를 동기로 기다릴 수 없다 | 씬 활성화 · 에셋 핫 리로드 · 캡처는 `flushRenderThread` + 펜스 지연 해제다. 남은 `waitIdle` 은 이유가 붙은 도구 · 수명 길(`RHIWaitIdleReason`)이고, 시나리오 `empty/noidlewait` · `editor/assetcascade` 가 `RHI.GPUIdleWaits` 를 본다. WebGPU 의 `waitIdleInternal` 은 기다리지 않고 해제 큐를 비운다 — WebGPU 는 GPU 가 쓰는 객체를 놓아도 구현이 끝날 때까지 살린다(명세). 되읽기는 늘 `mapAsync` 콜백이다 |
| 타임스탬프 | `IRHITimestampProvider` | 된다(`timestampPeriod`) | `timestamp-query` 선택, 시각은 패스 경계에서만(값은 나노초, 브라우저는 거칠게 준다) | WebGPU: 기능이 있으면 제공자가 칸을 연다 — `writeTimestamp` 는 빈 계산 패스의 시작 시각이고 프레임 끝에 풀어 되읽기 버퍼 풀로 기다리지 않고 읽는다. 지금 GPU 시계를 묻는 길이 없어 `readGPUClockNanos` 는 거짓(외부 타임라인은 열지 않는다) |
| 표면 | 창 핸들(`IRenderSurface`) | `ANativeWindow*`(`NativeSurfaceKind::AndroidWindow`), 앱 수명 안에서 사라졌다 생긴다 | canvas 선택자(`NativeSurfaceKind::WebCanvas`) | Android: `VK_KHR_android_surface`. 웹: `WGPUEmscriptenSurfaceSourceCanvasHTMLSelector` |

## 플랫폼 코드의 자리

1. **WebGPU RHI 백엔드** — `Source/Engine/Graphics/RHI/WebGPU/`. 셰이더 대상 `WGSL_WebGPU`(DXC SPIR-V → Tint → WGSL, 바인딩 표를 앞에 붙인 바이너리), 고정 슬롯 모델.
2. **Emscripten 빌드** — `cmake/Toolchain/Platform/Web.cmake`(링크 옵션 · 포트), 메인 루프는 브라우저 프레임 콜백(`emscripten_set_main_loop_arg`), canvas 창.
3. **Android Vulkan** — `cmake/Toolchain/Platform/Android.cmake`(NDK 라이브러리), `android_main` 진입 · `ANativeWindow` 창, `VK_KHR_android_surface`.
4. **Android 내용 읽기** — `AndroidMain` 이 앱 전용 폴더(`internalDataPath`)를 `SW_ANDROID_APP_FOLDER` 로 넘겨 `ProcessRoots` 의 바이너리 · 프로젝트 폴더로 쓰고, 팩 폴더 경로(`<앱 폴더>/Packs`)를 APK 의 `assets/Packs` 로 옮기는 `BundledContentFileSource` + `AndroidAssetFileReader`(AAssetManager)를 건다. 팩은 압축하지 않고 저장돼 `AAsset_openFileDescriptor64` 의 fd + 구간으로 바로 읽는다(`AsyncFileHandle::openNativeRange`) — 압축된 파일은 오류다.
5. **교차 빌드 쿠킹** — Android · 웹의 `CookAssets` 는 호스트 개발 빌드(`SW_HOST_BUILD_DIR`, 기본 `build/Ninja-Release` — 굽기 앞에 다시 짓는다)의 App 이 `--cook-texture-variants` 뒤 `--cook-all -<배포 백엔드의 명령줄 이름>` 으로 `<교차 빌드>/Bin/Packs` 에 굽는다 — `android-apk` 의 기본 `--packs-dir` 이다.

## 화면 대응(모바일 · 데스크톱)

- 안전 여백: 플랫폼 창이 `IWindow::getSafeAreaInsets`(물리 픽셀)를 준다 — Android 는 JNI `getRootWindowInsets`, 웹은 셸 HTML 의 CSS 변수 `--sw-safe-left/top/right/bottom`(= `env(safe-area-inset-*)`). `UIScaleSettings::makeViewport` 가 변마다 디버그 흉내(`gv_uiSafeZone`)와 큰 쪽을 고른다.
- 크기 · 회전 · 수명: `CallbackInputWindow::applyPlatformResize` 하나로 모은다. Android `APP_CMD_TERM_WINDOW` → `SurfaceLost`(`IRHIDevice::releaseSurface`), `INIT_WINDOW` → `SurfaceRestored`(`restoreSurface`), `PAUSE` · `RESUME` · 웹 `visibilitychange` → 앱 멈춤 · 돌아옴.
- 회전(pre-rotation): 서피스 `currentTransform` 이 90 · 180 · 270 이면 스왑체인을 그 회전으로 만들고(이미지는 기기 기본 방향, `getWidth/Height` · 백버퍼는 논리 크기), 화면에 쓰는 모든 것이 논리 크기 대기 텍스처에 쓴 뒤 출력 패스 하나(`RenderPassType::ScreenRotation`, `FrameScreenRotation`)가 돌려 옮긴다. 데스크톱에는 이 패스가 없다(`RHICapabilities::_bSurfacePreRotation`). 거울 회전은 IDENTITY + 합성기, 스왑체인은 회전 · 크기가 정말 바뀌었을 때만 다시 만든다(`VulkanRHISwapChain::hasSurfaceChanged`).
- 좁은 화면 · 방향: 리소스 태그 `screen.compact`(dp 너비 < `uiscale.xml` `_compactMaxWidth`) · `orientation.portrait` — UI 문서 · 스타일 시트가 읽을 때 `_when` 변형으로 다시 배치한다(캐시가 합성의 한 길을 지난다, 태그가 바뀌면 `reloadComposed`). 규칙은 `UIScaleSettings::makeScreenTags` 한 곳이다.
- 손가락: 터치 장치의 첫 손가락이 UI 포인터가 되고, 조작을 비켜 짚으면 `_minTouchTargetSize`(48)까지 넓혀 고른다(`UIEventRouter::hitTestTouch`).
- 품질 등급: `engine.settings.xml` 의 `<AutoDetect platform=… requireNativeBindless=…>` — 플랫폼 기본 RHI 의 정적 능력과 시스템 메모리로 고른다.
- 에디터: UI 미리보기 견본(`uiresolutions.xml`)이 기기 여백 · density 를 싣고 "돌리기" 로 가로를 본다. 미리보기 화면은 자기 뷰포트의 화면 태그를 그 화면에만 바꿔 끼운다(`ResourceContext::withTagOverride` — 엔진 태그 · 게임 UI 는 그대로, 변형은 캐시에 따로, 저장은 같은 태그로 조건을 되돌린다). 같은 조건의 시나리오는 `engine/automation/screenadapt`(단계 `Touch`).
- APK: `py -3 -m Scripts android-apk --game <게임>` 이 Android-Shipping 빌드의 `libApp.so` 와 쿠킹한 팩(`assets/Packs`, 압축하지 않는다)을 서명한 디버그 APK(`build/Android-Shipping/Apk/<게임>-debug.apk`)로 묶는다 — Gradle 없이 aapt2 · zipalign(16KB 페이지) · apksigner. 매니페스트 틀은 `cmake/Templates/AndroidManifest.xml`, 값(앱 id · 방향 · 판)은 `Config/Environment/android_package.json`.

## 남은 일(코드를 붙여 보지 못한 것)

- 웹 실행: `py -3 -m Scripts web-check --game Empty`(묶기 · COOP/COEP 서버 · 헤드리스 브라우저 — WebGPU · 오류 없음 · 화면 PNG · IDBFS 영속)를 아직 돌리지 않았다. Web-Shipping 빌드(단계 텍스처 16 계약이 처음 걸린다) 뒤 통합에서 돌린다. 입력 · 클립보드(paste) 판정은 없다 — 배포 빌드에 시나리오 실행기가 없어 앱 반응을 읽을 길이 없다(웹 시나리오를 쿠킹하거나 반응을 페이지로 내보내는 길이 필요하다).
- 웹 온라인: 스트림은 WebSocket 이다 — 클라이언트 `WebSocketStreamTransport`(Web), 서버는 같은 리슨에서 첫 바이트로 가르는 `WebSocketAcceptTransport`(온라인 서비스 리슨). 남은 것: `ws://` 만이라 https 페이지는 `wss://`(수락 쪽 TLS 종단)가 필요하다. 게임 세션(암호화 UDP)은 브라우저에 UDP 가 없어 아직 길이 없다(WebSocket · WebTransport 위 데이터그램 갈래).
- 웹 오디오: `Audio/Web/WebAudioBackend`(믹서 스레드 → SPSC 링 `Audio/CircularAudioBuffer` → AudioWorklet `WebAudioBackend.js`, 첫 사용자 몸짓에 `AudioContext.resume`, 워클릿 실패는 `notifyOutputLost` → 다시 열기)는 아직 컴파일 · 실행하지 않았다 — `web-check` 가 `[WebAudio] output started` 줄을 판정한다. Android AAudio(`Audio/Android`)는 실기기에서 장치 분리 → 다시 열기를 본다.
- Android 실기기: APK 기동 · 회전(pre-rotation 방향) · APK 팩 읽기(`AAssetDir_getNextFileName` 이 폴더 앞머리를 붙이는지) · 터치를 확인한다. 클립보드는 아직 OS 를 잇지 않는다(`AndroidClipboard.cpp` 의 `isAvailable` false — JNI `ClipboardManager` 는 Looper 스레드 제약을 실기기와 함께 본다).

## 입력(웹 · Android)

- 패드: 웹은 Gamepad API 를 프레임마다 읽는다 — W3C standard 배치만 옮기고(`PlatformGamepadLayout`), 배치가 다른 패드는 연결하지 않고 한 번 알린다. 브라우저는 사용자가 패드를 한 번 누르기 전에는 패드를 보이지 않는다. Android 는 패드 · 조이스틱 장치의 키 · 축 이벤트를 장치 번호마다 패드 칸으로 보내고(방아쇠 LTRIGGER/BRAKE · RTRIGGER/GAS 합침, HAT 축은 D패드), 연결 · 끊김은 `InputDevice.getDeviceIds()` 를 1 초마다 JNI 로 대조한다.
- 포인터 잠금: 웹은 Pointer Lock API(다음 입력 콜백까지 미뤄 청한다 — 사용자 입력 안에서만 된다), 잠긴 동안은 이동량만 온다. 가두기는 브라우저에 없고 Android 는 잠그지 않는다(한 번 오류 로그).
- Android 명령줄: 인텐트 문자열 추가값 `args` — `adb shell am start -n <패키지>/android.app.NativeActivity --es args "-scenario empty/noidlewait"`.

## 의존성

| 무엇 | 쓰는 곳 | 판(기억) | 라이선스 |
|---|---|---|---|
| Emscripten SDK(emsdk) | 웹 빌드 도구 사슬 — `Tools/emsdk`(`platform_sdk.py web`) | `search_paths.defaults.json` 의 `emsdk_version` | MIT / University of Illinois NCSA(emscripten) |
| Dawn | vcpkg `dawn`(`ThirdParty/dawn`) — 데스크톱은 네이티브 구현(`dawn::webgpu_dawn`, Windows `vulkan,d3d12`, Linux `vulkan,x11`), 웹은 같은 판의 emdawnwebgpu 포트 파일(`webgpu/webgpu.h`) | `vcpkg.json` `builtin-baseline` 의 판 | BSD-3-Clause |
| Tint 실행 파일 | 셰이더 쿠커의 SPIR-V → WGSL 단계 — `dawn[tint-tools]` 가 깔고 빌드가 App 옆에 둔다(`sw_copy_tint_tool`) | Dawn 과 같은 판 | BSD-3-Clause |
| Android NDK | `libvulkan.so` · `libandroid.so` · `liblog.so`, `android_native_app_glue`(NDK 소스) — `Tools/Android/ndk/<판>`(`platform_sdk.py android`) | `android_ndk_version` | Apache-2.0(NDK), 앱 글루 Apache-2.0 |
| Android SDK build-tools · 플랫폼 | APK 패키징(aapt2 · zipalign · apksigner, android.jar) — `Tools/Android`(`platform_sdk.py android`) | `android_build_tools` · `android_platform` | Apache-2.0 |

## 엔진 비교(기억)

- 언리얼: 모바일은 Vulkan(ES3.1 대체), 웹은 공식 지원을 내렸다. 바인딩은 `FRHIUniformBuffer` + 고정 슬롯이고 bindless 는 SM6 데스크톱 전용 — 이 저장소의 "bindless 아니면 고정 슬롯" 두 모델과 같다.
- 유니티: WebGPU 백엔드(6000.x 실험)가 DX11 같은 슬롯 모델이고, 텍스처 압축은 빌드 대상마다 형식을 고른다(Android ASTC, 웹은 DXT + ASTC 두 벌).
- Godot: 웹은 GL Compatibility 만, Android 는 Vulkan Mobile 렌더러. `RenderingDevice::texture_is_format_supported_for_usage` 로 형식을 묻는다.
