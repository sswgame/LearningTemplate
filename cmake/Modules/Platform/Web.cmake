# ==============================================================================
# @file cmake/Modules/Platform/Web.cmake
# @brief 웹(Emscripten) 플랫폼 정의 — 매크로 · 이름, 링크 옵션.
# @note Emscripten 은 POSIX 층을 흉내 낸다(파일 · 시계 · pthread). 소켓 · 프로세스처럼 없는 것은 그 자리의 `#elif` 가 오류로 알린다.
#       구성: `cmake --preset Web-Shipping` — 프리셋이 `Tools/emsdk`(설치 `py -3 Scripts/setup/platform_sdk.py web`)의 Emscripten 툴체인을 가리킨다(docs/PlatformTargets.md).
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
	-lidbfs.js # 세이브 · 설정의 영속 폴더(App/AppEntry.cpp 의 IDBFS 마운트)
	-sEXPORTED_RUNTIME_METHODS=FS # 확인 페이지(py -3 -m Scripts web-check)가 IDBFS 표지를 쓰고 읽는다
	-lwebsocket.js # 스트림 전송(Core/Network/Transport/Web/WebSocketStreamTransport — emscripten/websocket.h)
	-sEXIT_RUNTIME=1
)
# 브라우저 쪽 JS 는 C++ 소스(EM_JS · EM_ASM)가 아니라 기능 옆의 `Web/*.js` Emscripten 라이브러리(addToLibrary)에 둔다 — C++ 포맷터가 매크로 본문의
# `=>` · `!==` 를 띄워 브라우저 JS 가 문법 오류로 멈췄다(clang-format 의 WhitespaceSensitiveMacros 는 이 모양을 지키지 못한다). 모두 링크에 넘기고 바뀌면 다시 링크한다.
sw_glob_source_tree(web_js_libraries RECURSE "${CMAKE_SOURCE_DIR}/Source/*.js")
list(FILTER web_js_libraries INCLUDE REGEX "/Web/[^/]+\\.js$")
foreach(web_js_library IN LISTS web_js_libraries)
	target_link_options(sw_platform_web INTERFACE "--js-library=${web_js_library}")
endforeach()
set_property(TARGET sw_platform_web APPEND PROPERTY INTERFACE_LINK_DEPENDS ${web_js_libraries})
unset(web_js_libraries)
list(APPEND SW_FLAG_LIBRARIES sw_platform_web)

# WebGPU 라이브러리(`sw_graphics_webgpu_libs` — RHI 표의 graphics_libs 이름)는 ThirdParty/dawn 이 만든다: vcpkg dawn 포트가 깐 emdawnwebgpu 포트 파일
# (`--use-port=<vcpkg>/share/dawn/emdawnwebgpu.port.py`)이라 데스크톱 Dawn 과 같은 판의 webgpu.h 다. 이름은 링크 때 풀린다(이 파일이 먼저 읽힌다).
if(NOT TARGET sw_graphics_libs)
	add_library(sw_graphics_libs INTERFACE)
	target_link_libraries(sw_graphics_libs INTERFACE sw_graphics_webgpu_libs)
endif()
