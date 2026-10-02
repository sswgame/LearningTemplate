#include "pch.h"

// 인스펙터는 에디터 기능이라 Shipping 에는 없다 — 에디터 메타데이터(카테고리 · 표시 이름 · 숨김)도 Shipping 빌드에서 빠진다.

#if !defined( SW_SHIPPING )

    #include "Editor/Panels/Inspector/InspectorPropertyLayout.h"

    #include "Editor/Common/Widgets/EditorListFilter.h"

    #include "Engine/Reflection/ReflectionTypes.h"

namespace sw::editor
{
    void InspectorPropertyLayout::collectTypeChain( const TypeInfo& type, vector<const TypeInfo*>& outListType )
    {
        outListType.clear();
        for ( const TypeInfo* pType = &type; pType != nullptr; pType = pType->getParentType() )
        {
            for ( const TypeInfo* pSeen : outListType )
            {
                if ( pSeen == pType )
                {
                    std::reverse( outListType.begin(), outListType.end() );
                    return;
                }
            }
            outListType.push_back( pType );
        }
        std::reverse( outListType.begin(), outListType.end() );
    }

    void InspectorPropertyLayout::collectPropertyGroups( const TypeInfo& type, const vector<hashed_string>& listDrawnName, const EditorListFilter& filter,
                                                         vector<InspectorPropertyGroup>& outListGroup )
    {
        outListGroup.clear();
        // 상속분은 기반 → 파생 순으로 온다(파생이 같은 이름을 다시 선언하면 그 자리에서 바뀐다 — `TypeInfo::getPropertiesWithBase`).
        type.forEachProperty( [&]( const PropertyInfo& prop )
        {
            if ( prop._metadata._bHideInInspector == SW_TRUE )
                return;
            for ( const hashed_string& drawnName : listDrawnName )
            {
                if ( drawnName == prop._name )
                    return;
            }
            if ( filter.matchesAny( { string_view{ prop._name.c_str() }, string_view{ getPropertyLabel( prop ) },
                                      string_view{ prop._metadata._category.c_str() } } ) == false )
                return;

            const string_view       category = prop._metadata._category.empty() ? string_view{ "General" } : string_view{ prop._metadata._category.c_str() };
            InspectorPropertyGroup* pGroup   = nullptr;
            for ( InspectorPropertyGroup& group : outListGroup )
            {
                if ( group._category == category )
                {
                    pGroup = &group;
                    break;
                }
            }
            if ( pGroup == nullptr )
            {
                outListGroup.push_back( InspectorPropertyGroup{ string{ category }, {} } );
                pGroup = &outListGroup.back();
            }
            pGroup->_listProperty.push_back( &prop );
        }, true );
    }

    const utf8* InspectorPropertyLayout::getPropertyLabel( const PropertyInfo& prop )
    {
        if ( prop._metadata._displayName.empty() == false )
            return prop._metadata._displayName.c_str();
        if ( prop._listAlias.empty() == false && prop._listAlias.front().empty() == false )
            return prop._listAlias.front().c_str();
        return prop._name.c_str();
    }
} // namespace sw::editor

#endif // !SW_SHIPPING
