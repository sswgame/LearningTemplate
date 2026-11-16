# ==============================================================================
# @file cmake/Toolchain/Platform/Android.cmake
# @brief Android 플랫폼 정의 — 매크로 · 이름, NDK 시스템 라이브러리(android · log · vulkan), NativeActivity 글루.
# @note `SW_PLATFORM_LINUX` 를 켜지 않는다. 켜면 `__linux__` 검사를 통과해 X11 · ALSA · epoll 가지로 조용히 간다(언리얼 `PLATFORM_ANDROID` 도 Linux 와 따로다, 기억).
#       구성: `cmake --preset Android-Shipping` — 프리셋이 `Tools/Android/ndk/<판>`(설치 `py -3 Scripts/setup/platform_sdk.py android`)을 가리킨다. 앱 실행 파일은 공유 라이브러리(libApp.so)로 짓고 NativeActivity 가 연다
#       (sw_add_executable). APK 는 빌드 뒤 `py -3 -m Scripts android-apk`(매니페스트 틀 cmake/Templates/AndroidManifest.xml — Gradle 없이 aapt2 · zipalign · apksigner).
# ==============================================================================

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Android")
	return()
endif()

add_library(sw_platform_android INTERFACE)
target_compile_definitions(sw_platform_android INTERFACE SW_PLATFORM_ANDROID SW_PLATFORM_POSIX SW_PLATFORM_NAME="Android")  # 이름은 sw::build::kPlatformName
target_link_libraries(sw_platform_android INTERFACE android log aaudio)  # aaudio — 오디오 장치 백엔드(Audio/Android, API 26+)
list(APPEND SW_FLAG_LIBRARIES sw_platform_android)
# 앱이 공유 라이브러리(libApp.so — NativeActivity 가 연다)라 거기 묶이는 정적 라이브러리 · 오브젝트가 모두 PIC 여야 한다. 대상마다 켜지 않고 이 플랫폼 전체에 건다(빠뜨린 타깃이 링크에서 R_AARCH64_* 로 섰다).
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# NativeActivity 글루 — NDK 가 소스로 준다(새 의존성이 아니다). android_main 을 부르는 스레드 · ALooper 명령 파이프다.
if(NOT TARGET sw_android_native_app_glue)
	set(app_glue_dir "${CMAKE_ANDROID_NDK}/sources/android/native_app_glue")
	if(NOT EXISTS "${app_glue_dir}/android_native_app_glue.c")
		message(FATAL_ERROR "[Android] android_native_app_glue.c not found under ${app_glue_dir} - install the NDK with py -3 Scripts/setup/platform_sdk.py android")
	endif()
	add_library(sw_android_native_app_glue STATIC "${app_glue_dir}/android_native_app_glue.c")
	target_include_directories(sw_android_native_app_glue PUBLIC "${app_glue_dir}")
	target_link_libraries(sw_android_native_app_glue PUBLIC android log)
	# 글루의 진입점(ANativeActivity_onCreate)은 아무도 부르지 않아 링커가 버린다 — 남기라고 알린다.
	target_link_options(sw_android_native_app_glue INTERFACE "-u" "ANativeActivity_onCreate")
	# 엔진의 Android 창(Window/Android)이 글루 헤더(android_app)를 읽는다 — 링크는 앱 라이브러리 한 곳이다.
	target_include_directories(sw_platform_android INTERFACE "${app_glue_dir}")
endif()

# Vulkan — NDK 의 libvulkan.so(장치가 준다). RHI 표의 graphics_libs 이름이다(CookContract.json).
if(NOT TARGET sw_graphics_vulkan_libs)
	add_library(sw_graphics_vulkan_libs INTERFACE)
	target_link_libraries(sw_graphics_vulkan_libs INTERFACE vulkan)
endif()

if(NOT TARGET sw_graphics_libs)
	add_library(sw_graphics_libs INTERFACE)
	target_link_libraries(sw_graphics_libs INTERFACE sw_graphics_vulkan_libs)
endif()
