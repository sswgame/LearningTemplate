#include "pch.h"

#include "Engine/Renderer/Graph/FrameBufferTable.h"

#include "Core/Log/Logger.h"

#include "Engine/Graphics/RHI/RHIStructuredBuffer.h"

namespace sw
{
    void FrameBufferTable::registerBuffer( hashed_string name, uint32 slot, const RHIStructuredBuffer& buffer, RenderGraphAccess initialAccess )
    {
        registerBufferEntry( Entry{ name, slot, buffer._buffer.getPacked(), RenderGraphViewableKind::Buffer, initialAccess }, buffer );
    }

    void FrameBufferTable::registerGraphBuffer( hashed_string name, const RHIStructuredBuffer& buffer )
    {
        Entry entry{ name, 0, buffer._buffer.getPacked(), RenderGraphViewableKind::Buffer };
        entry._bGraphOwned = true;
        registerBufferEntry( entry, buffer );
    }

    void FrameBufferTable::registerBufferEntry( Entry entry, const RHIStructuredBuffer& buffer )
    {
        // 크기 없는 버퍼는 받지 않는다 — 버퍼 보기(`RenderBufferInspector`)가 읽을 길이를 모르는 등록이 생기지 않게.
        if ( buffer._buffer != 0 && ( buffer._elementSize == 0 || buffer._capacityElements == 0 ) )
        {
            SW_LOG_ERROR_ID( "FrameBufferTable.SizeMissing", "Frame resource '%#' slot %# was registered without an element size or capacity - the registration is ignored", entry._name.c_str(),
                             entry._slot );
            return;
        }
        entry._elementSize  = buffer._elementSize;
        entry._elementCount = buffer._capacityElements;
        entry._srv          = buffer._srv;
        entry._uav          = buffer._uav;
        registerEntry( entry );
    }

    void FrameBufferTable::registerTexture( hashed_string name, uint32 slot, RHITextureHandle texture, RenderGraphAccess initialAccess )
    {
        registerEntry( Entry{ name, slot, texture.getPacked(), RenderGraphViewableKind::Texture, initialAccess } );
    }

    void FrameBufferTable::registerAttachment( hashed_string name, RHITextureHandle texture )
    {
        Entry entry{ name, 0, texture.getPacked(), RenderGraphViewableKind::Texture };
        entry._bGraphOwned = true;
        registerEntry( entry );
    }

    void FrameBufferTable::registerEntry( const Entry& entry )
    {
        if ( entry._handle == 0 )
        {
            SW_LOG_ERROR_ID( "FrameBufferTable.InvalidHandle", "Frame resource '%#' slot %# was registered without a handle - its passes get no transitions", entry._name.c_str(), entry._slot );
            return;
        }
        vector<uint32>& listSlotEntry = _mapSlotEntry[entry._name];
        if ( entry._slot < listSlotEntry.size() && listSlotEntry[entry._slot] != constant::invalid_index::kUint32 )
        {
            SW_LOG_ERROR_ID( "FrameBufferTable.DuplicateName", "Frame resource '%#' slot %# is registered twice - the second registration is ignored", entry._name.c_str(), entry._slot );
            return;
        }
        // 그래프는 이름마다 상태 하나를 든다 — 칸마다 다른 처음 상태 · 종류면 한 전이로 모두를 옮길 수 없다. 앞 등록끼리는 이미 같으므로 하나(끝 칸은 늘 등록돼 있다)만 본다.
        const bool bFirst = listSlotEntry.empty();
        if ( bFirst == false )
        {
            const Entry& existing = _listEntry[listSlotEntry.back()];
            if ( existing._initialAccess != entry._initialAccess || existing._viewable != entry._viewable )
            {
                SW_LOG_ERROR_ID( "FrameBufferTable.SlotMismatch", "Frame resource '%#' slot %# differs from slot %# in kind or initial access - the registration is ignored", entry._name.c_str(),
                                 entry._slot, existing._slot );
                return;
            }
        }
        const uint32 entryIndex = static_cast<uint32>( _listEntry.size() );
        if ( bFirst )
            _listFirstEntry.push_back( entryIndex );
        if ( entry._slot >= listSlotEntry.size() )
            listSlotEntry.resize( static_cast<size_t>( entry._slot ) + 1u, constant::invalid_index::kUint32 );
        listSlotEntry[entry._slot] = entryIndex;
        _listEntry.push_back( entry );
    }

    void FrameBufferTable::reset()
    {
        for ( const uint32 entryIndex : _listFirstEntry )
        {
            _mapSlotEntry[_listEntry[entryIndex]._name].clear();
        }
        _listEntry.clear();
        _listFirstEntry.clear();
    }

    const vector<uint32>* FrameBufferTable::findSlots( hashed_string name ) const
    {
        const auto it = _mapSlotEntry.find( name );
        return it != _mapSlotEntry.end() && it->second.empty() == false ? &it->second : nullptr;
    }

    const FrameBufferTable::Entry* FrameBufferTable::findEntry( hashed_string name, uint32 slot ) const
    {
        const vector<uint32>* pSlots = findSlots( name );
        if ( pSlots == nullptr || slot >= pSlots->size() || ( *pSlots )[slot] == constant::invalid_index::kUint32 )
            return nullptr;
        return &_listEntry[( *pSlots )[slot]];
    }

    uint64 FrameBufferTable::findHandle( hashed_string name, uint32 slot, RenderGraphViewableKind viewable ) const
    {
        const Entry* pEntry = findEntry( name, slot );
        return pEntry != nullptr && pEntry->_viewable == viewable ? pEntry->_handle : 0;
    }
} // namespace sw
