#include "pch.h"

#include "Core/Network/Connection/NetConnection.h"

#include "Core/Math/MathUtil.h"
#include "Core/Network/Connection/NetFlowLog.h"
#include "Core/Serialization/BitStream.h"

namespace sw
{
    namespace
    {
        struct NetConnectionInternal
        {
            static constexpr int32 kChannelBits = 2;

            /** @brief 비신뢰 · 순서만 메시지의 흐름 번호입니다 — (패킷 번호 << 8 | 패킷 안 자리). 자리는 `kMessageCountMax`(255) 아래다. */
            static uint64 makeWirePosition( uint16 sequence, int32 position ) { return ( static_cast<uint64>( sequence ) << 8 ) | static_cast<uint64>( position & 0xFF ); }
            static_assert( static_cast<int32>( NetChannelType::Count ) <= ( 1 << kChannelBits ), "every channel must fit in the channel field" );
            static constexpr float64 kRttSmoothing  = 0.1;
            static constexpr float64 kLossSmoothing = 0.1;
            static constexpr float64 kStatsInterval = 0.25;

            /** @brief 통계 값 하나(`float32`)를 표본 쪽으로 @p factor 만큼 옮깁니다. 계산은 `float64`(시각과 같은 정밀도)로 합니다. */
            static float32 smoothTowards( float32 current, float64 sample, float64 factor )
            {
                const float64 value = static_cast<float64>( current );
                return static_cast<float32>( value + ( sample - value ) * factor );
            }
            static constexpr int32   kMessageCountMax = 255;
            static constexpr float64 kMinLostAge      = 0.5; ///< 확인이 이보다 늦으면 잃은 것으로 센다. 유지 간격(0.25초)보다 길게 잡는다(답을 잃으면 다음 교환의 ack 비트가 확인한다).
            static constexpr float64 kBandwidthWindow = 1.0; ///< 대역폭 통계의 윈도우(초).

            /** @brief 메시지 하나가 차지하는 비트 수입니다(채널, ID, 조각 비트, 길이, 본문). */
            static int32 computeMessageBits( NetChannelType channel, int32 size )
            {
                const int32 idBits   = channel == NetChannelType::Unreliable ? 0 : 16;
                const int32 moreBits = channel == NetChannelType::ReliableOrdered ? 1 : 0;
                return kChannelBits + idBits + moreBits + NetConnection::kMessageSizeBits + size * 8;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    const utf8* toString( NetDisconnectReason reason )
    {
        switch ( reason )
        {
            case NetDisconnectReason::None:
                return "None";
            case NetDisconnectReason::Requested:
                return "Requested";
            case NetDisconnectReason::Remote:
                return "Remote";
            case NetDisconnectReason::Timeout:
                return "Timeout";
            case NetDisconnectReason::ServerFull:
                return "ServerFull";
            case NetDisconnectReason::Rejected:
                return "Rejected";
            case NetDisconnectReason::VersionMismatch:
                return "VersionMismatch";
            case NetDisconnectReason::SecurityMismatch:
                return "SecurityMismatch";
            case NetDisconnectReason::AuthenticationFailed:
                return "AuthenticationFailed";
            case NetDisconnectReason::ReceiveOverflow:
                return "ReceiveOverflow";
        }
        return "Unknown";
    }

    NetConnection::NetConnection()
        : _sentPackets{}
        , _receivedPackets{}
        , _outgoingReliable{}
        , _incomingReliable{}
        , _arrIncoming{}
        , _listOutgoingSequenced{}
        , _listOutgoingUnreliable{}
        , _listParsedScratch{}
        , _listSequencedReceive{}
        , _listReliableAssembly{}
        , _listSpareBuffer{}
        , _listDeliveryNotice{}
        , _stats{}
        , _lastStatsTime{ 0.0 }
        , _lastAckRequestTime{ -1.0e9 }
        , _orderedSentCount{ 0 }
        , _orderedReceivedCount{ 0 }
        , _flowToken{ 0 }
        , _bFlowIdentity{ SW_FALSE }
        , _bFlowLocalServer{ SW_FALSE }
        , _nextPacketSequence{ 0 }
        , _nextReliableSendID{ 0 }
        , _oldestUnackedReliableID{ 0 }
        , _nextReliableReceiveID{ 0 }
        , _nextSequencedSendID{ 0 }
        , _bAckPending{ SW_FALSE }
        , _bDiscardingAssembly{ SW_FALSE }
        , _bUnreliableFirst{ SW_FALSE }
        , _bReceiveOverflowed{ SW_FALSE }
    {
    }

    void NetConnection::reset()
    {
        _sentPackets.reset();
        _receivedPackets.reset();
        _outgoingReliable.reset();
        _incomingReliable.reset();
        for ( IncomingQueue& queue : _arrIncoming )
        {
            queue._listMessage.clear();
            queue._byteCount = 0;
        }
        _listOutgoingSequenced.clear();
        _listOutgoingUnreliable.clear();
        _listSequencedReceive.clear();
        _listReliableAssembly.clear(); // 끊긴 연결의 반쪽 메시지가 같은 슬롯의 새 연결 메시지 앞에 붙지 않게 한다.
        _listDeliveryNotice.clear();   // 끊긴 연결의 판정은 내지 않는다 — 처리기는 연결 닫힘(`onConnectionClosed`)으로 그 연결의 상태를 지운다.
        _stats                   = NetConnectionStats{};
        _lastStatsTime           = 0.0;
        _lastAckRequestTime      = -1.0e9;
        _orderedSentCount        = 0;
        _orderedReceivedCount    = 0;
        _flowToken               = 0; // 끊긴 연결의 신원으로 새 연결의 흐름을 찍지 않는다
        _bFlowIdentity           = SW_FALSE;
        _bFlowLocalServer        = SW_FALSE;
        _nextPacketSequence      = 0;
        _nextReliableSendID      = 0;
        _oldestUnackedReliableID = 0;
        _nextReliableReceiveID   = 0;
        _nextSequencedSendID     = 0;
        _bAckPending             = SW_FALSE;
        _bDiscardingAssembly     = SW_FALSE;
        _bUnreliableFirst        = SW_FALSE;
        _bReceiveOverflowed      = SW_FALSE;
    }

    int32 NetConnection::getPendingReliableCount() const
    {
        return NetSequence::computeDifference( _nextReliableSendID, _oldestUnackedReliableID );
    }

    bool NetConnection::sendMessage( NetChannelType channel, const uint8* pData, int32 size )
    {
        const bool bInvalidMessage = size < 0 || ( size > 0 && pData == nullptr ) || channel == NetChannelType::Count;
        if ( bInvalidMessage )
            return false;
        if ( size > getMaxMessageSize( channel ) )
        {
            // 부른 쪽의 실수다(키트 예산이나 상한 상수). 조용히 버리면 받는 쪽은 영영 모른다.
            SW_LOG_ERROR_ID( "Net.MessageTooLarge", "NetConnection: a %# byte message exceeds the %# byte limit of channel %# - not sent", size, getMaxMessageSize( channel ),
                             static_cast<int32>( channel ) );
            return false;
        }
        switch ( channel )
        {
            case NetChannelType::ReliableOrdered:
            case NetChannelType::ReliableUnordered:
            {
                return queueReliable( channel, pData, size );
            }
            case NetChannelType::UnreliableSequenced:
            {
                // 흐름은 메시지 첫 바이트(종류)마다 따로 둔다. 같은 종류의 아직 나가지 않은 옛 메시지만 새 메시지로 바꾸고, 다른 종류는 서로 지우지 않는다.
                // 바꿀 때는 옛 버퍼에 덮어 써서, 틱마다 같은 종류를 보내는 흐름이 메시지마다 할당하지 않게 한다.
                const bool bEmpty = size == 0;
                for ( vector<uint8>& pending : _listOutgoingSequenced )
                {
                    const bool bSameStream = pending.empty() == bEmpty && ( bEmpty || pending[0] == pData[0] );
                    if ( bSameStream )
                    {
                        pending.assign( pData, pData + size );
                        return true;
                    }
                }
                _listOutgoingSequenced.emplace_back( pData, pData + size );
                return true;
            }
            case NetChannelType::Unreliable:
            {
                QueuedUnreliable& queued = _listOutgoingUnreliable.emplace_back();
                queued._buffer           = acquireBuffer();
                queued._buffer.assign( pData, pData + size );
                return true;
            }
            case NetChannelType::Count:
            {
                break;
            }
        }
        return false;
    }

    bool NetConnection::sendNotifiedMessage( const uint8* pData, int32 size, uint32 tag )
    {
        const bool bInvalidMessage = pData == nullptr || size < 1 || size > kMaxSingleMessageSize;
        if ( bInvalidMessage )
        {
            // 부른 쪽의 실수다. 판정이 나지 않는 메시지를 받아 두면 기다리는 쪽이 영영 모른다.
            SW_LOG_ERROR( "NetConnection: a notified message must be 1..%# bytes (got %#) - not sent", kMaxSingleMessageSize, size );
            return false;
        }
        QueuedUnreliable& queued = _listOutgoingUnreliable.emplace_back();
        queued._buffer           = acquireBuffer();
        queued._buffer.assign( pData, pData + size );
        queued._noticeTag = tag;
        queued._bNotified = SW_TRUE;
        return true;
    }

    void NetConnection::drainDeliveryNotices( vector<NetDeliveryNotice>& inoutListNotice )
    {
        inoutListNotice.insert( inoutListNotice.end(), _listDeliveryNotice.begin(), _listDeliveryNotice.end() );
        _listDeliveryNotice.clear();
    }

    void NetConnection::settleNotices( SentPacket& sent, NetDeliveryResult result )
    {
        if ( sent._bNoticeSettled == SW_TRUE )
            return;
        sent._bNoticeSettled = SW_TRUE;
        for ( int32 index = 0; index < sent._notifiedCount; ++index )
        {
            _listDeliveryNotice.push_back( NetDeliveryNotice{ sent._arrNotifiedTag[index], -1, sent._arrNotifiedKind[index], result } );
        }
    }

    int32 NetConnection::getMaxMessageSize( NetChannelType channel )
    {
        return channel == NetChannelType::ReliableOrdered ? kMaxReliableMessageSize : kMaxSingleMessageSize;
    }

    bool NetConnection::queueReliable( NetChannelType channel, const uint8* pData, int32 size )
    {
        // 조각 하나가 신뢰 ID 하나다. 재전송과 확인도 조각마다 한다. 창이 메시지 전체를 받을 수 없으면 조각을 하나도 넣지 않는다.
        // 신뢰 순서 없음은 조각나지 않는다(`getMaxMessageSize` 가 이미 1KB 로 막았다). 조각은 연속으로 ID를 받으므로 사이에 다른 메시지가 끼지 않는다.
        const int32 fragmentCount = MathUtil::max( 1, ( size + kMaxSingleMessageSize - 1 ) / kMaxSingleMessageSize );
        if ( getPendingReliableCount() + fragmentCount > kReliableWindow - 1 )
            return false; // 상대가 확인하지 않아 막힌 상태다(게임이 보내는 속도를 줄인다).
        for ( int32 fragment = 0; fragment < fragmentCount; ++fragment )
        {
            const int32       offset   = fragment * kMaxSingleMessageSize;
            const int32       length   = MathUtil::min( kMaxSingleMessageSize, size - offset );
            OutgoingReliable* pMessage = _outgoingReliable.insert( _nextReliableSendID );
            pMessage->_buffer.assign( pData + offset, pData + offset + length );
            pMessage->_lastSentTime = -1.0;
            pMessage->_bMore        = fragment + 1 < fragmentCount ? SW_TRUE : SW_FALSE;
            pMessage->_channel      = channel;
            ++_nextReliableSendID;
        }
        if ( channel == NetChannelType::ReliableOrdered )
        {
            recordFlow( channel, _orderedSentCount, pData, static_cast<size_t>( size ), false );
            ++_orderedSentCount;
        }
        return true;
    }

    void NetConnection::setFlowIdentity( uint64 connectionToken, bool bLocalIsServer )
    {
        _flowToken        = connectionToken;
        _bFlowIdentity    = SW_TRUE;
        _bFlowLocalServer = bLocalIsServer ? SW_TRUE : SW_FALSE;
    }

    void NetConnection::recordFlow( NetChannelType channel, uint64 wireIndex, const uint8* pData, size_t size, bool bReceive ) const
    {
#if SW_NET_FLOW_LOG_ENABLED
        if ( _bFlowIdentity == SW_FALSE )
            return;
        // 보낸 쪽이 서버인가 — 보낼 때는 이쪽, 받을 때는 상대다.
        const bool  bFromServer = ( _bFlowLocalServer != SW_FALSE ) != bReceive;
        const uint8 kind        = ( pData != nullptr && size > 0 ) ? pData[0] : static_cast<uint8>( 0 );
        NetFlowLog::record( NetFlowLog::makeFlowID( _flowToken, bFromServer, channel, wireIndex ), kind, bReceive );
#else
        (void)channel;
        (void)wireIndex;
        (void)pData;
        (void)size;
        (void)bReceive;
#endif
    }

    bool NetConnection::receiveMessage( NetChannelType channel, vector<uint8>& outBuffer )
    {
        const int32 channelIndex       = static_cast<int32>( channel );
        const bool  bNoIncomingMessage = channelIndex < 0 || channelIndex >= static_cast<int32>( NetChannelType::Count ) || _arrIncoming[channelIndex]._listMessage.empty();
        if ( bNoIncomingMessage )
            return false;
        // 바꿔 넣어 부른 쪽 버퍼의 옛 용량을 다시 쓴다 — 받는 쪽이 같은 버퍼로 계속 꺼내면 메시지마다 할당하지 않는다.
        IncomingQueue& queue = _arrIncoming[channelIndex];
        outBuffer.swap( queue._listMessage.front() );
        queue._byteCount -= static_cast<int32>( outBuffer.size() ) + kIncomingMessageOverheadBytes;
        recycleBuffer( queue._listMessage.front() );
        queue._listMessage.pop_front();
        if ( channel == NetChannelType::ReliableOrdered )
        {
            recordFlow( channel, _orderedReceivedCount, outBuffer.data(), outBuffer.size(), true );
            ++_orderedReceivedCount;
        }
        return true;
    }

    bool NetConnection::receiveMessageAppend( NetChannelType channel, vector<uint8>& inoutBytes )
    {
        const int32 channelIndex       = static_cast<int32>( channel );
        const bool  bNoIncomingMessage = channelIndex < 0 || channelIndex >= static_cast<int32>( NetChannelType::Count ) || _arrIncoming[channelIndex]._listMessage.empty();
        if ( bNoIncomingMessage )
            return false;
        IncomingQueue& queue   = _arrIncoming[channelIndex];
        vector<uint8>& message = queue._listMessage.front();
        inoutBytes.insert( inoutBytes.end(), message.begin(), message.end() );
        if ( channel == NetChannelType::ReliableOrdered )
        {
            recordFlow( channel, _orderedReceivedCount, message.data(), message.size(), true );
            ++_orderedReceivedCount;
        }
        popIncoming( queue );
        return true;
    }

    void NetConnection::popIncoming( IncomingQueue& queue )
    {
        vector<uint8>& message = queue._listMessage.front();
        queue._byteCount -= static_cast<int32>( message.size() ) + kIncomingMessageOverheadBytes;
        recycleBuffer( message );
        queue._listMessage.pop_front();
    }

    void NetConnection::pushIncoming( NetChannelType channel, vector<uint8>& buffer )
    {
        IncomingQueue& queue = _arrIncoming[static_cast<int32>( channel )];
        const int32    bytes = static_cast<int32>( buffer.size() ) + kIncomingMessageOverheadBytes;
        if ( queue._byteCount + bytes > kMaxPendingReceiveBytes )
        {
            // 게임이 꺼내는 속도보다 상대가 빨리 넣는다(느린 틱이거나 홍수). UDP 는 상대를 멈출 수 없으므로 엔드포인트가 끊는다.
            if ( _bReceiveOverflowed == SW_FALSE )
                SW_LOG_WARNING_ID( "Net.ReceiveOverflow", "NetConnection: the receive queue of channel %# exceeds %# bytes - dropping the connection", static_cast<int32>( channel ), kMaxPendingReceiveBytes );
            _bReceiveOverflowed = SW_TRUE;
            recycleBuffer( buffer );
            return;
        }
        queue._byteCount += bytes;
        queue._listMessage.push_back( std::move( buffer ) );
        buffer = vector<uint8>{};
    }

    vector<uint8> NetConnection::acquireBuffer()
    {
        if ( _listSpareBuffer.empty() )
            return vector<uint8>{};
        vector<uint8> buffer = std::move( _listSpareBuffer.back() );
        _listSpareBuffer.pop_back();
        return buffer;
    }

    void NetConnection::recycleBuffer( vector<uint8>& buffer )
    {
        const size_t capacity    = buffer.capacity();
        const bool   bReusable   = capacity > 0 && capacity <= static_cast<size_t>( kMaxSingleMessageSize );
        const bool   bRoomInList = _listSpareBuffer.size() < static_cast<size_t>( kMaxSpareBuffers );
        if ( bReusable && bRoomInList )
        {
            buffer.clear();
            _listSpareBuffer.push_back( std::move( buffer ) );
        }
        buffer = vector<uint8>{};
    }

    float64 NetConnection::computeResendDelay() const
    {
        return MathUtil::max( 0.1, static_cast<float64>( _stats._rtt ) + kResendMargin );
    }

    bool NetConnection::isReliableDue( const OutgoingReliable& message, float64 time, float64 resendDelay ) const
    {
        return message._lastSentTime < 0.0 || message._bResendNow != SW_FALSE || time - message._lastSentTime >= resendDelay;
    }

    bool NetConnection::hasDataToSend( float64 time ) const
    {
        const bool bHasImmediateSendData = _bAckPending || _listOutgoingSequenced.empty() == false || _listOutgoingUnreliable.empty() == false;
        if ( bHasImmediateSendData )
            return true;
        const float64 resendDelay = computeResendDelay();
        for ( uint16 messageID = _oldestUnackedReliableID; messageID != _nextReliableSendID; ++messageID )
        {
            const OutgoingReliable* pMessage = _outgoingReliable.find( messageID );
            if ( pMessage != nullptr && isReliableDue( *pMessage, time, resendDelay ) )
                return true;
        }
        return false;
    }

    uint32 NetConnection::computeAckBits( uint16 ack ) const
    {
        uint32 ackBits = 0;
        for ( int32 bitIndex = 0; bitIndex < 32; ++bitIndex )
        {
            if ( _receivedPackets.exists( static_cast<uint16>( ack - 1 - bitIndex ) ) )
                ackBits |= 1u << bitIndex;
        }
        return ackBits;
    }

    bool NetConnection::PacketBody::tryWriteMessage( NetChannelType channel, uint16 messageID, const vector<uint8>& buffer, bool bMore )
    {
        const int32 bits = 1 + NetConnectionInternal::computeMessageBits( channel, static_cast<int32>( buffer.size() ) );
        if ( _usedBits + bits > _budgetBits || _written >= NetConnectionInternal::kMessageCountMax )
            return false;
        BitWriter& writer = *_pWriter;
        writer.writeBool( true );
        writer.writeBits( static_cast<uint32>( channel ), NetConnectionInternal::kChannelBits );
        if ( channel != NetChannelType::Unreliable )
            writer.writeBits( messageID, 16 );
        if ( channel == NetChannelType::ReliableOrdered )
            writer.writeBool( bMore );
        writer.writeBits( static_cast<uint32>( buffer.size() ), kMessageSizeBits );
        if ( buffer.empty() == false )
            writer.writeBytes( buffer.data(), static_cast<int32>( buffer.size() ) );
        _usedBits += bits;
        ++_written;
        return true;
    }

    void NetConnection::expireQueuedUnreliable( float64 time, float64 maxAgeSeconds )
    {
        // 포화가 풀릴 때 옛 비신뢰가 몰려 나가지 않게 한다.
        for ( auto queuedIt = _listOutgoingUnreliable.rbegin(); queuedIt != _listOutgoingUnreliable.rend() && queuedIt->_queuedTime < 0.0; ++queuedIt )
        {
            queuedIt->_queuedTime = time; // 새 메시지는 뒤에 붙으므로 뒤에서부터 아직 적지 않은 것만 본다
        }
        while ( true )
        {
            const bool bOldestUnreliableExpired = maxAgeSeconds > 0.0 && _listOutgoingUnreliable.empty() == false && time - _listOutgoingUnreliable.front()._queuedTime > maxAgeSeconds;
            if ( bOldestUnreliableExpired == false )
                break;
            QueuedUnreliable& expired = _listOutgoingUnreliable.front();
            if ( expired._bNotified == SW_TRUE )
                _listDeliveryNotice.push_back( NetDeliveryNotice{ expired._noticeTag, -1, expired._buffer[0], NetDeliveryResult::Lost } ); // 싣지도 못했다
            recycleBuffer( expired._buffer );
            _listOutgoingUnreliable.pop_front();
            ++_stats._expiredUnreliableCount;
        }
    }

    void NetConnection::writeReliableMessages( PacketBody& body )
    {
        SentPacket& sent = *body._pSent;
        for ( uint16 messageID = _oldestUnackedReliableID; messageID != _nextReliableSendID; ++messageID )
        {
            OutgoingReliable* pMessage = _outgoingReliable.find( messageID );
            if ( pMessage == nullptr || isReliableDue( *pMessage, body._time, body._resendDelay ) == false )
                continue;
            if ( sent._reliableCount >= kMaxReliablePerPacket )
                break;
            if ( body.tryWriteMessage( pMessage->_channel, messageID, pMessage->_buffer, pMessage->_bMore != SW_FALSE ) == false )
                break;
            if ( pMessage->_lastSentTime >= 0.0 )
                ++_stats._resentMessageCount;
            else if ( pMessage->_channel == NetChannelType::ReliableUnordered )
                recordFlow( pMessage->_channel, messageID, pMessage->_buffer.data(), pMessage->_buffer.size(), false ); // 재전송은 같은 메시지라 처음만 찍는다
            pMessage->_lastSentTime                    = body._time;
            pMessage->_bResendNow                      = SW_FALSE;
            sent._arrReliableID[sent._reliableCount++] = messageID;
        }
    }

    void NetConnection::writeSequencedMessages( PacketBody& body )
    {
        // 쌓을 때 같은 종류의 옛것은 이미 새것으로 바뀌었다.
        size_t sequencedWritten = 0;
        while ( sequencedWritten < _listOutgoingSequenced.size() &&
                body.tryWriteMessage( NetChannelType::UnreliableSequenced, _nextSequencedSendID, _listOutgoingSequenced[sequencedWritten], false ) )
        {
            const vector<uint8>& written = _listOutgoingSequenced[sequencedWritten];
            recordFlow( NetChannelType::UnreliableSequenced, NetConnectionInternal::makeWirePosition( body._sequence, body._written - 1 ), written.data(), written.size(), false );
            ++_nextSequencedSendID;
            ++sequencedWritten;
        }
        _listOutgoingSequenced.erase( _listOutgoingSequenced.begin(), _listOutgoingSequenced.begin() + static_cast<std::ptrdiff_t>( sequencedWritten ) );
    }

    void NetConnection::writeUnreliableMessages( PacketBody& body )
    {
        SentPacket& sent = *body._pSent;
        while ( _listOutgoingUnreliable.empty() == false )
        {
            QueuedUnreliable& front           = _listOutgoingUnreliable.front();
            const bool        bNoticeSlotFull = front._bNotified == SW_TRUE && sent._notifiedCount >= kMaxNotifiedPerPacket;
            if ( bNoticeSlotFull )
                break; // 순서를 지키려고 뒤의 메시지도 다음 패킷으로 미룬다
            if ( body.tryWriteMessage( NetChannelType::Unreliable, 0, front._buffer, false ) == false )
                break;
            recordFlow( NetChannelType::Unreliable, NetConnectionInternal::makeWirePosition( body._sequence, body._written - 1 ), front._buffer.data(), front._buffer.size(),
                        false );
            if ( front._bNotified == SW_TRUE )
            {
                sent._arrNotifiedTag[sent._notifiedCount]  = front._noticeTag;
                sent._arrNotifiedKind[sent._notifiedCount] = front._buffer[0];
                ++sent._notifiedCount;
            }
            recycleBuffer( front._buffer );
            _listOutgoingUnreliable.pop_front();
        }
    }

    void NetConnection::writePacket( float64 time, BitWriter& writer, int32 maxBytes, float64 ackRequestInterval, float64 maxUnreliableAgeSeconds )
    {
        expireQueuedUnreliable( time, maxUnreliableAgeSeconds );
        const uint16 sequence = _nextPacketSequence++;
        const uint16 ack      = _receivedPackets.hasNewest() ? _receivedPackets.getNewest() : static_cast<uint16>( 0xFFFF );
        writer.writeBits( sequence, 16 );
        writer.writeBits( ack, 16 );
        writer.writeBits( _receivedPackets.hasNewest() ? computeAckBits( ack ) : 0u, 32 );
        const bool bReplyOnly = _bAckPending != SW_FALSE;
        _bAckPending          = SW_FALSE;

        // 이 슬롯을 쓰던 옛 패킷(창 하나 앞)이 확인도 손실 판정도 없이 밀려나면 그 전달 통지는 잃은 것이다.
        SentPacket* pEvicted = _sentPackets.find( static_cast<uint16>( sequence - kPacketWindow ) );
        if ( pEvicted != nullptr )
            settleNotices( *pEvicted, NetDeliveryResult::Lost );
        SentPacket* pSent      = _sentPackets.insert( sequence );
        pSent->_time           = time;
        pSent->_reliableCount  = 0;
        pSent->_notifiedCount  = 0;
        pSent->_bNoticeSettled = SW_FALSE;

        // 메시지 수를 먼저 쓰지 않고, 메시지마다 "더 있다" 1비트를 앞에 둔다. 몇 개가 들어갈지는 쓰면서 정한다.
        PacketBody body;
        body._pWriter     = &writer;
        body._pSent       = pSent;
        body._time        = time;
        body._resendDelay = computeResendDelay();
        body._budgetBits  = maxBytes * 8 - writer.getBitCount() - 8;
        body._sequence    = sequence;
        // 조각(1KB)은 패킷을 거의 채운다. 앞 패킷이 순서만과 비신뢰 메시지를 남겼으면 이번에는 그쪽부터 싣고 번갈아 간다.
        // 그러면 큰 신뢰 전송 동안 스냅샷이 굶지 않고, 비신뢰가 많아도 신뢰가 굶지 않는다. 한 번 앞섰으면 다음은 신뢰부터 싣는다.
        const bool bUnreliableFirst = _bUnreliableFirst != SW_FALSE;
        if ( bUnreliableFirst == false )
            writeReliableMessages( body );
        writeSequencedMessages( body );
        writeUnreliableMessages( body );
        if ( bUnreliableFirst )
            writeReliableMessages( body );
        const bool bUnreliableLeft = _listOutgoingSequenced.empty() == false || _listOutgoingUnreliable.empty() == false;
        _bUnreliableFirst          = bUnreliableFirst == false && bUnreliableLeft ? SW_TRUE : SW_FALSE;
        writer.writeBool( false );
        // 확인만 담은 답은 확인을 요청하지 않는다(답에 답이 꼬리를 물지 않게). 다만 마지막 요청에서 유지 간격이 지났으면 답이라도 요청한다.
        const bool bAckRequested = body._written > 0 || bReplyOnly == false || time - _lastAckRequestTime >= ackRequestInterval;
        if ( bAckRequested )
            _lastAckRequestTime = time;
        writer.writeBool( bAckRequested );
        pSent->_bAckRequested = bAckRequested ? SW_TRUE : SW_FALSE;
        pSent->_byteCount     = writer.getByteCount();
        ++_stats._sentPacketCount;
        updateStats( time );
    }

    bool NetConnection::parseMessages( BitReader& reader )
    {
        // 메시지 버퍼는 다시 쓸 줄에서 받는다. 넘기지 않은 것(중복 · 깨진 패킷)은 다음 패킷이 줄로 돌린다.
        for ( ParsedMessage& parsed : _listParsedScratch )
        {
            recycleBuffer( parsed._buffer );
        }
        _listParsedScratch.clear();
        while ( reader.readBool() )
        {
            ParsedMessage& message = _listParsedScratch.emplace_back();
            message._buffer        = acquireBuffer();
            message._channel       = static_cast<NetChannelType>( reader.readBits( NetConnectionInternal::kChannelBits ) );
            if ( message._channel >= NetChannelType::Count )
                return false; // 채널 슬롯 넷을 모두 쓰면 닿지 않는 분기다. 채널이 늘어도 깨진 값을 거르도록 남겨 둔다.
            message._id      = message._channel != NetChannelType::Unreliable ? static_cast<uint16>( reader.readBits( 16 ) ) : static_cast<uint16>( 0 );
            message._bMore   = message._channel == NetChannelType::ReliableOrdered && reader.readBool() ? SW_TRUE : SW_FALSE;
            const int32 size = static_cast<int32>( reader.readBits( kMessageSizeBits ) );
            if ( size > kMaxSingleMessageSize )
                return false;
            message._buffer.resize( static_cast<size_t>( size ) );
            if ( size > 0 && reader.readBytes( message._buffer.data(), size ) == false )
                return false;
            if ( reader.hasOverflowed() )
                return false;
        }
        return true;
    }

    void NetConnection::dispatchParsedMessage( ParsedMessage& message, uint16 sequence, uint32 position )
    {
        switch ( message._channel )
        {
            case NetChannelType::ReliableOrdered:
            case NetChannelType::ReliableUnordered:
            {
                // 이미 넘긴 것과 창 밖의 것은 버린다. 신뢰 순서는 슬롯에 두었다가 순서대로 넘기고(`deliverReliable`), 신뢰 순서 없음은 앞의 신뢰 메시지를
                // 기다리지 않고 받는 대로 넘기되 슬롯에 "넘겼다" 표시만 남겨 순서 커서가 지나가게 하고 늦게 온 중복을 거른다.
                const int32 ahead                           = NetSequence::computeDifference( message._id, _nextReliableReceiveID );
                const bool  bReliableOutOfWindowOrDuplicate = ahead < 0 || ahead >= kReliableWindow || _incomingReliable.exists( message._id );
                if ( bReliableOutOfWindowOrDuplicate )
                    return;
                IncomingReliable* pIncoming = _incomingReliable.insert( message._id );
                if ( pIncoming == nullptr )
                    return;
                if ( message._channel == NetChannelType::ReliableUnordered )
                {
                    recordFlow( message._channel, message._id, message._buffer.data(), message._buffer.size(), true );
                    pIncoming->_bDelivered = SW_TRUE;
                    pushIncoming( NetChannelType::ReliableUnordered, message._buffer );
                    return;
                }
                pIncoming->_buffer.swap( message._buffer ); // 슬롯의 옛 용량은 받은 메시지 쪽으로 가 다음 패킷이 재사용 목록에 돌려 놓는다
                pIncoming->_bMore = message._bMore;
                return;
            }
            case NetChannelType::UnreliableSequenced:
            {
                // 번호는 연결 하나에서 증가하기만 하므로 종류마다 마지막 번호와 비교한다. 늦게 온 같은 종류의 옛 메시지만 버린다.
                const uint8      kind    = message._buffer.empty() ? static_cast<uint8>( 0 ) : message._buffer[0];
                SequencedStream* pStream = nullptr;
                for ( SequencedStream& stream : _listSequencedReceive )
                {
                    if ( stream._kind == kind )
                        pStream = &stream;
                }
                if ( pStream != nullptr && NetSequence::isGreater( message._id, pStream->_lastID ) == false )
                    return; // 늦게 온 옛 메시지.
                if ( pStream == nullptr )
                {
                    _listSequencedReceive.push_back( SequencedStream{ message._id, kind } );
                    pStream = &_listSequencedReceive.back();
                }
                pStream->_lastID = message._id;
                recordFlow( message._channel, NetConnectionInternal::makeWirePosition( sequence, static_cast<int32>( position ) ), message._buffer.data(), message._buffer.size(), true );
                pushIncoming( NetChannelType::UnreliableSequenced, message._buffer );
                return;
            }
            case NetChannelType::Unreliable:
            {
                recordFlow( message._channel, NetConnectionInternal::makeWirePosition( sequence, static_cast<int32>( position ) ), message._buffer.data(), message._buffer.size(), true );
                pushIncoming( NetChannelType::Unreliable, message._buffer );
                return;
            }
            case NetChannelType::Count:
            {
                return;
            }
        }
    }

    bool NetConnection::readPacket( float64 time, BitReader& reader )
    {
        const uint16 sequence = static_cast<uint16>( reader.readBits( 16 ) );
        const uint16 ack      = static_cast<uint16>( reader.readBits( 16 ) );
        const uint32 ackBits  = reader.readBits( 32 );
        if ( reader.hasOverflowed() )
            return false;
        if ( _receivedPackets.exists( sequence ) || _receivedPackets.isStale( sequence ) )
            return false; // 중복이거나 너무 오래된 패킷.

        // 메시지를 먼저 모두 읽어서 깨진 패킷이면 아무것도 넣지 않는다. 시퀀스도 본문을 다 읽은 뒤에 기록한다.
        // 깨진 패킷을 확인해 버리면 보낸 쪽이 그 안의 신뢰 메시지를 전달된 것으로 알고 지운다.
        if ( parseMessages( reader ) == false )
            return false;
        const bool bAckRequested = reader.readBool();
        if ( reader.hasOverflowed() )
            return false;

        ReceivedPacket* pReceived = _receivedPackets.insert( sequence );
        pReceived->_time          = time;
        pReceived->_byteCount     = ( reader.getBitPosition() + 7 ) / 8;
        ++_stats._receivedPacketCount;
        if ( bAckRequested )
            _bAckPending = SW_TRUE;
        processAcks( time, ack, ackBits );

        for ( size_t position = 0; position < _listParsedScratch.size(); ++position )
        {
            dispatchParsedMessage( _listParsedScratch[position], sequence, static_cast<uint32>( position ) );
        }
        for ( IncomingReliable* pIncoming = _incomingReliable.find( _nextReliableReceiveID ); pIncoming != nullptr;
              pIncoming                   = _incomingReliable.find( _nextReliableReceiveID ) )
        {
            deliverReliable( *pIncoming );
            _incomingReliable.remove( _nextReliableReceiveID );
            ++_nextReliableReceiveID;
        }
        updateStats( time );
        return true;
    }

    void NetConnection::deliverReliable( IncomingReliable& incoming )
    {
        if ( incoming._bDelivered != SW_FALSE )
        {
            // 신뢰 순서 없음은 받을 때 이미 넘겼으므로 커서만 지나간다. 순서 메시지의 조각 사이에 끼어들었다면 상대가 규약을 어긴 것이다(보내는 쪽은 조각을 연속으로 둔다).
            if ( _listReliableAssembly.empty() == false && _bDiscardingAssembly == SW_FALSE )
            {
                SW_LOG_ERROR( "NetConnection: an unordered reliable message sits between the fragments of an ordered one - the peer breaks the protocol, dropping the ordered message" );
                _listReliableAssembly.clear();
                _bDiscardingAssembly = SW_TRUE;
            }
            return;
        }
        const bool bLast = incoming._bMore == SW_FALSE;
        // 조각이 하나뿐인 경우(대부분)는 모으지 않고 그대로 넘긴다.
        const bool bDeliverSingleFragment = bLast && _listReliableAssembly.empty() && _bDiscardingAssembly == SW_FALSE;
        if ( bDeliverSingleFragment )
        {
            pushIncoming( NetChannelType::ReliableOrdered, incoming._buffer );
            return;
        }
        if ( _bDiscardingAssembly == SW_FALSE )
        {
            if ( _listReliableAssembly.size() + incoming._buffer.size() > static_cast<size_t>( kMaxReliableMessageSize ) )
            {
                // 보내는 쪽은 상한을 넘겨 나누지 않으므로, 상한을 넘으면 상대가 규약을 어긴 것이다. 이 메시지는 마지막 조각까지 버려 받는 쪽 메모리를 지킨다.
                SW_LOG_ERROR( "NetConnection: a reliable message grew past %# bytes while reassembling - the peer breaks the protocol, dropping the message",
                              kMaxReliableMessageSize );
                _listReliableAssembly.clear();
                _bDiscardingAssembly = SW_TRUE;
            }
            else
            {
                _listReliableAssembly.insert( _listReliableAssembly.end(), incoming._buffer.begin(), incoming._buffer.end() );
            }
        }
        if ( bLast == false )
            return;
        if ( _bDiscardingAssembly == SW_FALSE )
            pushIncoming( NetChannelType::ReliableOrdered, _listReliableAssembly );
        _listReliableAssembly.clear(); // 옮긴 뒤의 빈 버퍼다. 다음 메시지가 새로 할당한다.
        _bDiscardingAssembly = SW_FALSE;
    }

    void NetConnection::processAcks( float64 time, uint16 ack, uint32 ackBits )
    {
        for ( int32 bitIndex = -1; bitIndex < 32; ++bitIndex )
        {
            if ( bitIndex >= 0 && ( ( ackBits >> bitIndex ) & 1u ) == 0 )
                continue;
            const uint16 sequence = static_cast<uint16>( ack - 1 - bitIndex );
            SentPacket*  pSent    = _sentPackets.find( sequence );
            if ( pSent == nullptr || pSent->_bAcked )
                continue;
            pSent->_bAcked = SW_TRUE;
            ++_stats._ackedPacketCount;
            settleNotices( *pSent, NetDeliveryResult::Delivered );
            // RTT 표본은 확인을 요청한 패킷이 가장 새 확인으로 돌아왔을 때만 잡는다. ack 비트로 늦게 확인된 패킷이나 답 패킷은 상대가 기다렸다가 보냈을 수 있기 때문이다.
            if ( bitIndex < 0 && pSent->_bAckRequested )
            {
                const float64 sample = time - pSent->_time;
                _stats._rtt          = _stats._rtt <= 0.0f ? static_cast<float32>( sample )
                                                           : NetConnectionInternal::smoothTowards( _stats._rtt, sample, NetConnectionInternal::kRttSmoothing );
            }
            for ( int32 index = 0; index < pSent->_reliableCount; ++index )
            {
                _outgoingReliable.remove( pSent->_arrReliableID[index] );
            }
        }
        detectLostPackets( ack );
        // 가장 오래된 미확인 신뢰 ID를 앞으로 옮긴다.
        while ( _oldestUnackedReliableID != _nextReliableSendID && _outgoingReliable.exists( _oldestUnackedReliableID ) == false )
        {
            ++_oldestUnackedReliableID;
        }
    }

    void NetConnection::detectLostPackets( uint16 ack )
    {
        // 확인된 가장 새 패킷보다 kFastResendGap 개를 넘게 앞서는데 확인이 없는 패킷은 잃은 것으로 본다(그 사이의 순서 바뀜은 기다린다). 확인 비트(32) 안만 본다.
        if ( _sentPackets.exists( ack ) == false || _sentPackets.find( ack )->_bAcked == SW_FALSE )
            return;
        for ( int32 offset = kFastResendGap; offset <= 33; ++offset )
        {
            SentPacket* pSent           = _sentPackets.find( static_cast<uint16>( ack - offset ) );
            const bool  bSkipSentPacket = pSent == nullptr || pSent->_bAcked != SW_FALSE || pSent->_bLossDetected != SW_FALSE;
            if ( bSkipSentPacket )
                continue;
            pSent->_bLossDetected = SW_TRUE;
            settleNotices( *pSent, NetDeliveryResult::Lost );
            for ( int32 index = 0; index < pSent->_reliableCount; ++index )
            {
                OutgoingReliable* pMessage = _outgoingReliable.find( pSent->_arrReliableID[index] );
                if ( pMessage != nullptr && pMessage->_lastSentTime >= 0.0 )
                    pMessage->_bResendNow = SW_TRUE;
            }
        }
    }

    void NetConnection::updateStats( float64 time )
    {
        if ( time - _lastStatsTime < NetConnectionInternal::kStatsInterval )
            return;
        _lastStatsTime = time;
        // 손실은 RTT 두 배를 넘게 확인이 없는 패킷의 비율로 센다(최근 64패킷).
        const float64 lostAge   = MathUtil::max( NetConnectionInternal::kMinLostAge, static_cast<float64>( _stats._rtt ) * 2.0 );
        int32         lostCount = 0;
        int32         sentCount = 0;
        for ( int32 offset = 1; offset <= 64; ++offset )
        {
            const SentPacket* pSent                         = _sentPackets.find( static_cast<uint16>( _nextPacketSequence - offset ) );
            const bool        bPacketCountedAsLostCandidate = pSent != nullptr && pSent->_bAckRequested && time - pSent->_time >= lostAge;
            if ( bPacketCountedAsLostCandidate )
            {
                ++sentCount;
                lostCount += pSent->_bAcked ? 0 : 1;
            }
        }
        // 대역폭은 최근 1초의 패킷 전부로 센다(기록 링 크기만큼, 초당 256패킷까지). 패킷 수로 자르면 보내기 간격이 짧을 때 1초의 일부만 세게 된다.
        int32 sentBytes = 0;
        int32 recvBytes = 0;
        for ( int32 offset = 1; offset <= _sentPackets.getSize(); ++offset )
        {
            const SentPacket* pSent = _sentPackets.find( static_cast<uint16>( _nextPacketSequence - offset ) );
            if ( pSent != nullptr && time - pSent->_time < NetConnectionInternal::kBandwidthWindow )
                sentBytes += pSent->_byteCount;
        }
        for ( int32 offset = 0; offset < _receivedPackets.getSize() && _receivedPackets.hasNewest(); ++offset )
        {
            const ReceivedPacket* pReceived = _receivedPackets.find( static_cast<uint16>( _receivedPackets.getNewest() - offset ) );
            if ( pReceived != nullptr && time - pReceived->_time < NetConnectionInternal::kBandwidthWindow )
                recvBytes += pReceived->_byteCount;
        }
        if ( sentCount > 0 )
        {
            const float64 sample = static_cast<float64>( lostCount ) / static_cast<float64>( sentCount );
            _stats._packetLoss   = NetConnectionInternal::smoothTowards( _stats._packetLoss, sample, NetConnectionInternal::kLossSmoothing * 4.0 );
        }
        _stats._sentBandwidth     = static_cast<float32>( sentBytes ) * 8.0f / 1000.0f;
        _stats._receivedBandwidth = static_cast<float32>( recvBytes ) * 8.0f / 1000.0f;
    }
} // namespace sw
