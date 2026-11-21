# 플랫폼 대상: Android · 웹(WebGPU)

> 결론: Android 는 Vulkan 1.2 + 디스크립터 인덱싱 필수(없으면 장치를 고르지 않는다). 웹은 WebGPU 하나, bindless 가 없어 DX11 과 같은 고정 슬롯 모델. 대상이 못 주는 것은 능력 깃발(`RHICapabilities`) · 형식 대체 · 쿠킹 변형으로 가르고, 필수인데 없으면 오류로 멈춘다.

범위: Android 지원, iOS 제외, 웹은 최신 브라우저 WebGPU 만(WebGL2 없음), 콘솔은 갈래를 만들지도 막지도 않는다.

## 규칙

- **웹 제약은 웹 깃발로만.** 웹이 못 하는 것은 능력 깃발 · 플랫폼 사슬 설정 값 · 쿠킹 변형(`CookContract.json` 백엔드 줄) 가운데 하나로 웹만 가른다. `SW_PLATFORM_WEB` 컴파일 갈래는 최후 수단. 공용 상수 · 셰이더 · 예산을 웹 한도에 맞춰 낮추지 않는다.
- 새 GPU 기능(64 비트 · 실수 · 텍스처 아토믹, 웨이브 연산 등)을 쓸 때 웹 때문에 막지 않는다 — 능력 깃발로 웹(과 못 주는 장치)만 다른 길로 보내고 아래 표에 줄을 더한다.
- "(기억)" 표시 값은 붙이기 전에 그 판의 명세로 다시 확인한다.
- 브라우저는 GPU 를 동기로 기다릴 수 없다 — 새 `waitIdle` 을 두지 않는다. 씬 활성화 · 핫 리로드 · 캡처는 `flushRenderThread` + 펜스 지연 해제, 도구 · 수명 길은 `RHIWaitIdleReason` 을 붙인다(시나리오 `empty/noidlewait` · `editor/assetcascade` 가 `RHI.GPUIdleWaits` 를 본다). 되읽기는 늘 `mapAsync` 콜백.
- 웹 장치는 만든 스레드에서만 쓴다 — `_bParallelCommandRecording` · `_bThreadSafeResourceCreation` 거짓, 렌더 스레드를 띄우지 않는다(`gv_useRenderThread` 기본값은 플랫폼 사슬로).
- 네트워크는 IPv6 를 지원한다: 서버 `_listenAddress="::"` 는 이중 스택, 클라이언트는 서버 주소의 군으로 연다, 이름은 A 먼저 없으면 AAAA(`PlatformSocketUtil::resolveAddress`).

## 요구 판

| 대상 | 요구 |
|---|---|
| Android | Vulkan 1.2(`constant::render::kVulkanRequiredMinor`), API 29 이상, NDK `libvulkan.so` 링크 |
| 웹 | WebGPU(`navigator.gpu`), Chrome · Edge 121+, Firefox 141+(Windows), Safari 26+(기억). 데스크톱에서 Dawn(vcpkg `dawn`)으로 같은 API 를 돌려 본다 |

## 차이를 가르는 법

| 항목 | Android Vulkan 1.2 | WebGPU |
|---|---|---|
| 바인딩 모델 | bindless 필수 — 기능 · 한도 못 미치는 장치는 건너뛴다(`VulkanRHIDevice::isBindlessCapable`) | 고정 슬롯(`SW_BINDLESS` 없음), 셰이더 갈래 `WEBGPU` |
| 바인딩 수 | 장치 한도 | 단계 텍스처 계약 16(`stage_texture_unit_limit` = `shaderslot::wgpu::kStageTextureBudget`) — 쿠킹 · 장치 생성이 멈춘다. 그 밖의 한도는 PSO 오류 |
| 루트 상수 | 푸시 상수 64 B | 상수 버퍼 `b2`(`SW_SLOT_ROOT_CB_EMUL`) 에뮬 |
| 웨이브 연산 | 그대로 | `SW_NO_WAVE_OPS` 로 쿠킹(스레드마다 원자 연산 길) |
| `R16G16_SNORM` 첨부 | 없으면 장치를 고르지 않는다 | 기능 없으면 `rg16float`(`WebGPURHIUtil::toTextureFormat`) |
| `R32G32B32_FLOAT` 텍스처 | 없으면 RGBA32F 로 펴서 올린다(`Texture2D::expandToFourChannels`) | 같음 |
| 블록 압축 | 쿠킹이 모든 대상 묶음(ASTC · ETC2)을 한 번 굽고 실행이 장치가 되는 첫 묶음을 고른다(`selectFamily`) | BC · ASTC · ETC2, 셋 다 없으면 오류 |
| `R32_FLOAT` 선형 샘플 | 선택 | `float32-filterable` 필수(`DriverUnsupported`) |
| 깊이 형식 · 되읽기 | `selectDepthFormat` 이 고른다 | `depth32float-stencil8` 있으면 그것, 없으면 깊이 되읽기 `Rejected` |
| 깊이 샘플 | 그대로 | R32 실수 사본을 `prepareTextureForShaderRead` 가 채운다 — 채우지 않은 사본을 읽으면 오류 로그 |
| 멀티 드로우 · 와이어프레임 · 지오메트리 · 테셀레이션 | 장치 값 | 능력 깃발 거짓, 그런 PSO 는 오류 |
| 간접 첫 인스턴스 | 1.2 코어 기능 | `indirect-first-instance` 필수 |
| 타임스탬프 | 된다 | `timestamp-query` 있으면 패스 경계에서만, `readGPUClockNanos` 거짓 |
| 표면 | `VK_KHR_android_surface`, 앱 수명 안에서 사라졌다 생긴다 | canvas 선택자 |

## 플랫폼 코드의 자리

1. WebGPU RHI — `Source/Engine/Graphics/RHI/WebGPU/`, 셰이더 대상 `WGSL_WebGPU`(DXC SPIR-V → Tint → WGSL).
2. Emscripten — `cmake/Toolchain/Platform/Web.cmake`, 메인 루프는 `emscripten_set_main_loop_arg`.
3. Android — `cmake/Toolchain/Platform/Android.cmake`, `android_main` · `ANativeWindow`. 팩은 APK `assets/Packs` 에 **압축하지 않고** 담는다(fd + 구간으로 읽는다 — 압축된 파일은 오류).
4. 교차 빌드 쿠킹 — 호스트 개발 빌드(`SW_HOST_BUILD_DIR`, 기본 `build/Ninja-Release`)의 App 이 `<교차 빌드>/Bin/Packs` 에 굽는다.

## 화면 · 입력

- 크기 · 회전 · 수명 이벤트는 `CallbackInputWindow::applyPlatformResize` 한 곳으로 모은다. 안전 여백은 `IWindow::getSafeAreaInsets`, 화면 태그(`screen.compact` · `orientation.portrait`) 규칙은 `UIScaleSettings::makeScreenTags` 한 곳.
- pre-rotation 은 출력 패스 하나(`RenderPassKind::ScreenRotation`)만 돌린다 — 다른 패스는 논리 크기로 그린다. 스왑체인은 회전 · 크기가 정말 바뀌었을 때만 다시 만든다.
- 터치 대상은 `_minTouchTargetSize`(48)까지 넓혀 고른다(`UIEventRouter::hitTestTouch`).
- APK: `py -3 -m Scripts android-apk --game <게임>`(값은 `Config/Environment/android_package.json`).
- Android 명령줄: `adb shell am start -n <패키지>/android.app.NativeActivity --es args "-scenario empty/noidlewait"`.

## 의존성

| 무엇 | 쓰는 곳 | 판(기억) | 라이선스 |
|---|---|---|---|
| Emscripten SDK | `Tools/emsdk`(`platform_sdk.py web`) | `search_paths.defaults.json` 의 `emsdk_version` | MIT / NCSA |
| Dawn | vcpkg `dawn`(`ThirdParty/dawn`) — 데스크톱 네이티브, 웹은 emdawnwebgpu 포트 | `vcpkg.json` `builtin-baseline` | BSD-3-Clause |
| Tint 실행 파일 | 셰이더 쿠커 SPIR-V → WGSL(`dawn[tint-tools]`, `sw_copy_tint_tool`) | Dawn 과 같은 판 | BSD-3-Clause |
| Android NDK | `Tools/Android/ndk/<판>`(`platform_sdk.py android`) | `android_ndk_version` | Apache-2.0 |
| Android SDK build-tools · 플랫폼 | APK 패키징 — `Tools/Android` | `android_build_tools` · `android_platform` | Apache-2.0 |
