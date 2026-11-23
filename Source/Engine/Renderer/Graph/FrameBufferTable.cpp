#include "pch.h"

#include "Engine/Renderer/Graph/FrameBufferTable.h"

#include "Core/Log/Logger.h"

#include "Engine/Graphics/RHI/RHIStructuredBuffer.h"

namespace sw
{
    void FrameBufferTable::registerBuffer( hashed_string name, uint32 slot, const RHIStructuredBuffer& buffer, RenderGraphAccess initialAccess )
    {
        // 크기 없는 버퍼는 받지 않는다 — 버퍼 보기(`RenderBufferInspector`)가 읽을 길이를 모르는 등록이 생기지 않게.
        if ( buffer._buffer != 0 && ( buffer._elementSize == 0 || buffer._capacityElements == 0 ) )
        {
            SW_LOG_ERROR_ID( "FrameBufferTable.SizeMissing", "Frame resource '%#' slot %# was registered without an element size or capacity - the registration is ignored", name.c_str(), slot );
            return;
        }
        Entry entry{ name, slot, buffer._buffer.getPacked(), RenderGraphViewableKind::Buffer, initialAccess };
        entry._elementSize  = buffer._elementSize;
        entry._elementCount = buffer._capacityElements;
        registerEntry( entry );
    }

    void FrameBufferTable::registerTexture( hashed_string name, uint32 slot, RHITextureHandle texture, RenderGraphAccess initialAccess )
    {
        registerEntry( Entry{ name, slot, texture.getPacked(), RenderGraphViewableKind::Texture, initialAccess } );
    }

    void FrameBufferTable::registerAttachment( hashed_string name, RHITextureHandle texture )
    {
        Entry entry{ name, 0, texture.getPacked(), RenderGraphViewableKind::Texture };
        entry._bAttachment = true;
        registerEntry( entry );
    }

    void FrameBufferTable::registerEntry( const Entry& entry )
    {
        if ( entry._handle == 0 )
        {
            SW_LOG_ERROR_ID( "FrameBufferTable.InvalidHandle", "Frame resource '%#' slot %# was registered without a handle - its passes get no transitions", entry._name.c_str(), entry._slot );
            return;
        }
        for ( const Entry& existing : _listEntry )
        {
            if ( existing._name != entry._name )
                continue;
            if ( existing._slot == entry._slot )
            {
                SW_LOG_ERROR_ID( "FrameBufferTable.DuplicateName", "Frame resource '%#' slot %# is registered twice - the second registration is ignored", entry._name.c_str(), entry._slot );
                return;
            }
            // 그래프는 이름마다 상태 하나를 든다 — 칸마다 다른 처음 상태 · 종류면 한 전이로 모두를 옮길 수 없다.
            if ( existing._initialAccess != entry._initialAccess || existing._viewable != entry._viewable )
            {
                SW_LOG_ERROR_ID( "FrameBufferTable.SlotMismatch", "Frame resource '%#' slot %# differs from slot %# in kind or initial access - the registration is ignored", entry._name.c_str(),
                                 entry._slot, existing._slot );
                return;
            }
        }
        _listEntry.push_back( entry );
    }

    uint64 FrameBufferTable::findHandle( hashed_string name, uint32 slot, RenderGraphViewableKind viewable ) const
    {
        for ( const Entry& entry : _listEntry )
        {
            if ( entry._name == name && entry._slot == slot && entry._viewable == viewable )
                return entry._handle;
        }
        return 0;
    }
} // namespace sw
