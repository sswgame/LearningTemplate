/**
 * @file EditorGlobalVariable.cpp
 * @brief EditorModule 전역 변수 정의
 */
#include "pch.h"

#include "Editor/Common/EditorGlobalVariable.h"

namespace sw
{
    SW_GLOBAL_VARIABLE_INT( gv_editorOpenAllPanels, 0, "시작할 때 도구 패널까지 전부 연다 (0=사용 안 함)" );
} // namespace sw
