/**
 * @file EditorGlobalVariable.h
 * @brief 여러 파일이 함께 읽는 EditorModule 전역 변수의 선언입니다. 한 파일만 읽는 스위치는 그 파일에서 정의합니다.
 * @note 등록 · 해제는 모듈을 올리고 내리는 쪽(`LiveReloadManager`)이 모듈 이름으로 합니다. 여기서는 선언만 합니다.
 */
#pragma once
#include "Core/GlobalVariable/GlobalVariableManager.h"

namespace sw
{
    /**
     * @brief `-gv_editorOpenAllPanels=1`: 시작할 때 도구 패널까지 모두 엽니다.
     * @details 도구 패널(Sequencer · Material · InputMap · DataTable…)은 기본이 닫힘이라 `-gv_editorPanelDump` 가 늘 기본
     *          레이아웃의 다섯 개만 보고 있었습니다. 나머지는 사람이 창을 띄워 메뉴에서 열어 보기 전에는 비어 있어도 알 수
     *          없었고, 그만큼 패널을 고치고 "검증했다" 고 말할 수 있는 범위가 좁았습니다. 이 스위치를 주면 덤프가 등록된
     *          패널 전부를 다룹니다.
     */
    SW_EXTERN_GLOBAL_VARIABLE_INT( gv_editorOpenAllPanels );
} // namespace sw
