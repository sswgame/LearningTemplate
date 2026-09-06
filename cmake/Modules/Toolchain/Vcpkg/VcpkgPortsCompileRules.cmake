# ==============================================================================
# @file cmake/Modules/Toolchain/Vcpkg/VcpkgPortsCompileRules.cmake
# @brief vcpkg 포트 COMPILE_OBJECT 규칙 오버라이드 (CMAKE_USER_MAKE_RULES_OVERRIDE)
# @note clang-cl은 CMAKE_<LANG>_FLAGS를 타겟 COMPILE_OPTIONS보다 앞에 두므로
# 포트의 -Werror가 CMAKE_C_FLAGS의 -Wno-error보다 앞선다.
# <FLAGS> 뒤에 -Wno-error를 붙여 커맨드 라인 마지막에 오게 한다.
# clang-cl에서 /EHsc가 없으면 예외가 꺼지며, spirv-cross는 throw를 쓴다.
# ==============================================================================

# ------------------------------------------------------------------------------
# 1) sw_vcpkgAppendNoWerror — 해당 언어 COMPILE_OBJECT에 -Wno-error
# ------------------------------------------------------------------------------
macro(sw_vcpkgAppendNoWerror lang)
	if(DEFINED CMAKE_${lang}_COMPILE_OBJECT AND NOT CMAKE_${lang}_COMPILE_OBJECT MATCHES "-Wno-error")
		string(REPLACE "<FLAGS>" "<FLAGS> -Wno-error" CMAKE_${lang}_COMPILE_OBJECT
			"${CMAKE_${lang}_COMPILE_OBJECT}")
	endif()
endmacro()

sw_vcpkgAppendNoWerror(C)
sw_vcpkgAppendNoWerror(CXX)

if(DEFINED CMAKE_CXX_COMPILE_OBJECT AND NOT CMAKE_CXX_COMPILE_OBJECT MATCHES "/EH")
	string(REPLACE "<FLAGS>" "<FLAGS> /EHsc" CMAKE_CXX_COMPILE_OBJECT
		"${CMAKE_CXX_COMPILE_OBJECT}")
endif()
