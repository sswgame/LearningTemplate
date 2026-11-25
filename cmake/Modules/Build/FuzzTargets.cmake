# ==============================================================================
# @file cmake/Modules/Build/FuzzTargets.cmake
# @brief 퍼저 실행 파일 하나와 그 스모크 테스트(`Fuzz.<이름>.Smoke`)를 만든다 — `sw_add_fuzz_target`
# ==============================================================================
#
# 크로미움 `fuzzer_test()` 꼴: 타깃마다 실행 파일 + 씨앗 말뭉치. 엔진은 공유 라이브러리 그대로 링크한다 — 정적 구성은 Shipping 뿐이라 정적 링크(크로미움 꼴)는
# 구성 하나를 더 유지해야 한다. 그 대신 실행 파일만 정적 아카이브 심볼(libFuzzer 의 `__sanitizer_cov_*` 콜백)을 내보낸다 — 대상 속성 `SW_FUZZ_TARGET` 이
# 리눅스 심볼 숨김(`--exclude-libs,ALL`, BuildLayout.cmake)을 이 타깃에서 뺀다. 실행 파일이 심볼 찾기 순서의 맨 앞이라 엔진 .so 의 계측이 ASan 의 약한
# 기본 구현이 아니라 libFuzzer 의 콜백에 묶인다. 빠지면 돌기는 하는데 커버리지가 실행 파일에만 닿는 조용한 결함이라, 스모크 테스트가 `Loaded N modules`
# (N ≥ 2 — 실행 파일 + 엔진 .so)를 요구한다.
#
# 말뭉치: 씨앗은 `IntegrationTests/Fuzz/Seeds/<이름>/`(커밋, 작은 것 몇 개 — 없으면 구성 오류), 자라는 말뭉치는 `<빌드>/Saved/Fuzz/<이름>/`(커밋하지 않는다).
# libFuzzer 는 첫 폴더에 새 입력을 쓰므로 자라는 말뭉치를 앞에 둔다 — 씨앗 폴더(소스 트리)를 더럽히지 않는다.

# sw_add_fuzz_target(<이름> SOURCES <소스…> LINK <타깃…> [MODULES <모듈 타깃…>])
#   MODULES 는 링크하지 않고 엔진 기동의 `ModuleTypes` 단계에서 순서대로 올릴 동적 모듈이다(`SW_FUZZ_MODULES`, IntegrationTests/Fuzz/FuzzEngineScope.cpp).
#   실행 파일 `Fuzz_<이름>`(`-fsanitize=fuzzer` 링크)과 CTest `Fuzz.<이름>.Smoke` 를 만든다. SW_ENABLE_FUZZING 구성에서만 부른다.
function(sw_add_fuzz_target FUZZ_NAME)
	cmake_parse_arguments(PARSE_ARGV 1 fuzz "" "" "SOURCES;LINK;MODULES")
	if(NOT SW_ENABLE_FUZZING)
		message(FATAL_ERROR "[Fuzz] sw_add_fuzz_target(${FUZZ_NAME}) needs SW_ENABLE_FUZZING (preset Linux-Fuzz)")
	endif()
	if(NOT fuzz_SOURCES)
		message(FATAL_ERROR "[Fuzz] sw_add_fuzz_target(${FUZZ_NAME}) needs SOURCES")
	endif()
	if(fuzz_UNPARSED_ARGUMENTS)
		message(FATAL_ERROR "[Fuzz] sw_add_fuzz_target(${FUZZ_NAME}) got unknown arguments: ${fuzz_UNPARSED_ARGUMENTS}")
	endif()
	set(seed_dir "${CMAKE_SOURCE_DIR}/IntegrationTests/Fuzz/Seeds/${FUZZ_NAME}")
	if(NOT IS_DIRECTORY "${seed_dir}")
		message(FATAL_ERROR "[Fuzz] sw_add_fuzz_target(${FUZZ_NAME}) needs seed inputs in ${seed_dir}")
	endif()

	set(target_name Fuzz_${FUZZ_NAME})
	sw_add_executable(${target_name} ${fuzz_SOURCES})
	target_link_libraries(${target_name} PRIVATE ${fuzz_LINK} sw_public_source_includes sw_global_options)
	target_link_options(${target_name} PRIVATE -fsanitize=fuzzer)
	target_compile_definitions(${target_name} PRIVATE "SW_LOG_TAG=\"${target_name}\"" "SW_REGISTRAR_MODULE=\"${target_name}\"")
	if(fuzz_MODULES)
		list(JOIN fuzz_MODULES "\",\"" module_text)
		target_compile_definitions(${target_name} PRIVATE "SW_FUZZ_MODULES=\"${module_text}\"")
		add_dependencies(${target_name} ${fuzz_MODULES}) # 이름으로 올리므로 링크 의존이 없다 — 실행 파일을 지으면 올릴 모듈도 선다
	endif()
	sw_configure_pch(${target_name} "${CMAKE_SOURCE_DIR}/Source/Engine/pch.h")
	set_target_properties(${target_name} PROPERTIES SW_FUZZ_TARGET TRUE FOLDER "IntegrationTests/Fuzz")

	set(corpus_dir "${CMAKE_BINARY_DIR}/Saved/Fuzz/${FUZZ_NAME}")
	file(MAKE_DIRECTORY "${corpus_dir}")
	add_test(NAME Fuzz.${FUZZ_NAME}.Smoke
		COMMAND $<TARGET_FILE:${target_name}> -runs=2000 "${corpus_dir}" "${seed_dir}"
		WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/Bin")
	# PASS_REGULAR_EXPRESSION 이 있으면 ctest 는 종료 코드를 보지 않는다 — 크래시 · 새니타이저 보고는 FAIL 식으로 따로 잡는다.
	set_tests_properties(Fuzz.${FUZZ_NAME}.Smoke PROPERTIES
		TIMEOUT 300
		PASS_REGULAR_EXPRESSION "Loaded ([2-9]|[1-9][0-9]+) modules"
		FAIL_REGULAR_EXPRESSION "ERROR: (AddressSanitizer|LeakSanitizer|libFuzzer)|runtime error:|deadly signal")
endfunction()
