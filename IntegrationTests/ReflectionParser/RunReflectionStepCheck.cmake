# `sw_add_reflection_step` 의 구성 · 생성 경로를 작은 프로젝트(IntegrationTests/ReflectionParser/ReflectionStep)로 구성하고 산출물을 본다(cmake -P).
# 왜: 자리 표시자 · OBJECT_DEPENDS · 후보 검사 명령 · 타깃 응답 파일은 구성 때만 돌아 파서 사례가 지나지 않는다(file(GENERATE) 인자 꼴 오류가 모든 구성을 멈춘 뒤에야 보였다).
# 입력: SOURCE_ROOT · WORK_DIR · GENERATED_CMAKE_VARS · GENERATOR · MAKE_PROGRAM · CXX_COMPILER
foreach(required SOURCE_ROOT WORK_DIR GENERATED_CMAKE_VARS GENERATOR MAKE_PROGRAM CXX_COMPILER)
	if(NOT DEFINED ${required})
		message(FATAL_ERROR "RunReflectionStepCheck: ${required} is not given")
	endif()
endforeach()

file(REMOVE_RECURSE "${WORK_DIR}")
set(project_dir "${SOURCE_ROOT}/IntegrationTests/ReflectionParser/ReflectionStep")
set(build_dir "${WORK_DIR}/build")
execute_process(
	COMMAND "${CMAKE_COMMAND}" -S "${project_dir}" -B "${build_dir}" -G "${GENERATOR}"
		"-DCMAKE_MAKE_PROGRAM=${MAKE_PROGRAM}" "-DCMAKE_CXX_COMPILER=${CXX_COMPILER}" "-DSW_SOURCE_ROOT=${SOURCE_ROOT}" "-DSW_GENERATED_CMAKE_VARS=${GENERATED_CMAKE_VARS}"
	RESULT_VARIABLE exit_code
	OUTPUT_VARIABLE output_text
	ERROR_VARIABLE output_text)
if(NOT exit_code EQUAL 0)
	message(FATAL_ERROR "sw_add_reflection_step did not configure (${exit_code}):\n${output_text}")
endif()

set(out_dir "${build_dir}/generated/fixture_module")
# 자리 표시자 — 헤더 입력은 셋 · 묶음 TU, .cpp 입력은 inl 하나, 대상의 FlagOps 우산
foreach(placeholder Fixture.gen.cpp Fixture.gen.h Fixture.gen.meta.cpp Source.cpp.gen.inl FlagOps.gen.h ReflectionBundle.0.cpp ReflectionMetaBundle.0.cpp)
	if(NOT EXISTS "${out_dir}/${placeholder}")
		message(FATAL_ERROR "sw_add_reflection_step did not plant ${placeholder} in ${out_dir}")
	endif()
endforeach()
if(EXISTS "${out_dir}/Plain.cpp.gen.inl")
	message(FATAL_ERROR "a .cpp without reflection macros became an input (Plain.cpp.gen.inl)")
endif()

# 입력 목록(응답 파일)과 후보 검사 목록
file(READ "${out_dir}/ReflectionParser.rsp" response_text)
foreach(input Fixture.h Source.cpp)
	string(FIND "${response_text}" "--input\n${project_dir}/${input}\n" found)
	if(found EQUAL -1)
		message(FATAL_ERROR "${input} is not an input in ReflectionParser.rsp:\n${response_text}")
	endif()
endforeach()
file(READ "${out_dir}/ReflectionCandidates.list" candidate_text)
string(FIND "${candidate_text}" "${project_dir}/Plain.cpp" found)
if(found EQUAL -1)
	message(FATAL_ERROR "Plain.cpp is not watched by the candidate check:\n${candidate_text}")
endif()

# 타깃 응답 파일 — 자기 정의와 링크한 타깃의 정의 · include 자리(전이)
file(READ "${out_dir}/ReflectionParser.target.rsp" target_text)
foreach(needle "--target-arg\n-DSW_FIXTURE_MODULE_DEFINE=1\n" "--target-arg\n-DSW_FIXTURE_DEPENDENCY_DEFINE=1\n" "--include\n${project_dir}/dependency_include\n")
	string(FIND "${target_text}" "${needle}" found)
	if(found EQUAL -1)
		message(FATAL_ERROR "ReflectionParser.target.rsp lacks '${needle}':\n${target_text}")
	endif()
endforeach()

# .cpp 입력의 오브젝트가 inl 에 기대는지(OBJECT_DEPENDS) — Ninja 생성기만 build.ninja 로 본다
if(GENERATOR MATCHES "Ninja")
	file(STRINGS "${build_dir}/build.ninja" list_source_edge REGEX "^build [^:]*Source\\.cpp\\.(obj|o):")
	if(NOT list_source_edge)
		message(FATAL_ERROR "no build edge for Source.cpp in build.ninja")
	endif()
	string(FIND "${list_source_edge}" "Source.cpp.gen.inl" found)
	if(found EQUAL -1)
		message(FATAL_ERROR "Source.cpp does not depend on Source.cpp.gen.inl (OBJECT_DEPENDS):\n${list_source_edge}")
	endif()
	file(STRINGS "${build_dir}/build.ninja" list_candidate_edge REGEX "ReflectionCandidates\\.stamp")
	if(NOT list_candidate_edge)
		message(FATAL_ERROR "the candidate check command (ReflectionCandidates.stamp) is not in build.ninja")
	endif()
endif()
