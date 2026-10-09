#include "pch.h"

#include "Core/Network/Transport/LoopbackStreamTransport.h"

#include "Core/Concurrency/mutex.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Math/MathUtil.h"
#include "Core/Network/Transport/StreamSendQueue.h"

#include <mutex>

namespace sw
{
    namespace
    {
        class LoopbackStreamTransport;

        enum class LoopbackEventKind : uint8
        {
            Opened = 0,
            Received,
            Writable,
            Closed
        };

        struct LoopbackEvent
        {
            vector<uint8>          _bytes{};
            NetAddress             _remote{};
            StreamConnectionHandle _handle{};
            StreamCloseReason      _reason{ StreamCloseReason::None };
            LoopbackEventKind      _kind{ LoopbackEventKind::Opened };
            uint8                  _bAccepted{ SW_FALSE };
        };

        /** @brief 연결 하나의 두 쪽 — 쪽 i 의 보낼 줄 `_arrQueue[i]` 는 쪽 1−i 가 아직 넘겨받지 않은 바이트다. */
        struct LoopbackLink
        {
            StreamSendQueue          _arrQueue[2]{};
            LoopbackStreamTransport* _arrSide[2]{ nullptr, nullptr };
            StreamConnectionHandle   _arrHandle[2]{};
            StreamCloseReason        _arrCloseReason[2]{ StreamCloseReason::None, StreamCloseReason::None };
            uint8                    _arrPaused[2]{ SW_FALSE, SW_FALSE };
            uint8                    _arrFinRequested[2]{ SW_FALSE, SW_FALSE }; ///< 이 쪽이 더 보내지 않는다(우아한 종료 — 줄이 비면 저쪽에 EOF)
            uint8                    _arrFinDelivered[2]{ SW_FALSE, SW_FALSE }; ///< 이 쪽의 EOF 를 저쪽이 넘겨받았다
            uint8                    _arrClosed[2]{ SW_FALSE, SW_FALSE };
            uint8                    _bUsed{ SW_FALSE };
        };

        struct LoopbackHandleSlot
        {
            int32  _linkIndex{ -1 };
            uint32 _generation{ 1 };
            uint8  _side{ 0 };
            uint8  _bUsed{ SW_FALSE };
        };

        struct LoopbackStreamTransportInternal
        {
            static constexpr uint16 kFirstEphemeralPort = 40000;
        };

        /** @brief 망 전체 — 잠금 하나가 링크 · 전송의 자리 표 · 사건 줄 · 난수를 지킨다. 콜백은 잠금 밖에서. */
        struct LoopbackNetworkState
        {
            mutable mutex                                   _mutex;
            vector<LoopbackLink>                            _listLink;
            vector<int32>                                   _listFreeLink;
            unordered_map<uint16, LoopbackStreamTransport*> _mapListener;
            LoopbackStreamConditions                        _conditions;
            uint32                                          _randomState;
            uint16                                          _nextEphemeralPort;

            int32 nextRandomInRange( int32 maxValue ) // 1..maxValue
            {
                _randomState ^= _randomState << 13;
                _randomState ^= _randomState >> 17;
                _randomState ^= _randomState << 5;
                return 1 + static_cast<int32>( _randomState % static_cast<uint32>( maxValue ) );
            }
        };

        class LoopbackStreamTransport final : public IStreamTransport
        {
        public:
            explicit LoopbackStreamTransport( LoopbackNetworkState* pState )
                : _listSlot{}
                , _listFreeSlot{}
                , _listPending{}
                , _listDispatch{}
                , _settings{}
                , _stats{}
                , _pState{ pState }
                , _pHandler{ nullptr }
                , _localPort{ 0 }
                , _listenPort{ 0 }
                , _bInitialized{ SW_FALSE }
            {
                std::scoped_lock<mutex> lock{ _pState->_mutex };
                _localPort = _pState->_nextEphemeralPort++;
            }

            ~LoopbackStreamTransport() override { shutdown(); }

            bool initialize( IStreamHandler* pHandler, const StreamTransportSettings& settings ) override
            {
                if ( pHandler == nullptr || settings._ioThreadCount != 0 )
                {
                    SW_LOG_ERROR( "LoopbackStreamTransport: needs a handler and _ioThreadCount == 0 (it is driven by pollIo)" );
                    return false;
                }
                _pHandler     = pHandler;
                _settings     = settings;
                _bInitialized = SW_TRUE;
                return true;
            }

            void shutdown() override
            {
                if ( _bInitialized == SW_FALSE )
                    return;
                {
                    std::scoped_lock<mutex> lock{ _pState->_mutex };
                    if ( _listenPort != 0 )
                        _pState->_mapListener.erase( _listenPort );
                    _listenPort = 0;
                    for ( LoopbackHandleSlot& slot : _listSlot )
                    {
                        if ( slot._bUsed == SW_TRUE )
                            abortLinkLocked( slot._linkIndex, slot._side, StreamCloseReason::Shutdown );
                    }
                }
                (void)pollIo( 0 ); // 닫힘 사건을 지금 넘긴다 — 돌아온 뒤에는 콜백이 없다
                {
                    // 저쪽이 아직 자리를 풀지 않은 링크는 이 전송을 가리킨 채 남는다 — 이 전송이 먼저 없어지면 저쪽의 자리 풀기가 없어진 전송을 읽는다.
                    // 두 쪽 모두 닫혔으므로 이 쪽 자리는 더 쓰이지 않는다(자리 풀기는 nullptr 을 "이미 풀렸다" 로 본다).
                    std::scoped_lock<mutex> lock{ _pState->_mutex };
                    for ( LoopbackLink& link : _pState->_listLink )
                    {
                        for ( LoopbackStreamTransport*& pSide : link._arrSide )
                        {
                            if ( pSide == this )
                                pSide = nullptr;
                        }
                    }
                }
                _bInitialized = SW_FALSE;
            }

            bool listen( const NetAddress& bindAddress ) override
            {
                std::scoped_lock<mutex> lock{ _pState->_mutex };
                const uint16            listenPort = bindAddress._port != 0 ? bindAddress._port : _localPort;
                if ( _pState->_mapListener.find( listenPort ) != _pState->_mapListener.end() )
                    return false;
                _pState->_mapListener[listenPort] = this;
                _listenPort                       = listenPort;
                return true;
            }

            uint16 getListenPort() const override { return _listenPort; }

            StreamConnectionHandle connect( const NetAddress& remote ) override
            {
                std::scoped_lock<mutex>        lock{ _pState->_mutex };
                const StreamConnectionHandle   handle = allocateSlotLocked();
                const auto                     found  = _pState->_mapListener.find( remote._port );
                LoopbackStreamTransport* const pPeer  = found != _pState->_mapListener.end() ? found->second : nullptr;
                if ( pPeer == nullptr || pPeer->_bInitialized == SW_FALSE || pPeer->countOpenLocked() >= pPeer->_settings._maxConnections )
                {
                    // 실제 전송처럼 핸들은 주고 다음 pollIo 에 ConnectFailed 로 닫는다.
                    _listSlot[handle._index]._linkIndex = -1;
                    pushEventLocked( LoopbackEvent{ {}, remote, handle, StreamCloseReason::ConnectFailed, LoopbackEventKind::Closed, SW_FALSE } );
                    return handle;
                }
                const int32                  linkIndex  = allocateLinkLocked();
                LoopbackLink&                link       = _pState->_listLink[static_cast<size_t>( linkIndex )];
                const StreamConnectionHandle peerHandle = pPeer->allocateSlotLocked();
                link._arrSide[0]                        = this;
                link._arrSide[1]                        = pPeer;
                link._arrHandle[0]                      = handle;
                link._arrHandle[1]                      = peerHandle;
                for ( int32 side = 0; side < 2; ++side )
                {
                    const StreamTransportSettings& sideSettings = link._arrSide[side]->_settings;
                    link._arrQueue[side].configure( sideSettings._sendHighWatermarkBytes, sideSettings._sendLowWatermarkBytes, sideSettings._maxQueuedSendBytes );
                }
                _listSlot[handle._index]._linkIndex            = linkIndex;
                _listSlot[handle._index]._side                 = 0;
                pPeer->_listSlot[peerHandle._index]._linkIndex = linkIndex;
                pPeer->_listSlot[peerHandle._index]._side      = 1;
                pushEventLocked( LoopbackEvent{ {}, remote, handle, StreamCloseReason::None, LoopbackEventKind::Opened, SW_FALSE } );
                pPeer->pushEventLocked( LoopbackEvent{ {}, NetAddress::makeLoopback( _localPort ), peerHandle, StreamCloseReason::None, LoopbackEventKind::Opened, SW_TRUE } );
                ++_stats._connectedCount;
                ++pPeer->_stats._acceptedCount;
                return handle;
            }

            StreamSendResult send( StreamConnectionHandle handle, const uint8* pData, int32 size ) override
            {
                std::scoped_lock<mutex> lock{ _pState->_mutex };
                LoopbackLink*           pLink = findLinkLocked( handle );
                const uint8             side  = pLink != nullptr ? _listSlot[handle._index]._side : 0;
                if ( pLink == nullptr || pLink->_arrFinRequested[side] == SW_TRUE || pLink->_arrClosed[side] == SW_TRUE )
                    return StreamSendResult::Closed;
                const StreamSendResult result = pLink->_arrQueue[side].append( pData, size );
                if ( result != StreamSendResult::QueueFull )
                    _stats._sentBytes += static_cast<uint64>( size );
                return result;
            }

            void close( StreamConnectionHandle handle, StreamCloseMode mode ) override
            {
                std::scoped_lock<mutex> lock{ _pState->_mutex };
                LoopbackLink*           pLink = findLinkLocked( handle );
                if ( pLink == nullptr )
                    return;
                const uint8 side = _listSlot[handle._index]._side;
                if ( mode == StreamCloseMode::Abort )
                {
                    abortLinkLocked( _listSlot[handle._index]._linkIndex, side, StreamCloseReason::LocalClose );
                    return;
                }
                if ( pLink->_arrFinRequested[side] == SW_FALSE )
                {
                    pLink->_arrFinRequested[side] = SW_TRUE;
                    pLink->_arrCloseReason[side]  = StreamCloseReason::LocalClose;
                }
            }

            void setReceivePaused( StreamConnectionHandle handle, bool bPaused ) override
            {
                std::scoped_lock<mutex> lock{ _pState->_mutex };
                LoopbackLink*           pLink = findLinkLocked( handle );
                if ( pLink != nullptr )
                    pLink->_arrPaused[_listSlot[handle._index]._side] = bPaused ? SW_TRUE : SW_FALSE;
            }

            int32 pollIo( int32 timeoutMilli ) override
            {
                (void)timeoutMilli; // 루프백은 기다릴 것이 없다 — 저쪽이 쓴 것은 이미 망에 있다
                {
                    std::scoped_lock<mutex> lock{ _pState->_mutex };
                    transferLocked();
                    _listDispatch.clear();
                    _listDispatch.swap( _listPending );
                }
                for ( LoopbackEvent& event : _listDispatch )
                {
                    switch ( event._kind )
                    {
                        case LoopbackEventKind::Opened:
                        {
                            _pHandler->onStreamOpened( event._handle, event._remote, event._bAccepted == SW_TRUE );
                            break;
                        }
                        case LoopbackEventKind::Received:
                        {
                            _pHandler->onStreamReceived( event._handle, event._bytes.data(), static_cast<int32>( event._bytes.size() ) );
                            break;
                        }
                        case LoopbackEventKind::Writable:
                        {
                            _pHandler->onStreamWritable( event._handle );
                            break;
                        }
                        case LoopbackEventKind::Closed:
                        {
                            _pHandler->onStreamClosed( event._handle, event._reason );
                            std::scoped_lock<mutex> lock{ _pState->_mutex };
                            freeSlotLocked( event._handle );
                            break;
                        }
                    }
                }
                return static_cast<int32>( _listDispatch.size() );
            }

            NetAddress getRemoteAddress( StreamConnectionHandle handle ) const override
            {
                std::scoped_lock<mutex> lock{ _pState->_mutex };
                const LoopbackLink*     pLink = const_cast<LoopbackStreamTransport*>( this )->findLinkLocked( handle );
                if ( pLink == nullptr )
                    return NetAddress{};
                const uint8                    peerSide = static_cast<uint8>( 1 - _listSlot[handle._index]._side );
                const LoopbackStreamTransport* pPeer    = pLink->_arrSide[peerSide];
                if ( pPeer == nullptr )
                    return NetAddress{}; // 저쪽 전송을 이미 내렸다
                return NetAddress::makeLoopback( peerSide == 1 ? pPeer->_listenPort : pPeer->_localPort );
            }

            StreamTransportStats getStats() const override
            {
                std::scoped_lock<mutex> lock{ _pState->_mutex };
                StreamTransportStats    stats = _stats;
                stats._openCount              = countOpenLocked();
                return stats;
            }

            void pushEventLocked( LoopbackEvent&& event ) { _listPending.push_back( std::move( event ) ); }

        private:
            StreamConnectionHandle allocateSlotLocked()
            {
                uint32 index = 0;
                if ( _listFreeSlot.empty() == false )
                {
                    index = _listFreeSlot.back();
                    _listFreeSlot.pop_back();
                }
                else
                {
                    index = static_cast<uint32>( _listSlot.size() );
                    _listSlot.push_back( LoopbackHandleSlot{} );
                }
                LoopbackHandleSlot& slot = _listSlot[index];
                slot._bUsed              = SW_TRUE;
                slot._linkIndex          = -1;
                return StreamConnectionHandle::make( index, slot._generation );
            }

            void freeSlotLocked( StreamConnectionHandle handle )
            {
                if ( handle._index >= _listSlot.size() || _listSlot[handle._index]._generation != handle._generation )
                    return;
                LoopbackHandleSlot& slot      = _listSlot[handle._index];
                const int32         linkIndex = slot._linkIndex;
                slot._bUsed                   = SW_FALSE;
                slot._linkIndex               = -1;
                ++slot._generation;
                if ( slot._generation == 0 )
                    slot._generation = 1;
                _listFreeSlot.push_back( handle._index );
                ++_stats._closedCount;
                if ( linkIndex >= 0 )
                {
                    LoopbackLink& link = _pState->_listLink[static_cast<size_t>( linkIndex )];
                    if ( link._arrClosed[0] == SW_TRUE && link._arrClosed[1] == SW_TRUE && link._bUsed == SW_TRUE )
                    {
                        // 두 쪽이 모두 닫힘을 넘겨받았는지는 두 번째 free 가 본다 — 처음 free 에서 링크를 지우면 저쪽 자리가 지운 링크를 가리킨다.
                        const uint8                  otherSide   = static_cast<uint8>( 1 - slot._side );
                        LoopbackStreamTransport*     pOther      = link._arrSide[otherSide];
                        const StreamConnectionHandle otherHandle = link._arrHandle[otherSide];
                        const bool                   bOtherFreed = pOther == nullptr || otherHandle._index >= pOther->_listSlot.size() ||
                                                 pOther->_listSlot[otherHandle._index]._generation != otherHandle._generation;
                        if ( bOtherFreed )
                        {
                            link = LoopbackLink{};
                            _pState->_listFreeLink.push_back( linkIndex );
                        }
                    }
                }
            }

            int32 allocateLinkLocked()
            {
                int32 linkIndex = 0;
                if ( _pState->_listFreeLink.empty() == false )
                {
                    linkIndex = _pState->_listFreeLink.back();
                    _pState->_listFreeLink.pop_back();
                }
                else
                {
                    linkIndex = static_cast<int32>( _pState->_listLink.size() );
                    _pState->_listLink.emplace_back();
                }
                _pState->_listLink[static_cast<size_t>( linkIndex )]._bUsed = SW_TRUE;
                return linkIndex;
            }

            LoopbackLink* findLinkLocked( StreamConnectionHandle handle )
            {
                if ( handle.isValid() == false || handle._index >= _listSlot.size() )
                    return nullptr;
                const LoopbackHandleSlot& slot = _listSlot[handle._index];
                if ( slot._bUsed == SW_FALSE || slot._generation != handle._generation || slot._linkIndex < 0 )
                    return nullptr;
                return &_pState->_listLink[static_cast<size_t>( slot._linkIndex )];
            }

            int32 countOpenLocked() const
            {
                int32 count = 0;
                for ( const LoopbackHandleSlot& slot : _listSlot )
                {
                    count += slot._bUsed == SW_TRUE ? 1 : 0;
                }
                return count;
            }

            void abortLinkLocked( int32 linkIndex, uint8 side, StreamCloseReason reason )
            {
                if ( linkIndex < 0 )
                    return;
                LoopbackLink& link = _pState->_listLink[static_cast<size_t>( linkIndex )];
                for ( uint8 index = 0; index < 2; ++index )
                {
                    link._arrQueue[index].clear();
                    if ( link._arrClosed[index] == SW_TRUE )
                        continue;
                    link._arrClosed[index]             = SW_TRUE;
                    const StreamCloseReason sideReason = index == side ? reason : StreamCloseReason::Reset;
                    link._arrSide[index]->pushEventLocked( LoopbackEvent{ {}, {}, link._arrHandle[index], sideReason, LoopbackEventKind::Closed, SW_FALSE } );
                }
            }

            /** @brief 이 전송이 받는 쪽인 링크마다 저쪽 보낼 줄에서 바이트를 넘겨받고, EOF · 닫힘을 정합니다. */
            void transferLocked()
            {
                int32 budget = _pState->_conditions._maxBytesPerPoll > 0 ? _pState->_conditions._maxBytesPerPoll : 0x7FFFFFFF;
                for ( LoopbackHandleSlot& slot : _listSlot )
                {
                    if ( slot._bUsed == SW_FALSE || slot._linkIndex < 0 )
                        continue;
                    LoopbackLink& link     = _pState->_listLink[static_cast<size_t>( slot._linkIndex )];
                    const uint8   side     = slot._side;
                    const uint8   peerSide = static_cast<uint8>( 1 - side );
                    if ( link._arrClosed[side] == SW_TRUE )
                        continue;
                    StreamSendQueue& queue = link._arrQueue[peerSide];
                    while ( link._arrPaused[side] == SW_FALSE && queue.isEmpty() == false && budget > 0 )
                    {
                        int32 take = queue.getQueuedBytes();
                        if ( _pState->_conditions._maxChunkBytes > 0 )
                            take = MathUtil::min( take, _pState->nextRandomInRange( _pState->_conditions._maxChunkBytes ) );
                        take = MathUtil::min( take, budget );
                        LoopbackEvent  event{ {}, {}, link._arrHandle[side], StreamCloseReason::None, LoopbackEventKind::Received, SW_FALSE };
                        StreamSendSpan arrSpan[8];
                        const int32    spanCount = queue.collectSpans( arrSpan, 8, take );
                        for ( int32 index = 0; index < spanCount; ++index )
                        {
                            event._bytes.insert( event._bytes.end(), arrSpan[index]._pData, arrSpan[index]._pData + arrSpan[index]._size );
                        }
                        const int32 moved = static_cast<int32>( event._bytes.size() );
                        budget -= moved;
                        _stats._receivedBytes += static_cast<uint64>( moved );
                        ++_stats._receiveCallCount;
                        pushEventLocked( std::move( event ) );
                        if ( queue.consume( moved ) && link._arrSide[peerSide] != nullptr ) // 먼저 내린 저쪽은 nullptr
                            link._arrSide[peerSide]->pushEventLocked( LoopbackEvent{ {}, {}, link._arrHandle[peerSide], StreamCloseReason::None, LoopbackEventKind::Writable, SW_FALSE } );
                    }
                    // 저쪽의 EOF — 저쪽 줄을 다 넘겨받은 뒤. 이쪽이 아직 닫지 않았으면 실제 전송처럼 이쪽도 우아하게 닫는다(반쯤 열린 연결은 두지 않는다).
                    if ( link._arrFinRequested[peerSide] == SW_TRUE && queue.isEmpty() && link._arrFinDelivered[peerSide] == SW_FALSE )
                    {
                        link._arrFinDelivered[peerSide] = SW_TRUE;
                        if ( link._arrFinRequested[side] == SW_FALSE )
                        {
                            link._arrFinRequested[side] = SW_TRUE;
                            link._arrCloseReason[side]  = StreamCloseReason::RemoteClose;
                        }
                    }
                    settleLinkLocked( link );
                }
            }

            /** @brief 두 쪽 EOF 가 모두 넘어갔으면 각 쪽에 닫힘을 한 번 냅니다. */
            static void settleLinkLocked( LoopbackLink& link )
            {
                if ( link._arrFinDelivered[0] == SW_FALSE || link._arrFinDelivered[1] == SW_FALSE )
                    return;
                for ( uint8 index = 0; index < 2; ++index )
                {
                    if ( link._arrClosed[index] == SW_TRUE )
                        continue;
                    link._arrClosed[index] = SW_TRUE;
                    link._arrSide[index]->pushEventLocked( LoopbackEvent{ {}, {}, link._arrHandle[index], link._arrCloseReason[index], LoopbackEventKind::Closed, SW_FALSE } );
                }
            }

            vector<LoopbackHandleSlot> _listSlot;
            vector<uint32>             _listFreeSlot;
            vector<LoopbackEvent>      _listPending;  ///< 망 잠금 안에서 쌓인 사건
            vector<LoopbackEvent>      _listDispatch; ///< pollIo 스레드 전용 — 잠금 밖에서 넘긴다
            StreamTransportSettings    _settings;
            StreamTransportStats       _stats;
            LoopbackNetworkState*      _pState;
            IStreamHandler*            _pHandler;
            uint16                     _localPort; ///< 이 전송이 건 연결의 "보낸 쪽 포트"(저쪽의 getRemoteAddress)
            uint16                     _listenPort;
            uint8                      _bInitialized;
        };
    } // namespace
} // namespace sw

namespace sw
{
    struct LoopbackStreamNetwork::State : LoopbackNetworkState
    {
    };

    LoopbackStreamNetwork::LoopbackStreamNetwork( uint32 seed )
        : _state{ make_unique<State>() }
    {
        _state->_randomState       = seed != 0 ? seed : 1u;
        _state->_nextEphemeralPort = LoopbackStreamTransportInternal::kFirstEphemeralPort;
    }

    LoopbackStreamNetwork::~LoopbackStreamNetwork() = default;

    unique_ptr<IStreamTransport> LoopbackStreamNetwork::createTransport() { return make_unique<LoopbackStreamTransport>( _state.get() ); }

    void LoopbackStreamNetwork::setConditions( const LoopbackStreamConditions& conditions )
    {
        std::scoped_lock<mutex> lock{ _state->_mutex };
        _state->_conditions = conditions;
    }
} // namespace sw
