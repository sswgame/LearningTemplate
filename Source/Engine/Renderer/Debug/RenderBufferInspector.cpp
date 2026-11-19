#include "pch.h"

#include "Engine/Renderer/Debug/RenderBufferInspector.h"

#include "Engine/Graphics/RHI/IRHICommandList.h"
#include "Engine/Renderer/Graph/FrameBufferTable.h"

namespace sw
{
    void RenderBufferInspector::service( IRHICommandList& cmd, IRHIDevice* pDevice, const FrameBufferTable& table, const RenderGraphExecutionContext& context, BufferStateFn findState,
                                         RenderDebugRegistry& registry )
    {
        if ( registry.takeBufferListRequest() )
        {
            vector<RenderBufferEntry> listEntry;
            for ( const FrameBufferTable::Entry& entry : table.getEntries() )
            {
                if ( entry._viewable != RenderGraphViewableKind::Buffer )
                    continue;
                listEntry.push_back( RenderBufferEntry{ entry._name.c_str(), entry._slot, entry._elementSize, entry._elementCount } );
            }
            registry.publishBufferList( std::move( listEntry ) );
        }

        // 읽는 중이면 도착을 본다 — 한 번에 하나만 읽는다(새 요청은 우편함에 남아 다음 차례를 기다린다).
        if ( _pendingSerial != 0 )
        {
            vector<uint8> listByte;
            uint64        tag = 0;
            if ( _readback.takeLatest( listByte, tag ) == false || tag != _tag )
                return;
            registry.publishBufferContent( RenderBufferContent{ _pendingEntry, std::move( listByte ), _pendingSerial } );
            _pendingSerial = 0;
            _readback.shutdown();
        }

        string name;
        uint32 slot   = 0;
        uint32 serial = 0;
        if ( registry.takeBufferReadRequest( name, slot, serial ) == false )
            return;
        // 실패(없는 이름 · 크기를 모름 · 읽기 버퍼를 못 만듦 · 고리가 참)는 빈 결과로 알린다 — 창이 "읽지 못함" 을 보이고 다시 요청할 수 있다.
        RenderBufferContent failure{
            RenderBufferEntry{ name, slot, 0, 0 },
            {},
            serial
        };
        const hashed_string key = hashed_string::findInterned( name );
        for ( const FrameBufferTable::Entry& entry : table.getEntries() )
        {
            const bool bTarget = entry._name == key && entry._slot == slot && entry._viewable == RenderGraphViewableKind::Buffer;
            if ( bTarget == false )
                continue;
            failure._entry                            = RenderBufferEntry{ name, slot, entry._elementSize, entry._elementCount };
            const RenderGraphAccess        lastAccess = context.findAccess( entry._name ).value_or( entry._initialAccess );
            const optional<RHIBufferState> stateAfter = findState( lastAccess );
            const bool                     bReadable  = entry._elementSize > 0 && entry._elementCount > 0 && stateAfter.has_value() &&
                                   _readback.initialize( pDevice, entry._elementSize, entry._elementCount );
            if ( bReadable && _readback.request( cmd, static_cast<RHIBufferHandle>( entry._handle ), *stateAfter, ++_tag ) )
            {
                _pendingEntry  = failure._entry;
                _pendingSerial = serial;
                return;
            }
            _readback.shutdown();
            break;
        }
        registry.publishBufferContent( std::move( failure ) );
    }

    void RenderBufferInspector::release()
    {
        _readback.shutdown();
        _pendingSerial = 0;
    }
} // namespace sw
