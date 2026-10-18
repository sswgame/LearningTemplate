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

# ImGui 를 모르는 층(모델: 에셋 · 커맨드 · 설정 · 로컬라이즈 · 버전 관리)은 아래 2) 의 EditorModel 이 짓고 EditorModule 소스에서는 뺀다.
# EditorScenarioRecordCommands.cpp 는 이미 ImGui 를 쓰는 파일이라 EditorModule 에 남는다(백로그 `C-37` "모델").
set(listEditorModelDir Common/Asset Common/Commands Common/Config Common/Localization Common/SourceControl)
set(listEditorModelSource "")
foreach(modelDir IN LISTS listEditorModelDir)
	file(GLOB_RECURSE listModelDirSource CONFIGURE_DEPENDS "${modelDir}/*.cpp" "${modelDir}/*.h")
	list(APPEND listEditorModelSource ${listModelDirSource})
endforeach()
list(REMOVE_ITEM listEditorModelSource "${CMAKE_CURRENT_SOURCE_DIR}/Common/Commands/Play/EditorScenarioRecordCommands.cpp")
list(REMOVE_ITEM listEditorSource ${listEditorModelSource})

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
# 2) EditorModel: 모델 층을 imgui 링크가 없는 OBJECT 라이브러리로 짓고 오브젝트를 EditorModule DLL 안에 그대로 넣는다(`sw_addModulePart`).
#    vcpkg 는 모든 패키지 헤더를 한 include 자리에 두므로(DirectXTex · meshoptimizer 를 링크하면 imgui.h 도 보인다) include 자리만으로는 막지 못한다.
#    그래서 ImGui 가 사용자 설정을 읽는 자리(`IMGUI_USER_CONFIG`, imgui.h 첫머리)에 #error 헤더를 준다. imgui.h 를 거치는 헤더(implot · ImGuizmo ·
#    노드 편집기 · 백엔드 포함)를 include 하면 컴파일이 멈춘다.
# ------------------------------------------------------------------------------
sw_addModulePart(EditorModel
	OWNER EditorModule
	SOURCES ${listEditorModelSource}
	LINK Engine RuntimeAPI Microsoft::DirectXTex cgltf meshoptimizer stb
)
set(editorModelImGuiBlock "${CMAKE_BINARY_DIR}/generated/sw/EditorModelForbidsImGui.h")
file(GENERATE OUTPUT "${editorModelImGuiBlock}" CONTENT
	"// EditorModel: generated - the editor model layer (Source/Editor/CMakeLists.txt listEditorModelDir) must not include ImGui\n#error \"EditorModel (ImGui-free editor layer) includes an ImGui header. Move the ImGui code to a view file outside listEditorModelDir.\"\n")
target_compile_definitions(EditorModel PRIVATE "IMGUI_USER_CONFIG=\"${editorModelImGuiBlock}\"")

# 에디터는 LTO 를 받지 않는다(링크 시간 — Test/ · Tools/ 와 같은 이유).
set_target_properties(EditorModule EditorModel PROPERTIES INTERPROCEDURAL_OPTIMIZATION FALSE)

# 확장 모듈은 에디터 헤더(Source/ 기준 경로)를 쓴다. ImGui 링크는 확장이 스스로 한다 — vcpkg imgui 는 정적이라 DLL 마다 사본이고 결속기가 컨텍스트를 맞춘다.
target_include_directories(EditorModule INTERFACE "${CMAKE_SOURCE_DIR}/Source")

# 에디터 빌드 단추(Build > Compile Editor)가 짓는 묶음 — EditorModule + 모든 확장 모듈(sw_addEditorExtension 이 의존을 더한다).
add_custom_target(EditorAll DEPENDS EditorModule)
set_target_properties(EditorAll PROPERTIES FOLDER "Source/Editor")
