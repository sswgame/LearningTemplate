#include "pch.h"

#include "Editor/Common/Widgets/PropertyGrid/Model/InspectorLayoutCache.h"

#include "Editor/Common/Widgets/PropertyGrid/Model/InspectorFavorites.h"

namespace sw::editor
{
    const vector<InspectorPropertyGroup>& InspectorLayoutCache::getOrCreateGroups( const TypeInfo& type, const vector<hashed_string>& listDrawnName, const EditorListFilter& filter,
                                                                                   string_view filterText, uint32 typeGeneration, bool bMayEvict )
    {
        // 모듈을 다시 올리면 TypeInfo · PropertyInfo 주소가 바뀐다 — 세대가 다르면 지은 것을 모두 버린다(리로드는 프레임 사이라 도는 묶음이 없다).
        if ( typeGeneration != _typeGeneration )
        {
            _listSlot.clear();
            _typeGeneration = typeGeneration;
        }
        for ( const unique_ptr<Slot>& pSlot : _listSlot )
        {
            const bool bSameKey = pSlot->_pType == &type && pSlot->_filterText == filterText && pSlot->_listDrawnName == listDrawnName;
            if ( bSameKey )
                return pSlot->_listGroup;
        }

        if ( bMayEvict && _listSlot.size() >= kMaxSlotCount )
            _listSlot.clear();
        ++_rebuildCount;
        Slot& slot          = *_listSlot.emplace_back( make_unique<Slot>() );
        slot._pType         = &type;
        slot._listDrawnName = listDrawnName;
        slot._filterText    = string{ filterText };
        InspectorPropertyLayout::collectPropertyGroups( type, listDrawnName, filter, slot._listGroup );
        size_t itemCount = 0;
        for ( const InspectorPropertyGroup& group : slot._listGroup )
        {
            itemCount += group._listItem.size();
        }
        slot._listDisplay.resize( itemCount ); // 줄이 주소를 들므로 채우기 전에 한 번만 잡는다
        size_t displayIndex = 0;
        for ( InspectorPropertyGroup& group : slot._listGroup )
        {
            for ( InspectorPropertyItem& item : group._listItem )
            {
                InspectorPropertyDisplay& display = slot._listDisplay[displayIndex++];
                display._unit                     = InspectorPropertyLayout::getDisplayUnit( *item._pProperty );
                display._favoriteKey              = InspectorFavorites::makePropertyKey( type, *item._pProperty );
                item._pDisplay                    = &display;
            }
        }
        return slot._listGroup;
    }
} // namespace sw::editor
