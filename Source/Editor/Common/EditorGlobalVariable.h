/**
 * @file EditorGlobalVariable.h
 * @brief EditorModule 전용 전역 변수입니다(모듈 로컬 등록 리스트와 진단 스위치들).
 *
 * @note **이 헤더를 include 하면 `SW_GVM_MODULE_HEAD` 가 바뀝니다.** 기본값은 Engine.dll 의 등록 리스트라, 그대로 쓰면
 *       모듈이 언로드된 뒤에도 매니저가 사라진 DLL 안의 변수를 계속 가리킵니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"

#undef SW_GVM_MODULE_HEAD
#define SW_GVM_MODULE_HEAD() ( ::sw::editor::getGlobalVariableHead() )

SW_DECLARE_MODULE_GLOBAL_VARIABLES( editor );

namespace sw
{
    /**
     * @brief `-gv_editorPanelDump=<N>`: N 번째 ImGui 프레임에 에디터 창별 드로우 통계를 덤프합니다.
     * @details 읽는 쪽은 `Editor/Common/Gui/EditorPanelDump.cpp` 입니다. 0 이면 아무것도 하지 않습니다.
     */
    SW_EXTERN_GLOBAL_VARIABLE_INT( gv_editorPanelDump );

    /**
     * @brief `-gv_editorOpenAllPanels=1`: 시작할 때 도구 패널까지 모두 엽니다.
     * @details 도구 패널(Sequencer · Material · InputMap · DataTable…)은 기본이 닫힘이라 `-gv_editorPanelDump` 가 늘 기본
     *          레이아웃의 다섯 개만 보고 있었습니다. 나머지는 사람이 창을 띄워 메뉴에서 열어 보기 전에는 비어 있어도 알 수
     *          없었고, 그만큼 패널을 고치고 "검증했다" 고 말할 수 있는 범위가 좁았습니다. 이 스위치를 주면 덤프가 등록된
     *          패널 전부를 다룹니다.
     */
    SW_EXTERN_GLOBAL_VARIABLE_INT( gv_editorOpenAllPanels );

    /**
     * @brief `-gv_editorOpenPanel=<id>`: 그 패널 **하나만** 열고 나머지는 닫습니다.
     * @details `-gv_editorOpenAllPanels` 는 모두 띄워 서로를 가립니다. 마지막에 등록된 것이 위로 와서 **원하는 패널이 화면
     *          캡처에 나오지 않습니다**(실제로 새 패널을 확인하려다 막혔습니다). 하나만 띄우면 그 패널이 반드시 보입니다.
     *          id 는 `registerDefaultPanels` 가 준 것입니다(예: `render_targets` · `profiler` · `material`).
     */
    SW_EXTERN_GLOBAL_VARIABLE_STRING( gv_editorOpenPanel );

    /**
     * @brief `-gv_editorStartupScene=<리소스 경로>`: 에디터가 시작할 때 이 씬을 엽니다.
     * @details 실제 기동 검증이 오랫동안 **빈 씬만** 보고 있었습니다. 활성 게임이 `Empty` 라 맵이 없어서 `SceneManager` 가
     *          씬 없이 떴다가 내려갑니다. 그래서 오브젝트를 순회하는 코드(뷰포트 피킹 · 컴포넌트 시각화 · Hierarchy 트리 ·
     *          Profiler 분포표 · 씬 세대 변경 훅)가 검증에서 한 번도 실행되지 않았습니다. 이 스위치로 테스트 씬을 열면 그
     *          경로가 모두 켜집니다.
     *          예: `-gv_editorStartupScene=game/empty/maps/editortest.scene.xml`
     */
    SW_EXTERN_GLOBAL_VARIABLE_STRING( gv_editorStartupScene );
} // namespace sw
