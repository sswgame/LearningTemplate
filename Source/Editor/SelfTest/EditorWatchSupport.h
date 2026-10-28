/**
 * @file EditorWatchSupport.h
 * @brief 에디터 탐침(`Editor.*`)이 각 패널 · 기능 폴더의 `*Watchs.cpp` 로 흩어져 있어, 그 탐침들이 함께 쓰는 시나리오용 전역 변수와 읽기 보조 함수를 한 곳에 선언합니다.
 * @details 변수는 시나리오가 `<Variable>` 로 정하는 탐침 대상(오브젝트 이름 · 패널 id …)입니다. 정의는 `EditorWatchSupport.cpp` 에 있습니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"

namespace sw
{
    class GameObject;
} // namespace sw

namespace sw::editor
{
    class EditorContext;

    /** @brief 탐침 `Editor.VisualizerOn` 이 볼 뷰포트 시각화 id 입니다. */
    SW_EXTERN_GLOBAL_VARIABLE( sw::string, gv_editorWatchVisualizer );
    /** @brief 탐침 `Editor.PanelOpen` · `Editor.PanelDirty` 가 볼 패널 id 입니다. */
    SW_EXTERN_GLOBAL_VARIABLE( sw::string, gv_editorWatchPanel );
    /** @brief 오브젝트 이름을 받는 탐침(`Editor.HierarchyRowIcon` · `Editor.BillboardIcon` · `Editor.ObjectActive` …)이 볼 오브젝트 이름입니다. */
    SW_EXTERN_GLOBAL_VARIABLE( sw::string, gv_editorWatchObject );
    /** @brief 탐침 `Editor.Overlay*` 가 볼 씬 뷰 오버레이 바 id 입니다(view · transform · display · tools). */
    SW_EXTERN_GLOBAL_VARIABLE( sw::string, gv_editorWatchOverlay );
    /** @brief 탐침 `Editor.SelectedProperty` 계열이 볼 `<컴포넌트 타입>.<프로퍼티>` 입니다. */
    SW_EXTERN_GLOBAL_VARIABLE( sw::string, gv_editorWatchProperty );

    /**
     * @brief 프로브가 읽을 에디터 컨텍스트입니다. 프로브는 에디터가 도는 동안만 불리므로 없으면 버그입니다(필수 서비스).
     * @details 프로브는 float 값만 돌려 사유를 줄 수 없어 이유를 로그로 남기고 nullptr 를 돌려줍니다. @p pWatch 는 `__func__` 입니다.
     */
    [[nodiscard]] EditorContext* getWatchContext( const utf8* pWatch );

    /** @brief 활성 씬에서 `gv_editorWatchObject` 이름의 오브젝트를 찾습니다. 없으면 nullptr 입니다. */
    [[nodiscard]] const GameObject* findWatchdObject();
} // namespace sw::editor
