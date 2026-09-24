/**
 * @file EditorGlobalVariable.cpp
 * @brief EditorModule 전역 변수 정의
 */
#include "pch.h"

#include "Editor/Common/EditorGlobalVariable.h"

namespace sw
{
    SW_GLOBAL_VARIABLE_INT( gv_editorPanelDump, 0, "N 번째 프레임에 에디터 ImGui 창별 드로우 통계를 덤프 (0=사용 안 함)" );
    SW_GLOBAL_VARIABLE_INT( gv_editorOpenAllPanels, 0, "시작할 때 도구 패널까지 전부 연다 (0=사용 안 함)" );
    SW_GLOBAL_VARIABLE_STRING( gv_editorOpenPanel, "", "이 id 의 패널 하나만 연다 (비우면 사용 안 함)" );
    SW_GLOBAL_VARIABLE_STRING( gv_editorStartupScene, "", "에디터 시작 시 열 씬의 리소스 경로 (비우면 열지 않는다)" );
} // namespace sw
