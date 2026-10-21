# ==============================================================================
# @file Source/Editor/CMakeLists.txt
# @brief 인게임 에디터 모듈 (개발 빌드 MODULE DLL)
# ==============================================================================

# ------------------------------------------------------------------------------
# 1) EditorModule: Dev 용 SHARED 이고, imgui 계열만 직접 링크한다(모듈 라이브러리 기본값은 sw_addModuleLibrary)
#    SHARED 인 까닭: 확장 모듈(GF_EditorPlugIn_* · SWGameEditor)이 링크해 레지스트리 · 위젯 · 선택 · Undo 를 쓴다(언리얼 UnrealEd 와 같다).
#    핫 리로드는 키트와 같은 경로(섀도 복사 + 지연 로드 결속)다.
#    유니티 빌드: TU 내부 도우미는 모두 익명 네임스페이스의 *Internal 구조체로 묶여 있다
# ------------------------------------------------------------------------------
file(GLOB_RECURSE listEditorSource CONFIGURE_DEPENDS "*.cpp" "*.c" "*.h" "*.hpp")

# 배포는 에디터를 짓지 않는다 — 소스 검사 게이트가 알도록 적는다(`sw_declareUnbuiltSources`). EditorTest 가 가져가는 몇 개는 그쪽에서 그대로 지어진다.
if(SW_SHIPPING_BUILD)
	sw_declareUnbuiltSources(${listEditorSource})
	return()
endif()

# 배포본이 아니어도 프로젝트가 에디터 모듈을 끌 수 있다(매니페스트 `EditorModule.module.json`).
sw_skipInactiveModule(EditorModule swSkipEditor)
if(swSkipEditor)
	return()
endif()

# 에디터 폴더는 층이고 아래 층은 위 층의 헤더를 include 하지 못한다. 층과 티어의 표는 `Scripts/lint/rules/GenerateEditorLayers.toml` 이고
# configure 가 `EditorLayers.cmake` 로 옮긴다(`Scripts/generate/GenerateEditorLayers.py` — 표와 폴더가 어긋나면 거기서 멈춘다).
# 2) 가 티어마다 부분(OBJECT 라이브러리)을 짓고, 부분마다 자기 티어보다 위 층의 모든 헤더 자리에 `#error` 껍데기를 먼저 놓는다.
# EditorModule 자신은 `Source/Editor` 바로 아래 파일(셸 · 모든 매니저를 만드는 조립 파일)만 가진다.
include("${CMAKE_BINARY_DIR}/generated/sw/config/EditorLayers.cmake")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/Scripts/lint/rules/GenerateEditorLayers.toml")

# ImGui 를 모르는 층(모델: 에셋 · 커맨드 · 설정 · 로컬라이즈 · 버전 관리 · 작업 공간)은 imgui 링크가 없는 부분(`*Model`)이 짓는다.
set(listEditorModelLayer Common/Asset Common/Commands Common/Config Common/Localization Common/SourceControl Common/Workspace)

# 층 하나의 소스(`<OUT>Source`)와 헤더(`<OUT>Header`, Source/Editor 기준 상대 경로). `Common` 은 바로 아래 파일만, `<root>` 는 Source/Editor 바로 아래 파일만.
function(sw_globEditorLayer LAYER OUT)
	if(LAYER STREQUAL "<root>")
		set(layerPrefix "")
		set(globMode GLOB)
	elseif(LAYER STREQUAL "Common")
		set(layerPrefix "Common/")
		set(globMode GLOB)
	else()
		set(layerPrefix "${LAYER}/")
		set(globMode GLOB_RECURSE)
	endif()
	file(${globMode} layerSource CONFIGURE_DEPENDS "${layerPrefix}*.cpp" "${layerPrefix}*.c" "${layerPrefix}*.h" "${layerPrefix}*.hpp")
	file(${globMode} layerHeader RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" CONFIGURE_DEPENDS "${layerPrefix}*.h" "${layerPrefix}*.hpp" "${layerPrefix}*.inl")
	set(${OUT}Source ${layerSource} PARENT_SCOPE)
	set(${OUT}Header ${layerHeader} PARENT_SCOPE)
endfunction()

set(listEditorRootSource "")
set(listEditorPartSource "")
foreach(tier IN LISTS SW_EDITOR_TIER_LIST)
	set(swEditorTierModelSource_${tier} "")
	set(swEditorTierViewSource_${tier} "")
	foreach(layer IN LISTS SW_EDITOR_TIER_${tier}_LAYERS)
		string(MAKE_C_IDENTIFIER "${layer}" layerKey)
		sw_globEditorLayer("${layer}" swEditorLayer_${layerKey})
		if(layer STREQUAL "<root>")
			list(APPEND listEditorRootSource ${swEditorLayer_${layerKey}Source})
		elseif(layer IN_LIST listEditorModelLayer)
			list(APPEND swEditorTierModelSource_${tier} ${swEditorLayer_${layerKey}Source})
		else()
			list(APPEND swEditorTierViewSource_${tier} ${swEditorLayer_${layerKey}Source})
		endif()
	endforeach()
	list(APPEND listEditorPartSource ${swEditorTierModelSource_${tier}} ${swEditorTierViewSource_${tier}})
endforeach()
set(listEditorUnassignedSource ${listEditorSource})
list(REMOVE_ITEM listEditorUnassignedSource ${listEditorRootSource} ${listEditorPartSource})
if(listEditorUnassignedSource)
	message(FATAL_ERROR "Editor sources outside every layer in GenerateEditorLayers.toml: ${listEditorUnassignedSource}")
endif()
set(listEditorSource ${listEditorRootSource})

sw_addModuleLibrary(EditorModule
	KIND editor
	DEV_TYPE SHARED
	EXPORTS EDITOR
	LOG_TAG "Editor"
	FOLDER "Source/Editor"
	SOURCES ${listEditorSource}
	LINK_PRIVATE Engine RuntimeAPI imgui imgui_nodeeditor imgui_notify imguizmo implot Microsoft::DirectXTex cgltf meshoptimizer stb sw_graphics_libs
	UNITY_BATCH 8
	REFLECTION_HEADERS
		"${CMAKE_CURRENT_SOURCE_DIR}/Common/Config/EditorConfig.h"
		"${CMAKE_CURRENT_SOURCE_DIR}/Common/Config/EditorPreferences.h"
		"${CMAKE_CURRENT_SOURCE_DIR}/Common/Config/EditorThemeTokens.h"
		"${CMAKE_CURRENT_SOURCE_DIR}/Common/Config/EditorToolDefaults.h"
		"${CMAKE_CURRENT_SOURCE_DIR}/Common/GUI/EditorChrome.h"
		"${CMAKE_CURRENT_SOURCE_DIR}/Common/Workspace/EditorNetPlaySettings.h"
		"${CMAKE_CURRENT_SOURCE_DIR}/SelfTest/EditorCurveProbeComponent.h"
)

# ------------------------------------------------------------------------------
# 2) 티어마다 부분(`EditorTier<N>Model` · `EditorTier<N>View`): 오브젝트는 EditorModule DLL 안에 그대로 들어간다(`sw_addModulePart`).
#    a) 위 층 헤더 껍데기: 부분 N 은 티어가 N 보다 큰 층의 헤더마다 같은 경로의 `#error` 파일을 자기 include 자리 맨 앞에 둔다. `Editor/Panels/X.h` 를
#       include 하면 진짜 헤더 대신 껍데기를 찾아 멈춘다(헤더를 거쳐 닿는 전이 include 도 같다). 껍데기 폴더는 `<빌드>/generated/sw/EditorLayerStubs/tier<N>`.
#       표의 `[prelude]` 헤더(위 층을 include 하지 않는 잎)는 어느 층에서나 쓰므로 껍데기를 놓지 않는다.
#    b) ImGui 를 모르는 `*Model`: vcpkg 는 모든 패키지 헤더를 한 include 자리에 두므로(DirectXTex · meshoptimizer 를 링크하면 imgui.h 도 보인다)
#       include 자리만으로는 막지 못한다. 그래서 ImGui 가 사용자 설정을 읽는 자리(`IMGUI_USER_CONFIG`, imgui.h 첫머리)에 #error 헤더를 준다.
#       imgui.h 를 거치는 헤더(implot · ImGuizmo · 노드 편집기 · 백엔드 포함)를 include 하면 컴파일이 멈춘다.
# ------------------------------------------------------------------------------
set(editorModelImGuiBlock "${CMAKE_BINARY_DIR}/generated/sw/EditorModelForbidsImGui.h")
file(GENERATE OUTPUT "${editorModelImGuiBlock}" CONTENT
	"// EditorModel: generated - the editor model layer (Source/Editor/CMakeLists.txt listEditorModelLayer) must not include ImGui\n#error \"EditorModel (ImGui-free editor layer) includes an ImGui header. Move the ImGui code to a view file outside listEditorModelLayer.\"\n")

# `TIER` 부분에 놓을 껍데기를 만든다(내용이 같으면 쓰지 않아 다시 짓지 않는다). 사라진 헤더의 껍데기는 지운다.
function(sw_addEditorLayerStubs PART TIER)
	set(stubRoot "${CMAKE_BINARY_DIR}/generated/sw/EditorLayerStubs/tier${TIER}")
	set(listNewStub "")
	foreach(upperTier IN LISTS SW_EDITOR_TIER_LIST)
		if(NOT upperTier GREATER TIER)
			continue()
		endif()
		foreach(layer IN LISTS SW_EDITOR_TIER_${upperTier}_LAYERS)
			string(MAKE_C_IDENTIFIER "${layer}" layerKey)
			foreach(header IN LISTS swEditorLayer_${layerKey}Header)
				if("Editor/${header}" IN_LIST SW_EDITOR_PRELUDE_HEADERS)
					continue()
				endif()
				set(stubPath "${stubRoot}/Editor/${header}")
				set(stubContent "#error \"Editor layer '${layer}' (tier ${upperTier}) cannot be included from tier ${TIER} code. A lower layer must not see an upper layer: move what you need down, or invert the dependency (docs/09_Decisions.md 5-11, Scripts/lint/rules/GenerateEditorLayers.toml).\"\n")
				set(oldContent "")
				if(EXISTS "${stubPath}")
					file(READ "${stubPath}" oldContent)
				endif()
				if(NOT oldContent STREQUAL stubContent)
					file(WRITE "${stubPath}" "${stubContent}")
				endif()
				list(APPEND listNewStub "${stubPath}")
			endforeach()
		endforeach()
	endforeach()
	set(manifestPath "${stubRoot}.manifest")
	if(EXISTS "${manifestPath}")
		file(STRINGS "${manifestPath}" listOldStub)
		list(REMOVE_ITEM listOldStub ${listNewStub})
		foreach(oldStub IN LISTS listOldStub)
			file(REMOVE "${oldStub}")
		endforeach()
	endif()
	string(REPLACE ";" "\n" manifestContent "${listNewStub}")
	file(WRITE "${manifestPath}" "${manifestContent}\n")
	target_include_directories(${PART} BEFORE PRIVATE "${stubRoot}")
endfunction()

set(listEditorPart "")
foreach(tier IN LISTS SW_EDITOR_TIER_LIST)
	if(swEditorTierModelSource_${tier})
		sw_addModulePart(EditorTier${tier}Model
			OWNER EditorModule
			SOURCES ${swEditorTierModelSource_${tier}}
			LINK Engine RuntimeAPI Microsoft::DirectXTex cgltf meshoptimizer stb
		)
		target_compile_definitions(EditorTier${tier}Model PRIVATE "IMGUI_USER_CONFIG=\"${editorModelImGuiBlock}\"")
		sw_addEditorLayerStubs(EditorTier${tier}Model ${tier})
		list(APPEND listEditorPart EditorTier${tier}Model)
	endif()
	if(swEditorTierViewSource_${tier})
		sw_addModulePart(EditorTier${tier}View
			OWNER EditorModule
			SOURCES ${swEditorTierViewSource_${tier}}
			LINK Engine RuntimeAPI imgui imgui_nodeeditor imgui_notify imguizmo implot Microsoft::DirectXTex cgltf meshoptimizer stb sw_graphics_libs
		)
		sw_addEditorLayerStubs(EditorTier${tier}View ${tier})
		list(APPEND listEditorPart EditorTier${tier}View)
	endif()
endforeach()

# 에디터는 LTO 를 받지 않는다(링크 시간 — Test/ · Tools/ 와 같은 이유).
set_target_properties(EditorModule ${listEditorPart} PROPERTIES INTERPROCEDURAL_OPTIMIZATION FALSE)

# 확장 모듈은 에디터 헤더(Source/ 기준 경로)를 쓴다. ImGui 링크는 확장이 스스로 한다 — vcpkg imgui 는 정적이라 DLL 마다 사본이고 결속기가 컨텍스트를 맞춘다.
target_include_directories(EditorModule INTERFACE "${CMAKE_SOURCE_DIR}/Source")

# 에디터 빌드 단추(Build > Compile Editor)가 짓는 묶음 — EditorModule + 모든 확장 모듈(sw_addEditorExtension 이 의존을 더한다).
add_custom_target(EditorAll DEPENDS EditorModule)
set_target_properties(EditorAll PROPERTIES FOLDER "Source/Editor")
