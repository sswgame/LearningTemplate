#include "pch.h"

#include "Editor/Panels/RenderDebugger/Model/RenderDebuggerViewModel.h"

#include "Core/String/StringBuilder.h"

#include "Engine/Profiling/FrameProfiler.h"

namespace sw::editor
{
    namespace
    {
        struct RenderDebuggerViewModelInternal
        {
            /** @brief 프로파일러 이름 @p pName 의 마지막 프레임 카운터 값입니다. 프로파일러가 없으면 0 입니다. */
            static uint64 readCounter( FrameProfiler* pProfiler, const utf8* pName )
            {
                return pProfiler != nullptr ? pProfiler->getLastFrameCount( pProfiler->registerScope( pName ) ) : 0u;
            }
        };
    } // namespace

    const RenderDebuggerState& RenderDebuggerViewModel::gather( const RenderDebuggerSources& sources )
    {
        _sources = sources;
        syncRegistry();

        // 가림 사본은 렌더러가 몇 프레임마다 바꾼다 — 번호가 바뀐 것만 다시 센다.
        if ( _sources._pOcclusion != _state._pOcclusion )
        {
            _state._pOcclusion = _sources._pOcclusion;
            _state._occlusion  = _state._pOcclusion != nullptr ? OcclusionTileStats::make( *_state._pOcclusion ) : OcclusionTileStats{};
        }
        using Internal                      = RenderDebuggerViewModelInternal;
        FrameProfiler* const pProfiler      = _sources._pProfiler;
        _state._streaming._textureCount     = Internal::readCounter( pProfiler, "RT.TextureStreaming.textures" );
        _state._streaming._belowWantedCount = Internal::readCounter( pProfiler, "RT.TextureStreaming.belowWanted" );
        _state._streaming._missingMipCount  = Internal::readCounter( pProfiler, "RT.TextureStreaming.missingMips" );
        _state._streaming._uploadsWaiting   = Internal::readCounter( pProfiler, "RT.TextureUpload.waiting" );
        _state._streaming._residentBytes    = Internal::readCounter( pProfiler, "RT.TextureStreaming.residentBytes" );
        _state._streaming._poolBytes        = Internal::readCounter( pProfiler, "RT.TextureStreaming.poolBytes" );
        _state._streaming._bytesInFlight    = Internal::readCounter( pProfiler, "RT.TextureStreaming.bytesInFlight" );
        return _state;
    }

    void RenderDebuggerViewModel::syncRegistry()
    {
        RenderDebugRegistry* const pRegistry = _sources._pRegistry;
        if ( pRegistry == nullptr )
            return;
        _state._frameDebugger = pRegistry->getFrameDebugger();
        if ( pRegistry->getDrawEventSerial() != _eventSerial )
        {
            _eventSerial = pRegistry->getDrawEventSerial();
            pRegistry->copyDrawEvents( _state._listEvent );
        }
        _state._stopEvent = _state._frameDebugger._bEnabled != SW_FALSE ? RenderDebugRegistry::findStopEvent( _state._listEvent, _state._frameDebugger ) : nullopt;

        if ( pRegistry->getGraphSerial() != _graphSerial )
        {
            _graphSerial = pRegistry->getGraphSerial();
            pRegistry->copyGraphs( _state._listGraph );
            if ( _state._graphIndex >= _state._listGraph.size() )
                _state._graphIndex = 0;
            rebuildGraphLayout();
        }
        if ( pRegistry->getBufferListSerial() != _bufferListSerial )
        {
            _bufferListSerial = pRegistry->getBufferListSerial();
            pRegistry->copyBufferList( _state._listBuffer );
            if ( _state._selectedBuffer.has_value() && *_state._selectedBuffer >= _state._listBuffer.size() )
                _state._selectedBuffer = nullopt;
        }
        // 기다리던 읽기가 도착했으면 받는다(다른 요청의 결과는 버린다 — 고르기를 바꾼 사이 도착).
        if ( _pendingBufferSerial != 0 && pRegistry->getBufferContentSerial() == _pendingBufferSerial && pRegistry->copyBufferContent( _state._bufferContent ) )
        {
            _pendingBufferSerial = 0;
            _state._bufferLayout = BufferViewLayout::make( _state._bufferContent._entry._elementSize, _scalar );
        }
        _state._bBufferReading = _pendingBufferSerial != 0 ? SW_TRUE : SW_FALSE;
    }

    void RenderDebuggerViewModel::rebuildGraphLayout()
    {
        ++_state._layoutSerial;
        _state._listPassMilliseconds.clear();
        if ( _state._graphIndex >= _state._listGraph.size() )
        {
            _state._graphLayout = RenderGraphLayout{};
            return;
        }
        const RenderGraphSnapshot& graph = _state._listGraph[_state._graphIndex];
        _state._graphLayout              = RenderGraphLayout::make( graph );
        // 패스 GPU 시간(`GPU.<패스>` — 프로파일러 · gpu.timings 와 같은 구간). 재지 않은 패스(계측 꺼짐 · 슬롯 밖)는 음수로 둔다.
        FrameProfiler* const pProfiler = _sources._pProfiler;
        for ( const RenderGraphSnapshotPass& pass : graph._listPass )
        {
            float32 milliseconds = -1.0f;
            if ( pProfiler != nullptr )
            {
                StringBuilder<constant::buffer::kMax128> scopeName;
                scopeName.appendFormat( "GPU.%#", pass._name.c_str() );
                const uint64 nanos = pProfiler->getLastFrameNanos( pProfiler->registerScope( string( scopeName.view() ).c_str() ) );
                if ( nanos > 0 )
                    milliseconds = static_cast<float32>( static_cast<float64>( nanos ) / constant::time::kNanosecondsPerMillisecond );
            }
            _state._listPassMilliseconds.push_back( milliseconds );
        }
    }

    void RenderDebuggerViewModel::selectTab( RenderDebuggerTab tab )
    {
        _state._tab = tab;
        if ( tab == RenderDebuggerTab::RenderGraph && _bGraphRequested == SW_FALSE )
            requestGraphCapture();
        if ( tab == RenderDebuggerTab::Buffers && _bBufferListRequested == SW_FALSE )
            requestBufferList();
    }

    void RenderDebuggerViewModel::setFrameDebuggerEnabled( bool bEnabled )
    {
        if ( _sources._pRegistry == nullptr )
            return;
        FrameDebuggerSettings settings{};
        settings._bEnabled = bEnabled ? SW_TRUE : SW_FALSE;
        _sources._pRegistry->setFrameDebugger( settings );
        _state._frameDebugger = settings;
    }

    void RenderDebuggerViewModel::stepFrameDebugger( int32 delta )
    {
        if ( _sources._pRegistry == nullptr )
            return;
        _state._frameDebugger = RenderDebugRegistry::stepStop( _state._listEvent, _state._frameDebugger, delta );
        _sources._pRegistry->setFrameDebugger( _state._frameDebugger );
    }

    void RenderDebuggerViewModel::selectEvent( uint32 eventIndex )
    {
        if ( _sources._pRegistry == nullptr || eventIndex >= _state._listEvent.size() )
            return;
        FrameDebuggerSettings settings = _state._frameDebugger;
        settings._stopPass             = _state._listEvent[eventIndex]._passName;
        settings._stopDraw             = _state._listEvent[eventIndex]._drawIndex;
        settings._bEnabled             = SW_TRUE;
        _sources._pRegistry->setFrameDebugger( settings );
        _state._frameDebugger = settings;
    }

    void RenderDebuggerViewModel::requestGraphCapture()
    {
        if ( _sources._pRegistry == nullptr )
            return;
        _sources._pRegistry->requestGraphCapture();
        _bGraphRequested = SW_TRUE;
    }

    void RenderDebuggerViewModel::selectGraph( uint32 graphIndex )
    {
        if ( graphIndex >= _state._listGraph.size() || graphIndex == _state._graphIndex )
            return;
        _state._graphIndex = graphIndex;
        rebuildGraphLayout();
    }

    void RenderDebuggerViewModel::requestBufferList()
    {
        if ( _sources._pRegistry == nullptr )
            return;
        _sources._pRegistry->requestBufferList();
        _bBufferListRequested = SW_TRUE;
    }

    void RenderDebuggerViewModel::selectBuffer( uint32 bufferIndex )
    {
        if ( bufferIndex >= _state._listBuffer.size() )
            return;
        _state._selectedBuffer = bufferIndex;
        refreshBuffer();
    }

    void RenderDebuggerViewModel::refreshBuffer()
    {
        if ( _sources._pRegistry == nullptr || _state._selectedBuffer.has_value() == false )
            return;
        const RenderBufferEntry& entry = _state._listBuffer[*_state._selectedBuffer];
        _pendingBufferSerial           = _sources._pRegistry->requestBufferRead( entry._name, entry._slot );
        _state._bBufferReading         = SW_TRUE;
    }

    void RenderDebuggerViewModel::setBufferScalar( BufferViewScalar scalar )
    {
        _scalar              = scalar;
        _state._bufferLayout = BufferViewLayout::make( _state._bufferContent._entry._elementSize, scalar );
    }
} // namespace sw::editor
