# ==============================================================================
# @file Source/Server/CMakeLists.txt
# @brief 전용 서버 실행 파일(Server) — 창 · GPU 없이 게임 모듈을 고정 틱으로 돌린다. Game · Server 타깃에서 짓는다(Client 타깃은 짓지 않는다).
# ==============================================================================

sw_glob_source_tree(SERVER_SOURCES RECURSE "*.cpp" "*.c" "*.h" "*.hpp")

if(SW_TARGET_TYPE STREQUAL "Client")
	sw_declare_excluded_sources(${SERVER_SOURCES})
	return()
endif()

# 개발 전용(`Tests/` · `Dev/`)은 배포가 짓지 않는다. 서버 개발 조종(`ModuleManager/Host/IServerDebugControl` — 구현은 DevTools 의 `Host/`)과 개발 계층은 `ServerApp` 이 `build::kShipping` 이 아닐 때만 만든다.
if(SW_SHIPPING_BUILD)
	sw_remove_excluded_sources(SERVER_SOURCES ${SW_DEV_ONLY_SOURCE_PATTERNS})
endif()

# 플랫폼 폴더(`Windows/` 의 서비스 호스트)는 그 플랫폼에서만 짓는다 — Core · Engine · App 과 같은 규칙.
sw_filter_platform_sources(SERVER_SOURCES)

sw_add_executable(Server ${SERVER_SOURCES})
sw_apply_target_layer_guard(Server server)
set_target_properties(Server PROPERTIES FOLDER "Source/Server")

target_link_libraries(Server
	PRIVATE
		ModuleManager
		Engine
		ModuleAPI
		sw_global_options
)
# 개발 도구(셰이더 · 설정 리로드) — 호스트 범위의 `DevToolsHost` 가 엔진 확장점에 붙는다. 배포 호스트는 링크하지 않는다.
if(NOT SW_SHIPPING_BUILD)
	target_link_libraries(Server PRIVATE DevTools)
endif()
target_compile_definitions(Server PRIVATE "SW_LOG_TAG=\"Server\"" "SW_REGISTRAR_MODULE=\"Server\"" SW_APP_INTERNAL)
sw_configure_pch(Server "${CMAKE_SOURCE_DIR}/Source/Engine/pch.h")

# Windows 서비스 제어(SCM)는 advapi32. 콘솔 서브시스템이어야 콘솔 제어 처리기 · 표준 입력이 있다(WIN32 를 붙이지 않는다).
if(WIN32)
	target_link_libraries(Server PRIVATE advapi32.lib)
endif()

# Tracy 클라이언트(개발 빌드 · Windows). Engine 이 지연 로드하므로 `-gv_tracy` 를 켜기 전에는 올라오지 않는다.
sw_copy_tracy_runtime(Server)
sw_apply_runtime_copies(Server)

# 클라이언트 전용 형식 이름 표(`ClientOnlyTypeTable`) — 전용 서버로 선 프로세스만 읽는다(서버 구성은 클라이언트 코드를 짓지 않고, 개발 구성의 Server 는 클라이언트 모듈을 올리지 않아 그 형식을 모른다). 클라이언트 전용 소스의 헤더에서 빌드가 만들어
# 실행 파일 옆(`Bin/ClientOnlyTypes.json`)에 두고, 패키지(`Scripts/package/package.py`)가 함께 싣는다. 헤더 고르기는 클라이언트 전용 소스의 정본과 같다
# (`SW_EXCLUDED_ENGINE_FOLDERS` · `SW_CLIENT_ONLY_SOURCE_PATTERNS`, 테스트 폴더는 뺀다). 언리얼이 쿠킹 때 서버에서 뺄 클래스 목록을 내는 자리와 같다.
# .cpp 안의 리플렉션 형식(`<이름>.cpp.gen.inl` 로 등록한다)도 든다. .cpp 는 매크로가 있는 것만 넘긴다. 클라이언트 .cpp 를 고칠 때마다 표를 다시 쓰지 않게 하려는 것이고,
# 매크로가 새로 생긴 .cpp 는 리플렉션 후보 검사(`sw_add_reflection_candidate_check`)가 다시 구성하게 한다.
sw_get_reflection_input_globs(list_reflection_input_glob "${CMAKE_SOURCE_DIR}/Source")
sw_glob_source_tree(list_source_header RECURSE ${list_reflection_input_glob})
set(list_client_only_header "")
foreach(header IN LISTS list_source_header)
	string(REPLACE "${CMAKE_SOURCE_DIR}/Source/" "" relative_header "${header}")
	if(relative_header MATCHES "/Tests/")
		continue()
	endif()
	set(client_only OFF)
	foreach(pattern IN LISTS SW_CLIENT_ONLY_SOURCE_PATTERNS)
		if("/Source/${relative_header}" MATCHES "^${pattern}") # 표의 정규식은 저장소 기준 경로에 맞는다
			set(client_only ON)
		endif()
	endforeach()
	foreach(folder IN LISTS SW_EXCLUDED_ENGINE_FOLDERS)
		string(FIND "${relative_header}" "Engine/${folder}/" folder_index)
		if(folder_index EQUAL 0)
			set(client_only ON)
		endif()
	endforeach()
	# 헤더는 매크로가 없어도 넘긴다 — 구성 뒤에 매크로가 생긴 헤더도 표에 든다(헤더를 다 넘기는 리플렉션 단계가 있어 후보 검사가 지켜보지 않는 헤더가 있다).
	if(client_only AND header MATCHES "\\.cpp$")
		sw_is_reflection_input(client_only "${header}")
	endif()
	if(client_only)
		list(APPEND list_client_only_header "${header}")
	endif()
endforeach()
set(client_only_header_list "${CMAKE_BINARY_DIR}/generated/ClientOnlyTypeHeaders.list")
string(REPLACE ";" "\n" client_only_header_text "${list_client_only_header}")
sw_write_if_changed("${client_only_header_list}" "${client_only_header_text}")
set(client_only_table "${CMAKE_BINARY_DIR}/Bin/ClientOnlyTypes.json")
add_custom_command(
	OUTPUT "${client_only_table}"
	COMMAND "${Python3_EXECUTABLE}" -X utf8 "${CMAKE_SOURCE_DIR}/Scripts/generate/client_only_types.py" --headers "${client_only_header_list}" --output "${client_only_table}"
	COMMAND "${CMAKE_COMMAND}" -E touch "${client_only_table}"
	DEPENDS ${list_client_only_header} "${client_only_header_list}" "${CMAKE_SOURCE_DIR}/Scripts/generate/client_only_types.py"
	COMMENT "[Server] Collecting client-only type names"
	VERBATIM
)
add_custom_target(ClientOnlyTypes DEPENDS "${client_only_table}")
set_target_properties(ClientOnlyTypes PROPERTIES FOLDER "Source/Server")
add_dependencies(Server ClientOnlyTypes)
