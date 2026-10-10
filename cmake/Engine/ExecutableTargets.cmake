# ==============================================================================
# @file cmake/Engine/ExecutableTargets.cmake
# @brief 실행 파일의 링크 · 배치 규칙 — 리플렉션 정적 라이브러리 통째 링크 · App/Server 의존 · 프로세스 매니페스트 · Shipping 심볼 분리
# ==============================================================================

# ------------------------------------------------------------------------------
# 정적 라이브러리 통째 링크 — 리플렉션 등록기 보존
# ------------------------------------------------------------------------------
# 생성된 *.gen.cpp 의 등록기는 파일 스코프 static 객체(생성자가 전역 링크드 리스트에 자신을
# 매단다)라 외부에서 참조되는 심볼이 없다. Dev 는 Engine 이 DLL 이라 전부 로드되지만, Shipping 은
# 정적 라이브러리라 링커가 "아무도 참조 안 하는 오브젝트 파일" 을 통째로 버린다. 타입은
# (StaticType() 정의가 같은 파일에 있어) 살아남고 **열거형만 조용히 사라진다** — RHITypes.gen.cpp
# 가 빠지면 RHIBackend · RHIFormat 이 등록되지 않아 EngineConfig 역직렬화가 기본값으로 떨어지고,
# 렌더패스 포맷이 전부 미상이 되며, KeyCodeUtil::fromName 이 Unknown 만 내서 InputMap 바인딩이
# 하나도 안 붙고, SaveGame 의 리플렉션 왕복이 깨진다. 그래서 리플렉션을 담은 정적 라이브러리는
# 통째로 링크한다.
#
# 플래그는 링커마다 다르다. 주의: 한쪽(`/WHOLEARCHIVE`)만 `WIN32` 가드 안에 두면 리눅스 Shipping 은 등록된 열거형이
# 거의 없는 채로 테스트를 돈다 — 그리고 `CI-Debug`(Engine 이 SHARED)만 돌려서는 재현되지 않는다.
#
#   | 링커                  | 플래그                                     |
#   | --------------------- | ------------------------------------------ |
#   | link.exe · lld-link   | `/WHOLEARCHIVE:<lib>`                      |
#   | GNU ld · lld · gold   | `--whole-archive <lib> --no-whole-archive` |
#
# 링크 옵션은 오브젝트보다 앞에 놓이지만 문제없다 — 통째로 올라온 멤버가 필요로 하는 심볼은 뒤에
# 오는 라이브러리가 채우고, 같은 아카이브가 뒤에 한 번 더 나와도 이미 올라온 멤버는 다시 올리지
# 않는다. STATIC 이 아닌 타겟(SHARED/MODULE/OBJECT)이나 없는 타겟은 조용히 건너뛴다 — 호출부가
# 구성마다 다른 목록을 그대로 넘겨도 되게.
function(sw_linkWholeArchive TARGET_NAME)
	foreach(reflLib IN LISTS ARGN)
		if(NOT TARGET ${reflLib})
			continue()
		endif()

		get_target_property(reflLibType ${reflLib} TYPE)

		if(NOT reflLibType STREQUAL "STATIC_LIBRARY")
			continue()
		endif()

		# 링크 옵션만 걸면 그 라이브러리의 링크 의존(키트가 PRIVATE 로 링크한 서드파티 — GF_SQLStore 의 sqlite3 · GF_Server_SQLStore 의
		# libpq)이 따라오지 않는다. 타깃으로도 링크해 전이 의존을 링크 줄에 올린다(같은 아카이브가 한 번 더 나와도 이미 올라온 멤버는 다시 올리지 않는다).
		target_link_libraries(${TARGET_NAME} PRIVATE ${reflLib})
		if(MSVC)
			target_link_options(${TARGET_NAME} PRIVATE "LINKER:/WHOLEARCHIVE:$<TARGET_FILE:${reflLib}>")
		else()
			target_link_options(${TARGET_NAME} PRIVATE
				"LINKER:--whole-archive,$<TARGET_FILE:${reflLib}>,--no-whole-archive")
		endif()
	endforeach()
endfunction()

# App의 런타임/플러그인/모듈 의존성을 구성합니다.
function(sw_configureAppDependencies TARGET_NAME)
	if(NOT TARGET ${TARGET_NAME})
		return()
	endif()

	# 1) 동적 모듈은 App 보다 먼저 빌드되어야 한다 — App 이 런타임에 로드하기 때문이다. 이름을 적지 않는다: 레지스트리가 답한다.
	sw_addDynamicModuleDependencies(${TARGET_NAME})
	sw_getDynamicModules(listDynamicModule)

	# 2) Shipping 은 게임을 정적으로 링크하고, App 이 서면 에셋을 쿠킹한다.
	#    (Dev 는 delay-load 라 링크하지 않는다 — 빌드 순서는 위 1) 이 이미 걸어 두었다.)
	#    쿠킹이 App 뒤인 이유: 씬 쿠킹이 App --cook-scenes 라서다. 반대로 걸면 깨끗한 트리에서 App 이
	#    없는 채로 쿠커가 돌아 죽는다(리눅스 CI). 기본 빌드(all)에 넣어 `cmake --build` 한 번이면 팩까지 선다.
	if(SW_SHIPPING_BUILD)
		if(TARGET SWGame)
			target_link_libraries(${TARGET_NAME} PRIVATE SWGame)
		endif()

		# 3) 리플렉션을 담은 정적 라이브러리(Engine · GameFramework · 킷 · 게임)는 통째로 링크한다. 생성된 *.gen.cpp 의 등록기는 파일 범위
		#    static 객체라(생성자가 전역 연결 리스트에 자신을 매단다) 밖에서 참조하는 심볼이 없고, 정적 링크에서 링커는 아무도 참조하지 않는
		#    오브젝트 파일을 버린다 — 그 타입 · 열거형은 등록되지 않고 씬의 컴포넌트는 MissingComponent 가 된다. 목록은 모듈 레지스트리이므로
		#    모든 모듈이 등록된 뒤인 여기서 읽는다. 플랫폼별 링커 플래그는 `sw_linkWholeArchive` 가 고른다.
		if(TARGET GameFramework AND NOT GameFramework IN_LIST listDynamicModule)
			message(FATAL_ERROR "[App] sw_configureAppDependencies ran before GameFramework registered itself — its reflection registrars would not be linked into ${TARGET_NAME}.")
		endif()
		set(listReflectionStaticLib Engine ${listDynamicModule})
		list(REMOVE_DUPLICATES listReflectionStaticLib)
		sw_linkWholeArchive(${TARGET_NAME} ${listReflectionStaticLib})

		# 쿠킹은 쿠커 실행 파일(App, 서버 타깃은 Server — SW_COOK_HOST_TARGET) 뒤에만 건다.
		if(TARGET CookAssets AND TARGET_NAME STREQUAL SW_COOK_HOST_TARGET)
			add_dependencies(CookAssets ${TARGET_NAME})
			set_target_properties(CookAssets PROPERTIES EXCLUDE_FROM_ALL FALSE)
		endif()
	endif()
endfunction()

# ------------------------------------------------------------------------------
# sw_embedProcessManifest — Windows 실행 파일에 프로세스 설정 매니페스트를 박는다
#   (UTF-8 ANSI 코드 페이지 · 긴 경로; cmake/Modules/Platform/WindowsProcess.manifest 설명)
#   .manifest 를 소스로 주면 CMake 가 링크 단계에서 CMAKE_MT 로 합쳐 넣는다. 실행 파일마다 부른다 — 앱만 켜고 테스트를
#   빼면 테스트가 앱과 다른 코드 페이지에서 돌아 경로 인코딩 결함을 볼 수 없다.
# ------------------------------------------------------------------------------
function(sw_embedProcessManifest TARGET_NAME)
	if(NOT WIN32)
		return()
	endif()
	target_sources(${TARGET_NAME} PRIVATE "${CMAKE_SOURCE_DIR}/cmake/Modules/Platform/WindowsProcess.manifest")
endfunction()

# sw_splitShippingDebugInfo — 리눅스 Shipping: 디버그 정보를 `Symbols/<이름>.debug` 로 떼고 실행 파일에는 `.gnu_debuglink` 만 남긴다
#   (배포물에 정보가 실리지 않는다). Windows 는 PDB 가 원래 따로다(`CMAKE_PDB_OUTPUT_DIRECTORY`).
# ------------------------------------------------------------------------------
function(sw_splitShippingDebugInfo TARGET_NAME)
	if(NOT SW_SHIPPING_BUILD OR WIN32 OR SW_RELEASE_DEBUG_INFO STREQUAL "none")
		return()
	endif()
	if(NOT CMAKE_OBJCOPY)
		message(FATAL_ERROR "sw_splitShippingDebugInfo(${TARGET_NAME}): CMAKE_OBJCOPY is not set - install llvm-objcopy or binutils")
	endif()
	set(symbolsDir "${CMAKE_BINARY_DIR}/Symbols")
	add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E make_directory "${symbolsDir}"
		COMMAND ${CMAKE_OBJCOPY} --only-keep-debug "$<TARGET_FILE:${TARGET_NAME}>" "${symbolsDir}/$<TARGET_FILE_NAME:${TARGET_NAME}>.debug"
		COMMAND ${CMAKE_OBJCOPY} --strip-debug "--add-gnu-debuglink=${symbolsDir}/$<TARGET_FILE_NAME:${TARGET_NAME}>.debug" "$<TARGET_FILE:${TARGET_NAME}>"
		COMMENT "Splitting debug info of ${TARGET_NAME} into Symbols/"
		VERBATIM)
endfunction()
