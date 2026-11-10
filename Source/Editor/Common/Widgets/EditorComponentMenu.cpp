#include "pch.h"

#include "Editor/Common/Widgets/EditorComponentMenu.h"

#include "Core/Container/map.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Log/LogOnce.h"

#include "Editor/Common/Commands/Scene/EditorSceneCommands.h"
#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Widgets/Model/EditorListFilter.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Localization/LocText.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/Reflection/TypeRegistry.h"

#include <imgui.h>

namespace sw::editor
{
    bool EditorComponentMenu::drawAddComponentList( GameObject* pObj, fixed_string<constant::buffer::kMax64>& search, const utf8* pMarkPrefix, bool bFocusSearch,
                                                    bool& outbFailed )
    {
        const hashed_string typeName = drawComponentTypeList( search, pMarkPrefix, bFocusSearch );
        if ( typeName.empty() )
            return false;
        if ( EditorSceneCommands::addComponent( pObj, typeName ) != nullptr )
            return true;
        outbFailed = true;
        return false;
    }

    hashed_string EditorComponentMenu::drawComponentTypeList( fixed_string<constant::buffer::kMax64>& search, const utf8* pMarkPrefix, bool bFocusSearch )
    {
        const vector<hashed_string> listType = GameObjectManager::getRegisteredComponentTypeNames();
        if ( listType.empty() )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EdCommon", "NoComponentTypes" ) );
            return {};
        }

        if ( bFocusSearch )
            ImGui::SetKeyboardFocusHere();
        EditorChrome::setNextItemWidthColumns( 24.0f );
        const string           markPrefix{ pMarkPrefix };
        const bool             bEnter = EditorWidgets::drawHintField( "##compSearch", search.data(), search.capacity(), SW_LOCKEY( "EdCommon", "SearchHint" ), ( markPrefix + ".search" ).c_str(), true )._bSubmitted;
        const EditorListFilter compFilter{ search.c_str() };
        const bool             bHasFilter = compFilter.isActive();

        const TypeRegistry& registry = editor::getSubsystem<TypeRegistry>();
        hashed_string       picked{};
        hashed_string       firstMatch{};
        auto                drawItem = [&]( const hashed_string& typeName, const TypeInfo* pTypeInfo )
        {
            const utf8* pDisplayName = ( pTypeInfo != nullptr ) ? pTypeInfo->getDisplayName() : typeName.c_str();
            if ( ImGui::MenuItem( pDisplayName ) )
                picked = typeName;
            // 자동화 시나리오가 타입 이름으로 누른다(EditorClick mark="<머리>.<타입>")
            if ( EditorSelfTestMarks::isEnabled() )
                EditorSelfTestMarks::record( ( markPrefix + "." + typeName.c_str() ).c_str() );
            if ( pTypeInfo != nullptr )
                EditorWidgets::drawTooltip( string{ pTypeInfo->getTooltip() }.c_str() );
        };

        if ( bHasFilter )
        {
            ImGui::Separator();
            uint32 matchCount{ 0 };
            for ( const hashed_string& typeName : listType )
            {
                const TypeInfo* pTypeInfo = registry.findType( typeName );
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
                EditorWidgets::drawEmptyHint( SW_LOCKEY( "EdCommon", "NoMatchingComponents" ) );
            // Enter 는 맨 위 줄을 고른다(유니티 Add Component 검색 창과 같다).
            const bool bPickFirstMatch = bEnter && matchCount > 0 && picked.empty();
            if ( bPickFirstMatch )
                picked = firstMatch;
            return picked;
        }

        map<string, vector<pair<hashed_string, const TypeInfo*>>> mapCategorized;
        for ( const hashed_string& typeName : listType )
        {
            const TypeInfo* pTypeInfo = registry.findType( typeName );
            if ( pTypeInfo != nullptr && pTypeInfo->isHiddenInMenu() )
                continue;
            string category{ ( pTypeInfo != nullptr && pTypeInfo->getCategory().empty() == false ) ? pTypeInfo->getCategory() : string_view{ "General" } };
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
        return picked;
    }
} // namespace sw::editor
