# ==============================================================================
# @file cmake/Modules/Platform/Linux.cmake
# @brief Linux 플랫폼 정의 (find_library, RPATH, INTERFACE)
# @note GPU(OpenGL/GLX/Vulkan) → sw_graphics_libs
# X11/xcb는 플랫폼(윈도잉/WSI)에 유지
# ==============================================================================

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
	return()
endif()

add_library(sw_platform_linux INTERFACE)
target_compile_definitions(sw_platform_linux INTERFACE SW_PLATFORM_LINUX SW_PLATFORM_NAME="Linux")  # 이름은 sw::build::kPlatformName

# 전용 서버(SW_TARGET_TYPE=Server)는 창이 없다 — X11 · xcb 를 찾지도 링크하지도 않는다. 서버 기계에는 X 가 없고, 링크해 두면 동적 링커가
# main 전에 libX11.so 를 찾다 실패한다. X11 을 쓰는 .cpp 는 `SW_WITH_CLIENT_CODE` 로 서버 빌드에서 빠진다(Window · Input 의 Linux 폴더).
if(NOT SW_TARGET_TYPE STREQUAL "Server")
	# ------------------------------------------------------------------------------
	# 1) X11 / xcb — 윈도잉 + Vulkan WSI (XGetXCBConnection / VK_KHR_xcb_surface)
	# ------------------------------------------------------------------------------
	# 클라이언트 빌드에는 모두 필수다 — 없으면 구성을 세운다(예전에는 경고만 남기고 링크 단계에서야 섰다).
	# 패키지: libx11-dev · libxcb1-dev · libx11-xcb-dev(apt 로 깐다).
	find_package(X11 REQUIRED)
	target_link_libraries(sw_platform_linux INTERFACE ${X11_LIBRARIES})
	target_include_directories(sw_platform_linux INTERFACE ${X11_INCLUDE_DIR})

	find_path(SW_XCB_INCLUDE_DIR NAMES xcb/xcb.h REQUIRED)
	find_library(SW_XCB_LIBRARY NAMES xcb REQUIRED)
	find_library(SW_X11_XCB_LIBRARY NAMES X11-xcb REQUIRED)
	target_include_directories(sw_platform_linux INTERFACE ${SW_XCB_INCLUDE_DIR})
	target_link_libraries(sw_platform_linux INTERFACE ${SW_XCB_LIBRARY} ${SW_X11_XCB_LIBRARY})

	# 개발 콘솔 오버레이(X11DevConsoleWindow)가 Xft(fontconfig 폴백 글꼴)로 한글 · 일본어 글자를 그린다. 패키지: libxft-dev(fontconfig · freetype 를 끌고 온다).
	# 라이선스: libXft · fontconfig 는 MIT 계열, FreeType 은 FTL(BSD 계열)/GPLv2 중 FTL 로 쓴다. 동적 링크이고 배포물에 싣지 않는 시스템 라이브러리다.
	# 다른 폴더의 대상이 이 INTERFACE 를 쓰므로 가져온 대상은 GLOBAL 이어야 한다.
	find_package(PkgConfig REQUIRED)
	pkg_check_modules(SW_XFT REQUIRED IMPORTED_TARGET GLOBAL xft fontconfig)
	target_link_libraries(sw_platform_linux INTERFACE PkgConfig::SW_XFT)

	# AT-SPI 접근성 브리지(`SW_ENABLE_ATSPI`) — D-Bus 위의 org.a11y.atspi 객체를 libdbus-1 로 내보낸다. 패키지: libdbus-1-dev(apt 로 깐다).
	# 라이선스: libdbus 는 AFL-2.1 또는 GPL-2.0+ 이중 — AFL-2.1(허용 라이선스)로 쓴다. 동적 링크이고 배포물에 싣지 않는 시스템 라이브러리다.
	if(SW_ENABLE_ATSPI)
		pkg_check_modules(SW_DBUS REQUIRED IMPORTED_TARGET GLOBAL dbus-1)
		target_link_libraries(sw_platform_linux INTERFACE PkgConfig::SW_DBUS)
		target_compile_definitions(sw_platform_linux INTERFACE SW_WITH_ATSPI)
		message(STATUS "[Linux] AT-SPI accessibility bridge enabled (libdbus-1 ${SW_DBUS_VERSION})")
	endif()

	# ------------------------------------------------------------------------------
	# XTest 입력 주입기(`SW_ENABLE_XTEST`, 선택 의존) — X 서버에 마우스 · 키를 넣어 X11 창 입력을 시험한다. 패키지: libxtst-dev(apt 로 깐다).
	# 라이선스: libXtst 는 MIT 계열이다. 동적 링크이고 배포물에 싣지 않는 시스템 라이브러리다. 없으면 오류로 멈추지 않고 기능을 끈다(주입기는 개발 도구).
	if(SW_ENABLE_XTEST)
		pkg_check_modules(SW_XTST IMPORTED_TARGET GLOBAL xtst)
		if(SW_XTST_FOUND)
			target_link_libraries(sw_platform_linux INTERFACE PkgConfig::SW_XTST)
			target_compile_definitions(sw_platform_linux INTERFACE SW_WITH_XTEST)
			message(STATUS "[Linux] XTest input injector enabled (libxtst ${SW_XTST_VERSION})")
		else()
			message(STATUS "[Linux] XTest injector not available (libxtst not found) - input.xtest reports it is off")
		endif()
	endif()

	# 1-1) Wayland(선택, `SW_ENABLE_WAYLAND`) — 창 · 입력 · 커서. 프로토콜 클라이언트 코드(xdg-shell · fractional-scale · cursor-shape · pointer-constraints · relative-pointer)는 구성 때 wayland-scanner 로 만든다.
	# 패키지: libwayland-dev(wayland-egl 포함) · libegl-dev(GL 컨텍스트 `EGLPlatformContext`) · libxkbcommon-dev · wayland-protocols(apt 로 깐다). Vulkan 표면은 vcpkg vulkan-loader[wayland] 가 낸다.
	# ------------------------------------------------------------------------------
	if(SW_ENABLE_WAYLAND)
		find_package(PkgConfig REQUIRED)
		pkg_check_modules(SW_WAYLAND REQUIRED IMPORTED_TARGET wayland-client wayland-cursor wayland-egl egl xkbcommon)
		pkg_get_variable(SW_WAYLAND_PROTOCOLS_DIR wayland-protocols pkgdatadir)
		if(NOT SW_WAYLAND_PROTOCOLS_DIR)
			message(FATAL_ERROR "SW_ENABLE_WAYLAND: wayland-protocols (pkgdatadir) not found")
		endif()
		find_program(SW_WAYLAND_SCANNER wayland-scanner REQUIRED)

		# 프로토콜 XML 목록(wayland-protocols pkgdatadir 아래 상대 경로). 헤더 이름은 XML 파일 이름에서 정해진다(`<이름>-client-protocol.h`).
		# fractional-scale · cursor-shape 는 staging 이라 wayland-protocols 1.32 이상이 있어야 한다 — 없으면 아래 검사가 구성을 세운다.
		set(swWaylandProtocolXMLs
			"stable/xdg-shell/xdg-shell.xml"
			"staging/fractional-scale/fractional-scale-v1.xml" # 소수 배율(`wp_fractional_scale_v1`)
			"staging/cursor-shape/cursor-shape-v1.xml" # 커서 모양(`wp_cursor_shape_manager_v1`)
			# cursor-shape 의 생성 코드가 `zwp_tablet_tool_v2` 인터페이스를 가리켜 링크에 필요하다(쓰지는 않는다).
			"unstable/tablet/tablet-unstable-v2.xml"
			"unstable/pointer-constraints/pointer-constraints-unstable-v1.xml" # 포인터 잠금 · 가둠
			"unstable/relative-pointer/relative-pointer-unstable-v1.xml" # 잠긴 동안의 상대 이동
		)
		set(swWaylandGenDir "${CMAKE_BINARY_DIR}/generated/wayland")
		file(MAKE_DIRECTORY "${swWaylandGenDir}")
		set(swWaylandGenFiles "")
		foreach(swProtocolRelXML IN LISTS swWaylandProtocolXMLs)
			set(swProtocolXML "${SW_WAYLAND_PROTOCOLS_DIR}/${swProtocolRelXML}")
			if(NOT EXISTS "${swProtocolXML}")
				message(FATAL_ERROR "SW_ENABLE_WAYLAND: protocol XML not found: ${swProtocolXML} (wayland-protocols 1.32 or newer is required)")
			endif()
			get_filename_component(swProtocolName "${swProtocolXML}" NAME_WE)
			set(swProtocolHeader "${swWaylandGenDir}/${swProtocolName}-client-protocol.h")
			set(swProtocolSource "${swWaylandGenDir}/${swProtocolName}-protocol.c")
			add_custom_command(
				OUTPUT "${swProtocolHeader}" "${swProtocolSource}"
				COMMAND "${SW_WAYLAND_SCANNER}" client-header "${swProtocolXML}" "${swProtocolHeader}"
				COMMAND "${SW_WAYLAND_SCANNER}" private-code "${swProtocolXML}" "${swProtocolSource}"
				DEPENDS "${swProtocolXML}"
				COMMENT "wayland-scanner: ${swProtocolName}"
				VERBATIM
			)
			list(APPEND swWaylandGenFiles "${swProtocolHeader}" "${swProtocolSource}")
		endforeach()
		# 생성 코드는 C 다. Engine(공유 라이브러리)에 들어가므로 위치 독립 코드로 짓는다.
		add_library(sw_wayland_protocols STATIC ${swWaylandGenFiles})
		set_target_properties(sw_wayland_protocols PROPERTIES POSITION_INDEPENDENT_CODE ON LINKER_LANGUAGE C)
		target_include_directories(sw_wayland_protocols PUBLIC "${swWaylandGenDir}")
		target_link_libraries(sw_wayland_protocols PUBLIC PkgConfig::SW_WAYLAND)
		target_link_libraries(sw_platform_linux INTERFACE sw_wayland_protocols)
		target_compile_definitions(sw_platform_linux INTERFACE SW_WITH_WAYLAND)
		message(STATUS "[Linux] Wayland window layer enabled (protocols from ${SW_WAYLAND_PROTOCOLS_DIR}: xdg-shell, fractional-scale, cursor-shape, tablet, pointer-constraints, relative-pointer)")
	endif()
endif()

# ------------------------------------------------------------------------------
# 2) GPU — OpenGL/GLX + Vulkan 로더 → sw_graphics_*_libs / sw_graphics_libs
# Engine PUBLIC 링크로만 전파 (tools/tests는 미링크)
# Vulkan: vcpkg vulkan-loader[wayland,xcb,xlib], 없으면 시스템 폴백
# ------------------------------------------------------------------------------
if(NOT TARGET sw_graphics_gl_libs)
	add_library(sw_graphics_gl_libs INTERFACE)
endif()

# GL · Vulkan 로더는 필수다 — 못 찾으면 구성을 세운다(예전에는 경고만 남기고 RHI 링크에서야 섰다). 아래 갈래는 폴백이 아니라
# FindOpenGL · FindVulkan 이 시스템마다 다른 모양(타깃 · 변수 · GLVND)으로 답하는 것을 받는 자리다.
find_package(OpenGL REQUIRED)

if(OpenGL_FOUND)
	if(TARGET OpenGL::GL)
		target_link_libraries(sw_graphics_gl_libs INTERFACE OpenGL::GL)
	elseif(OPENGL_gl_LIBRARY)
		target_link_libraries(sw_graphics_gl_libs INTERFACE ${OPENGL_gl_LIBRARY})
	endif()

	if(TARGET OpenGL::GLX)
		target_link_libraries(sw_graphics_gl_libs INTERFACE OpenGL::GLX)
	elseif(OPENGL_opengl_LIBRARY AND NOT TARGET OpenGL::GL)
		target_link_libraries(sw_graphics_gl_libs INTERFACE ${OPENGL_opengl_LIBRARY})
	else()
		find_library(SW_GLX_LIBRARY NAMES GLX glx REQUIRED)
		target_link_libraries(sw_graphics_gl_libs INTERFACE ${SW_GLX_LIBRARY})
	endif()
endif()

if(NOT TARGET sw_graphics_vulkan_libs)
	add_library(sw_graphics_vulkan_libs INTERFACE)
endif()

find_package(Vulkan QUIET)

if(TARGET Vulkan::Vulkan)
	target_link_libraries(sw_graphics_vulkan_libs INTERFACE Vulkan::Vulkan)
elseif(Vulkan_LIBRARIES)
	target_link_libraries(sw_graphics_vulkan_libs INTERFACE ${Vulkan_LIBRARIES})
else()
	# FindVulkan 은 헤더까지 찾아야 답한다 — 헤더는 vcpkg 가 주고 로더만 시스템에 있는 경우 라이브러리를 직접 찾는다.
	find_library(SW_SYSTEM_VULKAN_LIBRARY NAMES vulkan REQUIRED)
	target_link_libraries(sw_graphics_vulkan_libs INTERFACE ${SW_SYSTEM_VULKAN_LIBRARY})
	message(STATUS "[Linux] Vulkan loader (system): ${SW_SYSTEM_VULKAN_LIBRARY}")
endif()

if(NOT TARGET sw_graphics_libs)
	add_library(sw_graphics_libs INTERFACE)
	target_link_libraries(sw_graphics_libs INTERFACE
		sw_graphics_gl_libs
		sw_graphics_vulkan_libs
	)
endif()

list(APPEND sw_flag_libraries sw_platform_linux)
