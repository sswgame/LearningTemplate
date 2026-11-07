# ==============================================================================
# @file cmake/Modules/Platform/Web.cmake
# @brief 웹(Emscripten) 플랫폼 정의 — 매크로 · 이름, WebGPU 라이브러리 타깃, 링크 옵션.
# @note Emscripten 은 POSIX 층을 흉내 낸다(파일 · 시계 · pthread). 소켓 · 프로세스처럼 없는 것은 그 자리의 `#elif` 가 오류로 알린다.
#       구성: `emcmake cmake --preset web-release`(emsdk 4.0.10 이상 — emdawnwebgpu 포트가 든 판, docs/PlatformTargets.md). 새 의존성이라 아직 붙이지 않았다.
# ==============================================================================

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
	return()
endif()

add_library(sw_platform_web INTERFACE)
target_compile_definitions(sw_platform_web INTERFACE SW_PLATFORM_WEB SW_PLATFORM_POSIX SW_PLATFORM_NAME="Web")  # 이름은 sw::build::kPlatformName
# 스레드: 게임 스레드의 작업 스레드(태스크 · 파일 IO)는 pthread 다. 렌더 스레드는 웹에서 끈다(gv_useRenderThread — WebGPU 는 메인 스레드에서만).
# 페이지가 교차 출처 격리(COOP · COEP 헤더)여야 SharedArrayBuffer 가 있다 — 배포 서버 설정의 일이다.
target_compile_options(sw_platform_web INTERFACE -pthread)
target_link_options(sw_platform_web INTERFACE
	-pthread
	-sALLOW_MEMORY_GROWTH=1
	-sMAXIMUM_MEMORY=4GB
	-sPTHREAD_POOL_SIZE=navigator.hardwareConcurrency
	# 어댑터 · 장치 요청을 기다린다(`wgpuInstanceWaitAny` — WebGPURHIDevice::waitForFuture). JSPI 가 없는 브라우저면 -sASYNCIFY=1 로 바꾼다.
	-sJSPI=1
	# 콘텐츠: 쿠킹한 팩을 가상 파일 시스템에 얹는다(--preload-file 은 패키징 스크립트가 붙인다 — Scripts/package).
	-sFORCE_FILESYSTEM=1
	-sEXIT_RUNTIME=1
)
list(APPEND sw_flag_libraries sw_platform_web)

# WebGPU — emdawnwebgpu 포트(webgpu/webgpu.h · 구현은 브라우저). RHI 표의 graphics_libs 이름이다(CookContract.json).
if(NOT TARGET sw_graphics_webgpu_libs)
	add_library(sw_graphics_webgpu_libs INTERFACE)
	target_compile_options(sw_graphics_webgpu_libs INTERFACE --use-port=emdawnwebgpu)
	target_link_options(sw_graphics_webgpu_libs INTERFACE --use-port=emdawnwebgpu)
endif()

if(NOT TARGET sw_graphics_libs)
	add_library(sw_graphics_libs INTERFACE)
	target_link_libraries(sw_graphics_libs INTERFACE sw_graphics_webgpu_libs)
endif()
