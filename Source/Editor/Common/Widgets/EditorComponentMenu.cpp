#include "pch.h"

#include "Editor/Common/Widgets/EditorComponentMenu.h"

#include "Core/Container/map.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Editor/Common/Commands/EditorSceneCommands.h"
#include "Editor/Common/GUI/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorListFilter.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/Reflection/TypeRegistry.h"

#include <imgui.h>

namespace sw::editor
{
    bool EditorComponentMenu::drawAddComponentList( GameObject* pObj, fixed_string<constant::kMaxBuffer64>& search, const utf8* pMarkPrefix, bool bFocusSearch,
                                                    bool& outbFailed )
    {
        vector<hashed_string> listType;
        if ( pObj != nullptr && pObj->getManager() != nullptr )
            listType = pObj->getManager()->getRegisteredComponentTypeNames();
        if ( listType.empty() )
        {
            ImGui::TextDisabled( "No registered component types." );
            return false;
        }

        if ( bFocusSearch )
            ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth( 180.0f * EditorThemeUtil::getDpiScale() );
        const bool   bEnter = ImGui::InputTextWithHint( "##compSearch", "Search...", search.data(), search.capacity(), ImGuiInputTextFlags_EnterReturnsTrue );
        const string markPrefix{ pMarkPrefix };
        EditorSelfTestMarks::note( ( markPrefix + ".search" ).c_str() );
        const EditorListFilter compFilter{ search.c_str() };
        const bool             bHasFilter = compFilter.isActive();

        auto*         pRegistry = editor::getService<TypeRegistry>();
        bool          bAdded{ false };
        hashed_string firstMatch{};
        auto          addType = [&]( const hashed_string& typeName )
        {
            if ( EditorSceneCommands::addComponent( pObj, typeName ) == nullptr )
                outbFailed = true;
            else
                bAdded = true;
        };
        auto drawItem = [&]( const hashed_string& typeName, const TypeInfo* pTypeInfo )
        {
            const utf8* pDisplayName = ( pTypeInfo != nullptr ) ? pTypeInfo->getDisplayName() : typeName.c_str();
            if ( ImGui::MenuItem( pDisplayName ) )
                addType( typeName );
            // 자동화 시나리오가 타입 이름으로 누른다(EditorClick mark="<머리>.<타입>")
            if ( EditorSelfTestMarks::isEnabled() )
                EditorSelfTestMarks::note( ( markPrefix + "." + typeName.c_str() ).c_str() );
            if ( pTypeInfo != nullptr )
                EditorWidgets::drawTooltip( pTypeInfo->getTooltip().c_str() );
        };

        if ( bHasFilter )
        {
            ImGui::Separator();
            uint32 matchCount{ 0 };
            for ( const hashed_string& typeName : listType )
            {
                const TypeInfo* pTypeInfo = ( pRegistry != nullptr ) ? pRegistry->findType( typeName ) : nullptr;
                if ( pTypeInfo != nullptr && pTypeInfo->isHiddenInMenu() )
                    continue;
                const utf8* pDisplayName = ( pTypeInfo != nullptr ) ? pTypeInfo->getDisplayName() : typeName.c_str();
                if ( compFilter.matchesAny( { string_view{ pDisplayName }, typeName.view() } ) == false )
                    continue;
                if ( matchCount == 0 )
                    firstMatch = typeName;
                drawItem( typeName, pTypeInfo );
                ++matchCount;
            }
            if ( matchCount == 0 )
                ImGui::TextDisabled( "No matching components." );
            // Enter 는 맨 위 줄을 고른다(유니티 Add Component 검색 창과 같다).
            if ( bEnter && matchCount > 0 && bAdded == false )
                addType( firstMatch );
            return bAdded;
        }

        map<string, vector<pair<hashed_string, const TypeInfo*>>> mapCategorized;
        for ( const hashed_string& typeName : listType )
        {
            const TypeInfo* pTypeInfo = ( pRegistry != nullptr ) ? pRegistry->findType( typeName ) : nullptr;
            if ( pTypeInfo != nullptr && pTypeInfo->isHiddenInMenu() )
                continue;
            string category = ( pTypeInfo != nullptr && pTypeInfo->getCategory().empty() == false ) ? pTypeInfo->getCategory() : "General";
            mapCategorized[category].emplace_back( typeName, pTypeInfo );
        }
        for ( const auto& [category, items] : mapCategorized )
        {
            if ( category == "General" )
            {
                for ( const auto& [typeName, pTypeInfo] : items )
                {
                    drawItem( typeName, pTypeInfo );
                }
                continue;
            }
            if ( ImGui::BeginMenu( category.c_str() ) )
            {
                for ( const auto& [typeName, pTypeInfo] : items )
                {
                    drawItem( typeName, pTypeInfo );
                }
                ImGui::EndMenu();
            }
        }
        return bAdded;
    }
} // namespace sw::editor
