/**
 * @file EditorPanelText.h
 * @brief 패널 · 팝업 창의 보이는 이름 표(`editor/panels/editor.panels.xml`)와 ImGui 창 이름입니다.
 * @details 패널의 제목(`IEditorPanel::getPanelTitle`)은 창 ID 이고 도킹 배치 · 포커스 · 테스트가 그 이름으로 창을 찾는다 — 언어와 무관하게 고정한다.
 *          보이는 이름은 이 표의 영어 원문을 지금 언어로 푼 것이고, ImGui 창 이름은 "보이는 이름###제목" 이다(언리얼 탭의 표시 이름 · 탭 ID 와 같은 나눔).
 *          창을 이름으로 찾는 곳(`FindWindowByName` · `SetWindowFocus` · `DockBuilderDockWindow`)은 `makeWindowID` 를 넘긴다 — ImGui 는 "###" 뒤만 해시한다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/EditorExports.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw::editor
{
    /** @brief 창 하나의 보이는 이름입니다. */
    REFLECT()
    struct EditorPanelText
    {
        REFLECT_BODY();
        PROPERTY( Required, Meta = "NotLocalizable", Tooltip = "Panel title the code returns (getPanelTitle) - the window ID" )
        string _title{};
        PROPERTY( Required, Meta = "Localizable", Tooltip = "Name on the tab and the title bar (English source - the text is its localization key)" )
        string _label{};
    };

    /** @brief 창 이름 표 한 벌입니다. */
    REFLECT()
    struct EditorPanelTextTable
    {
        REFLECT_BODY();
        PROPERTY( Required, Tooltip = "Panel and popup windows" )
        vector<EditorPanelText> _listPanel;
    };

    /** @brief ImGui 창 이름입니다(자기 버퍼를 든 값 — 호출 인자로 바로 넘긴다). */
    using EditorWindowName = fixed_string<constant::buffer::kMax256>;

    struct SW_EDITOR_API EditorPanelTextUtil
    {
        /** @brief "보이는 이름###제목" 입니다. 표에 줄이 없으면 오류를 한 번 남기고 제목을 보인다(빠진 줄이 화면에 드러나게). */
        static EditorWindowName makeWindowName( const utf8* pTitle );
        /** @brief "###제목" 입니다. 이름으로 창을 찾는 ImGui 호출에 넘긴다(보이는 이름과 무관하게 같은 창). */
        static EditorWindowName makeWindowID( const utf8* pTitle );
    };
} // namespace sw::editor
