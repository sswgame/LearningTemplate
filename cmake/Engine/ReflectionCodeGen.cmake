# ==============================================================================
# @file cmake/Engine/ReflectionCodeGen.cmake
# @brief ReflectionParser로 .gen.cpp를 생성하는 CMake 코드젠 파이프라인
# ==============================================================================

include("${CMAKE_CURRENT_LIST_DIR}/ReflectionMacroScan.cmake")

# ------------------------------------------------------------------------------
# 0) 파서 타깃이 없을 때 — SW_REQUIRE_REFLECTION 이면 구성을 세우고, 아니면 경고 후 이 단계를 건너뛴다(OUT_VAR = ON 이면 건너뛸 것).
# ------------------------------------------------------------------------------
function(sw_skipWithoutReflectionParser TARGET_NAME OUT_VAR)
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
function(sw_writeIfChanged PATH TEXT)
	file(WRITE "${PATH}.in" "${TEXT}\n")
	configure_file("${PATH}.in" "${PATH}" COPYONLY)
endfunction()

# 현재 소스 폴더를 훑어 반사 매크로가 있는 헤더(OUT_HEADERS)와 없는 헤더(OUT_OTHERS)로 가른다.
function(sw_scanReflectionHeaders OUT_HEADERS OUT_OTHERS TARGET_NAME EXCLUDE_REGEX)
	message(VERBOSE "[Reflection] No HEADERS specified for target: ${TARGET_NAME}, auto-scanning for REFLECT macros...")
	file(GLOB_RECURSE listCandidateHeader "${CMAKE_CURRENT_SOURCE_DIR}/*.h" "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp")
	# 하위 폴더의 다른 모듈(키트 · 게임의 Editor/ 확장)은 그 모듈이 따로 훑는다.
	if(EXCLUDE_REGEX)
		list(FILTER listCandidateHeader EXCLUDE REGEX "${EXCLUDE_REGEX}")
	endif()
	# 배포 구성은 개발 전용 소스(단위 테스트 `Tests/` 등, `SW_DEV_ONLY_SOURCE_PATTERNS`)를 짓지 않는다 — 그 폴더의 테스트용 반사 타입도 코드젠하지 않는다
	# (빼지 않으면 테스트 타입이 배포 이미지의 타입 목록에 오른다). 소스를 빼는 규칙과 같은 목록을 쓴다.
	if(SW_SHIPPING_BUILD)
		foreach(devOnlyPattern IN LISTS SW_DEV_ONLY_SOURCE_PATTERNS)
			list(FILTER listCandidateHeader EXCLUDE REGEX "${devOnlyPattern}")
		endforeach()
	endif()

	# 이 구성이 짓지 않는다고 적은 소스(`sw_excludeUnbuiltSources` · `sw_declareUnbuiltSources` — 헤더도 적힌다)는 코드젠하지 않는다.
	# 훑기는 타깃 소스 목록과 따로 glob 하므로, 빼는 규칙마다 여기에 같은 정규식을 다시 적으면 하나를 빠뜨린다(전용 서버의 게임 `Client/` 가 그랬다).
	get_property(listUnbuilt GLOBAL PROPERTY SW_UNBUILT_SOURCES)
	if(listUnbuilt)
		list(REMOVE_ITEM listCandidateHeader ${listUnbuilt})
	endif()

	set(listHeader "")
	set(listOther "")
	foreach(hdr IN LISTS listCandidateHeader)
		sw_hasReflectionMacro(bReflected "${hdr}")

		if(bReflected)
			list(APPEND listHeader "${hdr}")
		else()
			list(APPEND listOther "${hdr}")
		endif()
	endforeach()
	set(${OUT_HEADERS} ${listHeader} PARENT_SCOPE)
	set(${OUT_OTHERS} ${listOther} PARENT_SCOPE)
endfunction()

# 파서 인자(`sw_addReflectionStep` 의 ARG_* 와 경로 변수를 읽는다 — 그 함수에서만 부른다).
# 파서에 넘길 빌드 대상 인자(`--target-arg`) — 타깃 매크로(플랫폼 · 아키텍처 · 클라이언트/서버 코드)와, 교차 빌드면 트리플 · sysroot · 시스템 헤더.
# 파서는 호스트에서 지어 대상(웹 · Android)을 읽으므로 자기 매크로로 추측하지 않는다(언리얼 UHT 도 대상 정의를 받는다). 값은 컴파일러가 받는 것과 같은 출처다.
function(sw_collectReflectionTargetArgs OUT_VAR)
	set(listArg "")
	foreach(swFlagLib IN ITEMS sw_global_options ${sw_flag_libraries})
		get_target_property(listDef ${swFlagLib} INTERFACE_COMPILE_DEFINITIONS)
		if(NOT listDef)
			continue()
		endif()
		foreach(def IN LISTS listDef)
			if(def MATCHES "^SW_(PLATFORM_[A-Z]+|X64|ARM64|WASM32|WITH_[A-Z]+_CODE)$")
				list(APPEND listArg "-D${def}")
			endif()
		endforeach()
	endforeach()
	list(REMOVE_DUPLICATES listArg)
	if(CMAKE_CROSSCOMPILING)
		if(CMAKE_CXX_COMPILER_TARGET)
			list(APPEND listArg "--target=${CMAKE_CXX_COMPILER_TARGET}")
		elseif(EMSCRIPTEN)
			list(APPEND listArg "--target=wasm32-unknown-emscripten") # emcc 는 트리플을 CMake 에 알리지 않는다
		else()
			message(FATAL_ERROR "[Reflection] cross build without CMAKE_CXX_COMPILER_TARGET - the parser cannot read the target's headers")
		endif()
		if(CMAKE_SYSROOT)
			list(APPEND listArg "--sysroot=${CMAKE_SYSROOT}")
		endif()
		# 대상 컴파일러의 시스템 헤더. 컴파일러 내장 헤더(lib/clang/<판>)는 빼고 파서(libclang)의 것을 쓴다 — 두 판이 섞이면 내장 매크로가 어긋난다.
		foreach(dir IN LISTS CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES)
			if(NOT dir MATCHES "/lib/clang/[0-9]")
				list(APPEND listArg "-isystem${dir}")
			endif()
		endforeach()
	endif()
	set(${OUT_VAR} ${listArg} PARENT_SCOPE)
endfunction()

function(sw_makeReflectionParserArgs OUT_VAR TARGET_NAME)
	set(parserArgs "")

	foreach(header IN LISTS ARG_HEADERS)
		list(APPEND parserArgs "--input" "${header}")
	endforeach()

	list(APPEND parserArgs "--output" "${ARG_OUTPUT_DIR}")
	list(APPEND parserArgs "--builtins" "${swReflectBuiltins}")
	list(APPEND parserArgs "--annotation-meta" "${swAnnotationMeta}")
	list(APPEND parserArgs "--emit-templates" "${swEmitTemplatesDir}")

	list(APPEND parserArgs "--module" "${ARG_MODULE}")
	sw_collectReflectionTargetArgs(listTargetArg)
	if(NOT listTargetArg)
		message(FATAL_ERROR "[Reflection] no target macros found on sw_global_options / sw_flag_libraries (${TARGET_NAME})")
	endif()
	foreach(targetArg IN LISTS listTargetArg)
		list(APPEND parserArgs "--target-arg" "${targetArg}")
	endforeach()
	# 설정은 절대 경로로 넘긴다(파서가 작업 폴더 · 프로젝트 루트를 추측하지 않는다). 로컬 parser_config.json · toolchain_config.json 은 그 옆이다.
	list(APPEND parserArgs "--config" "${CMAKE_SOURCE_DIR}/${SW_DIR_CONFIG_ENV}/${SW_FILE_PARSER_DEFAULTS}")

	# 실행 파일과 게임 모듈의 타입은 모듈 경계를 넘지 않는다. 그래서 생성기가 서버 버스 본문 타입(`kBusVersion`)에 내보내기 매크로를 요구하지 않게 알린다.
	# 그 밖의 라이브러리(Base · 키트, STATIC 배포 포함)는 늘 검사한다. 정적 배포에서도 같은 헤더가 DLL 빌드로 쓰이기 때문이다.
	get_target_property(swTargetType ${TARGET_NAME} TYPE)
	if(ARG_MODULE_LOCAL OR swTargetType STREQUAL "EXECUTABLE")
		list(APPEND parserArgs "--module-local")
	endif()

	# 리플렉션 대상 헤더가 생성 헤더(sw/config/*.gen.h 등)를 include 할 수 있으므로 항상 넣어준다.
	# 없으면 libclang 파싱이 "file not found" 로 실패한다.
	if(EXISTS "${CMAKE_BINARY_DIR}/generated")
		list(APPEND parserArgs "--include" "${CMAKE_BINARY_DIR}/generated")
	endif()

	# 셰이더 바인딩 계약(binding_slots.hlsli)은 Engine 의 PUBLIC include 자리다 — 계약 수(shaderslot::k*)를 배열 크기로 쓰는 반사 헤더
	# (WaterBodyComponent 등)를 파서도 컴파일러와 같은 자리에서 읽게 한다.
	list(APPEND parserArgs "--include" "${CMAKE_SOURCE_DIR}/Content/engine/shaders")
	foreach(inc IN LISTS ARG_INCLUDES)
		list(APPEND parserArgs "--include" "${inc}")
	endforeach()
	set(${OUT_VAR} ${parserArgs} PARENT_SCOPE)
endfunction()

# 반사 헤더 이름 STEM 을 TARGET_NAME 의 것으로 적는다. 생성 파일 이름은 헤더 이름(확장자 뺀 것)뿐이다 — 같은 이름의 반사 헤더가 둘이면 같은 타깃에서는 서로의 .gen.cpp 를 덮어쓰고,
# 다른 타깃(코어 ↔ 모듈, 키트 ↔ 플러그인 에디터 등 gen 폴더를 나눠 쓰는 쌍)에서는 둘 다 include 경로에 있어 .gen.h 가 서로를 가린다.
# 어느 쌍이 include 경로를 공유하는지는 타깃 그래프에 달려 있어 여기서 가려내지 않는다 - 전역 레지스트리(stem → 소유 타깃 · 헤더)로 모든 쌍을 막는다.
# 예외 · 별칭은 없다: 걸리면 헤더 이름을 고친다. 레지스트리는 구성마다 비어 시작한다(전역 속성).
function(sw_claimReflectionStem STEM TARGET_NAME HEADER)
	get_property(stemOwner GLOBAL PROPERTY SW_REFLECT_GEN_STEM_${STEM})
	if(NOT stemOwner)
		set_property(GLOBAL PROPERTY SW_REFLECT_GEN_STEM_${STEM} "${TARGET_NAME}|${HEADER}")
		return()
	endif()
	string(REPLACE "|" ";" stemOwnerParts "${stemOwner}")
	list(GET stemOwnerParts 0 stemOwnerTarget)
	list(GET stemOwnerParts 1 stemOwnerHeader)
	if(NOT stemOwnerHeader STREQUAL HEADER)
		message(FATAL_ERROR "[Reflection] two reflected headers share the name '${STEM}' (${STEM}.gen.cpp / ${STEM}.gen.h would overwrite or shadow each other):\n  ${stemOwnerTarget}: ${stemOwnerHeader}\n  ${TARGET_NAME}: ${HEADER}\n  Rename one of them.")
	endif()
endfunction()

# 헤더마다의 생성 파일을 OUT_FILES(전부) · OUT_CPP_FILES(.gen.cpp) · OUT_META_FILES(.gen.meta.cpp — 개발 전용 에디터 메타데이터) 에 담고,
# 없는 것은 자리 표시자를 심는다 — target_sources 가 구성 때 붙일 파일이 있어야 한다.
function(sw_planReflectionOutputs OUT_FILES OUT_CPP_FILES OUT_META_FILES TARGET_NAME OUTPUT_DIR)
	set(listFile "")
	set(listCpp "")
	set(listMeta "")
	foreach(header IN LISTS ARGN)
		get_filename_component(stem "${header}" NAME_WE)
		sw_claimReflectionStem(${stem} ${TARGET_NAME} "${header}")
		set(genFile "${OUTPUT_DIR}/${stem}${SW_EXT_REFLECTION_GEN_CPP}")
		set(genHeader "${OUTPUT_DIR}/${stem}${SW_EXT_REFLECTION_GEN_HEADER}")
		set(genMeta "${OUTPUT_DIR}/${stem}${SW_EXT_REFLECTION_GEN_META_CPP}")
		list(APPEND listFile "${genFile}" "${genHeader}" "${genMeta}")
		list(APPEND listCpp "${genFile}")
		list(APPEND listMeta "${genMeta}")

		# 자리 표시자 = 스탬프 없음 — 자리 표시자를 심으면 그 입력의 스탬프를 지운다. 파서는 스탬프가 없는 입력을 다시 만들고, 생성 글을 읽어 자리 표시자인지 추측하지 않는다.
		if(NOT EXISTS "${genFile}" OR NOT EXISTS "${genHeader}" OR NOT EXISTS "${genMeta}")
			file(WRITE "${genFile}" "// AUTO-GENERATED placeholder — regenerated by ReflectionParser\n")
			file(WRITE "${genHeader}" "// AUTO-GENERATED placeholder\n#pragma once\n")
			file(WRITE "${genMeta}" "// AUTO-GENERATED placeholder — regenerated by ReflectionParser\n")
			file(REMOVE "${genFile}${SW_EXT_REFLECTION_STAMP}")
		endif()
	endforeach()
	set(${OUT_FILES} ${listFile} PARENT_SCOPE)
	set(${OUT_CPP_FILES} ${listCpp} PARENT_SCOPE)
	set(${OUT_META_FILES} ${listMeta} PARENT_SCOPE)
endfunction()

# 자동 훑기는 구성 때 굳는다. 매크로가 없던 헤더가 바뀌면 그것만 다시 읽어 매크로가 생겼는지 본다(바뀐 헤더가 없으면 돌지 않는다).
# 후보 검사는 타깃 컴파일을 막지 않는 별도 ALL 타깃이다. 예전에는 _ReflectionGen 에 묶여 평범한 헤더 하나만 고쳐도
# 수천 개 후보를 다시 훑는 동안 컴파일이 기다렸다. 계약: ALL 에 들어 있으므로 기본 빌드(all)에서 검사가 FATAL_ERROR 로
# 실패하면 빌드 전체가 실패한다(컴파일과 병렬로 돌 뿐 결과는 같다). `--target <타깃>` 만 지으면 검사는 돌지 않는다 —
# 그때 새 매크로 헤더는 링크의 미해결 기호로 드러난다. 같은 이름 반사 헤더 FATAL 은 구성 때 판정이라 영향이 없다.
function(sw_addReflectionCandidateCheck TARGET_NAME OUTPUT_DIR)
	if(NOT ARGN)
		return()
	endif()
	set(swReflectCandidateList "${OUTPUT_DIR}/ReflectionCandidates.list")
	string(REPLACE ";" "\n" swReflectCandidateText "${ARGN}")
	sw_writeIfChanged("${swReflectCandidateList}" "${swReflectCandidateText}")
	set(swReflectScanStamp "${OUTPUT_DIR}/ReflectionCandidates.stamp")
	add_custom_command(
		OUTPUT "${swReflectScanStamp}"
		COMMAND ${CMAKE_COMMAND} "-DCANDIDATE_LIST=${swReflectCandidateList}" "-DSTAMP=${swReflectScanStamp}" "-DTARGET_NAME=${TARGET_NAME}"
			-P "${CMAKE_SOURCE_DIR}/cmake/Engine/CheckReflectionCandidates.cmake"
		DEPENDS ${ARGN} "${swReflectCandidateList}" "${CMAKE_SOURCE_DIR}/cmake/Engine/CheckReflectionCandidates.cmake"
		COMMENT "[Reflection] Checking ${TARGET_NAME} headers for new reflection macros"
		VERBATIM
	)
	add_custom_target(${TARGET_NAME}_ReflectionCandidateCheck ALL DEPENDS "${swReflectScanStamp}")
endfunction()

# ------------------------------------------------------------------------------
# 1) sw_addReflectionStep — ENUM() 헤더 → .gen.cpp / .gen.h
# HEADERS / INCLUDES / OUTPUT_DIR. .gen.cpp는 Unity 배치에서 제외 (ODR)
# 루트가 Tools(ReflectionParser) 이후 Engine을 호출해야 이 스텝이 살아 있음
# MODULE_LOCAL: 이 타깃의 타입을 다른 모듈이 링크하지 않는다(게임 모듈). 실행 파일 타깃은 저절로 이렇게 본다.
# MODULE(필수): 타입이 오르는 런타임 모듈 이름 — 런타임이 이 이름으로 타입을 올리고 내린다. 경로 글자로 추측하지 않는다(타입 → 모듈 판정은 여기 하나).
# CONTENT_OWNER: 콘텐츠 모듈 검사(`sw_addContentModuleCheck`)에서 이 타입들의 주인 — 비우면 모든 호스트가 가진 것(엔진 · 실행 파일), 게임은 `game:<폴더>`.
# ------------------------------------------------------------------------------
function(sw_addReflectionStep TARGET_NAME)
	cmake_parse_arguments(ARG "MODULE_LOCAL" "OUTPUT_DIR;EXCLUDE_REGEX;MODULE;CONTENT_OWNER" "HEADERS;INCLUDES" ${ARGN})

	sw_skipWithoutReflectionParser(${TARGET_NAME} swSkip)
	if(swSkip)
		return()
	endif()
	if(NOT ARG_MODULE)
		message(FATAL_ERROR "[Reflection] sw_addReflectionStep(${TARGET_NAME}): MODULE <런타임 모듈 이름> 이 없다 — 타입의 모듈은 부르는 쪽이 정한다")
	endif()

	if(NOT ARG_OUTPUT_DIR)
		set(ARG_OUTPUT_DIR "${CMAKE_BINARY_DIR}/generated/${TARGET_NAME}")
	endif()

	if(NOT ARG_HEADERS)
		sw_scanReflectionHeaders(ARG_HEADERS listNonReflectHeader ${TARGET_NAME} "${ARG_EXCLUDE_REGEX}")

		if(NOT ARG_HEADERS)
			message(VERBOSE "[Reflection] Auto-scan found no headers with REFLECT macros for target: ${TARGET_NAME}")
			return()
		endif()
	endif()

	set(swReflectBuiltins "${CMAKE_SOURCE_DIR}/${SW_FILE_REFLECT_BUILTINS}")
	set(swAnnotationMeta "${CMAKE_SOURCE_DIR}/${SW_FILE_ANNOTATION_META}")
	set(swEmitTemplatesDir "${CMAKE_SOURCE_DIR}/${SW_DIR_TOOLS_REFLECTION_TEMPLATES}")
	file(GLOB swEmitTpls "${swEmitTemplatesDir}/*${SW_EXT_REFLECTION_TEMPLATE}")
	sw_makeReflectionParserArgs(parserArgs ${TARGET_NAME})

	# 파서가 런타임에 읽는 설정. 바뀌면 clang 인자/SDK 경로가 달라지므로 재생성해야 함.
	set(swParserConfigs "")

	foreach(cfg
		"${CMAKE_SOURCE_DIR}/${SW_DIR_CONFIG_ENV}/${SW_FILE_PARSER_CONFIG}"
		"${CMAKE_SOURCE_DIR}/${SW_DIR_CONFIG_ENV}/${SW_FILE_PARSER_DEFAULTS}"
		"${CMAKE_SOURCE_DIR}/${SW_DIR_CONFIG_ENV}/${SW_FILE_TOOLCHAIN_CONFIG}")
		if(EXISTS "${cfg}")
			list(APPEND swParserConfigs "${cfg}")
		endif()
	endforeach()

	file(MAKE_DIRECTORY "${ARG_OUTPUT_DIR}")
	sw_planReflectionOutputs(generatedFiles generatedCppFiles generatedMetaFiles ${TARGET_NAME} "${ARG_OUTPUT_DIR}" ${ARG_HEADERS})

	set(swFlagOpsHeader "${ARG_OUTPUT_DIR}/${SW_FILE_REFLECTION_FLAG_OPS}")
	list(APPEND generatedFiles "${swFlagOpsHeader}")
	# 대상의 타입 목록(`<FQN>\t<모듈>`) — 콘텐츠 모듈 검사가 읽는다. 파서가 실행마다 스탬프에서 다시 모아 쓴다.
	set(swReflectedTypesList "${ARG_OUTPUT_DIR}/${SW_FILE_REFLECTED_TYPES_LIST}")
	list(APPEND generatedFiles "${swReflectedTypesList}")
	set_property(GLOBAL APPEND PROPERTY SW_REFLECTED_TYPE_LISTS "${ARG_CONTENT_OWNER}|${TARGET_NAME}|${swReflectedTypesList}")
	# 대상의 경로 칸 목록(`<이름>\t<대상>`) — 참조 칸 생성 목록(`sw_addAssetReferenceFieldList`)이 모은다.
	set(swAssetFieldsList "${ARG_OUTPUT_DIR}/${SW_FILE_ASSET_REFERENCE_FIELDS_LIST}")
	list(APPEND generatedFiles "${swAssetFieldsList}")
	set_property(GLOBAL APPEND PROPERTY SW_ASSET_REFERENCE_FIELD_LISTS "${ARG_CONTENT_OWNER}|${TARGET_NAME}|${swAssetFieldsList}")

	if(NOT EXISTS "${swFlagOpsHeader}")
		file(WRITE "${swFlagOpsHeader}" "// AUTO-GENERATED placeholder\n#pragma once\n#if !defined(__REFLECT_PARSER__)\n// no ENUM(Flags) in this target\n#endif\n")
	endif()

	# 헤더를 옮기기만 하면(내용은 그대로) mtime 이 안 바뀌어 파서가 다시 돌지 않고,
	# 옛 경로를 #include 한 .gen.cpp 가 그대로 남아 빌드가 깨진다. 입력 경로 목록을
	# 스탬프로 남겨 의존에 넣는다 — 내용이 같으면 COPYONLY 가 파일을 건드리지 않는다.
	set(swReflectInputList "${ARG_OUTPUT_DIR}/ReflectionInputs.list")
	string(REPLACE ";" "\n" swReflectInputText "${ARG_HEADERS}")
	sw_writeIfChanged("${swReflectInputList}" "${swReflectInputText}")

	# 반사된 헤더가 include 한 **반사되지 않은** 헤더(예: `using ScoreList = …`)가 바뀌어도 이 단계가 다시 돌게 한다. 파서가 실행마다
	# include 목록을 이 파일에 쓰고 ninja 가 읽는다. DEPENDS 가 반사된 헤더뿐이면 생성 코드가 낡은 타입 정보로 남는다.
	set(swReflectDepfile "${ARG_OUTPUT_DIR}/ReflectionParser.d")

	# 인자는 응답 파일(줄마다 하나)로 넘긴다 — 입력 헤더가 많고 경로가 길면 명령 줄이 Windows 한도(32767 자)를 넘는다.
	# 내용이 같으면 COPYONLY 가 파일을 건드리지 않아 불필요하게 다시 돌지 않는다.
	set(swReflectResponseFile "${ARG_OUTPUT_DIR}/ReflectionParser.rsp")
	set(swReflectResponseArgs ${parserArgs} "--depfile" "${swReflectDepfile}")
	string(REPLACE ";" "\n" swReflectResponseText "${swReflectResponseArgs}")
	sw_writeIfChanged("${swReflectResponseFile}" "${swReflectResponseText}")

	add_custom_command(
		OUTPUT ${generatedFiles}
		COMMAND ${CMAKE_COMMAND} -E make_directory "${ARG_OUTPUT_DIR}"
		COMMAND "$<TARGET_FILE:ReflectionParser>" "@${swReflectResponseFile}"
		DEPFILE "${swReflectDepfile}"
		DEPENDS ${ARG_HEADERS} "${swReflectInputList}" "${swReflectResponseFile}" ReflectionParser "$<TARGET_FILE:ReflectionParser>"
		"${swReflectBuiltins}" "${swAnnotationMeta}" ${swEmitTpls}
		${swParserConfigs}
		COMMENT "[Reflection] Running ReflectionParser for target: ${TARGET_NAME}"
		VERBATIM
	)

	target_sources(${TARGET_NAME} PRIVATE ${generatedCppFiles})
	set_source_files_properties(${generatedCppFiles} PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
	# 에디터 메타데이터(표시 · 편집 필드)는 개발 전용 단위다 — 배포 구성은 짓지 않아 런타임 메타데이터의 배치가 구성마다 같다(언리얼 WITH_METADATA 자리를 빌드 단위로).
	# 파서는 구성과 무관하게 같은 산출물을 쓰고, 고르는 것은 여기 한 곳이다.
	if(NOT SW_SHIPPING_BUILD)
		target_sources(${TARGET_NAME} PRIVATE ${generatedMetaFiles})
		set_source_files_properties(${generatedMetaFiles} PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
	endif()
	target_include_directories(${TARGET_NAME} PUBLIC "${ARG_OUTPUT_DIR}")

	# ENUM(Flags) 트레이트 우산은 **조건 없이** 강제 include 한다.
	#
	# 주의: 여기서 `ENUM( Flags` 를 정규식으로 훑어 조건부로 붙이지 말 것. 파서는 `AnnotationMeta.txt` 의 동의어를
	# 전부 받으므로(Flags · BitFlag · FLAG · Bitwise) 철자 하나가 다르면 우산은 만들어지는데 `/FI` 는 안 붙어
	# "invalid operands to binary expression" 으로 깨진다 — 같은 판정을 두 곳이 따로 내리면 안 된다.
	# 플래그가 없는 타깃의 우산은 사실상 빈 파일이라 붙여도 비용이 없다.
	if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
		target_compile_options(${TARGET_NAME} PUBLIC "/FI${swFlagOpsHeader}")
	else()
		target_compile_options(${TARGET_NAME} PUBLIC "SHELL:-include ${swFlagOpsHeader}")
	endif()

	sw_addReflectionCandidateCheck(${TARGET_NAME} "${ARG_OUTPUT_DIR}" ${listNonReflectHeader})
	add_custom_target(${TARGET_NAME}_ReflectionGen DEPENDS ${generatedFiles})
	add_dependencies(${TARGET_NAME} ReflectionParser ${TARGET_NAME}_ReflectionGen)

	# 타깃마다 두 줄씩 configure 출력을 메우지 않는다 — `cmake --log-level=VERBOSE` 로 다시 본다.
	list(LENGTH ARG_HEADERS headerCount)
	message(VERBOSE "[Reflection] ${TARGET_NAME}: ${headerCount} headers -> ${ARG_OUTPUT_DIR}")
endfunction()

# ------------------------------------------------------------------------------
# 2) sw_addReflectBuiltinsGen — Engine/Reflection/ReflectBuiltins.xxx → TypeRegistrar .gen.cpp
# Engine 전용. Unity 배치에서 제외
# ------------------------------------------------------------------------------
function(sw_addReflectBuiltinsGen TARGET_NAME)
	sw_skipWithoutReflectionParser(${TARGET_NAME} swSkip)
	if(swSkip)
		return()
	endif()

	set(swReflectBuiltins "${CMAKE_SOURCE_DIR}/${SW_FILE_REFLECT_BUILTINS}")
	set(swEmitTemplatesDir "${CMAKE_SOURCE_DIR}/${SW_DIR_TOOLS_REFLECTION_TEMPLATES}")
	file(GLOB swBuiltinTpls
		"${swEmitTemplatesDir}/${SW_FILE_TPL_BUILTIN_HEADER}"
		"${swEmitTemplatesDir}/${SW_FILE_TPL_BUILTIN_REGISTRAR}"
		"${swEmitTemplatesDir}/${SW_FILE_TPL_BUILTIN_FOOTER}")
	set(outDir "${CMAKE_BINARY_DIR}/generated/${TARGET_NAME}")
	set(genFile "${outDir}/ReflectBuiltins${SW_EXT_REFLECTION_GEN_CPP}")
	file(MAKE_DIRECTORY "${outDir}")

	# 플레이스홀더는 target_sources 가 설정 시점에 붙일 파일이 있어야 해서 쓴다. 그런데 이걸
	# 그냥 쓰면 **출력이 이미 존재하고 의존물보다 새것**이 되어 ninja 가 커스텀 커맨드를 영영
	# 돌리지 않는다 — 생성 파일을 지우면 플레이스홀더가 최종 산출물로 굳어 링크가
	# `forceLinkBuiltinTypes` 미정의로 깨진다(리컨피그해도 같은 덫이 다시 놓인다).
	# 그래서 스탬프를 함께 출력물로 두고, 플레이스홀더를 새로 쓴 경우엔 스탬프를 지운다(sw_addReflectionStep 과 같은 규칙: 자리 표시자 = 스탬프 없음).
	set(swBuiltinStamp "${genFile}${SW_EXT_REFLECTION_STAMP}")

	if(NOT EXISTS "${genFile}")
		file(WRITE "${genFile}" "// AUTO-GENERATED placeholder — regenerated by ReflectionParser\n")
		file(REMOVE "${swBuiltinStamp}")
	endif()

	add_custom_command(
		OUTPUT "${genFile}" "${swBuiltinStamp}"
		COMMAND ${CMAKE_COMMAND} -E make_directory "${outDir}"
		COMMAND "$<TARGET_FILE:ReflectionParser>"
		"--builtins" "${swReflectBuiltins}"
		"--emit-templates" "${swEmitTemplatesDir}"
		"--module" "${TARGET_NAME}"
		"--emit-builtins-gen" "${genFile}"
		COMMAND ${CMAKE_COMMAND} -E touch "${swBuiltinStamp}"
		DEPENDS ReflectionParser "${swReflectBuiltins}" ${swBuiltinTpls}
		COMMENT "[Reflection] Emitting ReflectBuiltins.gen.cpp for ${TARGET_NAME}"
		VERBATIM
	)

	target_sources(${TARGET_NAME} PRIVATE "${genFile}")
	add_custom_target(${TARGET_NAME}_ReflectBuiltinsGen DEPENDS "${swBuiltinStamp}")
	add_dependencies(${TARGET_NAME} ${TARGET_NAME}_ReflectBuiltinsGen)
	set_target_properties(${TARGET_NAME}_ReflectBuiltinsGen PROPERTIES FOLDER "Reflection")
	set_source_files_properties("${genFile}" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
	target_include_directories(${TARGET_NAME} PRIVATE "${outDir}")
	add_dependencies(${TARGET_NAME} ReflectionParser)
	message(VERBOSE "[Reflection] Builtins TypeRegistrar gen -> ${genFile}")
endfunction()

# ------------------------------------------------------------------------------
# 3) sw_addContentModuleCheck — 콘텐츠(씬 · 프리팹)가 쓰는 타입의 모듈을 그 도메인이 가지는지 빌드 단계에서 본다
# 타입 → 모듈은 대상마다 파서가 쓴 `ReflectedTypes.list` 만 읽는다(예전에는 configure 가 헤더를 정규식으로 훑어 주석 · 템플릿의 REFLECT 도 셌다).
# 모든 리플렉션 단계가 선 뒤(루트 CMakeLists 끝)에 한 번 부른다. 입력 목록은 `<주인>|<타깃>|<목록 경로>` 를 줄마다 적은 파일이다.
# ------------------------------------------------------------------------------
function(sw_addContentModuleCheck)
	get_property(listEntry GLOBAL PROPERTY SW_REFLECTED_TYPE_LISTS)
	if(NOT listEntry)
		return()
	endif()
	set(listInputLine "")
	set(listTypeList "")
	set(listGenTarget "")
	foreach(entry IN LISTS listEntry)
		string(REPLACE "|" ";" entryParts "${entry}")
		list(GET entryParts 0 owner)
		list(GET entryParts 1 target)
		list(GET entryParts 2 typeList)
		list(APPEND listInputLine "${owner}\t${typeList}")
		list(APPEND listTypeList "${typeList}")
		list(APPEND listGenTarget "${target}_ReflectionGen")
	endforeach()
	set(swInputs "${CMAKE_BINARY_DIR}/generated/ContentModuleInputs.list")
	string(REPLACE ";" "\n" swInputText "${listInputLine}")
	sw_writeIfChanged("${swInputs}" "${swInputText}")
	set(swStamp "${CMAKE_BINARY_DIR}/generated/ContentModules.stamp")
	add_custom_command(
		OUTPUT "${swStamp}"
		COMMAND "${Python3_EXECUTABLE}" -X utf8 "${CMAKE_SOURCE_DIR}/Scripts/generate/ContentModules.py" --inputs "${swInputs}" --stamp "${swStamp}"
		DEPENDS ${listTypeList} "${swInputs}" "${CMAKE_SOURCE_DIR}/Scripts/generate/ContentModules.py"
		COMMENT "[Reflection] Checking that content uses only modules its domain has"
		VERBATIM
	)
	# 콘텐츠를 고친 뒤에는 목록이 바뀌거나 다시 구성할 때 본다(씬마다 의존을 걸면 저장할 때마다 검사가 돈다 - 예전 configure 검사와 같은 범위).
	add_custom_target(ContentModuleCheck ALL DEPENDS "${swStamp}")
	add_dependencies(ContentModuleCheck ${listGenTarget})
	set_target_properties(ContentModuleCheck PROPERTIES FOLDER "Reflection")
endfunction()

# ------------------------------------------------------------------------------
# 4) sw_addAssetReferenceFieldList — 파서가 쓴 경로 칸 목록을 커밋하는 표(`Config/Dev/AssetReferenceFields.generated.json`)로 모은다
# 커밋 전 린트와 `move-asset` 은 빌드 없이 돌아 빌드 폴더를 읽을 수 없다. 그래서 표를 저장소에 두고 빌드가 주인(이 구성이 짓는 대상)의 줄만 갈아 끼운다(게임은 구성마다 하나라 다른 게임의 줄은 그대로다).
# 낡음은 `App --check-asset-refs`(ToolCheckAssetRefs)가 리플렉션과 표를 대조해 잡는다.
# ------------------------------------------------------------------------------
function(sw_addAssetReferenceFieldList)
	# 배포 구성은 개발 전용 소스(`SW_DEV_ONLY_SOURCE_PATTERNS` — Tests/ · Dev/)를 짓지 않아 그 형식의 칸을 모른다 — 표를 다시 쓰면 그 칸이 빠져 구성마다 표가 갈린다.
	# 표는 클라이언트 개발 구성(검사 `ToolCheckAssetRefs` 가 도는 곳)만 쓴다 — 서버 구성은 렌더 칸을 짓지 않아 그 줄을 지운다.
	if(SW_SHIPPING_BUILD OR SW_TARGET_TYPE STREQUAL "Server")
		return()
	endif()
	get_property(listEntry GLOBAL PROPERTY SW_ASSET_REFERENCE_FIELD_LISTS)
	if(NOT listEntry)
		return()
	endif()
	set(listInputLine "")
	set(listFieldList "")
	set(listGenTarget "")
	foreach(entry IN LISTS listEntry)
		string(REPLACE "|" ";" entryParts "${entry}")
		list(GET entryParts 0 owner)
		list(GET entryParts 1 target)
		list(GET entryParts 2 fieldList)
		list(APPEND listInputLine "${owner}\t${fieldList}")
		list(APPEND listFieldList "${fieldList}")
		list(APPEND listGenTarget "${target}_ReflectionGen")
	endforeach()
	set(swInputs "${CMAKE_BINARY_DIR}/generated/AssetReferenceFieldInputs.list")
	string(REPLACE ";" "\n" swInputText "${listInputLine}")
	sw_writeIfChanged("${swInputs}" "${swInputText}")
	set(swStamp "${CMAKE_BINARY_DIR}/generated/AssetReferenceFieldList.stamp")
	add_custom_command(
		OUTPUT "${swStamp}"
		COMMAND "${Python3_EXECUTABLE}" -X utf8 "${CMAKE_SOURCE_DIR}/Scripts/generate/AssetReferenceFieldList.py" --inputs "${swInputs}" --output "${CMAKE_SOURCE_DIR}/Config/Dev/AssetReferenceFields.generated.json" --stamp "${swStamp}"
		DEPENDS ${listFieldList} "${swInputs}" "${CMAKE_SOURCE_DIR}/Scripts/generate/AssetReferenceFieldList.py"
		COMMENT "[Reflection] Collecting asset reference fields"
		VERBATIM
	)
	add_custom_target(AssetReferenceFieldList ALL DEPENDS "${swStamp}")
	add_dependencies(AssetReferenceFieldList ${listGenTarget})
	set_target_properties(AssetReferenceFieldList PROPERTIES FOLDER "Reflection")
endfunction()
