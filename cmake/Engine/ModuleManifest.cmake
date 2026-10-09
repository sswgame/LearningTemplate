# ==============================================================================
# @file cmake/Engine/ModuleManifest.cmake
# @brief 모듈 매니페스트(`<모듈>.module.json`) 읽기 · 해석 — 꺼진 모듈은 짓지 않고, 매니페스트를 `Bin/Modules/` 에 복사한다
# ==============================================================================
#
# 규칙은 런타임(`Engine/Module/ModuleCatalog.cpp`)과 같다 — 한쪽만 고치면 빌드한 것과 App 이 올리는 것이 갈린다.
# 그래서 해석 결과(켜진 모듈 · 적재 순서)를 `Bin/Modules/ResolvedModules.txt` 로 남기고, 시험(`ModuleCatalogTest.BuildAndRuntimeAgree`)이
# 같은 매니페스트를 C++ 로 해석해 견준다.
#
# 켜짐 = (프로젝트 `SWGame.module.json` 의 `_listModuleOverride` 에 있으면 그 값, 없으면 `_bEnabledByDefault`)
# 그리고 이 플랫폼(`_listPlatform`) · 구성(`_listConfiguration` — Dev | Shipping)에 있음
# 그리고 이 빌드 타깃(`SW_TARGET_TYPE` — Game 은 Client · Server 둘 다)이 `_listTarget`(Client | Server)과 겹침. 프로젝트는 늘 켜져 있다.
# 그리고 (`SW_BUILD_UNUSED_KITS=OFF` 이면) 키트는 켜진 비키트 모듈에서 의존으로 닿음 — 이 조건만 빌드 쪽에 있고, 뺀 키트는 매니페스트도 복사하지 않는다.
# 켜진 모듈의 의존은 있어야 하고 · 켜져 있어야 하고 · `_minVersion` 이상이어야 하며 · 순환이 없어야 한다 — 아니면 구성이 선다.
# 적재 순서 = 의존이 먼저, 동점은 이름 순(`TopologicalSortUtil::sortByDependency` 와 같다).
#
# 주의: `cmake -P` 스크립트(시험)도 이 파일을 include 한다. 스크립트 모드는 cmake_minimum_required 가 없으면 정책이 전부 OLD 라
# `IN_LIST`(CMP0057)가 "Unknown arguments" 가 된다 — 조용히 다르게 돌지 않게 여기서 세운다.
if(NOT CMAKE_MINIMUM_REQUIRED_VERSION)
	message(FATAL_ERROR "[Module] include ModuleManifest.cmake after cmake_minimum_required() — script mode (cmake -P) otherwise runs it under OLD policies")
endif()

# 매니페스트 키 · 종류 낱말(런타임 `ModuleCatalogInternal` 과 같은 표).
set(SW_MODULE_MANIFEST_KEYS _name _version _kind _description _listDependency _listPlatform _listConfiguration _listTarget _bEnabledByDefault _listModuleOverride)
set(SW_MODULE_KINDS GameFramework Kit Game Editor Rhi)

# 매니페스트 하나를 읽어 전역 속성 `SW_MODULE_<이름>_*` 에 담는다. 형식이 틀리면 구성을 세운다.
function(sw_readModuleManifest MANIFEST_PATH)
	file(READ "${MANIFEST_PATH}" swJson)
	string(JSON swName ERROR_VARIABLE swError GET "${swJson}" _name)

	if(swError OR swName STREQUAL "")
		message(FATAL_ERROR "[Module] ${MANIFEST_PATH}: _name is missing (${swError})")
	endif()

	get_filename_component(swFileName "${MANIFEST_PATH}" NAME)

	if(NOT swFileName STREQUAL "${swName}.module.json")
		message(FATAL_ERROR "[Module] ${MANIFEST_PATH}: the file must be named '${swName}.module.json'")
	endif()

	string(JSON swKeyCount LENGTH "${swJson}")
	math(EXPR swLastKey "${swKeyCount} - 1")

	foreach(swKeyIndex RANGE ${swLastKey})
		string(JSON swKey MEMBER "${swJson}" ${swKeyIndex})

		if(NOT swKey IN_LIST SW_MODULE_MANIFEST_KEYS)
			message(FATAL_ERROR "[Module] ${MANIFEST_PATH}: unknown key '${swKey}'")
		endif()
	endforeach()

	string(JSON swVersion ERROR_VARIABLE swError GET "${swJson}" _version)

	if(swError OR NOT swVersion MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
		message(FATAL_ERROR "[Module] ${MANIFEST_PATH}: _version must be 'major.minor.patch'")
	endif()

	string(JSON swKind ERROR_VARIABLE swError GET "${swJson}" _kind)

	if(swError OR NOT swKind IN_LIST SW_MODULE_KINDS)
		message(FATAL_ERROR "[Module] ${MANIFEST_PATH}: unknown _kind '${swKind}' (one of ${SW_MODULE_KINDS})")
	endif()

	foreach(swListKey _listPlatform _listConfiguration _listTarget)
		set(swValues "")
		string(JSON swCount ERROR_VARIABLE swError LENGTH "${swJson}" ${swListKey})

		if(swError OR swCount EQUAL 0)
			message(FATAL_ERROR "[Module] ${MANIFEST_PATH}: ${swListKey} must be a non-empty array")
		endif()

		math(EXPR swLast "${swCount} - 1")

		foreach(swIndex RANGE ${swLast})
			string(JSON swValue GET "${swJson}" ${swListKey} ${swIndex})
			list(APPEND swValues ${swValue})
		endforeach()

		set(swValues_${swListKey} ${swValues})
	endforeach()

	foreach(swPlatform IN LISTS swValues__listPlatform)
		if(NOT swPlatform MATCHES "^(Windows|Linux)$")
			message(FATAL_ERROR "[Module] ${MANIFEST_PATH}: unknown platform '${swPlatform}'")
		endif()
	endforeach()

	foreach(swConfiguration IN LISTS swValues__listConfiguration)
		if(NOT swConfiguration MATCHES "^(Dev|Shipping)$")
			message(FATAL_ERROR "[Module] ${MANIFEST_PATH}: unknown configuration '${swConfiguration}'")
		endif()
	endforeach()

	foreach(swTarget IN LISTS swValues__listTarget)
		if(NOT swTarget MATCHES "^(Client|Server)$")
			message(FATAL_ERROR "[Module] ${MANIFEST_PATH}: unknown target '${swTarget}' (Client | Server)")
		endif()
	endforeach()

	string(JSON swEnabled ERROR_VARIABLE swError GET "${swJson}" _bEnabledByDefault)

	if(swError)
		set(swEnabled ON)
	endif()

	set(swDependencyNames "")
	set(swDependencyMinVersions "")
	string(JSON swCount ERROR_VARIABLE swError LENGTH "${swJson}" _listDependency)

	if(NOT swError AND swCount GREATER 0)
		math(EXPR swLast "${swCount} - 1")

		foreach(swIndex RANGE ${swLast})
			string(JSON swDependency GET "${swJson}" _listDependency ${swIndex} _name)
			string(JSON swMinVersion ERROR_VARIABLE swMinError GET "${swJson}" _listDependency ${swIndex} _minVersion)

			if(swMinError)
				set(swMinVersion "0.0.0")
			endif()

			list(APPEND swDependencyNames ${swDependency})
			list(APPEND swDependencyMinVersions ${swMinVersion})
		endforeach()
	endif()

	set(swOverrideNames "")
	set(swOverrideValues "")
	string(JSON swCount ERROR_VARIABLE swError LENGTH "${swJson}" _listModuleOverride)

	if(NOT swError)
		if(NOT swKind STREQUAL "Game")
			message(FATAL_ERROR "[Module] ${MANIFEST_PATH}: only a Game module (the project) has _listModuleOverride")
		endif()

		if(swCount GREATER 0)
			math(EXPR swLast "${swCount} - 1")

			foreach(swIndex RANGE ${swLast})
				string(JSON swOverrideName GET "${swJson}" _listModuleOverride ${swIndex} _name)
				string(JSON swOverrideValue GET "${swJson}" _listModuleOverride ${swIndex} _bEnabled)
				list(APPEND swOverrideNames ${swOverrideName})
				list(APPEND swOverrideValues ${swOverrideValue})
			endforeach()
		endif()
	endif()

	get_property(swKnown GLOBAL PROPERTY SW_MODULE_NAMES)

	if(swName IN_LIST swKnown)
		get_property(swOtherPath GLOBAL PROPERTY SW_MODULE_${swName}_FILE)
		message(FATAL_ERROR "[Module] '${swName}' is declared twice (${swOtherPath}, ${MANIFEST_PATH})")
	endif()

	get_filename_component(swDirectory "${MANIFEST_PATH}" DIRECTORY)
	set_property(GLOBAL APPEND PROPERTY SW_MODULE_NAMES ${swName})
	set_property(GLOBAL PROPERTY SW_MODULE_${swName}_FILE "${MANIFEST_PATH}")
	set_property(GLOBAL PROPERTY SW_MODULE_${swName}_DIR "${swDirectory}")
	set_property(GLOBAL PROPERTY SW_MODULE_${swName}_KIND ${swKind})
	set_property(GLOBAL PROPERTY SW_MODULE_${swName}_VERSION ${swVersion})
	set_property(GLOBAL PROPERTY SW_MODULE_${swName}_PLATFORMS ${swValues__listPlatform})
	set_property(GLOBAL PROPERTY SW_MODULE_${swName}_CONFIGURATIONS ${swValues__listConfiguration})
	set_property(GLOBAL PROPERTY SW_MODULE_${swName}_TARGETS ${swValues__listTarget})
	set_property(GLOBAL PROPERTY SW_MODULE_${swName}_ENABLED_BY_DEFAULT ${swEnabled})
	set_property(GLOBAL PROPERTY SW_MODULE_${swName}_DEPENDENCIES ${swDependencyNames})
	set_property(GLOBAL PROPERTY SW_MODULE_${swName}_DEPENDENCY_MIN_VERSIONS ${swDependencyMinVersions})
	set_property(GLOBAL PROPERTY SW_MODULE_${swName}_OVERRIDE_NAMES ${swOverrideNames})
	set_property(GLOBAL PROPERTY SW_MODULE_${swName}_OVERRIDE_VALUES ${swOverrideValues})
endfunction()

# 저장소의 매니페스트를 모두 읽고 해석한다. 고르지 않은 게임의 매니페스트는 읽지 않는다(게임 모듈 이름은 모두 SWGame 이다).
# 결과: 전역 속성 `SW_MODULE_<이름>_ACTIVE`(ON/OFF) · `_REASON`, `SW_MODULE_LOAD_ORDER`. 틀리면 구성을 세운다.
function(sw_resolveModuleManifests)
	file(GLOB_RECURSE swListManifest CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/Source/*.module.json")
	list(SORT swListManifest)

	foreach(swManifest IN LISTS swListManifest)
		if(swManifest MATCHES "/Source/Games/([^/]+)/" AND NOT CMAKE_MATCH_1 STREQUAL SW_ACTIVE_GAME)
			continue()
		endif()

		sw_readModuleManifest("${swManifest}")
	endforeach()

	# 플랫폼 · 구성 이름은 sw_platform_name · sw_configuration_name(cmake/Engine/BuildLayout.cmake) — 배포 백엔드 확인과 같은 낱말이다.
	# 빌드 타깃이 담는 모듈 대상 — Game 은 둘 다.
	if(SW_TARGET_TYPE STREQUAL "Game")
		set(swBuildTargets Client Server)
	else()
		set(swBuildTargets ${SW_TARGET_TYPE})
	endif()

	get_property(swNames GLOBAL PROPERTY SW_MODULE_NAMES)

	if(NOT "SWGame" IN_LIST swNames)
		message(FATAL_ERROR "[Module] Source/Games/${SW_ACTIVE_GAME}/SWGame.module.json is missing — the project manifest decides which modules are on")
	endif()

	# 1) 프로젝트의 켜기/끄기 표 — 모르는 이름은 오류다.
	get_property(swOverrideNames GLOBAL PROPERTY SW_MODULE_SWGame_OVERRIDE_NAMES)
	get_property(swOverrideValues GLOBAL PROPERTY SW_MODULE_SWGame_OVERRIDE_VALUES)
	set(swOverrideIndex 0)

	foreach(swOverrideName IN LISTS swOverrideNames)
		if(NOT swOverrideName IN_LIST swNames)
			message(FATAL_ERROR "[Module] SWGame.module.json: module override names an unknown module '${swOverrideName}'")
		endif()

		list(GET swOverrideValues ${swOverrideIndex} swOverrideValue)
		set(swOverride_${swOverrideName} ${swOverrideValue})
		math(EXPR swOverrideIndex "${swOverrideIndex} + 1")
	endforeach()

	# 2) 켜짐
	set(swActive "")

	foreach(swName IN LISTS swNames)
		get_property(swDefault GLOBAL PROPERTY SW_MODULE_${swName}_ENABLED_BY_DEFAULT)
		get_property(swPlatforms GLOBAL PROPERTY SW_MODULE_${swName}_PLATFORMS)
		get_property(swConfigurations GLOBAL PROPERTY SW_MODULE_${swName}_CONFIGURATIONS)
		get_property(swModuleTargets GLOBAL PROPERTY SW_MODULE_${swName}_TARGETS)
		set(swTargetHit OFF)

		foreach(swTarget IN LISTS swModuleTargets)
			if(swTarget IN_LIST swBuildTargets)
				set(swTargetHit ON)
			endif()
		endforeach()

		set(swEnabled ${swDefault})
		set(swReason "disabled by default")

		if(DEFINED swOverride_${swName})
			set(swEnabled ${swOverride_${swName}})
			set(swReason "disabled by the project")
		endif()

		if(swName STREQUAL "SWGame")
			set(swEnabled ON)
		endif()

		if(NOT swEnabled)
			set_property(GLOBAL PROPERTY SW_MODULE_${swName}_ACTIVE OFF)
			set_property(GLOBAL PROPERTY SW_MODULE_${swName}_REASON "${swReason}")
		elseif(NOT sw_platform_name IN_LIST swPlatforms)
			set_property(GLOBAL PROPERTY SW_MODULE_${swName}_ACTIVE OFF)
			set_property(GLOBAL PROPERTY SW_MODULE_${swName}_REASON "not available on ${sw_platform_name}")
		elseif(NOT sw_configuration_name IN_LIST swConfigurations)
			set_property(GLOBAL PROPERTY SW_MODULE_${swName}_ACTIVE OFF)
			set_property(GLOBAL PROPERTY SW_MODULE_${swName}_REASON "not built for ${sw_configuration_name}")
		elseif(NOT swTargetHit)
			set_property(GLOBAL PROPERTY SW_MODULE_${swName}_ACTIVE OFF)
			set_property(GLOBAL PROPERTY SW_MODULE_${swName}_REASON "not built for the ${SW_TARGET_TYPE} target")
		else()
			set_property(GLOBAL PROPERTY SW_MODULE_${swName}_ACTIVE ON)
			list(APPEND swActive ${swName})
		endif()
	endforeach()

	# 2-1) 활성 게임이 쓰지 않는 키트(`SW_BUILD_UNUSED_KITS=OFF`)는 짓지 않는다. 아래 5) 가 그 매니페스트를 `Bin/Modules/` 에 두지 않으므로
	# 런타임 카탈로그도 그 키트를 모른다 — 고르지 않은 게임의 매니페스트와 같다(`ModuleCatalogTest.BuildAndRuntimeAgree`).
	set(swUnusedKits "")

	if(DEFINED SW_BUILD_UNUSED_KITS AND NOT SW_BUILD_UNUSED_KITS)
		sw_collectUnusedKits(swActive swUnusedKits)

		foreach(swName IN LISTS swUnusedKits)
			set_property(GLOBAL PROPERTY SW_MODULE_${swName}_ACTIVE OFF)
			set_property(GLOBAL PROPERTY SW_MODULE_${swName}_REASON "not used by ${SW_ACTIVE_GAME} (SW_BUILD_UNUSED_KITS=OFF)")
			list(REMOVE_ITEM swActive ${swName})
		endforeach()
	endif()

	# 3) 켜진 모듈의 의존 — 있고 · 켜져 있고 · 버전이 맞아야 한다.
	foreach(swName IN LISTS swActive)
		get_property(swDependencies GLOBAL PROPERTY SW_MODULE_${swName}_DEPENDENCIES)
		get_property(swMinVersions GLOBAL PROPERTY SW_MODULE_${swName}_DEPENDENCY_MIN_VERSIONS)
		set(swDependencyIndex 0)

		foreach(swDependency IN LISTS swDependencies)
			if(NOT swDependency IN_LIST swNames)
				message(FATAL_ERROR "[Module] Module '${swName}' depends on '${swDependency}', which has no manifest (missing module)")
			endif()

			get_property(swDependencyActive GLOBAL PROPERTY SW_MODULE_${swDependency}_ACTIVE)

			if(NOT swDependencyActive)
				get_property(swDependencyReason GLOBAL PROPERTY SW_MODULE_${swDependency}_REASON)
				message(FATAL_ERROR "[Module] Module '${swName}' depends on '${swDependency}', which is ${swDependencyReason}")
			endif()

			list(GET swMinVersions ${swDependencyIndex} swMinVersion)
			get_property(swDependencyVersion GLOBAL PROPERTY SW_MODULE_${swDependency}_VERSION)

			if(swDependencyVersion VERSION_LESS swMinVersion)
				message(FATAL_ERROR "[Module] Module '${swName}' needs '${swDependency}' ${swMinVersion} or later, but ${swDependencyVersion} is present")
			endif()

			math(EXPR swDependencyIndex "${swDependencyIndex} + 1")
		endforeach()
	endforeach()

	# 4) 적재 순서 — 의존이 먼저, 동점은 이름 순(켜진 모듈의 의존은 위 3) 이 모두 켜져 있음을 확인했다).
	set(swOrder ${swActive})
	sw_sortModulesByDependency(swOrder)
	set_property(GLOBAL PROPERTY SW_MODULE_LOAD_ORDER ${swOrder})

	# 5) Dev 는 매니페스트를 실행 파일 옆 `Modules/` 에 둔다 — App 이 같은 규칙으로 다시 해석해 적재 순서를 정한다. 꺼진 모듈의 것도 둔다(무엇이 왜 꺼졌는지 App 도 안다).
	# 쓰지 않아 뺀 키트(2-1)는 두지 않는다 — 런타임에는 그 규칙이 없다.
	if(NOT SW_SHIPPING_BUILD)
		set(swCatalogDirectory "${CMAKE_BINARY_DIR}/Bin/Modules")
		file(MAKE_DIRECTORY "${swCatalogDirectory}")
		file(GLOB swStaleList "${swCatalogDirectory}/*.module.json")

		foreach(swName IN LISTS swNames)
			if(swName IN_LIST swUnusedKits)
				continue()
			endif()

			get_property(swFile GLOBAL PROPERTY SW_MODULE_${swName}_FILE)
			configure_file("${swFile}" "${swCatalogDirectory}/${swName}.module.json" COPYONLY)
			list(REMOVE_ITEM swStaleList "${swCatalogDirectory}/${swName}.module.json")
		endforeach()

		if(swStaleList)
			file(REMOVE ${swStaleList})
		endif()

		list(JOIN swOrder "\n" swOrderText)
		file(CONFIGURE OUTPUT "${swCatalogDirectory}/ResolvedModules.txt" CONTENT "${swOrderText}\n")

		sw_removeStaleBinaryOutputs("${swOrder}")
	endif()

	list(LENGTH swNames swManifestCount)
	list(LENGTH swActive swActiveCount)
	message(STATUS "[Module] ${swActiveCount} of ${swManifestCount} modules active (${sw_platform_name} ${sw_configuration_name} ${SW_TARGET_TYPE})")
endfunction()

# ------------------------------------------------------------------------------
# sw_removeStaleBinaryOutputs — Dev `Bin` 의 옛 산출물을 configure 때 지운다(ninja 는 지어 놓은 파일을 치우지 않는다).
#   1) `Bin/Modules` 의 모듈 산출물(GF_* · RHI_* · EditorModule · SWGame*) 가운데 이 구성에서 꺼진 모듈의 것 — 켜진 모듈 것은 건드리지 않는다.
#      서드파티 DLL · PDB(모듈 이름 꼴이 아닌 것 — 서드파티는 `Bin` 에 한 벌, `sw_stageModuleRuntimeDlls`)와 남은 섀도 복사본(`<모듈>_temp_p<pid>_…`).
#      살아 있는 App 이 쓰는 섀도 복사본은 잠겨 있어 지워지지 않는다 — 아래 경고로 남는다.
#   2) `Bin` 바로 아래의 옛 자리 산출물 — 모듈 DLL · PDB · 매니페스트(지금은 `Bin/Modules`), 시험 실행 파일(지금은 `TestBin`),
#      `Bin/Symbols` 에 같은 이름이 생긴 PDB(옛 자리).
#   실행 중인 App · 시험이 DLL 을 쥐고 있으면 지우지 못한다 — 경고만 하고 넘어간다(다음 configure 가 다시 지운다).
# ------------------------------------------------------------------------------
function(sw_removeStaleBinaryOutputs ACTIVE_MODULES)
	set(swBinDir "${CMAKE_BINARY_DIR}/Bin")
	set(swModulePattern "^(GF_.+|RHI_.+|EditorModule|SWGame.*)$")
	set(swImageSuffixes ".dll;.pdb;.so;.exp;.lib;.ilk;.debug")
	set(swStale "")

	file(GLOB swModuleFolderFiles LIST_DIRECTORIES false "${swBinDir}/Modules/*")
	foreach(swPath IN LISTS swModuleFolderFiles)
		get_filename_component(swStem "${swPath}" NAME_WE)
		get_filename_component(swSuffix "${swPath}" LAST_EXT)
		string(REGEX REPLACE "^lib" "" swStemNoPrefix "${swStem}")
		get_filename_component(swFileName "${swPath}" NAME)
		if(swFileName MATCHES "_temp_")
			list(APPEND swStale "${swPath}")
		elseif(swSuffix IN_LIST swImageSuffixes AND swStemNoPrefix MATCHES "${swModulePattern}" AND NOT swStemNoPrefix IN_LIST ACTIVE_MODULES)
			list(APPEND swStale "${swPath}")
		elseif((swSuffix STREQUAL ".dll" OR swSuffix STREQUAL ".pdb") AND NOT swStemNoPrefix MATCHES "${swModulePattern}")
			list(APPEND swStale "${swPath}")
		endif()
	endforeach()

	file(GLOB swBinFiles LIST_DIRECTORIES false "${swBinDir}/*")
	foreach(swPath IN LISTS swBinFiles)
		get_filename_component(swFileName "${swPath}" NAME)
		get_filename_component(swStem "${swPath}" NAME_WE)
		get_filename_component(swSuffix "${swPath}" LAST_EXT)
		string(REGEX REPLACE "^lib" "" swStemNoPrefix "${swStem}")
		if(swStemNoPrefix MATCHES "${swModulePattern}" AND (swSuffix IN_LIST swImageSuffixes OR swFileName MATCHES "\\.module\\.json$"))
			list(APPEND swStale "${swPath}")
		elseif(swStem MATCHES "Test$" AND (swSuffix STREQUAL ".exe" OR swSuffix STREQUAL ".pdb" OR swSuffix STREQUAL ""))
			list(APPEND swStale "${swPath}")
		elseif(swSuffix STREQUAL ".pdb" AND EXISTS "${swBinDir}/Symbols/${swFileName}")
			list(APPEND swStale "${swPath}")
		endif()
	endforeach()

	set(swRemovedCount 0)
	foreach(swPath IN LISTS swStale)
		execute_process(COMMAND "${CMAKE_COMMAND}" -E rm -f "${swPath}" RESULT_VARIABLE swResult OUTPUT_QUIET ERROR_QUIET)
		if(swResult EQUAL 0 AND NOT EXISTS "${swPath}")
			math(EXPR swRemovedCount "${swRemovedCount} + 1")
		else()
			message(WARNING "[Module] Could not remove a stale output (in use?): ${swPath}")
		endif()
	endforeach()
	if(swRemovedCount GREATER 0)
		message(STATUS "[Module] Removed ${swRemovedCount} stale output(s) from Bin (old layout or modules this configuration turns off)")
	endif()
endfunction()

# 모듈 NAME 이 이 구성에서 켜져 있으면 OUT_VAR 를 ON 으로 둔다. 매니페스트가 없는 모듈은 구성을 세운다(모든 동적 모듈은 매니페스트를 갖는다).
function(sw_isModuleActive NAME OUT_VAR)
	get_property(swNames GLOBAL PROPERTY SW_MODULE_NAMES)

	if(NOT NAME IN_LIST swNames)
		message(FATAL_ERROR "[Module] '${NAME}' has no manifest — add ${NAME}.module.json next to its CMakeLists.txt")
	endif()

	get_property(swActive GLOBAL PROPERTY SW_MODULE_${NAME}_ACTIVE)
	set(${OUT_VAR} ${swActive} PARENT_SCOPE)
endfunction()

# 꺼진 모듈 NAME 의 폴더를 "이 구성이 짓지 않는다" 로 적고 OUT_VAR 를 ON 으로 둔다(켜져 있으면 OFF). 모듈 타깃을 만드는 함수의 첫 줄에서 부른다.
function(sw_skipInactiveModule NAME OUT_VAR)
	sw_isModuleActive(${NAME} swActive)

	if(swActive)
		set(${OUT_VAR} OFF PARENT_SCOPE)
		return()
	endif()

	get_property(swReason GLOBAL PROPERTY SW_MODULE_${NAME}_REASON)
	message(STATUS "[Module] ${NAME} is not built — ${swReason}")
	sw_declareUnbuiltDirectory("${CMAKE_CURRENT_SOURCE_DIR}")
	set(${OUT_VAR} ON PARENT_SCOPE)
endfunction()

# 소스 목록 LIST_VAR 에서 꺼진 키트의 헤더(`GameFramework/Kits/<묶음>/<키트>/…`)를 include 하는 파일을 뺀다 — 시험 실행 파일이 꺼진 키트를 링크하지 않게.
# 뺀 것은 "이 구성이 짓지 않는 소스" 로 적는다.
function(sw_excludeSourcesOfInactiveKits LIST_VAR)
	get_property(swNames GLOBAL PROPERTY SW_MODULE_NAMES)
	set(swInactivePrefixes "")

	foreach(swName IN LISTS swNames)
		get_property(swKind GLOBAL PROPERTY SW_MODULE_${swName}_KIND)
		get_property(swActive GLOBAL PROPERTY SW_MODULE_${swName}_ACTIVE)

		if(swKind STREQUAL "Kit" AND NOT swActive)
			get_property(swDirectory GLOBAL PROPERTY SW_MODULE_${swName}_DIR)
			file(RELATIVE_PATH swRelative "${CMAKE_SOURCE_DIR}/Source" "${swDirectory}")
			list(APPEND swInactivePrefixes "${swRelative}/")
		endif()
	endforeach()

	if(NOT swInactivePrefixes)
		return()
	endif()

	set(swKept "")
	set(swDropped "")

	foreach(swSource IN LISTS ${LIST_VAR})
		set(swUsesInactive OFF)

		if(swSource MATCHES "\\.(cpp|h)$")
			file(STRINGS "${swSource}" swIncludes REGEX "^#include \"GameFramework/Kits/")

			foreach(swPrefix IN LISTS swInactivePrefixes)
				string(FIND "${swIncludes}" "\"${swPrefix}" swHit)

				if(NOT swHit EQUAL -1)
					set(swUsesInactive ON)
				endif()
			endforeach()
		endif()

		if(swUsesInactive)
			list(APPEND swDropped "${swSource}")
		else()
			list(APPEND swKept "${swSource}")
		endif()
	endforeach()

	if(swDropped)
		sw_declareUnbuiltSources(${swDropped})
	endif()

	set(${LIST_VAR} ${swKept} PARENT_SCOPE)
endfunction()

# 모듈 이름 목록(LIST_VAR)을 의존이 먼저 · 동점은 이름 순으로 줄 세운다(Kahn — 런타임 `TopologicalSortUtil::sortByDependency` 와 같다).
# 목록 밖의 의존은 보지 않는다(키트 목록에서 GameFramework 같은 것). 아무도 못 고르면 순환이다 — 구성을 세운다.
function(sw_sortModulesByDependency LIST_VAR)
	set(swRemaining ${${LIST_VAR}})
	set(swOrder "")

	while(swRemaining)
		set(swReady "")

		foreach(swName IN LISTS swRemaining)
			get_property(swDependencies GLOBAL PROPERTY SW_MODULE_${swName}_DEPENDENCIES)
			set(swIsReady ON)

			foreach(swDependency IN LISTS swDependencies)
				if(swDependency IN_LIST swRemaining)
					set(swIsReady OFF)
				endif()
			endforeach()

			if(swIsReady)
				list(APPEND swReady ${swName})
			endif()
		endforeach()

		if(NOT swReady)
			message(FATAL_ERROR "[Module] Module dependency cycle among: ${swRemaining}")
		endif()

		list(SORT swReady)
		list(GET swReady 0 swPicked)
		list(APPEND swOrder ${swPicked})
		list(REMOVE_ITEM swRemaining ${swPicked})
	endwhile()

	set(${LIST_VAR} ${swOrder} PARENT_SCOPE)
endfunction()

# 종류가 KIND 인 모듈의 폴더를 의존 순서로(동점은 이름 순) OUT_VAR 에 — 꺼진 것도 든다(그 폴더의 팩토리가 "짓지 않는다" 로 적고 건너뛴다).
# 의존 순서인 이유: 서버 · 클라이언트 키트가 같은 기능의 공유 키트 타깃을 링크한다(`sw_linkSharedKit` — 그 타깃이 먼저 있어야 한다).
function(sw_getModuleDirectoriesOfKind KIND OUT_VAR)
	get_property(swNames GLOBAL PROPERTY SW_MODULE_NAMES)
	set(swOfKind "")

	foreach(swName IN LISTS swNames)
		get_property(swKind GLOBAL PROPERTY SW_MODULE_${swName}_KIND)

		if(swKind STREQUAL KIND)
			list(APPEND swOfKind ${swName})
		endif()
	endforeach()

	sw_sortModulesByDependency(swOfKind)
	set(swDirectories "")

	foreach(swName IN LISTS swOfKind)
		get_property(swDirectory GLOBAL PROPERTY SW_MODULE_${swName}_DIR)
		list(APPEND swDirectories "${swDirectory}")
	endforeach()

	set(${OUT_VAR} ${swDirectories} PARENT_SCOPE)
endfunction()

# 모듈 NAME 의 매니페스트 의존 가운데 종류가 KIND(Kit · GameFramework · …)인 것을 OUT_VAR 에(선언 순서 그대로).
function(sw_getModuleDependenciesOfKind NAME KIND OUT_VAR)
	get_property(listDependency GLOBAL PROPERTY SW_MODULE_${NAME}_DEPENDENCIES)
	set(listOfKind "")

	foreach(dependency IN LISTS listDependency)
		get_property(dependencyKind GLOBAL PROPERTY SW_MODULE_${dependency}_KIND)

		if(dependencyKind STREQUAL KIND)
			list(APPEND listOfKind ${dependency})
		endif()
	endforeach()

	set(${OUT_VAR} ${listOfKind} PARENT_SCOPE)
endfunction()

# 켜진 모듈 목록(ACTIVE_LIST_VAR) 가운데 아무도 쓰지 않는 키트를 OUT_VAR 에(이름 순). 쓰는 것 = 키트가 아닌 켜진 모듈(게임 · 에디터 · RHI · GameFramework)과
# 프로젝트가 `_listModuleOverride` 로 켠 키트에서 의존을 따라 닿는 키트다. `SW_BUILD_UNUSED_KITS=OFF` 일 때 `sw_resolveModuleManifests` 가 이 키트들을 끈다.
function(sw_collectUnusedKits ACTIVE_LIST_VAR OUT_VAR)
	get_property(swOverrideNames GLOBAL PROPERTY SW_MODULE_SWGame_OVERRIDE_NAMES)
	get_property(swOverrideValues GLOBAL PROPERTY SW_MODULE_SWGame_OVERRIDE_VALUES)
	set(swPending "")

	foreach(swName IN LISTS ${ACTIVE_LIST_VAR})
		get_property(swKind GLOBAL PROPERTY SW_MODULE_${swName}_KIND)
		set(swSeed OFF)

		if(NOT swKind STREQUAL "Kit")
			set(swSeed ON)
		else()
			list(FIND swOverrideNames ${swName} swOverrideIndex)

			if(NOT swOverrideIndex EQUAL -1)
				list(GET swOverrideValues ${swOverrideIndex} swOverrideValue)

				if(swOverrideValue)
					set(swSeed ON)
				endif()
			endif()
		endif()

		if(swSeed)
			list(APPEND swPending ${swName})
		endif()
	endforeach()

	set(swReached "")

	while(swPending)
		list(POP_FRONT swPending swName)

		if(swName IN_LIST swReached)
			continue()
		endif()

		list(APPEND swReached ${swName})
		get_property(swDependencies GLOBAL PROPERTY SW_MODULE_${swName}_DEPENDENCIES)
		list(APPEND swPending ${swDependencies})
	endwhile()

	set(swUnused "")

	foreach(swName IN LISTS ${ACTIVE_LIST_VAR})
		get_property(swKind GLOBAL PROPERTY SW_MODULE_${swName}_KIND)

		if(swKind STREQUAL "Kit" AND NOT swName IN_LIST swReached)
			list(APPEND swUnused ${swName})
		endif()
	endforeach()

	list(SORT swUnused)
	set(${OUT_VAR} ${swUnused} PARENT_SCOPE)
endfunction()
