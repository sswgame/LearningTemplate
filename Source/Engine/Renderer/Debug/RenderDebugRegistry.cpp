#include "pch.h"

#include "Engine/Renderer/Debug/RenderDebugRegistry.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Foundation/EngineSubsystemRegistration.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingLayout.h"
#include "Engine/Reflection/EnumName.h"

namespace sw
{
    namespace
    {
        struct RenderDebugRegistryInternal
        {
            /** @brief 바인딩 종류의 HLSL 레지스터 글자입니다(b 상수 · t 읽기 · u 읽고 쓰기 · s 샘플러). */
            static const utf8* getRegisterLetter( ShaderBindingKind kind )
            {
                switch ( kind )
                {
                    case ShaderBindingKind::ConstantBuffer:
                        return "b";
                    case ShaderBindingKind::Texture:
                    case ShaderBindingKind::StructuredBuffer:
                        return "t";
                    case ShaderBindingKind::RwStructuredBuffer:
                    case ShaderBindingKind::RwTexture:
                        return "u";
                    case ShaderBindingKind::Sampler:
                        return "s";
                    case ShaderBindingKind::Unknown:
                        break;
                }
                return "?";
            }

            /** @brief 멤버(이름 · 형식 · 오프셋) 한 줄을 들여 적습니다. */
            static void appendMembers( const vector<ShaderVariableInfo>& listMember, vector<string>& outList )
            {
                for ( const ShaderVariableInfo& member : listMember )
                {
                    StringBuilder<constant::buffer::kMax256> line;
                    line.appendFormat( "    %# %# @%#", member._name.c_str(), member._type.c_str(), member._offset );
                    outList.emplace_back( line.view() );
                }
            }
        };
    } // namespace

    void RenderDebugRegistry::publishGraphs( vector<RenderGraphSnapshot> listGraph )
    {
        {
            ScopedLock<mutex> lock{ _mutex };
            _listGraph = std::move( listGraph );
        }
        _graphSerial.fetch_add( 1, std::memory_order_release );
    }

    void RenderDebugRegistry::copyGraphs( vector<RenderGraphSnapshot>& outList ) const
    {
        ScopedLock<mutex> lock{ _mutex };
        outList = _listGraph;
    }

    void RenderDebugRegistry::setFrameDebugger( const FrameDebuggerSettings& settings )
    {
        ScopedLock<mutex> lock{ _mutex };
        _settings = settings;
    }

    FrameDebuggerSettings RenderDebugRegistry::getFrameDebugger() const
    {
        ScopedLock<mutex> lock{ _mutex };
        return _settings;
    }

    void RenderDebugRegistry::copyDrawEvents( vector<FrameDebugDrawEvent>& outList ) const
    {
        ScopedLock<mutex> lock{ _mutex };
        outList = _listEvent;
    }

    RenderFrameStats RenderDebugRegistry::getStats() const
    {
        ScopedLock<mutex> lock{ _mutex };
        return _stats;
    }

    void RenderDebugRegistry::beginFrame( const vector<hashed_string>& listPassOrder )
    {
        const FrameDebuggerSettings settings     = getFrameDebugger();
        const bool                  bStatsWanted = _statsWantedFrames.load( std::memory_order_acquire ) > 0;
        const bool                  bRecording   = settings._bEnabled != SW_FALSE || bStatsWanted;
        {
            ScopedLock<mutex> lock{ _frameMutex };
            _frameSettings = settings;
            _mapPassDraw.clear();
            _mapPSOBinding.clear();
            _listFrameEvent.clear();
            _frameDrawCount     = 0;
            _frameTriangleCount = 0;
            _stopOrder          = nullopt;
            _mapPassOrder.clear();
            if ( settings._bEnabled != SW_FALSE )
            {
                for ( uint32 order = 0; order < static_cast<uint32>( listPassOrder.size() ); ++order )
                {
                    _mapPassOrder[listPassOrder[order]] = order;
                    if ( string_view{ listPassOrder[order].c_str() } == settings._stopPass )
                        _stopOrder = order;
                }
            }
        }
        _bRecording.store( bRecording ? SW_TRUE : SW_FALSE, std::memory_order_relaxed );
    }

    bool RenderDebugRegistry::isPastStop( optional<uint32> passOrder, uint32 drawIndex, uint32 stopOrder, uint32 stopDraw )
    {
        if ( passOrder.has_value() == false || *passOrder < stopOrder )
            return false;
        return *passOrder > stopOrder || drawIndex > stopDraw;
    }

    bool RenderDebugRegistry::allowDraw( hashed_string passName, uint64 pso, const ShaderBindingLayout* pLayout, uint32 indexCount, uint32 groupCount, uint64 triangleCount )
    {
        ScopedLock<mutex>      lock{ _frameMutex };
        const uint32           drawIndex = _mapPassDraw[passName]++;
        const auto             orderIt   = _mapPassOrder.find( passName );
        const optional<uint32> passOrder = orderIt != _mapPassOrder.end() ? optional<uint32>{ orderIt->second } : nullopt;
        const bool             bAllowed  = _frameSettings._bEnabled == SW_FALSE || _stopOrder.has_value() == false ||
                              isPastStop( passOrder, drawIndex, *_stopOrder, _frameSettings._stopDraw ) == false;
        if ( bAllowed )
        {
            ++_frameDrawCount;
            _frameTriangleCount += triangleCount;
        }
        if ( _frameSettings._bEnabled == SW_FALSE )
            return bAllowed;

        FrameDebugDrawEvent event{};
        event._passName      = passName.c_str();
        event._pso           = pso;
        event._triangleCount = triangleCount;
        event._drawIndex     = drawIndex;
        event._passOrder     = passOrder.value_or( MathUtil::kMaxUInt32 );
        event._indexCount    = indexCount;
        event._groupCount    = groupCount;
        event._bSkipped      = bAllowed ? SW_FALSE : SW_TRUE;
        if ( pLayout != nullptr )
        {
            // 같은 PSO 의 드로우가 많다 — 바인딩 글은 프레임 안에서 PSO 마다 한 번 만든다.
            auto it = _mapPSOBinding.find( pso );
            if ( it == _mapPSOBinding.end() )
            {
                vector<string> listBinding;
                describeBindings( *pLayout, listBinding );
                it = _mapPSOBinding.emplace( pso, std::move( listBinding ) ).first;
            }
            event._listBinding = it->second;
        }
        _listFrameEvent.push_back( std::move( event ) );
        return bAllowed;
    }

    void RenderDebugRegistry::endFrame()
    {
        if ( _bRecording.exchange( SW_FALSE, std::memory_order_relaxed ) == SW_FALSE )
            return;
        // 원하던 통계는 남은 프레임을 하나 줄인다(HUD 가 다시 원하지 않으면 멈춘다).
        uint32 wanted = _statsWantedFrames.load( std::memory_order_acquire );
        while ( wanted > 0 && _statsWantedFrames.compare_exchange_weak( wanted, wanted - 1, std::memory_order_acq_rel ) == false )
        {
        }

        vector<FrameDebugDrawEvent> listEvent;
        RenderFrameStats            stats{};
        bool                        bPublishEvents = false;
        {
            ScopedLock<mutex> lock{ _frameMutex };
            stats._drawCount     = _frameDrawCount;
            stats._triangleCount = _frameTriangleCount;
            bPublishEvents       = _frameSettings._bEnabled != SW_FALSE;
            if ( bPublishEvents )
            {
                listEvent = std::move( _listFrameEvent );
                _listFrameEvent.clear();
            }
        }
        // 병렬 기록 워커가 패스마다 따로 적었다 — 실행 순서 · 패스 안 번호로 다시 줄 세운다(그래프에 없는 패스는 끝).
        VectorUtil::stableSort( listEvent, []( const FrameDebugDrawEvent& lhs, const FrameDebugDrawEvent& rhs )
        { return lhs._passOrder != rhs._passOrder ? lhs._passOrder < rhs._passOrder : lhs._drawIndex < rhs._drawIndex; } );
        {
            ScopedLock<mutex> lock{ _mutex };
            stats._serial = _stats._serial + 1;
            _stats        = stats;
            if ( bPublishEvents )
                _listEvent = std::move( listEvent );
        }
        if ( bPublishEvents )
            _drawEventSerial.fetch_add( 1, std::memory_order_release );
    }

    void RenderDebugRegistry::describeBindings( const ShaderBindingLayout& layout, vector<string>& outList )
    {
        for ( const ShaderBindingSlot& slot : layout.getSlots() )
        {
            StringBuilder<constant::buffer::kMax256> line;
            line.appendFormat( "%#%# space%# %# %#", RenderDebugRegistryInternal::getRegisterLetter( slot._kind ), slot._registerIndex, slot._space, slot._name.c_str(),
                               getEnumName( slot._kind ) );
            if ( slot._kind == ShaderBindingKind::ConstantBuffer )
                line.appendFormat( " %# B", slot._cbTotalSize );
            else if ( slot._elementStride > 0 )
                line.appendFormat( " stride %#", slot._elementStride );
            if ( slot._arrayCount > 1 )
                line.appendFormat( " [%#]", slot._arrayCount );
            outList.emplace_back( line.view() );
            RenderDebugRegistryInternal::appendMembers( slot._kind == ShaderBindingKind::ConstantBuffer ? slot._listCbMember : slot._listElementMember, outList );
        }
    }

    void RenderDebugRegistry::publishBufferList( vector<RenderBufferEntry> listEntry )
    {
        {
            ScopedLock<mutex> lock{ _mutex };
            _listBuffer = std::move( listEntry );
        }
        _bufferListSerial.fetch_add( 1, std::memory_order_release );
    }

    void RenderDebugRegistry::copyBufferList( vector<RenderBufferEntry>& outList ) const
    {
        ScopedLock<mutex> lock{ _mutex };
        outList = _listBuffer;
    }

    uint32 RenderDebugRegistry::requestBufferRead( const string& name, uint32 slot )
    {
        ScopedLock<mutex> lock{ _mutex };
        ++_lastBufferRequestSerial;
        _pendingBufferName   = name;
        _pendingBufferSlot   = slot;
        _pendingBufferSerial = _lastBufferRequestSerial;
        return _lastBufferRequestSerial;
    }

    bool RenderDebugRegistry::takeBufferReadRequest( string& outName, uint32& outSlot, uint32& outSerial )
    {
        ScopedLock<mutex> lock{ _mutex };
        if ( _pendingBufferSerial == 0 )
            return false;
        outName              = std::move( _pendingBufferName );
        outSlot              = _pendingBufferSlot;
        outSerial            = _pendingBufferSerial;
        _pendingBufferSerial = 0;
        _pendingBufferName.clear();
        return true;
    }

    void RenderDebugRegistry::publishBufferContent( RenderBufferContent content )
    {
        const uint32 serial = content._serial;
        {
            ScopedLock<mutex> lock{ _mutex };
            _bufferContent = std::move( content );
        }
        _bufferContentSerial.store( serial, std::memory_order_release );
    }

    bool RenderDebugRegistry::copyBufferContent( RenderBufferContent& outContent ) const
    {
        ScopedLock<mutex> lock{ _mutex };
        if ( _bufferContent._serial == 0 )
            return false;
        outContent = _bufferContent;
        return true;
    }
} // namespace sw

namespace sw
{
    SW_ENGINE_SERVICE_SUBSYSTEM( RenderDebugRegistry, HostOnly, Services, (), SW_ENGINE_DEFAULT_FACTORY( RenderDebugRegistry ) );
} // namespace sw
