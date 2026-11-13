# ==============================================================================
# @file cmake/Engine/ModuleManifest.cmake
# @brief 모듈 매니페스트(`<모듈>.module.json`) 읽기 · 해석 — 켜짐 · 의존 확인 · 적재 순서, Dev 는 매니페스트를 `Bin/Modules/` 에 복사한다
# ==============================================================================
#
# 해석 결과를 묻는 쪽(꺼진 모듈 건너뛰기)은 `ModuleActivation.cmake`, `Bin` 의 옛 산출물 지우기는 `BuildLayout.cmake` 다.
#
# 규칙은 런타임(`Engine/Module/ModuleCatalog.cpp`)과 같다 — 한쪽만 고치면 빌드한 것과 App 이 올리는 것이 갈린다.
# 그래서 해석 결과(켜진 모듈 · 적재 순서)를 `Bin/Modules/ResolvedModules.txt` 로 남기고, 테스트(`ModuleCatalogTest.BuildAndRuntimeAgree`)이
# 같은 매니페스트를 C++ 로 해석해 견준다.
#
# 켜짐 = (프로젝트 `SWGame.module.json` 의 `_listModuleOverride` 에 있으면 그 값, 없으면 `_bEnabledByDefault`)
# 그리고 이 플랫폼(`_listExcludedPlatform` 에 없음) · 구성(`_listConfiguration` — Dev | Shipping)에 있음
# 그리고 이 빌드 타깃(`SW_TARGET_TYPE` — Game 은 Client · Server 둘 다)이 `_listTarget`(Client | Server)과 겹침. 프로젝트는 늘 켜져 있다.
# 그리고 (`SW_BUILD_UNUSED_KITS=OFF` 이면) 키트는 켜진 비키트 모듈이나 함께 짓는 게임 팩(`SW_GAMES`)에서 의존으로 닿음 — 이 조건만 빌드 쪽에 있고, 뺀 키트는 매니페스트도 복사하지 않는다.
# 켜진 모듈의 의존은 있어야 하고 · 켜져 있어야 하고 · `_minVersion` 이상이어야 하며 · 순환이 없어야 한다 — 아니면 구성이 선다.
# 적재 순서 = 의존이 먼저, 동점은 이름 순(`DependencyGraph::sortByName` 와 같다).
#
# 주의: `cmake -P` 스크립트(테스트)도 이 파일을 include 한다. 스크립트 모드는 cmake_minimum_required 가 없으면 정책이 전부 OLD 라
# `IN_LIST`(CMP0057)가 "Unknown arguments" 가 된다 — 조용히 다르게 돌지 않게 여기서 세운다.
if(NOT CMAKE_MINIMUM_REQUIRED_VERSION)
	message(FATAL_ERROR "[Module] include ModuleManifest.cmake after cmake_minimum_required() — script mode (cmake -P) otherwise runs it under OLD policies")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/../Config/JsonValue.cmake")

# 매니페스트 키(런타임 `ModuleCatalogInternal` 과 같은 표).
set(SW_MODULE_MANIFEST_KEYS _name _version _kind _description _listDependency _listExcludedPlatform _listConfiguration _listTarget _bEnabledByDefault _listModuleOverride)

# 종류 · 플랫폼 · 구성 · 대상 낱말은 런타임 열거형(`Source/Engine/Module/ModuleCatalog.h` 의 `ModuleKind` · `ModulePlatform` ·
# `ModuleConfiguration` · `ModuleTarget`)의 열거자 이름이 정본이다 — 매니페스트를 리플렉션으로 읽는 C++ 이 받는 낱말이 곧 그 이름이라,
# CMake 가 따로 표를 들면 한쪽에만 더한 낱말이 빌드와 App 에서 갈린다. 헤더의 열거형 몸통에서 줄 머리의 이름을 뽑는다.
set(SW_MODULE_CATALOG_HEADER "${CMAKE_CURRENT_LIST_DIR}/../../Source/Engine/Module/ModuleCatalog.h")
if(NOT CMAKE_SCRIPT_MODE_FILE)
	set_property(DIRECTORY "${CMAKE_SOURCE_DIR}" APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${SW_MODULE_CATALOG_HEADER}")
endif()

# 열거형 ENUM_NAME 의 열거자 이름들(OUT_VAR). 열거형을 찾지 못하거나 비어 있으면 구성을 세운다.
function(sw_read_module_enum_names OUT_VAR ENUM_NAME)
	file(READ "${SW_MODULE_CATALOG_HEADER}" swHeaderText)
	string(REGEX MATCH "enum class ${ENUM_NAME}[^{]*{([^}]*)}" swMatch "${swHeaderText}")
	set(swNames "")
	string(REPLACE "\n" ";" swLines "${CMAKE_MATCH_1}")
	foreach(swLine IN LISTS swLines)
		if(swLine MATCHES "^[ \t]*([A-Za-z_][A-Za-z0-9_]*)[ \t]*[,=]")
			list(APPEND swNames "${CMAKE_MATCH_1}")
		endif()
	endforeach()
	if(NOT swNames)
		message(FATAL_ERROR "[Module] ${SW_MODULE_CATALOG_HEADER}: enum class ${ENUM_NAME} not found or empty — the manifest words are read from it")
	endif()
	set(${OUT_VAR} ${swNames} PARENT_SCOPE)
endfunction()

sw_read_module_enum_names(SW_MODULE_KINDS ModuleKind)
sw_read_module_enum_names(SW_MODULE_PLATFORMS ModulePlatform)
sw_read_module_enum_names(SW_MODULE_CONFIGURATIONS ModuleConfiguration)
sw_read_module_enum_names(SW_MODULE_TARGETS ModuleTarget)

# 매니페스트 키가 모두 표(`SW_MODULE_MANIFEST_KEYS`)에 있는지 본다.
function(sw_check_manifest_keys MANIFEST_PATH)
	sw_require_json_value(swKeyCount "${MANIFEST_PATH}" LENGTH)
	math(EXPR swLastKey "${swKeyCount} - 1")

	foreach(swKeyIndex RANGE ${swLastKey})
		sw_require_json_value(swKey "${MANIFEST_PATH}" MEMBER ${swKeyIndex})

		if(NOT swKey IN_LIST SW_MODULE_MANIFEST_KEYS)
			message(FATAL_ERROR "[Module] ${MANIFEST_PATH}: unknown key '${swKey}'")
		endif()
	endforeach()
endfunction()

# 비면 안 되는 낱말 배열 KEY 를 OUT_VAR 에 읽는다. 낱말은 모두 ALLOWED_WORDS(런타임 열거자 이름)에 있어야 하고 LABEL 은 오류 글에 쓴다.
function(sw_read_manifest_words OUT_VAR MANIFEST_PATH KEY LABEL ALLOWED_WORDS)
	sw_require_json_value(swCount "${MANIFEST_PATH}" LENGTH ${KEY})

	if(swCount EQUAL 0)
		message(FATAL_ERROR "[Module] ${MANIFEST_PATH}: ${KEY} must be a non-empty array")
	endif()

	set(swValues "")
	math(EXPR swLast "${swCount} - 1")

	foreach(swIndex RANGE ${swLast})
		sw_require_json_value(swValue "${MANIFEST_PATH}" ${KEY} ${swIndex})

		if(NOT swValue IN_LIST ALLOWED_WORDS)
			message(FATAL_ERROR "[Module] ${MANIFEST_PATH}: unknown ${LABEL} '${swValue}' (one of ${ALLOWED_WORDS})")
		endif()

		list(APPEND swValues ${swValue})
	endforeach()

	set(${OUT_VAR} ${swValues} PARENT_SCOPE)
endfunction()

# 모듈이 도는 플랫폼을 OUT_VAR 에 낸다. 기본은 전부고, 선택 키 `_listExcludedPlatform` 이 뺀다(플랫폼 하나를 더할 때 매니페스트를 고치지 않는다, 언리얼 `PlatformDenyList` 와 같다 — 기억).
# 에디터 · 에디터 확장은 종류가 데스크톱 전용이라 Android · Web 을 적지 않아도 빠진다. 런타임 `ModuleCatalog::computePlatformMask` 와 같은 규칙.
function(sw_read_manifest_platforms OUT_VAR MANIFEST_PATH KIND)
	set(swPlatforms ${SW_MODULE_PLATFORMS})
	sw_read_optional_json_value(swCount "${MANIFEST_PATH}" LENGTH _listExcludedPlatform)

	if(DEFINED swCount)
		sw_read_manifest_words(swExcluded "${MANIFEST_PATH}" _listExcludedPlatform platform "${SW_MODULE_PLATFORMS}")
		list(REMOVE_ITEM swPlatforms ${swExcluded})
	endif()

	if(KIND STREQUAL "Editor" OR KIND STREQUAL "EditorExtension")
		list(REMOVE_ITEM swPlatforms Android Web)
	endif()

	if(NOT swPlatforms)
		message(FATAL_ERROR "[Module] ${MANIFEST_PATH}: _listExcludedPlatform leaves no platform")
	endif()

	set(${OUT_VAR} ${swPlatforms} PARENT_SCOPE)
endfunction()

# 선택 키 `_listDependency` 를 이름(OUT_NAMES) · 최소 버전(OUT_MIN_VERSIONS, 기본 0.0.0)으로 읽는다.
function(sw_read_manifest_dependencies OUT_NAMES OUT_MIN_VERSIONS MANIFEST_PATH)
	set(swNames "")
	set(swMinVersions "")
	sw_read_optional_json_value(swCount "${MANIFEST_PATH}" LENGTH _listDependency)

	if(DEFINED swCount AND swCount GREATER 0)
		math(EXPR swLast "${swCount} - 1")

		foreach(swIndex RANGE ${swLast})
			sw_require_json_value(swDependency "${MANIFEST_PATH}" _listDependency ${swIndex} _name)
			sw_read_optional_json_value(swMinVersion "${MANIFEST_PATH}" _listDependency ${swIndex} _minVersion)

			if(NOT DEFINED swMinVersion)
				set(swMinVersion "0.0.0")
			endif()

			list(APPEND swNames ${swDependency})
			list(APPEND swMinVersions ${swMinVersion})
		endforeach()
	endif()

	set(${OUT_NAMES} ${swNames} PARENT_SCOPE)
	set(${OUT_MIN_VERSIONS} ${swMinVersions} PARENT_SCOPE)
endfunction()

# 선택 키 `_listModuleOverride`(프로젝트의 켜기/끄기 표)를 이름(OUT_NAMES) · 값(OUT_VALUES)으로 읽는다. 게임 모듈만 가진다.
function(sw_read_manifest_overrides OUT_NAMES OUT_VALUES MANIFEST_PATH KIND)
	set(swNames "")
	set(swValues "")
	sw_read_optional_json_value(swCount "${MANIFEST_PATH}" LENGTH _listModuleOverride)

	if(DEFINED swCount)
		if(NOT KIND STREQUAL "Game")
			message(FATAL_ERROR "[Module] ${MANIFEST_PATH}: only a Game module (the project) has _listModuleOverride")
		endif()

		if(swCount GREATER 0)
			math(EXPR swLast "${swCount} - 1")

			foreach(swIndex RANGE ${swLast})
				sw_require_json_value(swOverrideName "${MANIFEST_PATH}" _listModuleOverride ${swIndex} _name)
				sw_require_json_value(swOverrideValue "${MANIFEST_PATH}" _listModuleOverride ${swIndex} _bEnabled)
				list(APPEND swNames ${swOverrideName})
				list(APPEND swValues ${swOverrideValue})
			endforeach()
		endif()
	endif()

	set(${OUT_NAMES} ${swNames} PARENT_SCOPE)
	set(${OUT_VALUES} ${swValues} PARENT_SCOPE)
endfunction()

# 매니페스트 하나를 읽어 전역 속성 `SW_MODULE_<이름>_*` 에 담는다. 형식이 틀리면 구성을 세운다.
function(sw_read_module_manifest MANIFEST_PATH)
	sw_require_json_value(swName "${MANIFEST_PATH}" _name)

	if(swName STREQUAL "")
		message(FATAL_ERROR "[Module] ${MANIFEST_PATH}: _name is empty")
	endif()

	get_filename_component(swFileName "${MANIFEST_PATH}" NAME)

	if(NOT swFileName STREQUAL "${swName}.module.json")
		message(FATAL_ERROR "[Module] ${MANIFEST_PATH}: the file must be named '${swName}.module.json'")
	endif()

	sw_check_manifest_keys("${MANIFEST_PATH}")
	sw_require_json_value(swVersion "${MANIFEST_PATH}" _version)

	if(NOT swVersion MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
		message(FATAL_ERROR "[Module] ${MANIFEST_PATH}: _version must be 'major.minor.patch'")
	endif()

	sw_require_json_value(swKind "${MANIFEST_PATH}" _kind)

	if(NOT swKind IN_LIST SW_MODULE_KINDS)
		message(FATAL_ERROR "[Module] ${MANIFEST_PATH}: unknown _kind '${swKind}' (one of ${SW_MODULE_KINDS})")
	endif()

	sw_read_manifest_platforms(swPlatforms "${MANIFEST_PATH}" ${swKind})
	sw_read_manifest_words(swConfigurations "${MANIFEST_PATH}" _listConfiguration configuration "${SW_MODULE_CONFIGURATIONS}")
	sw_read_manifest_words(swTargets "${MANIFEST_PATH}" _listTarget target "${SW_MODULE_TARGETS}")

	# 선택 키 셋(`_bEnabledByDefault` · `_listDependency` · `_minVersion`)은 매니페스트 형식의 기본값이다 — 설정 파일에는 기본값과 다른 값만 적는다.
	# 같은 기본값을 런타임 `ModuleCatalog::parseManifest` 가 들고, `ModuleCatalogTest.BuildAndRuntimeAgree` 가 두 해석을 견준다.
	sw_read_optional_json_value(swEnabled "${MANIFEST_PATH}" _bEnabledByDefault)

	if(NOT DEFINED swEnabled)
		set(swEnabled ON)
	endif()

	sw_read_manifest_dependencies(swDependencyNames swDependencyMinVersions "${MANIFEST_PATH}")

	# 에디터 확장은 Dev 전용이고 EditorModule 에 의존한다(언리얼 Type "Editor" 모듈과 같다) — 런타임 `ModuleCatalog::parseManifest` 와 같은 검사.
	if(swKind STREQUAL "EditorExtension")
		if(NOT swConfigurations STREQUAL "Dev")
			message(FATAL_ERROR "[Module] ${MANIFEST_PATH}: an EditorExtension module is built for Dev only (_listConfiguration [\"Dev\"])")
		endif()

		if(NOT "EditorModule" IN_LIST swDependencyNames)
			message(FATAL_ERROR "[Module] ${MANIFEST_PATH}: an EditorExtension module must list EditorModule in _listDependency")
		endif()
	endif()

	sw_read_manifest_overrides(swOverrideNames swOverrideValues "${MANIFEST_PATH}" ${swKind})
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
	set_property(GLOBAL PROPERTY SW_MODULE_${swName}_PLATFORMS ${swPlatforms})
	set_property(GLOBAL PROPERTY SW_MODULE_${swName}_CONFIGURATIONS ${swConfigurations})
	set_property(GLOBAL PROPERTY SW_MODULE_${swName}_TARGETS ${swTargets})
	set_property(GLOBAL PROPERTY SW_MODULE_${swName}_ENABLED_BY_DEFAULT ${swEnabled})
	set_property(GLOBAL PROPERTY SW_MODULE_${swName}_DEPENDENCIES ${swDependencyNames})
	set_property(GLOBAL PROPERTY SW_MODULE_${swName}_DEPENDENCY_MIN_VERSIONS ${swDependencyMinVersions})
	set_property(GLOBAL PROPERTY SW_MODULE_${swName}_OVERRIDE_NAMES ${swOverrideNames})
	set_property(GLOBAL PROPERTY SW_MODULE_${swName}_OVERRIDE_VALUES ${swOverrideValues})
endfunction()

# 활성 게임과 함께 지을 게임 팩(`SW_GAMES`, Dev 전용)을 전역 속성 `SW_GAME_PACKS` 에 둔다 — 활성 게임은 늘 지으므로 뺀다. 없는 이름이면 구성을 세운다.
function(sw_select_game_packs)
	set(swPacks "")

	if(NOT SW_SHIPPING_BUILD AND SW_GAMES)
		set(swGamesDirectory "${CMAKE_SOURCE_DIR}/Source/Games")
		file(GLOB swListCandidate RELATIVE "${swGamesDirectory}" "${swGamesDirectory}/*")
		set(swAvailable "")

		foreach(swCandidate IN LISTS swListCandidate)
			if(EXISTS "${swGamesDirectory}/${swCandidate}/CMakeLists.txt")
				list(APPEND swAvailable ${swCandidate})
			endif()
		endforeach()

		if(SW_GAMES STREQUAL "ALL")
			set(swRequested ${swAvailable})
		else()
			set(swRequested ${SW_GAMES})
		endif()

		foreach(swGame IN LISTS swRequested)
			if(NOT swGame IN_LIST swAvailable)
				message(FATAL_ERROR "[Games] SW_GAMES names '${swGame}', which has no CMakeLists.txt under Source/Games/ (available: ${swAvailable})")
			endif()

			if(NOT swGame STREQUAL SW_ACTIVE_GAME)
				list(APPEND swPacks ${swGame})
			endif()
		endforeach()

		list(REMOVE_DUPLICATES swPacks)
		list(SORT swPacks)
	endif()

	set_property(GLOBAL PROPERTY SW_GAME_PACKS ${swPacks})
endfunction()

# 함께 짓는 게임 팩(활성 게임이 아닌 것)의 매니페스트에서 의존 이름과 켠 모듈을 읽어 `SW_GAME_PACK_<게임>_DEPENDENCIES` · `_ENABLED_OVERRIDES` 에 둔다.
# 모듈 표(`SW_MODULE_NAMES`)에는 넣지 않는다 — 게임 모듈 이름은 모두 SWGame 이고, 이 빌드의 켜기/끄기 표는 활성 게임의 것이다.
# 켠 모듈은 의존과 따로 둔다 — 의존은 게임이 링크하지만, 켠 모듈(서버 전용 저장 드라이버)은 짓고 올리기만 한다(언리얼 `.uproject` 의 Plugins 항목. 런타임 `appendGameSeeds` 와 같은 규칙).
function(sw_read_game_pack_manifest MANIFEST_PATH GAME_NAME)
	sw_require_json_value(swKind "${MANIFEST_PATH}" _kind)

	if(NOT swKind STREQUAL "Game")
		message(FATAL_ERROR "[Module] ${MANIFEST_PATH}: a game pack manifest must have _kind Game")
	endif()

	sw_read_manifest_dependencies(swDependencyNames swIgnoredMinVersions "${MANIFEST_PATH}")
	set_property(GLOBAL PROPERTY SW_GAME_PACK_${GAME_NAME}_DEPENDENCIES ${swDependencyNames})
	sw_read_manifest_overrides(swOverrideNames swOverrideValues "${MANIFEST_PATH}" "${swKind}")
	set(swEnabledNames "")
	set(swOverrideIndex 0)

	foreach(swOverrideName IN LISTS swOverrideNames)
		list(GET swOverrideValues ${swOverrideIndex} swOverrideValue)

		if(swOverrideValue)
			list(APPEND swEnabledNames ${swOverrideName})
		endif()

		math(EXPR swOverrideIndex "${swOverrideIndex} + 1")
	endforeach()

	set_property(GLOBAL PROPERTY SW_GAME_PACK_${GAME_NAME}_ENABLED_OVERRIDES ${swEnabledNames})
endfunction()

# 저장소의 매니페스트를 모두 읽는다. 고르지 않은 게임의 매니페스트는 읽지 않고, 함께 짓는 게임 팩(`SW_GAMES`)은 의존만 읽는다
# (게임 모듈 이름은 모두 SWGame 이다 — `sw_read_game_pack_manifest`).
function(sw_read_all_module_manifests)
	sw_glob_source_tree(swListManifest RECURSE "${CMAKE_SOURCE_DIR}/Source/*.module.json")
	list(SORT swListManifest)
	# 매니페스트의 **내용**(켜짐 · 의존 · 종류)이 무엇을 지을지 정한다 — 내용이 바뀌면 다시 구성해야 한다(GLOB 의 CONFIGURE_DEPENDS 는 목록만 본다).
	set_property(DIRECTORY "${CMAKE_SOURCE_DIR}" APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${swListManifest})

	sw_select_game_packs()
	get_property(swGamePacks GLOBAL PROPERTY SW_GAME_PACKS)

	foreach(swManifest IN LISTS swListManifest)
		if(swManifest MATCHES "/Source/Games/([^/]+)/" AND NOT CMAKE_MATCH_1 STREQUAL SW_ACTIVE_GAME)
			set(swGame "${CMAKE_MATCH_1}")
			get_filename_component(swManifestFile "${swManifest}" NAME)

			if(swGame IN_LIST swGamePacks AND swManifestFile STREQUAL "SWGame.module.json")
				sw_read_game_pack_manifest("${swManifest}" ${swGame})
			endif()

			continue()
		endif()

		sw_read_module_manifest("${swManifest}")
	endforeach()
endfunction()

# 모듈 NAME 을 끄고 까닭을 남긴다.
function(sw_deactivate_module NAME REASON)
	set_property(GLOBAL PROPERTY SW_MODULE_${NAME}_ACTIVE OFF)
	set_property(GLOBAL PROPERTY SW_MODULE_${NAME}_REASON "${REASON}")
endfunction()

# 모듈마다 켜짐을 정해 전역 속성에 남기고, 켜진 이름을 OUT_ACTIVE 에 담는다. 프로젝트의 켜기/끄기 표에 모르는 이름이 있으면 오류다.
function(sw_decide_module_activation OUT_ACTIVE)
	get_property(swNames GLOBAL PROPERTY SW_MODULE_NAMES)
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

	# 플랫폼 · 구성 이름은 sw_platform_name · sw_configuration_name(cmake/Engine/BuildLayout.cmake) — 배포 백엔드 확인과 같은 낱말이다.
	# 빌드 타깃이 담는 모듈 대상 — Game 은 둘 다.
	if(SW_TARGET_TYPE STREQUAL "Game")
		set(swBuildTargets Client Server)
	else()
		set(swBuildTargets ${SW_TARGET_TYPE})
	endif()

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
			sw_deactivate_module(${swName} "${swReason}")
		elseif(NOT sw_platform_name IN_LIST swPlatforms)
			sw_deactivate_module(${swName} "not available on ${sw_platform_name}")
		elseif(NOT sw_configuration_name IN_LIST swConfigurations)
			sw_deactivate_module(${swName} "not built for ${sw_configuration_name}")
		elseif(NOT swTargetHit)
			sw_deactivate_module(${swName} "not built for the ${SW_TARGET_TYPE} target")
		else()
			set_property(GLOBAL PROPERTY SW_MODULE_${swName}_ACTIVE ON)
			list(APPEND swActive ${swName})
		endif()
	endforeach()

	set(${OUT_ACTIVE} ${swActive} PARENT_SCOPE)
endfunction()

# 에디터 확장은 의존(EditorModule · 키트)이 하나라도 꺼져 있으면 함께 꺼진다 — 오류가 아니다(그 확장만 없다). 런타임 `ModuleCatalog::resolve` 와 같다.
# 의존이 쓰지 않아 뺀 키트면 확장도 그 목록(INOUT_UNUSED)에 넣는다 — 그 매니페스트도 `Bin/Modules` 에 두지 않는다.
function(sw_deactivate_extensions_of_inactive_dependencies INOUT_ACTIVE INOUT_UNUSED)
	get_property(swNames GLOBAL PROPERTY SW_MODULE_NAMES)
	set(swActive ${${INOUT_ACTIVE}})
	set(swUnused ${${INOUT_UNUSED}})

	# 확장이 확장에 의존할 수 있고 목록은 이름 순이다 — 뒤의 확장이 꺼지면 앞의 확장도 꺼져야 하므로 바뀌는 것이 없을 때까지 돈다.
	set(swChanged ON)
	while(swChanged)
		set(swChanged OFF)
		foreach(swName IN LISTS swActive)
			get_property(swKind GLOBAL PROPERTY SW_MODULE_${swName}_KIND)
			if(NOT swKind STREQUAL "EditorExtension")
				continue()
			endif()
			get_property(swDependencies GLOBAL PROPERTY SW_MODULE_${swName}_DEPENDENCIES)
			foreach(swDependency IN LISTS swDependencies)
				if(NOT swDependency IN_LIST swNames)
					break()
				endif()
				get_property(swDependencyActive GLOBAL PROPERTY SW_MODULE_${swDependency}_ACTIVE)
				if(NOT swDependencyActive)
					get_property(swDependencyReason GLOBAL PROPERTY SW_MODULE_${swDependency}_REASON)
					sw_deactivate_module(${swName} "needs ${swDependency}, which is ${swDependencyReason}")
					list(REMOVE_ITEM swActive ${swName})
					if(swDependency IN_LIST swUnused)
						list(APPEND swUnused ${swName})
					endif()
					set(swChanged ON)
					break()
				endif()
			endforeach()
		endforeach()
	endwhile()

	set(${INOUT_ACTIVE} ${swActive} PARENT_SCOPE)
	set(${INOUT_UNUSED} ${swUnused} PARENT_SCOPE)
endfunction()

# 켜진 모듈(ACTIVE)의 의존은 있고 · 켜져 있고 · 버전이 맞아야 한다 — 아니면 구성을 세운다.
function(sw_require_active_dependencies ACTIVE)
	get_property(swNames GLOBAL PROPERTY SW_MODULE_NAMES)

	foreach(swName IN LISTS ACTIVE)
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
endfunction()

# Dev 는 매니페스트를 실행 파일 옆 `Modules/` 에 둔다 — App 이 같은 규칙으로 다시 해석해 적재 순서를 정한다. 꺼진 모듈의 것도 둔다(무엇이 왜 꺼졌는지 App 도 안다).
# 쓰지 않아 뺀 키트(UNUSED)는 두지 않는다 — 런타임에는 그 규칙이 없다.
function(sw_write_runtime_module_catalog LOAD_ORDER UNUSED)
	get_property(swNames GLOBAL PROPERTY SW_MODULE_NAMES)
	set(swCatalogDirectory "${CMAKE_BINARY_DIR}/Bin/Modules")
	file(MAKE_DIRECTORY "${swCatalogDirectory}")
	set(swKeptManifests "")
	foreach(swName IN LISTS swNames)
		if(NOT swName IN_LIST UNUSED)
			list(APPEND swKeptManifests "${swName}")
		endif()
	endforeach()
	# 카탈로그는 스스로 닫혀 있어야 한다 — 남긴 매니페스트의 의존이 빠졌으면(쓰지 않아 뺀 키트) 그것도 뺀다. 이 구성의 대상이 아니라 꺼진 모듈
	# (서버 빌드의 GF_Client_* — 런타임이 대상으로 거른다)은 위 목록에 남는데, 그 의존 키트는 빠져 런타임 해석이 "의존 매니페스트 없음" 으로 졌다
	# (ModuleCatalogLoaderTest.LoadsManifestsBesideTheExecutable, GF_Client_ServerDirectory -> GF_ServerDirectory). 뺀 것이 또 빼게 하므로 바뀌지 않을 때까지 돈다.
	set(swChanged ON)
	while(swChanged)
		set(swChanged OFF)
		foreach(swName IN LISTS swKeptManifests)
			get_property(swDependencies GLOBAL PROPERTY SW_MODULE_${swName}_DEPENDENCIES)
			foreach(swDependency IN LISTS swDependencies)
				if(swDependency IN_LIST swNames AND NOT swDependency IN_LIST swKeptManifests)
					list(REMOVE_ITEM swKeptManifests "${swName}")
					set(swChanged ON)
					break()
				endif()
			endforeach()
		endforeach()
	endwhile()

	# 옛 매니페스트는 **복사 전에** 지운다. Windows 에서 대소문자만 바뀐 이름(`GF_ClassicJRPG` → `GF_ClassicJRPG`)은 같은 파일이라
	# 복사가 옛 철자 파일 이름을 그대로 두고, 복사 뒤에 글자 그대로 비교해 지우면 방금 복사한 매니페스트를 지운다.
	file(GLOB swExistingList "${swCatalogDirectory}/*.module.json")
	foreach(swExisting IN LISTS swExistingList)
		get_filename_component(swExistingFile "${swExisting}" NAME)
		string(REGEX REPLACE "\\.module\\.json$" "" swExistingName "${swExistingFile}")
		if(NOT swExistingName IN_LIST swKeptManifests)
			file(REMOVE "${swExisting}")
		endif()
	endforeach()

	foreach(swName IN LISTS swKeptManifests)
		get_property(swFile GLOBAL PROPERTY SW_MODULE_${swName}_FILE)
		configure_file("${swFile}" "${swCatalogDirectory}/${swName}.module.json" COPYONLY)
	endforeach()

	list(JOIN LOAD_ORDER "\n" swOrderText)
	file(CONFIGURE OUTPUT "${swCatalogDirectory}/ResolvedModules.txt" CONTENT "${swOrderText}\n")
	# `Modules/SWGame` 이 어느 게임인가 — `-game=` 이 없을 때 `GameSelection` 이 읽는다(이름을 엔진 TU 에 굽지 않는다).
	file(CONFIGURE OUTPUT "${swCatalogDirectory}/${SW_FILE_ACTIVE_GAME}" CONTENT "${SW_ACTIVE_GAME}\n")

	sw_remove_stale_binary_outputs("${LOAD_ORDER}")
endfunction()

# 저장소의 매니페스트를 모두 읽고 해석한다.
# 결과: 전역 속성 `SW_MODULE_<이름>_ACTIVE`(ON/OFF) · `_REASON`, `SW_MODULE_LOAD_ORDER`. 틀리면 구성을 세운다.
function(sw_resolve_module_manifests)
	sw_read_all_module_manifests()
	get_property(swNames GLOBAL PROPERTY SW_MODULE_NAMES)

	if(NOT "SWGame" IN_LIST swNames)
		message(FATAL_ERROR "[Module] Source/Games/${SW_ACTIVE_GAME}/SWGame.module.json is missing — the project manifest decides which modules are on")
	endif()

	sw_decide_module_activation(swActive)

	# 활성 게임이 쓰지 않는 키트(`SW_BUILD_UNUSED_KITS=OFF`)는 짓지 않는다. 아래 카탈로그가 그 매니페스트를 `Bin/Modules/` 에 두지 않으므로
	# 런타임 카탈로그도 그 키트를 모른다 — 고르지 않은 게임의 매니페스트와 같다(`ModuleCatalogTest.BuildAndRuntimeAgree`).
	set(swUnusedKits "")

	if(DEFINED SW_BUILD_UNUSED_KITS AND NOT SW_BUILD_UNUSED_KITS)
		sw_collect_unused_kits(swActive swUnusedKits)

		foreach(swName IN LISTS swUnusedKits)
			sw_deactivate_module(${swName} "not used by ${SW_ACTIVE_GAME} or SW_GAMES (SW_BUILD_UNUSED_KITS=OFF)")
			list(REMOVE_ITEM swActive ${swName})
		endforeach()
	endif()

	sw_deactivate_extensions_of_inactive_dependencies(swActive swUnusedKits)
	sw_require_active_dependencies("${swActive}")

	# 적재 순서 — 의존이 먼저, 동점은 이름 순(켜진 모듈의 의존은 모두 켜져 있음을 위에서 확인했다).
	set(swOrder ${swActive})
	sw_sort_modules_by_dependency(swOrder)
	set_property(GLOBAL PROPERTY SW_MODULE_LOAD_ORDER ${swOrder})

	if(NOT SW_SHIPPING_BUILD)
		sw_write_runtime_module_catalog("${swOrder}" "${swUnusedKits}")
	endif()

	list(LENGTH swNames swManifestCount)
	list(LENGTH swActive swActiveCount)
	message(STATUS "[Module] ${swActiveCount} of ${swManifestCount} modules active (${sw_platform_name} ${sw_configuration_name} ${SW_TARGET_TYPE})")
endfunction()

# 모듈 이름 목록(LIST_VAR)을 의존이 먼저 · 동점은 이름 순으로 줄 세운다(Kahn — 런타임 `DependencyGraph::sortByName` 와 같다).
# 목록 밖의 의존은 보지 않는다(키트 목록에서 GameFramework 같은 것). 아무도 못 고르면 순환이다 — 구성을 세운다.
function(sw_sort_modules_by_dependency LIST_VAR)
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
# 의존 순서인 이유: 서버 · 클라이언트 키트가 같은 기능의 런타임 키트 타깃을 링크한다(`sw_link_runtime_kit` — 그 타깃이 먼저 있어야 한다).
function(sw_get_module_directories_of_kind KIND OUT_VAR)
	get_property(swNames GLOBAL PROPERTY SW_MODULE_NAMES)
	set(swOfKind "")

	foreach(swName IN LISTS swNames)
		get_property(swKind GLOBAL PROPERTY SW_MODULE_${swName}_KIND)

		if(swKind STREQUAL KIND)
			list(APPEND swOfKind ${swName})
		endif()
	endforeach()

	sw_sort_modules_by_dependency(swOfKind)
	set(swDirectories "")

	foreach(swName IN LISTS swOfKind)
		get_property(swDirectory GLOBAL PROPERTY SW_MODULE_${swName}_DIR)
		list(APPEND swDirectories "${swDirectory}")
	endforeach()

	set(${OUT_VAR} ${swDirectories} PARENT_SCOPE)
endfunction()

# 모듈 NAME 의 매니페스트 의존 가운데 종류가 KIND(Kit · GameFramework · …)인 것을 OUT_VAR 에(선언 순서 그대로).
function(sw_get_module_dependencies_of_kind NAME KIND OUT_VAR)
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
# 프로젝트가 `_listModuleOverride` 로 켠 키트에서 의존을 따라 닿는 키트다. `SW_BUILD_UNUSED_KITS=OFF` 일 때 `sw_resolve_module_manifests` 가 이 키트들을 끈다.
function(sw_collect_unused_kits ACTIVE_LIST_VAR OUT_VAR)
	get_property(swOverrideNames GLOBAL PROPERTY SW_MODULE_SWGame_OVERRIDE_NAMES)
	get_property(swOverrideValues GLOBAL PROPERTY SW_MODULE_SWGame_OVERRIDE_VALUES)
	set(swPending "")

	foreach(swName IN LISTS ${ACTIVE_LIST_VAR})
		get_property(swKind GLOBAL PROPERTY SW_MODULE_${swName}_KIND)
		set(swSeed OFF)

		# 에디터 확장은 자기 키트를 쓰는 쪽이 아니다 — 키트가 쓰이지 않으면 확장이 따라 꺼진다(`sw_deactivate_extensions_of_inactive_dependencies`).
		if(swKind STREQUAL "EditorExtension")
		elseif(NOT swKind STREQUAL "Kit")
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

	# 함께 짓는 게임 팩(`SW_GAMES`)의 의존도 쓰는 쪽이다 — 키트는 고른 게임들의 합집합이 된다.
	get_property(swGamePacks GLOBAL PROPERTY SW_GAME_PACKS)

	foreach(swGame IN LISTS swGamePacks)
		get_property(swPackDependencies GLOBAL PROPERTY SW_GAME_PACK_${swGame}_DEPENDENCIES)
		get_property(swPackOverrides GLOBAL PROPERTY SW_GAME_PACK_${swGame}_ENABLED_OVERRIDES)
		list(APPEND swPending ${swPackDependencies} ${swPackOverrides})
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
		# 키트 하나는 최대 세 모듈이다(공유 · 서버 전용 · 클라이언트 전용, `ModuleCatalog.h`) — 게임은 공유 모듈에만 의존할 수 있으므로(서버 전용은 클라이언트 구성에 없다) 공유 모듈을 쓰면 짝도 쓰인다.
		# 언리얼 플러그인을 켜면 그 플러그인의 Runtime · ServerOnly 모듈이 함께 켜지는 것과 같다(기억에 의존). 대상이 맞지 않는 짝은 앞의 대상 검사가 이미 껐다.
		if(swName MATCHES "^GF_(.+)$" AND NOT swName MATCHES "^GF_(Server|Client|EditorPlugin)_")
			list(APPEND swPending GF_Server_${CMAKE_MATCH_1} GF_Client_${CMAKE_MATCH_1})
		endif()
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
