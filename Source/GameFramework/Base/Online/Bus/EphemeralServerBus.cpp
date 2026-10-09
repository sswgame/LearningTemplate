#include "pch.h"

#include "GameFramework/Base/Online/Bus/EphemeralServerBus.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/string.h"

#include "GameFramework/Base/Online/Cache/EphemeralStore.h"

namespace sw
{
    SW_LOG_CALLER( "EphemeralServerBus" );

    namespace
    {
        struct EphemeralServerBusInternal
        {
            static constexpr uint8  kEnvelopeVersion = 1;
            static constexpr size_t kEnvelopeSize    = 1 + 8 + 8;
            static constexpr utf8   kChannelPrefix[] = "bus:";

            static string makeChannel( string_view topic )
            {
                string channel{ kChannelPrefix };
                channel += topic;
                return channel;
            }

            static void appendUint64( vector<uint8>& outBytes, uint64 value )
            {
                for ( int32 byteIndex = 0; byteIndex < 8; ++byteIndex )
                {
                    outBytes.push_back( static_cast<uint8>( value >> ( byteIndex * 8 ) ) );
                }
            }

            static uint64 readUint64( const uint8* pData )
            {
                uint64 value = 0;
                for ( int32 byteIndex = 0; byteIndex < 8; ++byteIndex )
                {
                    value |= static_cast<uint64>( pData[byteIndex] ) << ( byteIndex * 8 );
                }
                return value;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    EphemeralServerBus::EphemeralServerBus( IEphemeralStore* pStore, uint64 serverId )
        : _pStore{ pStore }
        , _serverId{ serverId }
        , _nextSequence{ 1 }
        , _droppedCount{ 0 }
    {
    }

    void EphemeralServerBus::publish( string_view topic, const uint8* pData, int32 size )
    {
        const bool bValid = isValidTopic( topic ) && 0 <= size && size <= kMaxMessageSize && ( size == 0 || pData != nullptr );
        if ( bValid == false )
        {
            SW_LOG_WARNING( "Dropped a bus message: topic '%#' size %#", topic, size );
            return;
        }
        vector<uint8> envelopeBytes;
        envelopeBytes.reserve( EphemeralServerBusInternal::kEnvelopeSize + static_cast<size_t>( size ) );
        envelopeBytes.push_back( EphemeralServerBusInternal::kEnvelopeVersion );
        EphemeralServerBusInternal::appendUint64( envelopeBytes, _serverId );
        EphemeralServerBusInternal::appendUint64( envelopeBytes, _nextSequence++ );
        envelopeBytes.insert( envelopeBytes.end(), pData, pData + size );
        (void)_pStore->submit( EphemeralRequest::makePublish( EphemeralServerBusInternal::makeChannel( topic ), std::move( envelopeBytes ) ) );
    }

    void EphemeralServerBus::subscribe( string_view topic )
    {
        if ( isValidTopic( topic ) == false )
        {
            SW_LOG_WARNING( "Ignored a bus subscription to an invalid topic '%#'", topic );
            return;
        }
        _pStore->subscribe( EphemeralServerBusInternal::makeChannel( topic ) );
    }

    void EphemeralServerBus::unsubscribe( string_view topic ) { _pStore->unsubscribe( EphemeralServerBusInternal::makeChannel( topic ) ); }

    int32 EphemeralServerBus::pollMessages( vector<ServerBusMessage>& outListMessage )
    {
        vector<EphemeralReply> listReply; // 발행 답(받은 수)은 쓰지 않는다 — 최대 한 번 배달
        (void)_pStore->pollReplies( listReply );
        vector<EphemeralMessage> listMessage;
        (void)_pStore->pollMessages( listMessage );
        const string_view prefix{ EphemeralServerBusInternal::kChannelPrefix };
        int32             addedCount = 0;
        for ( EphemeralMessage& message : listMessage )
        {
            const bool bEnvelope = message._channel.size() > prefix.size() && StringUtil::startsWith( message._channel, prefix ) &&
                                   message._bytes.size() >= EphemeralServerBusInternal::kEnvelopeSize &&
                                   message._bytes[0] == EphemeralServerBusInternal::kEnvelopeVersion;
            if ( bEnvelope == false )
            {
                ++_droppedCount;
                continue;
            }
            ServerBusMessage& busMessage = outListMessage.emplace_back();
            busMessage._topic            = message._channel.substr( prefix.size() );
            busMessage._originServerId   = EphemeralServerBusInternal::readUint64( message._bytes.data() + 1 );
            busMessage._sequence         = EphemeralServerBusInternal::readUint64( message._bytes.data() + 9 );
            busMessage._bytes.assign( message._bytes.begin() + static_cast<ptrdiff_t>( EphemeralServerBusInternal::kEnvelopeSize ), message._bytes.end() );
            ++addedCount;
        }
        return addedCount;
    }
} // namespace sw
