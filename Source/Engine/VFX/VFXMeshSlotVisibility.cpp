#include "pch.h"

#include "Engine/VFX/VFXMeshSlotVisibility.h"

#include "Engine/Visual/MeshInstanceBatch.h"

namespace sw
{
    void VFXMeshSlotVisibility::reset( MeshInstanceBatch& batch )
    {
        for ( uint32 entry = 0; entry < batch.getCount(); ++entry )
        {
            batch.setEntryVisible( entry, false );
        }
        _listVisibleSlot.clear();
        _listSlotMark.assign( batch.getCount(), 0 );
    }

    void VFXMeshSlotVisibility::update( MeshInstanceBatch& batch, const vector<uint32>& listSlot )
    {
        SW_ASSERT( _listSlotMark.size() == batch.getCount() ); // `reset` 을 먼저 부른다
        for ( const uint32 slot : listSlot )
        {
            SW_ASSERT( slot < _listSlotMark.size() );
            _listSlotMark[slot] = 1;
            batch.setEntryVisible( slot, true ); // 이미 보이면 아무것도 하지 않는다
        }
        // 지난번 보였고 이번 표시가 없으면 이번에 죽은 칸이다. 표시는 이번 칸만 켰으므로 여기서 다시 끈다.
        for ( const uint32 slot : _listVisibleSlot )
        {
            if ( _listSlotMark[slot] == 0 )
                batch.setEntryVisible( slot, false );
        }
        for ( const uint32 slot : listSlot )
        {
            _listSlotMark[slot] = 0;
        }
        _listVisibleSlot = listSlot;
    }
} // namespace sw
