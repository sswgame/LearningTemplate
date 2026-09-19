#include "pch.h"

#include "Editor/Common/Gui/EditorActionMenuManager.h"

#include <imgui.h>

namespace sw::editor
{
    void EditorActionMenuManager::registerItem( ActionMenuLocation location, string_view path,
                                                Delegate<void()> action, string_view shortcut,
                                                Delegate<bool()> enabledPredicate )
    {
        const size_t locIdx = static_cast<size_t>( location );
        if ( locIdx >= static_cast<size_t>( ActionMenuLocation::Count ) )
            return;

        ActionMenuItem item;
        item._path             = string{ path };
        item._shortcut         = string{ shortcut };
        item._action           = std::move( action );
        item._enabledPredicate = std::move( enabledPredicate );

        _arrItem[locIdx].push_back( std::move( item ) );
    }

    void EditorActionMenuManager::drawActionMenu( ActionMenuLocation location )
    {
        const size_t locIdx = static_cast<size_t>( location );
        if ( locIdx >= static_cast<size_t>( ActionMenuLocation::Count ) || _arrItem[locIdx].empty() )
            return;

        // **인덱스로 돌고, 부르기 전에 델리게이트를 복사한다.** 액션이든 술어든 같은 위치에
        // 항목을 더할 수 있고(확장 메뉴는 원래 그러라고 있다), 그러면 벡터가 재할당돼 범위 for 가
        // 들고 있던 참조와 반복자가 뜬 메모리를 가리킨다. `ReloadFileManager::dispatchEvents` 와
        // 같은 모양이고, 거기서는 ASAN 이 `heap-use-after-free` 로 잡았다.
        //
        // 문자열은 복사하지 않는다 — ImGui 호출은 이 목록을 건드리지 않으므로 참조로 충분하고,
        // 목록을 바꿀 수 있는 두 호출 뒤에는 그 참조를 더 쓰지 않는다.
        for ( size_t itemIndex = 0; itemIndex < _arrItem[locIdx].size(); ++itemIndex )
        {
            const Delegate<bool()> enabledPredicate = _arrItem[locIdx][itemIndex]._enabledPredicate;

            bool bEnabled = true;
            if ( enabledPredicate.isBound() )
                bEnabled = enabledPredicate();

            // 술어가 목록을 줄였을 수 있다.
            if ( itemIndex >= _arrItem[locIdx].size() )
                break;

            const ActionMenuItem&  item      = _arrItem[locIdx][itemIndex];
            const Delegate<void()> action    = item._action;
            const utf8*            pShortcut = item._shortcut.empty() ? nullptr : item._shortcut.c_str();

            // Submenu support (e.g. "Create/3D Object")
            const size_t slashPos = item._path.find( '/' );
            if ( slashPos != string::npos )
            {
                const string subMenu = item._path.substr( 0, slashPos );
                const string subItem = item._path.substr( slashPos + 1 );
                if ( ImGui::BeginMenu( subMenu.c_str(), bEnabled ) )
                {
                    if ( ImGui::MenuItem( subItem.c_str(), pShortcut, false, bEnabled ) && action.isBound() )
                        action();
                    ImGui::EndMenu();
                }
            }
            else
            {
                if ( ImGui::MenuItem( item._path.c_str(), pShortcut, false, bEnabled ) && action.isBound() )
                    action();
            }
        }
    }
} // namespace sw::editor
