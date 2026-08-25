# ==============================================================================
# @file cmake/Modules/Toolchain/Vcpkg/VcpkgPortsToolchain.cmake
# @brief vcpkg 포트 빌드 전용 툴체인 — clang-cl + LLVM을 절대 경로로 지정
# ==============================================================================

# ------------------------------------------------------------------------------
# 1) clang-cl / lld-link / llvm-rc — 포트 빌드 컴파일러
# ------------------------------------------------------------------------------
include("${CMAKE_CURRENT_LIST_DIR}/../FindLlvmBin.cmake")
sw_findLlvmBin(llvmBin)

if(NOT llvmBin)
	message(FATAL_ERROR
		"[VcpkgPortsToolchain] clang-cl.exe를 찾을 수 없습니다.\n"
		"  LLVM을 설치하거나 LLVM_DIR/LLVM_ROOT를 설정한 뒤\n"
		"  python Scripts/setup/SetupEnvironment.py 를 실행하세요."
	)
endif()

set(CMAKE_C_COMPILER   "${llvmBin}/clang-cl.exe" CACHE STRING "" FORCE)
set(CMAKE_CXX_COMPILER "${llvmBin}/clang-cl.exe" CACHE STRING "" FORCE)
set(CMAKE_LINKER       "${llvmBin}/lld-link.exe" CACHE STRING "" FORCE)
if(EXISTS "${llvmBin}/llvm-rc.exe")
	set(CMAKE_RC_COMPILER "${llvmBin}/llvm-rc.exe" CACHE STRING "" FORCE)
endif()

# ------------------------------------------------------------------------------
# 2) toolchain_config.json — SDK / MSVC 도구 경로 (이 파일 기준 저장소 루트)
# ------------------------------------------------------------------------------
get_filename_component(swToolchainDir "${CMAKE_CURRENT_LIST_DIR}" ABSOLUTE)
get_filename_component(swRepoRoot "${swToolchainDir}/../../../.." ABSOLUTE)
set(swCfgJson "")
set(swSdkDir "")
set(swSdkVer "")
set(swMsvcTools "")
foreach(candidateRoot IN ITEMS "${swRepoRoot}" "${CMAKE_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}")
	get_filename_component(absRoot "${candidateRoot}" ABSOLUTE)
	set(cfgCandidate "${absRoot}/Config/Environment/toolchain_config.json")
	if(EXISTS "${cfgCandidate}")
		set(swCfgJson "${cfgCandidate}")
		file(READ "${swCfgJson}" cfgContent)
		string(JSON swSdkDir ERROR_VARIABLE e1 GET "${cfgContent}" "windows_sdk_dir")
		string(JSON swSdkVer ERROR_VARIABLE e2 GET "${cfgContent}" "windows_sdk_version")
		string(JSON swMsvcTools ERROR_VARIABLE e3 GET "${cfgContent}" "msvc_tools_dir")
		break()
	endif()
endforeach()

# ------------------------------------------------------------------------------
# 3) 정적 아카이브 — llvm-lib, 없으면 MSVC lib.exe
#    프로젝트 LLVM 키트에 llvm-lib가 없을 수 있음
# ------------------------------------------------------------------------------
set(swAr "")
if(EXISTS "${llvmBin}/llvm-lib.exe")
	set(swAr "${llvmBin}/llvm-lib.exe")
elseif(swMsvcTools AND NOT swMsvcTools STREQUAL "")
	foreach(hostArch IN ITEMS Hostx64 Hostx86)
		foreach(targetArch IN ITEMS x64 x86)
			set(arCandidate "${swMsvcTools}/bin/${hostArch}/${targetArch}/lib.exe")
			if(EXISTS "${arCandidate}")
				set(swAr "${arCandidate}")
				break()
			endif()
		endforeach()
		if(swAr)
			break()
		endif()
	endforeach()
endif()
if(swAr)
	set(CMAKE_AR "${swAr}" CACHE FILEPATH "" FORCE)
	set(CMAKE_C_COMPILER_AR "${swAr}" CACHE FILEPATH "" FORCE)
	set(CMAKE_CXX_COMPILER_AR "${swAr}" CACHE FILEPATH "" FORCE)
	message(STATUS "[VcpkgPortsToolchain] CMAKE_AR=${CMAKE_AR}")
else()
	message(WARNING "[VcpkgPortsToolchain] lib.exe / llvm-lib not found — static port archives may fail")
endif()
if(EXISTS "${llvmBin}/llvm-ranlib.exe")
	set(CMAKE_RANLIB "${llvmBin}/llvm-ranlib.exe" CACHE FILEPATH "" FORCE)
endif()

# ------------------------------------------------------------------------------
# 4) mt.exe — clang-cl + Ninja 포트는 vslinkexe 매니페스트가 필요
#    없으면 /MANIFEST:NO 로 폴백
# ------------------------------------------------------------------------------
set(swMt "")
if(EXISTS "${llvmBin}/llvm-mt.exe")
	set(swMt "${llvmBin}/llvm-mt.exe")
elseif(swSdkDir AND swSdkVer AND NOT swSdkDir STREQUAL "" AND NOT swSdkVer STREQUAL "")
	foreach(arch IN ITEMS x64 x86)
		set(mtCandidate "${swSdkDir}/bin/${swSdkVer}/${arch}/mt.exe")
		if(EXISTS "${mtCandidate}")
			set(swMt "${mtCandidate}")
			break()
		endif()
	endforeach()
endif()

if(NOT swMt)
	foreach(kitsRoot IN ITEMS
		"C:/Program Files (x86)/Windows Kits/10"
		"C:/Program Files/Windows Kits/10"
	)
		if(NOT IS_DIRECTORY "${kitsRoot}/bin")
			continue()
		endif()
		file(GLOB sdkVerDirs LIST_DIRECTORIES true "${kitsRoot}/bin/10.*")
		list(SORT sdkVerDirs COMPARE NATURAL ORDER DESCENDING)
		foreach(verDir IN LISTS sdkVerDirs)
			foreach(arch IN ITEMS x64 x86)
				set(mtCandidate "${verDir}/${arch}/mt.exe")
				if(EXISTS "${mtCandidate}")
					set(swMt "${mtCandidate}")
					break()
				endif()
			endforeach()
			if(swMt)
				break()
			endif()
		endforeach()
		if(swMt)
			break()
		endif()
	endforeach()
endif()

if(swMt)
	set(CMAKE_MT "${swMt}" CACHE FILEPATH "매니페스트 도구" FORCE)
	message(STATUS "[VcpkgPortsToolchain] CMAKE_MT=${CMAKE_MT}")
else()
	set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /MANIFEST:NO" CACHE STRING "" FORCE)
	set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} /MANIFEST:NO" CACHE STRING "" FORCE)
	set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} /MANIFEST:NO" CACHE STRING "" FORCE)
	message(WARNING
		"[VcpkgPortsToolchain] mt.exe / llvm-mt not found — using /MANIFEST:NO for port links")
endif()

# ------------------------------------------------------------------------------
# 5) 포트 compile rule 오버라이드
#    -Werror 뒤에 -Wno-error (spirv-reflect 등)
#    clang-cl에 /EHsc (spirv-cross throw). CMAKE_*_FLAGS 를 CACHE FORCE 하면
#    Windows-Clang 기본값(/EHsc /GR)이 빠져 예외가 꺼진다
# ------------------------------------------------------------------------------
get_filename_component(_swVcpkgCompileRules
	"${CMAKE_CURRENT_LIST_DIR}/VcpkgPortsCompileRules.cmake" ABSOLUTE)
set(CMAKE_USER_MAKE_RULES_OVERRIDE "${_swVcpkgCompileRules}")
unset(_swVcpkgCompileRules)
