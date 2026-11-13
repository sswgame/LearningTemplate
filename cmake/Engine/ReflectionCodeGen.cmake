# ==============================================================================
# @file cmake/Engine/ReflectionCodeGen.cmake
# @brief ReflectionParser로 .gen.cpp를 생성하는 CMake 코드젠 파이프라인
# ==============================================================================

include("${CMAKE_CURRENT_LIST_DIR}/ReflectionMacroScan.cmake")

# ------------------------------------------------------------------------------
# 0) 파서 타깃이 없을 때 — SW_REQUIRE_REFLECTION 이면 구성을 세우고, 아니면 경고 후 이 단계를 건너뛴다(OUT_VAR = ON 이면 건너뛸 것).
# ------------------------------------------------------------------------------
function(sw_skip_without_reflection_parser TARGET_NAME OUT_VAR)
	if(TARGET ReflectionParser)
		set(${OUT_VAR} OFF PARENT_SCOPE)
		return()
	endif()
	if(SW_REQUIRE_REFLECTION)
		message(FATAL_ERROR
			"[Reflection] ReflectionParser target missing but SW_REQUIRE_REFLECTION=ON (${TARGET_NAME}).\n"
			"  Install LLVM/libclang (Config/Environment/toolchain_config.json llvm_path) or set -DSW_REQUIRE_REFLECTION=OFF.")
	endif()
	message(WARNING "[Reflection] ReflectionParser target not found. Skipping reflection for: ${TARGET_NAME}")
	set(${OUT_VAR} ON PARENT_SCOPE)
endfunction()

# ------------------------------------------------------------------------------
# 내용이 달라졌을 때만 PATH 를 건드린다(같으면 mtime 이 그대로라 그 파일에 건 단계가 다시 돌지 않는다).
# ------------------------------------------------------------------------------
function(sw_write_if_changed PATH TEXT)
	file(WRITE "${PATH}.in" "${TEXT}\n")
	configure_file("${PATH}.in" "${PATH}" COPYONLY)
endfunction()

# 현재 소스 폴더를 훑어 반사 매크로가 있는 헤더(OUT_HEADERS)와 없는 헤더(OUT_OTHERS)로 가른다.
function(sw_scan_reflection_headers OUT_HEADERS OUT_OTHERS TARGET_NAME EXCLUDE_REGEX)
	message(VERBOSE "[Reflection] No HEADERS specified for target: ${TARGET_NAME}, auto-scanning for REFLECT macros...")
	sw_glob_source_tree(list_candidate_header RECURSE "${CMAKE_CURRENT_SOURCE_DIR}/*.h" "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp")
	# 하위 폴더의 다른 모듈(키트 · 게임의 Editor/ 확장)은 그 모듈이 따로 훑는다.
	if(EXCLUDE_REGEX)
		list(FILTER list_candidate_header EXCLUDE REGEX "${EXCLUDE_REGEX}")
	endif()
	# 배포 구성은 개발 전용 소스(단위 테스트 `Tests/` 등, `SW_DEV_ONLY_SOURCE_PATTERNS`)를 짓지 않는다 — 그 폴더의 테스트용 반사 타입도 코드젠하지 않는다
	# (빼지 않으면 테스트 타입이 배포 이미지의 타입 목록에 오른다). 소스를 빼는 규칙과 같은 목록을 쓴다.
	if(SW_SHIPPING_BUILD)
		foreach(dev_only_pattern IN LISTS SW_DEV_ONLY_SOURCE_PATTERNS)
			list(FILTER list_candidate_header EXCLUDE REGEX "${dev_only_pattern}")
		endforeach()
	endif()

	# 이 구성이 짓지 않는다고 적은 소스(`sw_remove_excluded_sources` · `sw_declare_excluded_sources` — 헤더도 적힌다)는 코드젠하지 않는다.
	# 훑기는 타깃 소스 목록과 따로 glob 하므로, 빼는 규칙마다 여기에 같은 정규식을 다시 적으면 하나를 빠뜨린다(전용 서버의 게임 `Client/` 가 그랬다).
	get_property(list_excluded GLOBAL PROPERTY SW_EXCLUDED_SOURCES)
	if(list_excluded)
		list(REMOVE_ITEM list_candidate_header ${list_excluded})
	endif()

	set(list_header "")
	set(list_other "")
	foreach(hdr IN LISTS list_candidate_header)
		sw_has_reflection_macro(reflected "${hdr}")

		if(reflected)
			list(APPEND list_header "${hdr}")
		else()
			list(APPEND list_other "${hdr}")
		endif()
	endforeach()
	set(${OUT_HEADERS} ${list_header} PARENT_SCOPE)
	set(${OUT_OTHERS} ${list_other} PARENT_SCOPE)
endfunction()

# 파서 인자(`sw_add_reflection_step` 의 ARG_* 와 경로 변수를 읽는다 — 그 함수에서만 부른다).
# 파서에 넘길 빌드 대상 인자(`--target-arg`) — 타깃 매크로(플랫폼 · 아키텍처 · 클라이언트/서버 코드)와, 교차 빌드면 트리플 · sysroot · 시스템 헤더.
# 파서는 호스트에서 지어 대상(웹 · Android)을 읽으므로 자기 매크로로 추측하지 않는다(언리얼 UHT 도 대상 정의를 받는다). 값은 컴파일러가 받는 것과 같은 출처다.
# 교차 컴파일러의 `#include <...>` 검색 목록을 `-E -v` 로 읽어 내장 헤더(lib/clang/<판>) 앞 · 뒤로 나눈다. 구성마다 한 번(캐시 변수).
function(sw_query_compiler_system_includes OUT_BEFORE OUT_AFTER)
	if(NOT DEFINED SW_CROSS_SYSTEM_INCLUDES_BEFORE)
		set(list_query_arg -E -v -x c++)
		if(CMAKE_CXX_COMPILER_TARGET)
			list(APPEND list_query_arg "--target=${CMAKE_CXX_COMPILER_TARGET}")
		endif()
		if(CMAKE_SYSROOT)
			list(APPEND list_query_arg "--sysroot=${CMAKE_SYSROOT}")
		endif()
		file(WRITE "${CMAKE_BINARY_DIR}/CMakeFiles/swIncludeProbe.cpp" "")
		execute_process(
			COMMAND "${CMAKE_CXX_COMPILER}" ${list_query_arg} "${CMAKE_BINARY_DIR}/CMakeFiles/swIncludeProbe.cpp"
			OUTPUT_QUIET ERROR_VARIABLE probe_output RESULT_VARIABLE probe_result)
		if(NOT probe_result EQUAL 0)
			message(FATAL_ERROR "[Reflection] '${CMAKE_CXX_COMPILER} -E -v' failed (${probe_result}) - the parser cannot learn the target's include order:\n${probe_output}")
		endif()
		string(REPLACE "\\" "/" probe_output "${probe_output}")
		string(REGEX MATCH "#include <\\.\\.\\.> search starts here:\n(.*)\nEnd of search list" _ "${probe_output}")
		if(NOT CMAKE_MATCH_1)
			message(FATAL_ERROR "[Reflection] no include search list in '${CMAKE_CXX_COMPILER} -E -v' output:\n${probe_output}")
		endif()
		string(REPLACE "\n" ";" list_line "${CMAKE_MATCH_1}")
		set(list_before "")
		set(list_after "")
		set(after_builtin FALSE)
		foreach(line IN LISTS list_line)
			string(STRIP "${line}" dir)
			if(dir STREQUAL "")
				continue()
			endif()
			if(dir MATCHES "/lib/clang/[0-9]")
				set(after_builtin TRUE)
			elseif(after_builtin)
				list(APPEND list_after "${dir}")
			else()
				list(APPEND list_before "${dir}")
			endif()
		endforeach()
		set(SW_CROSS_SYSTEM_INCLUDES_BEFORE "${list_before}" CACHE INTERNAL "cross compiler system includes before the builtin headers")
		set(SW_CROSS_SYSTEM_INCLUDES_AFTER "${list_after}" CACHE INTERNAL "cross compiler system includes after the builtin headers")
	endif()
	set(${OUT_BEFORE} "${SW_CROSS_SYSTEM_INCLUDES_BEFORE}" PARENT_SCOPE)
	set(${OUT_AFTER} "${SW_CROSS_SYSTEM_INCLUDES_AFTER}" PARENT_SCOPE)
endfunction()

function(sw_collect_reflection_target_args OUT_VAR)
	set(list_arg "")
	foreach(flag_lib IN ITEMS sw_global_options ${SW_FLAG_LIBRARIES})
		get_target_property(list_def ${flag_lib} INTERFACE_COMPILE_DEFINITIONS)
		if(NOT list_def)
			continue()
		endif()
		foreach(def IN LISTS list_def)
			if(def MATCHES "^SW_(PLATFORM_[A-Z]+|X64|ARM64|WASM32|WITH_[A-Z]+_CODE)$")
				list(APPEND list_arg "-D${def}")
			endif()
		endforeach()
	endforeach()
	list(REMOVE_DUPLICATES list_arg)
	if(CMAKE_CROSSCOMPILING)
		if(CMAKE_CXX_COMPILER_TARGET)
			list(APPEND list_arg "--target=${CMAKE_CXX_COMPILER_TARGET}")
		elseif(EMSCRIPTEN)
			list(APPEND list_arg "--target=wasm32-unknown-emscripten") # emcc 는 트리플을 CMake 에 알리지 않는다
		else()
			message(FATAL_ERROR "[Reflection] cross build without CMAKE_CXX_COMPILER_TARGET - the parser cannot read the target's headers")
		endif()
		if(CMAKE_SYSROOT)
			list(APPEND list_arg "--sysroot=${CMAKE_SYSROOT}")
		endif()
		# 대상 컴파일러의 시스템 헤더 검색 순서를 컴파일러에게 직접 묻는다 — CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES 는 순서를 바꾸고 일부를 뺀다
		# (Emscripten 의 include/compat 이 빠지고 C 헤더가 libc++ 앞에 와서 파서가 <cstdio> 의 짝을 못 찾았다).
		# 컴파일러 내장 헤더(lib/clang/<판>)는 빼고 파서(libclang)의 것을 쓴다 — 두 판이 섞이면 내장 매크로가 어긋난다.
		# 내장 헤더 앞의 폴더는 -isystem, 뒤의 폴더는 -idirafter 로 넘겨 컴파일러와 같은 순서가 되게 한다.
		sw_query_compiler_system_includes(list_before list_after)
		foreach(dir IN LISTS list_before)
			list(APPEND list_arg "-isystem${dir}")
		endforeach()
		foreach(dir IN LISTS list_after)
			list(APPEND list_arg "-idirafter${dir}")
		endforeach()
	endif()
	set(${OUT_VAR} ${list_arg} PARENT_SCOPE)
endfunction()

function(sw_make_reflection_parser_args OUT_VAR TARGET_NAME)
	set(parser_args "")

	foreach(header IN LISTS ARG_HEADERS)
		list(APPEND parser_args "--input" "${header}")
	endforeach()

	list(APPEND parser_args "--output" "${ARG_OUTPUT_DIR}")
	list(APPEND parser_args "--builtins" "${reflect_builtins}")
	list(APPEND parser_args "--annotation-meta" "${annotation_meta}")
	list(APPEND parser_args "--emit-templates" "${emit_templates_dir}")

	list(APPEND parser_args "--module" "${ARG_MODULE}")
	sw_collect_reflection_target_args(list_target_arg)
	if(NOT list_target_arg)
		message(FATAL_ERROR "[Reflection] no target macros found on sw_global_options / SW_FLAG_LIBRARIES (${TARGET_NAME})")
	endif()
	foreach(target_arg IN LISTS list_target_arg)
		list(APPEND parser_args "--target-arg" "${target_arg}")
	endforeach()
	# 설정은 절대 경로로 넘긴다(파서가 작업 폴더 · 프로젝트 루트를 추측하지 않는다). 로컬 parser_config.json · toolchain_config.json 은 그 옆이다.
	list(APPEND parser_args "--config" "${CMAKE_SOURCE_DIR}/${SW_DIR_CONFIG_ENV}/${SW_FILE_PARSER_DEFAULTS}")

	# 실행 파일과 게임 모듈의 타입은 모듈 경계를 넘지 않는다. 그래서 생성기가 서버 버스 본문 타입(`kBusVersion`)에 내보내기 매크로를 요구하지 않게 알린다.
	# 그 밖의 라이브러리(Base · 키트, STATIC 배포 포함)는 늘 검사한다. 정적 배포에서도 같은 헤더가 DLL 빌드로 쓰이기 때문이다.
	get_target_property(target_type ${TARGET_NAME} TYPE)
	if(ARG_MODULE_LOCAL OR target_type STREQUAL "EXECUTABLE")
		list(APPEND parser_args "--module-local")
	endif()

	# 리플렉션 대상 헤더가 생성 헤더(sw/config/*.gen.h 등)를 include 할 수 있으므로 항상 넣어준다.
	# 없으면 libclang 파싱이 "file not found" 로 실패한다.
	if(EXISTS "${CMAKE_BINARY_DIR}/generated")
		list(APPEND parser_args "--include" "${CMAKE_BINARY_DIR}/generated")
	endif()

	# 셰이더 바인딩 계약(binding_slots.hlsli)은 Engine 의 PUBLIC include 자리다 — 계약 수(shaderslot::k*)를 배열 크기로 쓰는 반사 헤더
	# (WaterBodyComponent 등)를 파서도 컴파일러와 같은 자리에서 읽게 한다.
	list(APPEND parser_args "--include" "${CMAKE_SOURCE_DIR}/Content/engine/shaders")
	foreach(inc IN LISTS ARG_INCLUDES)
		list(APPEND parser_args "--include" "${inc}")
	endforeach()
	set(${OUT_VAR} ${parser_args} PARENT_SCOPE)
endfunction()

# 반사 헤더 이름 STEM 을 TARGET_NAME 의 것으로 적는다. 생성 파일 이름은 헤더 이름(확장자 뺀 것)뿐이다 — 같은 이름의 반사 헤더가 둘이면 같은 타깃에서는 서로의 .gen.cpp 를 덮어쓰고,
# 다른 타깃(코어 ↔ 모듈, 키트 ↔ 플러그인 에디터 등 gen 폴더를 나눠 쓰는 쌍)에서는 둘 다 include 경로에 있어 .gen.h 가 서로를 가린다.
# 어느 쌍이 include 경로를 공유하는지는 타깃 그래프에 달려 있어 여기서 가려내지 않는다 - 전역 레지스트리(stem → 소유 타깃 · 헤더)로 모든 쌍을 막는다.
# 예외 · 별칭은 없다: 걸리면 헤더 이름을 고친다. 레지스트리는 구성마다 비어 시작한다(전역 속성).
function(sw_claim_reflection_stem STEM TARGET_NAME HEADER)
	get_property(stem_owner GLOBAL PROPERTY SW_REFLECT_GEN_STEM_${STEM})
	if(NOT stem_owner)
		set_property(GLOBAL PROPERTY SW_REFLECT_GEN_STEM_${STEM} "${TARGET_NAME}|${HEADER}")
		return()
	endif()
	string(REPLACE "|" ";" stem_owner_parts "${stem_owner}")
	list(GET stem_owner_parts 0 stem_owner_target)
	list(GET stem_owner_parts 1 stem_owner_header)
	if(NOT stem_owner_header STREQUAL HEADER)
		message(FATAL_ERROR "[Reflection] two reflected headers share the name '${STEM}' (${STEM}.gen.cpp / ${STEM}.gen.h would overwrite or shadow each other):\n  ${stem_owner_target}: ${stem_owner_header}\n  ${TARGET_NAME}: ${HEADER}\n  Rename one of them.")
	endif()
endfunction()

# 생성 TU 묶음 하나에 넣는 헤더 수(NEXT 5-4 W-R5). 헤더당 TU 하나면 PCH 를 헤더 수만큼 열고 같은 템플릿을 TU 마다 인스턴스화한다 —
# 생성 TU 가 Debug 컴파일 CPU 의 약 30 % 였다(build_time_report.py). 언리얼 UHT 가 모듈의 .gen.cpp 를 Module.<이름>.N.cpp 묶음으로 짓는 자리와 같다.
# 크게 잡을수록 TU 수가 줄지만 한 헤더 변경이 다시 짓는 양이 는다 — 8~16 의 가운데.
set(SW_REFLECTION_BUNDLE_SIZE 12)

# @p FILES 를 SW_REFLECTION_BUNDLE_SIZE 개씩 묶은 TU(`<OUTPUT_DIR>/<PREFIX><번호>.cpp`, 생성 파일을 #include 만 한다)를 구성 때 쓰고 OUT_BUNDLES 에 담는다.
# 묶음은 자기가 include 하는 생성 파일에 기대 다시 지어진다(OBJECT_DEPENDS — 첫 빌드에도 생성이 먼저 돈다). 남는 옛 묶음 파일은 소스에 들지 않아 무해하다.
function(sw_write_reflection_bundles OUT_BUNDLES OUTPUT_DIR PREFIX)
	set(list_bundle "")
	set(list_member "")
	set(bundle_count 0)
	list(LENGTH ARGN file_count)
	set(file_index 0)
	foreach(gen_file IN LISTS ARGN)
		list(APPEND list_member "${gen_file}")
		math(EXPR file_index "${file_index} + 1")
		list(LENGTH list_member member_count)
		if(member_count EQUAL SW_REFLECTION_BUNDLE_SIZE OR file_index EQUAL file_count)
			set(bundle_file "${OUTPUT_DIR}/${PREFIX}${bundle_count}.cpp")
			set(bundle_text "// AUTO-GENERATED -- reflection bundle (cmake/Engine/ReflectionCodeGen.cmake)")
			foreach(member IN LISTS list_member)
				string(APPEND bundle_text "\n#include \"${member}\"")
			endforeach()
			sw_write_if_changed("${bundle_file}" "${bundle_text}")
			set_source_files_properties("${bundle_file}" PROPERTIES OBJECT_DEPENDS "${list_member}")
			list(APPEND list_bundle "${bundle_file}")
			math(EXPR bundle_count "${bundle_count} + 1")
			set(list_member "")
		endif()
	endforeach()
	set(${OUT_BUNDLES} ${list_bundle} PARENT_SCOPE)
endfunction()

# 헤더마다의 생성 파일을 OUT_FILES(전부) 에 담고, 컴파일할 묶음 TU 를 OUT_CPP_FILES(.gen.cpp 묶음) · OUT_META_FILES(.gen.meta.cpp 묶음 — 개발 전용 에디터 메타데이터) 에 담는다.
# 없는 생성 파일은 자리 표시자를 심는다 — 묶음이 구성 때 include 할 파일이 있어야 한다.
function(sw_plan_reflection_outputs OUT_FILES OUT_CPP_FILES OUT_META_FILES TARGET_NAME OUTPUT_DIR)
	set(list_file "")
	set(list_cpp "")
	set(list_meta "")
	foreach(header IN LISTS ARGN)
		get_filename_component(stem "${header}" NAME_WE)
		sw_claim_reflection_stem(${stem} ${TARGET_NAME} "${header}")
		set(gen_file "${OUTPUT_DIR}/${stem}${SW_EXT_REFLECTION_GEN_CPP}")
		set(gen_header "${OUTPUT_DIR}/${stem}${SW_EXT_REFLECTION_GEN_HEADER}")
		set(gen_meta "${OUTPUT_DIR}/${stem}${SW_EXT_REFLECTION_GEN_META_CPP}")
		list(APPEND list_file "${gen_file}" "${gen_header}" "${gen_meta}")
		list(APPEND list_cpp "${gen_file}")
		list(APPEND list_meta "${gen_meta}")

		# 자리 표시자 = 스탬프 없음 — 자리 표시자를 심으면 그 입력의 스탬프를 지운다. 파서는 스탬프가 없는 입력을 다시 만들고, 생성 글을 읽어 자리 표시자인지 추측하지 않는다.
		if(NOT EXISTS "${gen_file}" OR NOT EXISTS "${gen_header}" OR NOT EXISTS "${gen_meta}")
			file(WRITE "${gen_file}" "// AUTO-GENERATED placeholder — regenerated by ReflectionParser\n")
			file(WRITE "${gen_header}" "// AUTO-GENERATED placeholder\n#pragma once\n")
			file(WRITE "${gen_meta}" "// AUTO-GENERATED placeholder — regenerated by ReflectionParser\n")
			file(REMOVE "${gen_file}${SW_EXT_REFLECTION_STAMP}")
		endif()
	endforeach()
	sw_write_reflection_bundles(list_cpp_bundle "${OUTPUT_DIR}" "ReflectionBundle." ${list_cpp})
	sw_write_reflection_bundles(list_meta_bundle "${OUTPUT_DIR}" "ReflectionMetaBundle." ${list_meta})
	set(${OUT_FILES} ${list_file} PARENT_SCOPE)
	set(${OUT_CPP_FILES} ${list_cpp_bundle} PARENT_SCOPE)
	set(${OUT_META_FILES} ${list_meta_bundle} PARENT_SCOPE)
endfunction()

# 자동 훑기는 구성 때 굳는다. 매크로가 없던 헤더가 바뀌면 그것만 다시 읽어 매크로가 생겼는지 본다(바뀐 헤더가 없으면 돌지 않는다).
# 후보 검사는 타깃 컴파일을 막지 않는 별도 ALL 타깃이다. 예전에는 _ReflectionGen 에 묶여 평범한 헤더 하나만 고쳐도
# 수천 개 후보를 다시 훑는 동안 컴파일이 기다렸다. 계약: ALL 에 들어 있으므로 기본 빌드(all)에서 검사가 FATAL_ERROR 로
# 실패하면 빌드 전체가 실패한다(컴파일과 병렬로 돌 뿐 결과는 같다). `--target <타깃>` 만 지으면 검사는 돌지 않는다 —
# 그때 새 매크로 헤더는 링크의 미해결 기호로 드러난다. 같은 이름 반사 헤더 FATAL 은 구성 때 판정이라 영향이 없다.
function(sw_add_reflection_candidate_check TARGET_NAME OUTPUT_DIR)
	if(NOT ARGN)
		return()
	endif()
	set(reflect_candidate_list "${OUTPUT_DIR}/ReflectionCandidates.list")
	string(REPLACE ";" "\n" reflect_candidate_text "${ARGN}")
	sw_write_if_changed("${reflect_candidate_list}" "${reflect_candidate_text}")
	set(reflect_scan_stamp "${OUTPUT_DIR}/ReflectionCandidates.stamp")
	add_custom_command(
		OUTPUT "${reflect_scan_stamp}"
		COMMAND ${CMAKE_COMMAND} "-DCANDIDATE_LIST=${reflect_candidate_list}" "-DSTAMP=${reflect_scan_stamp}" "-DTARGET_NAME=${TARGET_NAME}"
			-P "${CMAKE_SOURCE_DIR}/cmake/Engine/CheckReflectionCandidates.cmake"
		DEPENDS ${ARGN} "${reflect_candidate_list}" "${CMAKE_SOURCE_DIR}/cmake/Engine/CheckReflectionCandidates.cmake"
		COMMENT "[Reflection] Checking ${TARGET_NAME} headers for new reflection macros"
		VERBATIM
	)
	add_custom_target(${TARGET_NAME}_ReflectionCandidateCheck ALL DEPENDS "${reflect_scan_stamp}")
endfunction()

# ------------------------------------------------------------------------------
# 1) sw_add_reflection_step — ENUM() 헤더 → .gen.cpp / .gen.h
# HEADERS / INCLUDES / OUTPUT_DIR. .gen.cpp는 Unity 배치에서 제외 (ODR)
# 루트가 Tools(ReflectionParser) 이후 Engine을 호출해야 이 스텝이 살아 있음
# MODULE_LOCAL: 이 타깃의 타입을 다른 모듈이 링크하지 않는다(게임 모듈). 실행 파일 타깃은 저절로 이렇게 본다.
# MODULE(필수): 타입이 오르는 런타임 모듈 이름 — 런타임이 이 이름으로 타입을 올리고 내린다. 경로 글자로 추측하지 않는다(타입 → 모듈 판정은 여기 하나).
# CONTENT_OWNER: 콘텐츠 모듈 검사(`sw_add_content_module_check`)에서 이 타입들의 주인 — 비우면 모든 호스트가 가진 것(엔진 · 실행 파일), 게임은 `game:<폴더>`.
# ------------------------------------------------------------------------------
function(sw_add_reflection_step TARGET_NAME)
	cmake_parse_arguments(ARG "MODULE_LOCAL" "OUTPUT_DIR;EXCLUDE_REGEX;MODULE;CONTENT_OWNER" "HEADERS;INCLUDES" ${ARGN})

	sw_skip_without_reflection_parser(${TARGET_NAME} skip)
	if(skip)
		return()
	endif()
	if(NOT ARG_MODULE)
		message(FATAL_ERROR "[Reflection] sw_add_reflection_step(${TARGET_NAME}): MODULE <런타임 모듈 이름> 이 없다 — 타입의 모듈은 부르는 쪽이 정한다")
	endif()

	if(NOT ARG_OUTPUT_DIR)
		set(ARG_OUTPUT_DIR "${CMAKE_BINARY_DIR}/generated/${TARGET_NAME}")
	endif()

	if(NOT ARG_HEADERS)
		sw_scan_reflection_headers(ARG_HEADERS list_non_reflect_header ${TARGET_NAME} "${ARG_EXCLUDE_REGEX}")

		if(NOT ARG_HEADERS)
			message(VERBOSE "[Reflection] Auto-scan found no headers with REFLECT macros for target: ${TARGET_NAME}")
			return()
		endif()
	endif()

	set(reflect_builtins "${CMAKE_SOURCE_DIR}/${SW_FILE_REFLECT_BUILTINS}")
	set(annotation_meta "${CMAKE_SOURCE_DIR}/${SW_FILE_ANNOTATION_META}")
	set(emit_templates_dir "${CMAKE_SOURCE_DIR}/${SW_DIR_TOOLS_REFLECTION_TEMPLATES}")
	file(GLOB emit_tpls "${emit_templates_dir}/*${SW_EXT_REFLECTION_TEMPLATE}")
	sw_make_reflection_parser_args(parser_args ${TARGET_NAME})

	# 파서가 런타임에 읽는 설정. 바뀌면 clang 인자/SDK 경로가 달라지므로 재생성해야 함.
	set(parser_configs "")

	foreach(cfg
		"${CMAKE_SOURCE_DIR}/${SW_DIR_CONFIG_ENV}/${SW_FILE_PARSER_CONFIG}"
		"${CMAKE_SOURCE_DIR}/${SW_DIR_CONFIG_ENV}/${SW_FILE_PARSER_DEFAULTS}"
		"${CMAKE_SOURCE_DIR}/${SW_DIR_CONFIG_ENV}/${SW_FILE_TOOLCHAIN_CONFIG}")
		if(EXISTS "${cfg}")
			list(APPEND parser_configs "${cfg}")
		endif()
	endforeach()

	file(MAKE_DIRECTORY "${ARG_OUTPUT_DIR}")
	sw_plan_reflection_outputs(generated_files generated_cpp_files generated_meta_files ${TARGET_NAME} "${ARG_OUTPUT_DIR}" ${ARG_HEADERS})

	set(flag_ops_header "${ARG_OUTPUT_DIR}/${SW_FILE_REFLECTION_FLAG_OPS}")
	list(APPEND generated_files "${flag_ops_header}")
	# 대상의 타입 목록(`<FQN>\t<모듈>`) — 콘텐츠 모듈 검사가 읽는다. 파서가 실행마다 스탬프에서 다시 모아 쓴다.
	set(reflected_types_list "${ARG_OUTPUT_DIR}/${SW_FILE_REFLECTED_TYPES_LIST}")
	list(APPEND generated_files "${reflected_types_list}")
	set_property(GLOBAL APPEND PROPERTY SW_REFLECTED_TYPE_LISTS "${ARG_CONTENT_OWNER}|${TARGET_NAME}|${reflected_types_list}")
	# 대상의 경로 칸 목록(`<이름>\t<대상>`) — 참조 칸 생성 목록(`sw_add_asset_reference_field_list`)이 모은다.
	set(asset_fields_list "${ARG_OUTPUT_DIR}/${SW_FILE_ASSET_REFERENCE_FIELDS_LIST}")
	list(APPEND generated_files "${asset_fields_list}")
	set_property(GLOBAL APPEND PROPERTY SW_ASSET_REFERENCE_FIELD_LISTS "${ARG_CONTENT_OWNER}|${TARGET_NAME}|${asset_fields_list}")

	if(NOT EXISTS "${flag_ops_header}")
		file(WRITE "${flag_ops_header}" "// AUTO-GENERATED placeholder\n#pragma once\n#if !defined(__REFLECT_PARSER__)\n// no ENUM(Flags) in this target\n#endif\n")
	endif()

	# 헤더를 옮기기만 하면(내용은 그대로) mtime 이 안 바뀌어 파서가 다시 돌지 않고,
	# 옛 경로를 #include 한 .gen.cpp 가 그대로 남아 빌드가 깨진다. 입력 경로 목록을
	# 스탬프로 남겨 의존에 넣는다 — 내용이 같으면 COPYONLY 가 파일을 건드리지 않는다.
	set(reflect_input_list "${ARG_OUTPUT_DIR}/ReflectionInputs.list")
	string(REPLACE ";" "\n" reflect_input_text "${ARG_HEADERS}")
	sw_write_if_changed("${reflect_input_list}" "${reflect_input_text}")

	# 반사된 헤더가 include 한 **반사되지 않은** 헤더(예: `using ScoreList = …`)가 바뀌어도 이 단계가 다시 돌게 한다. 파서가 실행마다
	# include 목록을 이 파일에 쓰고 ninja 가 읽는다. DEPENDS 가 반사된 헤더뿐이면 생성 코드가 낡은 타입 정보로 남는다.
	set(reflect_depfile "${ARG_OUTPUT_DIR}/ReflectionParser.d")

	# 인자는 응답 파일(줄마다 하나)로 넘긴다 — 입력 헤더가 많고 경로가 길면 명령 줄이 Windows 한도(32767 자)를 넘는다.
	# 내용이 같으면 COPYONLY 가 파일을 건드리지 않아 불필요하게 다시 돌지 않는다.
	set(reflect_response_file "${ARG_OUTPUT_DIR}/ReflectionParser.rsp")
	set(reflect_response_args ${parser_args} "--depfile" "${reflect_depfile}")
	string(REPLACE ";" "\n" reflect_response_text "${reflect_response_args}")
	sw_write_if_changed("${reflect_response_file}" "${reflect_response_text}")

	add_custom_command(
		OUTPUT ${generated_files}
		COMMAND ${CMAKE_COMMAND} -E make_directory "${ARG_OUTPUT_DIR}"
		COMMAND "$<TARGET_FILE:ReflectionParser>" "@${reflect_response_file}"
		DEPFILE "${reflect_depfile}"
		DEPENDS ${ARG_HEADERS} "${reflect_input_list}" "${reflect_response_file}" ReflectionParser "$<TARGET_FILE:ReflectionParser>"
		"${reflect_builtins}" "${annotation_meta}" ${emit_tpls}
		${parser_configs}
		COMMENT "[Reflection] Running ReflectionParser for target: ${TARGET_NAME}"
		VERBATIM
	)

	target_sources(${TARGET_NAME} PRIVATE ${generated_cpp_files})
	set_source_files_properties(${generated_cpp_files} PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
	# 에디터 메타데이터(표시 · 편집 필드)는 개발 전용 단위다 — 배포 구성은 짓지 않아 런타임 메타데이터의 배치가 구성마다 같다(언리얼 WITH_METADATA 자리를 빌드 단위로).
	# 파서는 구성과 무관하게 같은 산출물을 쓰고, 고르는 것은 여기 한 곳이다.
	if(NOT SW_SHIPPING_BUILD)
		target_sources(${TARGET_NAME} PRIVATE ${generated_meta_files})
		set_source_files_properties(${generated_meta_files} PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
	endif()
	target_include_directories(${TARGET_NAME} PUBLIC "${ARG_OUTPUT_DIR}")

	# ENUM(Flags) 트레이트 우산은 **조건 없이** 강제 include 한다.
	#
	# 주의: 여기서 `ENUM( Flags` 를 정규식으로 훑어 조건부로 붙이지 말 것. 파서는 `AnnotationMeta.txt` 의 동의어를
	# 전부 받으므로(Flags · BitFlag · FLAG · Bitwise) 철자 하나가 다르면 우산은 만들어지는데 `/FI` 는 안 붙어
	# "invalid operands to binary expression" 으로 깨진다 — 같은 판정을 두 곳이 따로 내리면 안 된다.
	# 플래그가 없는 타깃의 우산은 사실상 빈 파일이라 붙여도 비용이 없다.
	if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
		target_compile_options(${TARGET_NAME} PUBLIC "/FI${flag_ops_header}")
	else()
		target_compile_options(${TARGET_NAME} PUBLIC "SHELL:-include ${flag_ops_header}")
	endif()

	sw_add_reflection_candidate_check(${TARGET_NAME} "${ARG_OUTPUT_DIR}" ${list_non_reflect_header})
	add_custom_target(${TARGET_NAME}_ReflectionGen DEPENDS ${generated_files})
	if(TARGET SwHostReflectionParser)
		add_dependencies(${TARGET_NAME}_ReflectionGen SwHostReflectionParser) # 교차 빌드 — 호스트 파서를 먼저 다시 짓는다(HostTools.cmake)
	endif()
	add_dependencies(${TARGET_NAME} ReflectionParser ${TARGET_NAME}_ReflectionGen)

	# 타깃마다 두 줄씩 configure 출력을 메우지 않는다 — `cmake --log-level=VERBOSE` 로 다시 본다.
	list(LENGTH ARG_HEADERS header_count)
	message(VERBOSE "[Reflection] ${TARGET_NAME}: ${header_count} headers -> ${ARG_OUTPUT_DIR}")
endfunction()

# ------------------------------------------------------------------------------
# 2) sw_add_reflect_builtins_gen — Engine/Reflection/ReflectBuiltins.xxx → TypeRegistrar .gen.cpp
# Engine 전용. Unity 배치에서 제외
# ------------------------------------------------------------------------------
function(sw_add_reflect_builtins_gen TARGET_NAME)
	sw_skip_without_reflection_parser(${TARGET_NAME} skip)
	if(skip)
		return()
	endif()

	set(reflect_builtins "${CMAKE_SOURCE_DIR}/${SW_FILE_REFLECT_BUILTINS}")
	set(emit_templates_dir "${CMAKE_SOURCE_DIR}/${SW_DIR_TOOLS_REFLECTION_TEMPLATES}")
	file(GLOB builtin_tpls
		"${emit_templates_dir}/${SW_FILE_TPL_BUILTIN_HEADER}"
		"${emit_templates_dir}/${SW_FILE_TPL_BUILTIN_REGISTRAR}"
		"${emit_templates_dir}/${SW_FILE_TPL_BUILTIN_FOOTER}")
	set(out_dir "${CMAKE_BINARY_DIR}/generated/${TARGET_NAME}")
	set(gen_file "${out_dir}/ReflectBuiltins${SW_EXT_REFLECTION_GEN_CPP}")
	file(MAKE_DIRECTORY "${out_dir}")

	# 플레이스홀더는 target_sources 가 설정 시점에 붙일 파일이 있어야 해서 쓴다. 그런데 이걸
	# 그냥 쓰면 **출력이 이미 존재하고 의존물보다 새것**이 되어 ninja 가 커스텀 커맨드를 영영
	# 돌리지 않는다 — 생성 파일을 지우면 플레이스홀더가 최종 산출물로 굳어 링크가
	# `forceLinkBuiltinTypes` 미정의로 깨진다(리컨피그해도 같은 덫이 다시 놓인다).
	# 그래서 스탬프를 함께 출력물로 두고, 플레이스홀더를 새로 쓴 경우엔 스탬프를 지운다(sw_add_reflection_step 과 같은 규칙: 자리 표시자 = 스탬프 없음).
	set(builtin_stamp "${gen_file}${SW_EXT_REFLECTION_STAMP}")

	if(NOT EXISTS "${gen_file}")
		file(WRITE "${gen_file}" "// AUTO-GENERATED placeholder — regenerated by ReflectionParser\n")
		file(REMOVE "${builtin_stamp}")
	endif()

	add_custom_command(
		OUTPUT "${gen_file}" "${builtin_stamp}"
		COMMAND ${CMAKE_COMMAND} -E make_directory "${out_dir}"
		COMMAND "$<TARGET_FILE:ReflectionParser>"
		"--builtins" "${reflect_builtins}"
		"--emit-templates" "${emit_templates_dir}"
		"--module" "${TARGET_NAME}"
		"--emit-builtins-gen" "${gen_file}"
		COMMAND ${CMAKE_COMMAND} -E touch "${builtin_stamp}"
		DEPENDS ReflectionParser "${reflect_builtins}" ${builtin_tpls}
		COMMENT "[Reflection] Emitting ReflectBuiltins.gen.cpp for ${TARGET_NAME}"
		VERBATIM
	)

	target_sources(${TARGET_NAME} PRIVATE "${gen_file}")
	add_custom_target(${TARGET_NAME}_ReflectBuiltinsGen DEPENDS "${builtin_stamp}")
	if(TARGET SwHostReflectionParser)
		add_dependencies(${TARGET_NAME}_ReflectBuiltinsGen SwHostReflectionParser) # 교차 빌드 — 호스트 파서를 먼저 다시 짓는다
	endif()
	add_dependencies(${TARGET_NAME} ${TARGET_NAME}_ReflectBuiltinsGen)
	set_target_properties(${TARGET_NAME}_ReflectBuiltinsGen PROPERTIES FOLDER "Reflection")
	set_source_files_properties("${gen_file}" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
	target_include_directories(${TARGET_NAME} PRIVATE "${out_dir}")
	add_dependencies(${TARGET_NAME} ReflectionParser)
	message(VERBOSE "[Reflection] Builtins TypeRegistrar gen -> ${gen_file}")
endfunction()

# ------------------------------------------------------------------------------
# 3) sw_add_content_module_check — 콘텐츠(씬 · 프리팹)가 쓰는 타입의 모듈을 그 도메인이 가지는지 빌드 단계에서 본다
# 타입 → 모듈은 대상마다 파서가 쓴 `ReflectedTypes.list` 만 읽는다(예전에는 configure 가 헤더를 정규식으로 훑어 주석 · 템플릿의 REFLECT 도 셌다).
# 모든 리플렉션 단계가 선 뒤(루트 CMakeLists 끝)에 한 번 부른다. 입력 목록은 `<주인>|<타깃>|<목록 경로>` 를 줄마다 적은 파일이다.
# ------------------------------------------------------------------------------
function(sw_add_content_module_check)
	get_property(list_entry GLOBAL PROPERTY SW_REFLECTED_TYPE_LISTS)
	if(NOT list_entry)
		return()
	endif()
	set(list_input_line "")
	set(list_type_list "")
	set(list_gen_target "")
	foreach(entry IN LISTS list_entry)
		string(REPLACE "|" ";" entry_parts "${entry}")
		list(GET entry_parts 0 owner)
		list(GET entry_parts 1 target)
		list(GET entry_parts 2 type_list)
		list(APPEND list_input_line "${owner}\t${type_list}")
		list(APPEND list_type_list "${type_list}")
		list(APPEND list_gen_target "${target}_ReflectionGen")
	endforeach()
	set(inputs "${CMAKE_BINARY_DIR}/generated/ContentModuleInputs.list")
	string(REPLACE ";" "\n" input_text "${list_input_line}")
	sw_write_if_changed("${inputs}" "${input_text}")
	set(stamp "${CMAKE_BINARY_DIR}/generated/ContentModules.stamp")
	add_custom_command(
		OUTPUT "${stamp}"
		COMMAND "${Python3_EXECUTABLE}" -X utf8 "${CMAKE_SOURCE_DIR}/Scripts/generate/content_modules.py" --inputs "${inputs}" --stamp "${stamp}"
		DEPENDS ${list_type_list} "${inputs}" "${CMAKE_SOURCE_DIR}/Scripts/generate/content_modules.py"
		COMMENT "[Reflection] Checking that content uses only modules its domain has"
		VERBATIM
	)
	# 콘텐츠를 고친 뒤에는 목록이 바뀌거나 다시 구성할 때 본다(씬마다 의존을 걸면 저장할 때마다 검사가 돈다 - 예전 configure 검사와 같은 범위).
	add_custom_target(ContentModuleCheck ALL DEPENDS "${stamp}")
	add_dependencies(ContentModuleCheck ${list_gen_target})
	set_target_properties(ContentModuleCheck PROPERTIES FOLDER "Reflection")
endfunction()

# ------------------------------------------------------------------------------
# 4) sw_add_asset_reference_field_list — 파서가 쓴 경로 칸 목록을 커밋하는 표(`Config/Dev/AssetReferenceFields.generated.json`)로 모은다
# 커밋 전 린트와 `move-asset` 은 빌드 없이 돌아 빌드 폴더를 읽을 수 없다. 그래서 표를 저장소에 두고 빌드가 주인(이 구성이 짓는 대상)의 줄만 갈아 끼운다(게임은 구성마다 하나라 다른 게임의 줄은 그대로다).
# 낡음은 `App --check-asset-refs`(ToolCheckAssetRefs)가 리플렉션과 표를 대조해 잡는다.
# ------------------------------------------------------------------------------
function(sw_add_asset_reference_field_list)
	# 배포 구성은 개발 전용 소스(`SW_DEV_ONLY_SOURCE_PATTERNS` — Tests/ · Dev/)를 짓지 않아 그 형식의 칸을 모른다 — 표를 다시 쓰면 그 칸이 빠져 구성마다 표가 갈린다.
	# 표는 클라이언트 개발 구성(검사 `ToolCheckAssetRefs` 가 도는 곳)만 쓴다 — 서버 구성은 렌더 칸을 짓지 않아 그 줄을 지운다.
	if(SW_SHIPPING_BUILD OR SW_TARGET_TYPE STREQUAL "Server")
		return()
	endif()
	get_property(list_entry GLOBAL PROPERTY SW_ASSET_REFERENCE_FIELD_LISTS)
	if(NOT list_entry)
		return()
	endif()
	set(list_input_line "")
	set(list_field_list "")
	set(list_gen_target "")
	foreach(entry IN LISTS list_entry)
		string(REPLACE "|" ";" entry_parts "${entry}")
		list(GET entry_parts 0 owner)
		list(GET entry_parts 1 target)
		list(GET entry_parts 2 field_list)
		list(APPEND list_input_line "${owner}\t${field_list}")
		list(APPEND list_field_list "${field_list}")
		list(APPEND list_gen_target "${target}_ReflectionGen")
	endforeach()
	set(inputs "${CMAKE_BINARY_DIR}/generated/AssetReferenceFieldInputs.list")
	string(REPLACE ";" "\n" input_text "${list_input_line}")
	sw_write_if_changed("${inputs}" "${input_text}")
	set(stamp "${CMAKE_BINARY_DIR}/generated/AssetReferenceFieldList.stamp")
	add_custom_command(
		OUTPUT "${stamp}"
		COMMAND "${Python3_EXECUTABLE}" -X utf8 "${CMAKE_SOURCE_DIR}/Scripts/generate/asset_reference_field_list.py" --inputs "${inputs}" --output "${CMAKE_SOURCE_DIR}/Config/Dev/AssetReferenceFields.generated.json" --stamp "${stamp}"
		DEPENDS ${list_field_list} "${inputs}" "${CMAKE_SOURCE_DIR}/Scripts/generate/asset_reference_field_list.py"
		COMMENT "[Reflection] Collecting asset reference fields"
		VERBATIM
	)
	add_custom_target(AssetReferenceFieldList ALL DEPENDS "${stamp}")
	add_dependencies(AssetReferenceFieldList ${list_gen_target})
	set_target_properties(AssetReferenceFieldList PROPERTIES FOLDER "Reflection")
endfunction()
