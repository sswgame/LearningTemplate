# ==============================================================================
# @file Source/Editor/CMakeLists.txt
# @brief 인게임 에디터 모듈 (개발 빌드 MODULE DLL)
# ==============================================================================

# ------------------------------------------------------------------------------
# 1) EditorModule: Dev 용 SHARED 이고, imgui 계열만 직접 링크한다(모듈 라이브러리 기본값은 sw_addModuleLibrary)
#    SHARED 인 까닭: 확장 모듈(GF_EditorPlugin_* · SWGameEditor)이 링크해 레지스트리 · 위젯 · 선택 · Undo 를 쓴다(언리얼 UnrealEd 와 같다).
#    핫 리로드는 키트와 같은 경로(섀도 복사 + 지연 로드 결속)다.
#    유니티 빌드: TU 내부 도우미는 모두 익명 네임스페이스의 *Internal 구조체로 묶여 있다
# ------------------------------------------------------------------------------
file(GLOB_RECURSE listEditorSource CONFIGURE_DEPENDS "*.cpp" "*.c" "*.h" "*.hpp")
set(listEditorAllSource ${listEditorSource})

# 에디터 폴더는 층이고 아래 층은 위 층의 헤더를 include 하지 못한다. 층과 티어의 표는 `Scripts/generate/rules/EditorLayers.toml` 이고
# configure 가 `EditorLayers.cmake` 로 옮긴다(`Scripts/generate/EditorLayers.py` — 표와 폴더가 어긋나면 거기서 멈춘다).
# 2) 가 티어마다 부분(OBJECT 라이브러리)을 짓고, 부분마다 자기 티어보다 위 층의 모든 헤더 자리에 `#error` 껍데기를 먼저 놓는다.
# EditorModule 자신은 `Source/Editor` 바로 아래 파일(셸 · 모든 매니저를 만드는 조립 파일)만 가진다.
include("${CMAKE_BINARY_DIR}/generated/sw/config/EditorLayers.cmake")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/Scripts/generate/rules/EditorLayers.toml")

# ImGui 를 모르는 층(모델: 에셋 · 커맨드 · 설정 · 로컬라이즈 · 버전 관리 · 에디터 작업 · 작업 공간)과, 뷰 층(Panels · Viewport)의 `Model` 폴더(`Panels/<패널>/Model` · `Viewport/Model`)는
# imgui 링크가 없는 부분(`*Model`)이 짓는다. 뷰 층 파일이 ImGui 를 쓰지 않게 되면 `Model` 폴더로 옮긴다(`git mv` + include 경로).
set(listEditorModelLayer Common/Asset Common/Commands Common/Config Common/Localization Common/SourceControl Common/Task Common/Workspace)

# EditorUITest(ImGui 컨텍스트만 있으면 도는 테스트)가 EditorModule 대신 직접 컴파일하는 에디터 파일. 이 파일들의 주인이 목록을 선언하고 테스트는 속성만 읽는다(`SW_EDITOR_UI_TEST_SOURCES`).
# 폴더(Common/Backend/Platform)는 폴더째 모아 새 플랫폼 백엔드가 저절로 따라오고(못 지으면 링크 오류로 드러난다), 폴더로 가를 수 없는 최상위 두 파일만 이름을 적는다 -
# 같은 폴더의 EditorUICapture · DrawSnapshotHandoff 는 엔진 밖 의존을 끌어 EditorUITest 가 받을 수 없고, 이 둘을 가르는 선언이 달리 없다. 이 둘이 이 목록의 유일한 손 입력이며,
# 없어지면 구성이 멈춘다. `Render/` 는 RHI 디바이스가 필요해 뺀다.
file(GLOB listEditorUITestSource CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Common/Backend/Platform/*.cpp")
list(APPEND listEditorUITestSource
	"${CMAKE_CURRENT_SOURCE_DIR}/Common/Backend/ImGuiOsInputFilter.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Common/Backend/ImGuiKeyForward.cpp")
foreach(source IN LISTS listEditorUITestSource)
	if(NOT source IN_LIST listEditorAllSource)
		message(FATAL_ERROR "[EditorUITest] ${source} is not an Editor source (SW_EDITOR_UI_TEST_SOURCES in Source/Editor/CMakeLists.txt)")
	endif()
endforeach()
set_property(GLOBAL PROPERTY SW_EDITOR_UI_TEST_SOURCES "${listEditorUITestSource}")

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
set(listEditorViewModelRegex "")   # 뷰 층 안 `Model` 폴더의 절대 경로 정규식(코어 리플렉션 입력 · EditorModule 훑기 제외에 같이 쓴다)
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
			# 뷰 층 안의 `Model` 폴더(`Panels/<패널>/Model` · `Viewport/Model`)는 ImGui 를 모르는 헤드리스 파일이라 같은 티어의 모델 부분이 짓는다.
			set(layerModelSource ${swEditorLayer_${layerKey}Source})
			set(layerViewSource ${swEditorLayer_${layerKey}Source})
			set(layerModelRegex "/Source/Editor/${layer}/([^/]+/)*Model/")
			if(NOT layer STREQUAL "Common")   # Common 은 바로 아래 파일만 가져(하위 폴더는 listEditorModelLayer 가 코어로 담는다) 여기 Model 폴더가 없다
				list(APPEND listEditorViewModelRegex "${layerModelRegex}")
			endif()
			list(FILTER layerModelSource INCLUDE REGEX "${layerModelRegex}")
			list(FILTER layerViewSource EXCLUDE REGEX "${layerModelRegex}")
			list(APPEND swEditorTierModelSource_${tier} ${layerModelSource})
			list(APPEND swEditorTierViewSource_${tier} ${layerViewSource})
		endif()
	endforeach()
	list(APPEND listEditorPartSource ${swEditorTierModelSource_${tier}} ${swEditorTierViewSource_${tier}})
endforeach()
set(listEditorUnassignedSource ${listEditorSource})
list(REMOVE_ITEM listEditorUnassignedSource ${listEditorRootSource} ${listEditorPartSource})
if(listEditorUnassignedSource)
	message(FATAL_ERROR "Editor sources outside every layer in EditorLayers.toml: ${listEditorUnassignedSource}")
endif()
set(listEditorSource ${listEditorRootSource})


# ------------------------------------------------------------------------------
# 2) 티어마다 부분(`EditorTier<N>Model` · `EditorTier<N>View`): 오브젝트는 EditorModule DLL 안에 그대로 들어간다(`sw_addModulePart`).
#    a) 위 층 헤더 껍데기: 부분 N 은 티어가 N 보다 큰 층의 헤더마다 같은 경로의 `#error` 파일을 자기 include 자리 맨 앞에 둔다. `Editor/Panels/X.h` 를
#       include 하면 진짜 헤더 대신 껍데기를 찾아 멈춘다(헤더를 거쳐 닿는 전이 include 도 같다). 껍데기 폴더는 `<빌드>/generated/sw/EditorLayerStubs/tier<N>`.
#       표의 `[prelude]` 헤더(위 층을 include 하지 않는 잎)는 어느 층에서나 쓰므로 껍데기를 놓지 않는다.
#    b) ImGui 를 모르는 `*Model`: vcpkg 는 모든 패키지 헤더를 한 include 자리에 두므로(DirectXTex · meshoptimizer 를 링크하면 imgui.h 도 보인다)
#       include 자리만으로는 막지 못한다. 그래서 ImGui 가 사용자 설정을 읽는 자리(`IMGUI_USER_CONFIG`, imgui.h 첫머리)에 #error 헤더를 준다.
#       imgui.h 를 거치는 헤더(implot · ImGuizmo · 노드 편집기 · 백엔드 포함)를 include 하면 컴파일이 멈춘다.
# ------------------------------------------------------------------------------

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
				set(stubContent "#error \"Editor layer '${layer}' (tier ${upperTier}) cannot be included from tier ${TIER} code. A lower layer must not see an upper layer: move what you need down, or invert the dependency.\"\n")
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

# ------------------------------------------------------------------------------
# 1-1) EditorCore: ImGui 를 모르는 층(`listEditorModelLayer`)의 오브젝트 — 모델 층이 ImGui 를 include 하면 컴파일이 멈춘다(위 `#error` 껍데기). 소비자는 EditorModule 이다.
#      소스 목록은 위 층 표 한 곳(`swEditorTierModelSource_<N>`)에서 나오고, 테스트는 손으로 나열하지 않는다(폴더에 파일을 더하면 둘 다 받는다).
#      - 티어마다 부분(`EditorTier<N>Model`)을 따로 짓는다 — 티어 N 부분은 N 보다 위 층 헤더 자리에 `#error` 껍데기가 놓여 층 방향을 컴파일이 지킨다.
#      - 리플렉션(`EditorCoreReflect`)은 `listEditorModelLayer` 폴더와 뷰 층 `Model` 폴더의 REFLECT · ENUM 헤더 전부(위 `listEditorReflectCore` - 입력을 폴더 정규식으로 못 정해 같은 조건으로 구성 때 모은다)의 `.gen.cpp` 를 한 번만 짓는다. 소비자는 이 헤더를 자기 리플렉션 입력에 넣지 않는다. EditorModule 은 이 폴더들을 자기 리플렉션 입력에서
#        뺀다(같은 `StaticType()` 이 두 곳에 정의되지 않게). FlagOps 우산과 gen 폴더는 `sw_linkEditorCore` 가 소비자에게 같은 값으로 준다.
#      - 오브젝트는 EditorModule(Dev 의 SHARED) 이미지에 들어간다(`SW_EDITOR_EXPORTS` 로 지어 내보낸다). 에디터 쪽 단위 테스트(`Tests/`)는
#        에디터 자체 테스트 `editor.unitTests` 가 에디터 안에서 돌린다. 배포 · 전용 서버에는 에디터가 없어 만들지 않는다.
# ------------------------------------------------------------------------------
set(editorModelImGuiBlock "${CMAKE_BINARY_DIR}/generated/sw/EditorModelForbidsImGui.h")
file(GENERATE OUTPUT "${editorModelImGuiBlock}" CONTENT
	"// EditorModel: generated - the editor model layer (Source/Editor/CMakeLists.txt listEditorModelLayer) must not include ImGui\n#error \"EditorModel (ImGui-free editor layer) includes an ImGui header. Move the ImGui code to a view file outside listEditorModelLayer.\"\n")

# EditorCore 에 속한 폴더의 정규식(후보 헤더의 절대 경로에 맞춘다). `listEditorModelLayer` 의 폴더만 만든다(층 표의 나머지는 코어 밖이다).
set(listEditorCoreRegex "")
foreach(layer IN LISTS listEditorModelLayer)
	list(APPEND listEditorCoreRegex "/Source/Editor/${layer}/")
endforeach()
list(JOIN listEditorCoreRegex "|" editorCoreRegex)
list(JOIN listEditorViewModelRegex "|" editorViewModelRegex)
# 코어 리플렉션이 받는 폴더 = 코어 폴더 + 뷰 층 `Model` 폴더. EditorModule 은 이 둘을 자기 훑기에서 뺀다.
set(editorCoreReflectRegex "${editorCoreRegex}|${editorViewModelRegex}")

# REFLECT · ENUM 이 든 헤더(`sw_addReflectionStep` 의 자동 훑기와 같은 조건)를 모은다. 코어 리플렉션은 폴더 정규식 하나로 "코어 폴더 + 뷰 층 Model 폴더" 를 훑을 수 없어(CMake 정규식에 부정 전방탐색이 없다)
# 여기서 같은 조건으로 입력 목록을 만들어 명시한다 — 새 REFLECT 헤더는 구성 때 저절로 들어온다.
set(listEditorReflectCore "")
set(listEditorReflectModule "")
foreach(header IN LISTS listEditorAllSource)
	if(NOT header MATCHES "\\.(h|hpp)$")
		continue()
	endif()
	file(STRINGS "${header}" listReflectLine REGEX "^[ \t]*(REFLECT\\(|ENUM\\(|REFLECT_CONTAINER\\()")
	if(NOT listReflectLine)
		continue()
	endif()
	if(header MATCHES "${editorCoreReflectRegex}")
		list(APPEND listEditorReflectCore "${header}")
	else()
		list(APPEND listEditorReflectModule "${header}")
	endif()
endforeach()

# 코어(EditorCoreReflect)와 EditorModule 의 gen 이름 중복은 `sw_addReflectionStep` 의 전역 stem 레지스트리가 구성에서 막는다(cmake/Engine/ReflectionCodeGen.cmake).

# 코어 부분 하나가 받는 공통 설정. EditorModule 과 같은 정의 · 로그 태그 · 유니티 묶음을 받아 DLL 에 들어가도 달라지지 않는다.
function(sw_applyEditorCoreSettings TARGET_NAME)
	target_compile_definitions(${TARGET_NAME} PRIVATE "SW_LOG_TAG=\"Editor\"" "SW_REGISTRAR_MODULE=\"${SW_TARGET_EDITOR_MODULE}\"" SW_MODULE_EXPORTS SW_EDITOR_EXPORTS)
	target_include_directories(${TARGET_NAME} PRIVATE "${CMAKE_SOURCE_DIR}/Source")
	target_link_libraries(${TARGET_NAME} PRIVATE Engine DevTools ModuleAPI Microsoft::DirectXTex cgltf meshoptimizer stb sw_global_options)
	set_target_properties(${TARGET_NAME} PROPERTIES
		FOLDER "Source/Editor"
		EXCLUDE_FROM_ALL ON
		POSITION_INDEPENDENT_CODE ON
		INTERPROCEDURAL_OPTIMIZATION FALSE)
	sw_configurePCH(${TARGET_NAME} "${CMAKE_SOURCE_DIR}/Source/Engine/pch.h")
	sw_setUnityBuild(${TARGET_NAME} BATCH_SIZE 8)
endfunction()

# 소비자(EditorModule)가 코어를 받는다: 오브젝트를 링크하고, 리플렉션이 만든 FlagOps 우산(`/FI`)과 gen 폴더를 같은 값으로 준다.
# VISIBILITY 가 PUBLIC 이면 그 소비자를 링크하는 확장 모듈도 우산을 받는다(EditorModule 의 옛 리플렉션이 하던 일).
function(sw_linkEditorCore TARGET_NAME VISIBILITY)
	get_property(listCore GLOBAL PROPERTY SW_EDITOR_CORE_TARGETS)
	if(NOT listCore)
		message(FATAL_ERROR "sw_linkEditorCore(${TARGET_NAME}): EditorCore 가 만들어지지 않았다 (Source/Editor/CMakeLists.txt 가 먼저 구성돼야 한다)")
	endif()
	target_link_libraries(${TARGET_NAME} PRIVATE ${listCore})
	if(TARGET EditorCoreReflect)
		foreach(property COMPILE_OPTIONS INCLUDE_DIRECTORIES)
			get_target_property(listValue EditorCoreReflect INTERFACE_${property})
			if(listValue)
				set_property(TARGET ${TARGET_NAME} APPEND PROPERTY ${property} ${listValue})
				if(VISIBILITY STREQUAL "PUBLIC")
					set_property(TARGET ${TARGET_NAME} APPEND PROPERTY INTERFACE_${property} ${listValue})
				endif()
			endif()
		endforeach()
		add_dependencies(${TARGET_NAME} EditorCoreReflect_ReflectionGen)
	endif()
endfunction()

if(NOT SW_TARGET_TYPE STREQUAL "Server" AND NOT SW_SHIPPING_BUILD)
	set(listEditorCoreTarget "")

	# 리플렉션: 코어 헤더의 `.gen.cpp`. 입력은 폴더를 훑어 정한다(새 REFLECT 헤더를 손 목록에 빠뜨리는 일이 없다).
	sw_skipWithoutReflectionParser(EditorCoreReflect swSkipCoreReflect)
	if(NOT swSkipCoreReflect)
		if(NOT listEditorReflectCore)
			# HEADERS 가 비면 sw_addReflectionStep 이 조용히 Source/Editor 전체를 훑는다 - 코어가 EditorModule 과 같은 타입을 두 번 정의하게 되므로 여기서 멈춘다.
			message(FATAL_ERROR "[Reflection] EditorCoreReflect: no REFLECT header found in the EditorCore folders (empty HEADERS would make sw_addReflectionStep scan every editor header)")
		endif()
		add_library(EditorCoreReflect OBJECT)
		sw_applyEditorCoreSettings(EditorCoreReflect)
		# 코어 오브젝트는 EditorModule 안에 들어간다(`sw_linkEditorCore`) — 타입도 그 모듈로 오르고 내린다.
		sw_addReflectionStep(EditorCoreReflect
			MODULE ${SW_TARGET_EDITOR_MODULE}
			CONTENT_OWNER ${SW_TARGET_EDITOR_MODULE}
			HEADERS ${listEditorReflectCore}
			INCLUDES "${CMAKE_SOURCE_DIR}/Source"
		)
		list(APPEND listEditorCoreTarget EditorCoreReflect)
	endif()

	foreach(tier IN LISTS SW_EDITOR_TIER_LIST)
		if(NOT swEditorTierModelSource_${tier})
			continue()
		endif()
		set(part EditorTier${tier}Model)
		add_library(${part} OBJECT ${swEditorTierModelSource_${tier}})
		sw_applyEditorCoreSettings(${part})
		# 모델은 imgui 링크가 없다. vcpkg 는 모든 헤더를 한 include 자리에 두므로, ImGui 가 사용자 설정을 읽는 자리에 #error 를 건다.
		target_compile_definitions(${part} PRIVATE "IMGUI_USER_CONFIG=\"${editorModelImGuiBlock}\"")
		sw_addEditorLayerStubs(${part} ${tier})
		if(TARGET EditorCoreReflect)
			foreach(property COMPILE_OPTIONS INCLUDE_DIRECTORIES)
				get_target_property(listValue EditorCoreReflect INTERFACE_${property})
				if(listValue)
					set_property(TARGET ${part} APPEND PROPERTY ${property} ${listValue})
				endif()
			endforeach()
			add_dependencies(${part} EditorCoreReflect_ReflectionGen)
		endif()
		list(APPEND listEditorCoreTarget ${part})
	endforeach()
	set_property(GLOBAL PROPERTY SW_EDITOR_CORE_TARGETS "${listEditorCoreTarget}")

	# 모델 임포트의 메시 쪼개기는 같은 모델 · 설정 · 시드면 구성마다 같은 조각을 굽는다 — Engine 에 있을 때의 결정성 표시를 그대로 옮긴다.
	# 그 파일을 짓는 부분을 소스 목록으로 찾는다(티어 번호를 적지 않는다). 못 찾으면 결정성이 소리 없이 꺼지므로 멈춘다.
	set(swEditorFractureSource "${CMAKE_CURRENT_SOURCE_DIR}/Common/Asset/Model/MeshFracture.cpp")
	set(bFractureMarked FALSE)
	foreach(part IN LISTS listEditorCoreTarget)
		get_target_property(listPartSource ${part} SOURCES)
		if(listPartSource AND swEditorFractureSource IN_LIST listPartSource)
			sw_markDeterministicSources(${part} "${swEditorFractureSource}")
			set(bFractureMarked TRUE)
		endif()
	endforeach()
	if(NOT bFractureMarked)
		message(FATAL_ERROR "No EditorCore part builds ${swEditorFractureSource} - its deterministic mark has no target")
	endif()
endif()

# 배포는 에디터(코어 포함)를 짓지 않는다 — 소스 검사 게이트가 알도록 적는다(`sw_declareUnbuiltSources`).
if(SW_SHIPPING_BUILD)
	sw_declareUnbuiltSources(${listEditorAllSource})
	return()
endif()

# 배포본이 아니어도 프로젝트가 에디터 모듈을 끌 수 있다(매니페스트 `EditorModule.module.json`).
sw_skipInactiveModule(EditorModule swSkipEditor)
if(swSkipEditor)
	return()
endif()

sw_addModuleLibrary(EditorModule
	KIND editor
	DEV_TYPE SHARED
	EXPORTS EDITOR
	LOG_TAG "Editor"
	FOLDER "Source/Editor"
	SOURCES ${listEditorSource}
	LINK_PRIVATE Engine DevTools ModuleAPI imgui imgui_nodeeditor imgui_notify imguizmo implot Microsoft::DirectXTex cgltf meshoptimizer stb sw_graphics_libs
	UNITY_BATCH 8
	# REFLECTION_HEADERS 를 주지 않는다 — 리플렉션 입력은 폴더를 훑어 REFLECT · ENUM 매크로가 있는 헤더로 정한다(손 목록에 빠뜨리면 StaticType 링크 오류).
	# 코어 폴더의 헤더는 뺀다 — 그쪽은 EditorCoreReflect 가 한 번만 짓는다(두 곳에 있으면 StaticType 이 중복 정의된다).
	REFLECTION_EXCLUDE "${editorCoreReflectRegex}"
)

# EditorCore 의 오브젝트와 FlagOps 우산을 받는다. View 부분이 아래에서 이 타깃의 컴파일 옵션을 복사하므로 부분을 만들기 전에 부른다.
sw_linkEditorCore(EditorModule PUBLIC)

set(listEditorPart "")
foreach(tier IN LISTS SW_EDITOR_TIER_LIST)
	if(swEditorTierViewSource_${tier})
		sw_addModulePart(EditorTier${tier}View
			OWNER EditorModule
			SOURCES ${swEditorTierViewSource_${tier}}
			LINK Engine DevTools ModuleAPI imgui imgui_nodeeditor imgui_notify imguizmo implot Microsoft::DirectXTex cgltf meshoptimizer stb sw_graphics_libs
		)
		sw_addEditorLayerStubs(EditorTier${tier}View ${tier})
		if(TARGET EditorCoreReflect)
			add_dependencies(EditorTier${tier}View EditorCoreReflect_ReflectionGen)
		endif()
		list(APPEND listEditorPart EditorTier${tier}View)
	endif()
endforeach()

# ------------------------------------------------------------------------------
# 2-1) 에디터 코드의 ImGui 입력 함수 금지(EditorUX 10-4 · 10-5 I2 · I4 — 형식이 먼저, 린트는 형식이 못 막는 `ImGuiIO` 필드 읽기만)
#      모든 뷰 부분(EditorTier<N>View)과 루트(EditorModule), 에디터 확장 모듈(`sw_addEditorExtension`)이 imgui.h 끝(사용자 헤더 자리)에
#      `Common/Input/EditorInputBan.h` 를 붙이고 사용 중단 경고를 오류로 올린다(`sw_applyEditorInputBan`). 모델 부분은 ImGui 를 아예 include 하지 못한다.
#      받지 않는 곳은 어댑터 두 파일(SW_EDITOR_INPUT_ADAPTER — 유니티 묶음에서 뺀다)뿐이다.
# ------------------------------------------------------------------------------
option(SW_EDITOR_INPUT_BAN "Editor code fails to compile when it calls ImGui input/popup/drag functions directly (EditorInputBan.h)" ON)
foreach(tier IN LISTS SW_EDITOR_TIER_LIST)
	if(TARGET EditorTier${tier}View)
		sw_applyEditorInputBan(EditorTier${tier}View)
	endif()
endforeach()
sw_applyEditorInputBan(EditorModule)

# 에디터는 LTO 를 받지 않는다(링크 시간 — Tools/ 와 같은 이유).
set_target_properties(EditorModule ${listEditorPart} PROPERTIES INTERPROCEDURAL_OPTIMIZATION FALSE)   # EditorCore 부분은 만들 때 이미 껐다

# 확장 모듈은 에디터 헤더(Source/ 기준 경로)를 쓴다. ImGui 링크는 확장이 스스로 한다 — vcpkg imgui 는 정적이라 DLL 마다 사본이고 결속기가 컨텍스트를 맞춘다.
target_include_directories(EditorModule INTERFACE "${CMAKE_SOURCE_DIR}/Source")

# 에디터 빌드 단추(Build > Compile Editor)가 짓는 묶음 — EditorModule + 모든 확장 모듈(sw_addEditorExtension 이 의존을 더한다).
add_custom_target(EditorAll DEPENDS EditorModule)
set_target_properties(EditorAll PROPERTIES FOLDER "Source/Editor")
