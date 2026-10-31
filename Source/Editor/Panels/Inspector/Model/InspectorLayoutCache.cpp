#include "pch.h"

#include "Editor/Panels/Inspector/Model/InspectorLayoutCache.h"

#include "Editor/Panels/Inspector/Model/InspectorFavorites.h"

namespace sw::editor
{
    const vector<InspectorPropertyGroup>& InspectorLayoutCache::getOrCreateGroups( const TypeInfo& type, const vector<hashed_string>& listDrawnName, const EditorListFilter& filter,
                                                                                   string_view filterText, uint32 typeGeneration, bool bMayEvict )
    {
        // 모듈을 다시 올리면 TypeInfo · PropertyInfo 주소가 바뀐다 — 세대가 다르면 지은 것을 모두 버린다(리로드는 프레임 사이라 도는 묶음이 없다).
        if ( typeGeneration != _typeGeneration )
        {
            _listSlot.clear();
            _mapDisplay.clear();
            _typeGeneration = typeGeneration;
        }
        for ( const unique_ptr<Slot>& pSlot : _listSlot )
        {
            const bool bSameKey = pSlot->_pType == &type && pSlot->_filterText == filterText && pSlot->_listDrawnName == listDrawnName;
            if ( bSameKey )
                return pSlot->_listGroup;
        }

        if ( bMayEvict && _listSlot.size() >= kMaxSlotCount )
            _listSlot.clear(); // 표시 정보는 남긴다 — 프로퍼티별이라 검색 글과 무관하다
        ++_rebuildCount;
        Slot& slot          = *_listSlot.emplace_back( make_unique<Slot>() );
        slot._pType         = &type;
        slot._listDrawnName = listDrawnName;
        slot._filterText    = string{ filterText };
        InspectorPropertyLayout::collectPropertyGroups( type, listDrawnName, filter, slot._listGroup );
        for ( const InspectorPropertyGroup& group : slot._listGroup )
        {
            for ( const PropertyInfo* pProp : group._listProperty )
            {
                if ( _mapDisplay.find( pProp ) != _mapDisplay.end() )
                    continue;
                unique_ptr<InspectorPropertyDisplay> pDisplay = make_unique<InspectorPropertyDisplay>();
                pDisplay->_unit                               = InspectorPropertyLayout::getDisplayUnit( *pProp );
                pDisplay->_favoriteKey                        = InspectorFavorites::makePropertyKey( type, *pProp );
                _mapDisplay.emplace( pProp, std::move( pDisplay ) );
            }
        }
        return slot._listGroup;
    }

    InspectorPropertyDisplay* InspectorLayoutCache::findDisplay( const PropertyInfo& prop )
    {
        const auto iter = _mapDisplay.find( &prop );
        return iter != _mapDisplay.end() ? iter->second.get() : nullptr;
    }

    const InspectorPropertyDisplay& InspectorLayoutCache::getLaidOutDisplay( const PropertyInfo& prop ) const
    {
        const auto iter = _mapDisplay.find( &prop );
        SW_ASSERT( iter != _mapDisplay.end() ); // 행은 이 캐시가 돌려준 묶음에서만 그린다
        return *iter->second;
    }
} // namespace sw::editor
