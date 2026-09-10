/**
 * @file EditorGlobalVariable.cpp
 * @brief EditorModule 전역 변수 정의와 모듈 로컬 등록/해제
 */
#include "pch.h"

#include "Editor/Common/EditorGlobalVariable.h"

#include "Editor/Common/Workspace/EditorService.h"

#include "sw/config/ConfigConstants.h"

SW_IMPLEMENT_MODULE_GLOBAL_VARIABLES( editor, config::kTargetEditorModule );

namespace sw
{
    SW_GLOBAL_VARIABLE_INT( gv_editorPanelDump, 0, "N 번째 프레임에 에디터 ImGui 창별 드로우 통계를 덤프 (0=사용 안 함)" );
    SW_GLOBAL_VARIABLE_INT( gv_editorOpenAllPanels, 0, "시작할 때 도구 패널까지 전부 연다 (0=사용 안 함)" );
    SW_GLOBAL_VARIABLE_STRING( gv_editorStartupScene, "", "에디터 시작 시 열 씬의 리소스 경로 (비우면 열지 않는다)" );
} // namespace sw
