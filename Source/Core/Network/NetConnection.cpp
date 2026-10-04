#include "pch.h"

#include "Core/Network/NetConnection.h"

#include "Core/Math/MathUtil.h"
#include "Core/Network/BitStream.h"

namespace sw
{
    namespace
    {
        struct NetConnectionInternal
        {
            static constexpr int32   kChannelBits   = 2;
            static constexpr float64 kRttSmoothing  = 0.1;
            static constexpr float64 kLossSmoothing = 0.1;
            static constexpr float64 kStatsInterval = 0.25;

            /** @brief 통계 한 칸(`float32`)을 표본 쪽으로 @p factor 만큼 옮깁니다. 계산은 `float64`(시각과 같은 정밀도)로 합니다. */
            static float32 smoothTowards( float32 current, float64 sample, float64 factor )
            {
                const float64 value = static_cast<float64>( current );
                return static_cast<float32>( value + ( sample - value ) * factor );
            }
            static constexpr int32   kMessageCountMax = 255;
            static constexpr float64 kMinLostAge      = 0.5; ///< 확인이 이보다 늦으면 잃은 것으로 센다 — 유지 간격(0.25 초)보다 길게(답을 잃으면 다음 교환의 묶음이 확인한다)

            /** @brief 메시지 하나가 차지할 비트(채널 · id · 길이 · 몸)입니다. */
            static int32 computeMessageBits( NetChannelType channel, int32 size )
            {
                const int32 idBits = channel == NetChannelType::Unreliable ? 0 : 16;
                return kChannelBits + idBits + NetConnection::kMessageSizeBits + size * 8;
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
        }
        return "Unknown";
    }

    NetConnection::NetConnection()
        : _sentPackets{ 256 }
        , _receivedPackets{ 256 }
        , _outgoingReliable{ kReliableWindow }
        , _incomingReliable{ kReliableWindow }
        , _arrIncoming{}
        , _listOutgoingSequenced{}
        , _listOutgoingUnreliable{}
        , _listParsedScratch{}
        , _listSequencedReceive{}
        , _stats{}
        , _lastStatsTime{ 0.0 }
        , _lastAckRequestTime{ -1.0e9 }
        , _nextPacketSequence{ 0 }
        , _nextReliableSendId{ 0 }
        , _oldestUnackedReliableId{ 0 }
        , _nextReliableReceiveId{ 0 }
        , _nextSequencedSendId{ 0 }
        , _bAckPending{ SW_FALSE }
    {
    }

    void NetConnection::reset()
    {
        _sentPackets.reset();
        _receivedPackets.reset();
        _outgoingReliable.reset();
        _incomingReliable.reset();
        for ( deque<vector<uint8>>& listIncoming : _arrIncoming )
            listIncoming.clear();
        _listOutgoingSequenced.clear();
        _listOutgoingUnreliable.clear();
        _listSequencedReceive.clear();
        _stats                   = NetConnectionStats{};
        _lastStatsTime           = 0.0;
        _lastAckRequestTime      = -1.0e9;
        _nextPacketSequence      = 0;
        _nextReliableSendId      = 0;
        _oldestUnackedReliableId = 0;
        _nextReliableReceiveId   = 0;
        _nextSequencedSendId     = 0;
        _bAckPending             = SW_FALSE;
    }

    int32 NetConnection::getPendingReliableCount() const { return NetSequence::computeDifference( _nextReliableSendId, _oldestUnackedReliableId ); }

    bool NetConnection::sendMessage( NetChannelType channel, const uint8* pData, int32 size )
    {
        if ( size < 0 || size > kMaxMessageSize || ( size > 0 && pData == nullptr ) )
            return false;
        vector<uint8> buffer( pData, pData + size );
        switch ( channel )
        {
            case NetChannelType::ReliableOrdered:
            {
                if ( getPendingReliableCount() >= kReliableWindow - 1 )
                    return false; // 상대가 확인하지 않는다 — 막힘(게임이 보내는 속도를 줄인다)
                OutgoingReliable* pMessage = _outgoingReliable.insert( _nextReliableSendId );
                pMessage->_buffer          = std::move( buffer );
                pMessage->_lastSentTime    = -1.0;
                ++_nextReliableSendId;
                return true;
            }
            case NetChannelType::UnreliableSequenced:
            {
                // 흐름은 메시지 첫 바이트(종류)마다 — 같은 종류의 아직 안 나간 옛것만 새것으로 바꾼다(다른 종류는 서로 지우지 않는다).
                for ( vector<uint8>& pending : _listOutgoingSequenced )
                {
                    const bool bSameStream = pending.empty() == buffer.empty() && ( buffer.empty() || pending[0] == buffer[0] );
                    if ( bSameStream )
                    {
                        pending = std::move( buffer );
                        return true;
                    }
                }
                _listOutgoingSequenced.push_back( std::move( buffer ) );
                return true;
            }
            case NetChannelType::Unreliable:
            {
                _listOutgoingUnreliable.push_back( std::move( buffer ) );
                return true;
            }
            case NetChannelType::Count:
            {
                break;
            }
        }
        return false;
    }

    bool NetConnection::receiveMessage( NetChannelType channel, vector<uint8>& outBuffer )
    {
        const int32 channelIndex = static_cast<int32>( channel );
        if ( channelIndex < 0 || channelIndex >= static_cast<int32>( NetChannelType::Count ) || _arrIncoming[channelIndex].empty() )
            return false;
        outBuffer = std::move( _arrIncoming[channelIndex].front() );
        _arrIncoming[channelIndex].pop_front();
        return true;
    }

    float64 NetConnection::computeResendDelay() const { return MathUtil::max( 0.1, static_cast<float64>( _stats._rtt ) * 1.5 ); }

    bool NetConnection::hasDataToSend( float64 time ) const
    {
        if ( _bAckPending || _listOutgoingSequenced.empty() == false || _listOutgoingUnreliable.empty() == false )
            return true;
        const float64 resendDelay = computeResendDelay();
        for ( uint16 messageId = _oldestUnackedReliableId; messageId != _nextReliableSendId; ++messageId )
        {
            const OutgoingReliable* pMessage = _outgoingReliable.find( messageId );
            if ( pMessage != nullptr && ( pMessage->_lastSentTime < 0.0 || time - pMessage->_lastSentTime >= resendDelay ) )
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

    void NetConnection::writePacket( float64 time, BitWriter& writer, int32 maxBytes, float64 ackRequestInterval )
    {
        const uint16 sequence = _nextPacketSequence++;
        const uint16 ack      = _receivedPackets.hasNewest() ? _receivedPackets.getNewest() : static_cast<uint16>( 0xFFFF );
        writer.writeBits( sequence, 16 );
        writer.writeBits( ack, 16 );
        writer.writeBits( _receivedPackets.hasNewest() ? computeAckBits( ack ) : 0u, 32 );
        const bool bReplyOnly = _bAckPending != SW_FALSE;
        _bAckPending          = SW_FALSE;

        SentPacket* pSent     = _sentPackets.insert( sequence );
        pSent->_time          = time;
        pSent->_reliableCount = 0;

        // 메시지 수를 먼저 쓰지 않고, 메시지마다 "더 있다" 1 비트를 앞에 둔다(몇 개가 들어갈지 쓰면서 정한다).
        const int32   budgetBits   = maxBytes * 8 - writer.getBitCount() - 8;
        int32         usedBits     = 0;
        int32         written      = 0;
        const float64 resendDelay  = computeResendDelay();
        const auto    writeMessage = [&]( NetChannelType channel, uint16 messageId, const vector<uint8>& buffer ) -> bool
        {
            const int32 bits = 1 + NetConnectionInternal::computeMessageBits( channel, static_cast<int32>( buffer.size() ) );
            if ( usedBits + bits > budgetBits || written >= NetConnectionInternal::kMessageCountMax )
                return false;
            writer.writeBool( true );
            writer.writeBits( static_cast<uint32>( channel ), NetConnectionInternal::kChannelBits );
            if ( channel != NetChannelType::Unreliable )
                writer.writeBits( messageId, 16 );
            writer.writeBits( static_cast<uint32>( buffer.size() ), kMessageSizeBits );
            if ( buffer.empty() == false )
                writer.writeBytes( buffer.data(), static_cast<int32>( buffer.size() ) );
            usedBits += bits;
            ++written;
            return true;
        };

        // 1) 신뢰 — 오래된 것부터, 처음 보내거나 재전송 시각이 된 것.
        for ( uint16 messageId = _oldestUnackedReliableId; messageId != _nextReliableSendId; ++messageId )
        {
            OutgoingReliable* pMessage = _outgoingReliable.find( messageId );
            if ( pMessage == nullptr || ( pMessage->_lastSentTime >= 0.0 && time - pMessage->_lastSentTime < resendDelay ) )
                continue;
            if ( pSent->_reliableCount >= kMaxReliablePerPacket )
                break;
            if ( writeMessage( NetChannelType::ReliableOrdered, messageId, pMessage->_buffer ) == false )
                break;
            if ( pMessage->_lastSentTime >= 0.0 )
                ++_stats._resentMessageCount;
            pMessage->_lastSentTime                        = time;
            pSent->_arrReliableId[pSent->_reliableCount++] = messageId;
        }
        // 2) 순서만 — 종류마다 가장 새 것 하나씩(쌓을 때 옛것은 이미 바뀌었다). 들어가지 않은 것은 다음 패킷에.
        size_t sequencedWritten = 0;
        while ( sequencedWritten < _listOutgoingSequenced.size() &&
                writeMessage( NetChannelType::UnreliableSequenced, _nextSequencedSendId, _listOutgoingSequenced[sequencedWritten] ) )
        {
            ++_nextSequencedSendId;
            ++sequencedWritten;
        }
        _listOutgoingSequenced.erase( _listOutgoingSequenced.begin(), _listOutgoingSequenced.begin() + static_cast<std::ptrdiff_t>( sequencedWritten ) );
        // 3) 비신뢰 — 들어가는 만큼, 못 들어간 것은 다음 패킷으로.
        while ( _listOutgoingUnreliable.empty() == false )
        {
            if ( writeMessage( NetChannelType::Unreliable, 0, _listOutgoingUnreliable.front() ) == false )
                break;
            _listOutgoingUnreliable.pop_front();
        }
        writer.writeBool( false );
        // 확인만 담은 답은 확인을 바라지 않는다(답에 답이 꼬리를 물지 않게) — 마지막 요청에서 유지 간격이 지났으면 답이라도 바란다.
        const bool bAckRequested = written > 0 || bReplyOnly == false || time - _lastAckRequestTime >= ackRequestInterval;
        if ( bAckRequested )
            _lastAckRequestTime = time;
        writer.writeBool( bAckRequested );
        pSent->_bAckRequested = bAckRequested ? SW_TRUE : SW_FALSE;
        pSent->_byteCount     = writer.getByteCount();
        ++_stats._sentPacketCount;
        updateStats( time );
    }

    bool NetConnection::readPacket( float64 time, BitReader& reader )
    {
        const uint16 sequence = static_cast<uint16>( reader.readBits( 16 ) );
        const uint16 ack      = static_cast<uint16>( reader.readBits( 16 ) );
        const uint32 ackBits  = reader.readBits( 32 );
        if ( reader.hasOverflowed() )
            return false;
        if ( _receivedPackets.exists( sequence ) )
            return false; // 중복 패킷
        ReceivedPacket* pReceived = _receivedPackets.insert( sequence );
        if ( pReceived == nullptr )
            return false; // 너무 오래된 패킷
        pReceived->_time = time;

        // 메시지 — 먼저 모두 읽어 깨진 패킷이면 아무것도 넣지 않는다.
        vector<ParsedMessage>& listParsed = _listParsedScratch;
        listParsed.clear();
        while ( reader.readBool() )
        {
            ParsedMessage message;
            message._channel = static_cast<NetChannelType>( reader.readBits( NetConnectionInternal::kChannelBits ) );
            if ( message._channel == NetChannelType::Count )
                return false;
            message._id      = message._channel != NetChannelType::Unreliable ? static_cast<uint16>( reader.readBits( 16 ) ) : static_cast<uint16>( 0 );
            const int32 size = static_cast<int32>( reader.readBits( kMessageSizeBits ) );
            if ( size > kMaxMessageSize )
                return false;
            message._buffer.resize( static_cast<size_t>( size ) );
            if ( size > 0 && reader.readBytes( message._buffer.data(), size ) == false )
                return false;
            if ( reader.hasOverflowed() )
                return false;
            listParsed.push_back( std::move( message ) );
        }
        const bool bAckRequested = reader.readBool();
        if ( reader.hasOverflowed() )
            return false;

        pReceived->_byteCount = ( reader.getBitPosition() + 7 ) / 8;
        ++_stats._receivedPacketCount;
        if ( bAckRequested )
            _bAckPending = SW_TRUE;
        processAcks( time, ack, ackBits );

        for ( ParsedMessage& message : listParsed )
        {
            switch ( message._channel )
            {
                case NetChannelType::ReliableOrdered:
                {
                    // 이미 건넨 것 · 창 밖은 버리고, 나머지는 자리에 두었다가 순서대로 건넨다.
                    const int32 ahead = NetSequence::computeDifference( message._id, _nextReliableReceiveId );
                    if ( ahead < 0 || ahead >= kReliableWindow || _incomingReliable.exists( message._id ) )
                        break;
                    IncomingReliable* pIncoming = _incomingReliable.insert( message._id );
                    if ( pIncoming != nullptr )
                        pIncoming->_buffer = std::move( message._buffer );
                    break;
                }
                case NetChannelType::UnreliableSequenced:
                {
                    // 번호는 연결 하나에서 늘기만 하므로 종류마다 마지막 번호와 비교한다 — 늦게 온 같은 종류의 옛것만 버린다.
                    const uint8      kind    = message._buffer.empty() ? static_cast<uint8>( 0 ) : message._buffer[0];
                    SequencedStream* pStream = nullptr;
                    for ( SequencedStream& stream : _listSequencedReceive )
                    {
                        if ( stream._kind == kind )
                            pStream = &stream;
                    }
                    if ( pStream != nullptr && NetSequence::isGreater( message._id, pStream->_lastId ) == false )
                        break; // 늦게 온 옛것
                    if ( pStream == nullptr )
                    {
                        _listSequencedReceive.push_back( SequencedStream{ message._id, kind } );
                        pStream = &_listSequencedReceive.back();
                    }
                    pStream->_lastId = message._id;
                    _arrIncoming[static_cast<int32>( NetChannelType::UnreliableSequenced )].push_back( std::move( message._buffer ) );
                    break;
                }
                case NetChannelType::Unreliable:
                {
                    _arrIncoming[static_cast<int32>( NetChannelType::Unreliable )].push_back( std::move( message._buffer ) );
                    break;
                }
                case NetChannelType::Count:
                {
                    break;
                }
            }
        }
        for ( IncomingReliable* pIncoming = _incomingReliable.find( _nextReliableReceiveId ); pIncoming != nullptr;
              pIncoming                   = _incomingReliable.find( _nextReliableReceiveId ) )
        {
            _arrIncoming[static_cast<int32>( NetChannelType::ReliableOrdered )].push_back( std::move( pIncoming->_buffer ) );
            _incomingReliable.remove( _nextReliableReceiveId );
            ++_nextReliableReceiveId;
        }
        updateStats( time );
        return true;
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
            // RTT 표본 — 확인을 바란 패킷이 가장 새 확인으로 돌아왔을 때만(묶음으로 늦게 확인된 것 · 답 패킷은 상대가 기다렸다 보냈을 수 있다).
            if ( bitIndex < 0 && pSent->_bAckRequested )
            {
                const float64 sample = time - pSent->_time;
                _stats._rtt          = _stats._rtt <= 0.0f ? static_cast<float32>( sample )
                                                           : NetConnectionInternal::smoothTowards( _stats._rtt, sample, NetConnectionInternal::kRttSmoothing );
            }
            for ( int32 index = 0; index < pSent->_reliableCount; ++index )
                _outgoingReliable.remove( pSent->_arrReliableId[index] );
        }
        // 가장 오래된 미확인 신뢰 id 를 앞으로.
        while ( _oldestUnackedReliableId != _nextReliableSendId && _outgoingReliable.exists( _oldestUnackedReliableId ) == false )
            ++_oldestUnackedReliableId;
    }

    void NetConnection::updateStats( float64 time )
    {
        if ( time - _lastStatsTime < NetConnectionInternal::kStatsInterval )
            return;
        _lastStatsTime = time;
        // 손실 — RTT 두 배 넘게 확인이 없는 패킷의 몫(최근 창 안).
        const float64 lostAge   = MathUtil::max( NetConnectionInternal::kMinLostAge, static_cast<float64>( _stats._rtt ) * 2.0 );
        int32         lostCount = 0;
        int32         sentCount = 0;
        int32         sentBytes = 0;
        int32         recvBytes = 0;
        for ( int32 offset = 1; offset <= 64; ++offset )
        {
            const SentPacket* pSent = _sentPackets.find( static_cast<uint16>( _nextPacketSequence - offset ) );
            if ( pSent != nullptr && pSent->_bAckRequested && time - pSent->_time >= lostAge )
            {
                ++sentCount;
                lostCount += pSent->_bAcked ? 0 : 1;
            }
            if ( pSent != nullptr && time - pSent->_time < 1.0 )
                sentBytes += pSent->_byteCount;
        }
        for ( int32 offset = 0; offset < 64 && _receivedPackets.hasNewest(); ++offset )
        {
            const ReceivedPacket* pReceived = _receivedPackets.find( static_cast<uint16>( _receivedPackets.getNewest() - offset ) );
            if ( pReceived != nullptr && time - pReceived->_time < 1.0 )
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
