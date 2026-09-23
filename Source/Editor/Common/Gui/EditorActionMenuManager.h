/**
 * @file EditorActionMenuManager.h
 * @brief 에디터 주요 패널의 우클릭 액션 메뉴 관리 (EditorContext 소유)
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"

namespace sw::editor
{
    /** @brief 액션 메뉴 표시 위치 */
    enum class ActionMenuLocation : uint8
    {
        Hierarchy = 0,
        ContentBrowser,
        Viewport,
        Inspector,
        Count ///< 위치 수입니다. 배열 크기와 경계 검사가 이 값을 봅니다.
    };

    /** @brief 개별 액션 메뉴 항목 */
    struct ActionMenuItem
    {
        string           _path;
        string           _shortcut;
        Delegate<void()> _action;
        Delegate<bool()> _enabledPredicate;
    };

    /**
     * @class EditorActionMenuManager
     * @brief 에디터 주요 패널(Hierarchy, Content Browser 등)의 우클릭 액션 메뉴를 실행 중에 확장 · 관리합니다(EditorContext 소유).
     */
    class EditorActionMenuManager
    {
    public:
        EditorActionMenuManager()  = default;
        ~EditorActionMenuManager() = default;

        void registerItem( ActionMenuLocation location, string_view path, Delegate<void()> action,
                           string_view shortcut = "", Delegate<bool()> enabledPredicate = {} );
        void drawActionMenu( ActionMenuLocation location );

    private:
        vector<ActionMenuItem> _arrItem[static_cast<size_t>( ActionMenuLocation::Count )];
    };
} // namespace sw::editor
