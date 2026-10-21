#include "pch.h"

#include "GameFramework/Base/Online/Bus/EphemeralServerBus.h"

#include "Core/Container/LittleEndianUtil.h"
#include "Core/Container/StringUtil.h"

#include "GameFramework/Base/Online/Cache/EphemeralStore.h"
#include "GameFramework/Base/Online/Cache/EphemeralStoreRouter.h"

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
        };
    } // namespace
} // namespace sw

namespace sw
{
    EphemeralServerBus::EphemeralServerBus( EphemeralStoreRouter* pRouter, uint64 serverID )
        : _listSubscription{}
        , _listQueuedMessage{}
        , _pRouter{ pRouter }
        , _serverID{ serverID }
        , _nextSequence{ 1 }
        , _droppedCount{ 0 }
    {
    }

    EphemeralServerBus::~EphemeralServerBus()
    {
        // 라우터가 이 객체의 델리게이트를 보관한다. 라우터가 먼저 종료했으면(`shutdown`) 구독 목록이 비어 있어 해지는 아무 일도 하지 않는다.
        for ( const TopicSubscription& subscription : _listSubscription )
        {
            _pRouter->unsubscribe( subscription._subscriptionID );
        }
    }

    bool EphemeralServerBus::bindRouter( EphemeralStoreRouter* pRouter )
    {
        if ( _listSubscription.empty() == false )
            return false;
        _pRouter = pRouter;
        return true;
    }

    void EphemeralServerBus::publish( string_view topic, const uint8* pData, int32 size )
    {
        const bool bValid = isValidTopic( topic ) && 0 <= size && size <= kMaxMessageSize && ( size == 0 || pData != nullptr );
        if ( bValid == false )
        {
            SW_LOG_WARNING( "Dropped a bus message: topic '%#' size %#", topic, size );
            return;
        }
        if ( _pRouter == nullptr || _pRouter->getStore() == nullptr )
        {
            SW_LOG_WARNING( "Dropped a bus message on topic '%#' - the cache router is not initialized", topic );
            return;
        }
        vector<uint8> envelopeBytes;
        envelopeBytes.reserve( EphemeralServerBusInternal::kEnvelopeSize + static_cast<size_t>( size ) );
        envelopeBytes.push_back( EphemeralServerBusInternal::kEnvelopeVersion );
        LittleEndianUtil::appendUint64( envelopeBytes, _serverID );
        LittleEndianUtil::appendUint64( envelopeBytes, _nextSequence++ );
        envelopeBytes.insert( envelopeBytes.end(), pData, pData + size );
        // 발행 답(받은 수)은 쓰지 않는다 — 최대 한 번 배달. 빈 델리게이트면 라우터가 답을 버린다.
        (void)_pRouter->submit( EphemeralRequest::makePublish( EphemeralServerBusInternal::makeChannel( topic ), std::move( envelopeBytes ) ),
                                EphemeralStoreRouter::ReplyDelegate{} );
    }

    void EphemeralServerBus::subscribe( string_view topic )
    {
        if ( isValidTopic( topic ) == false )
        {
            SW_LOG_WARNING( "Ignored a bus subscription to an invalid topic '%#'", topic );
            return;
        }
        for ( const TopicSubscription& subscription : _listSubscription )
        {
            if ( subscription._topic == topic )
                return;
        }
        if ( _pRouter == nullptr )
        {
            SW_LOG_WARNING( "Bus subscription to '%#' failed - no cache router is bound", topic );
            return;
        }
        const uint64 subscriptionID = _pRouter->subscribe( EphemeralServerBusInternal::makeChannel( topic ),
                                                           SW_DELEGATE_METHOD( EphemeralStoreRouter::MessageDelegate, &EphemeralServerBus::onChannelMessage, this ) );
        if ( subscriptionID == 0 )
        {
            SW_LOG_WARNING( "Bus subscription to '%#' failed - the cache router is not initialized", topic );
            return;
        }
        TopicSubscription& subscription = _listSubscription.emplace_back();
        subscription._topic             = string( topic );
        subscription._subscriptionID    = subscriptionID;
    }

    void EphemeralServerBus::unsubscribe( string_view topic )
    {
        for ( size_t index = 0; index < _listSubscription.size(); ++index )
        {
            if ( _listSubscription[index]._topic != topic )
                continue;
            _pRouter->unsubscribe( _listSubscription[index]._subscriptionID );
            _listSubscription.erase( _listSubscription.begin() + static_cast<ptrdiff_t>( index ) );
            return;
        }
    }

    int32 EphemeralServerBus::pollMessages( vector<ServerBusMessage>& outListMessage )
    {
        const int32 addedCount = static_cast<int32>( _listQueuedMessage.size() );
        for ( ServerBusMessage& message : _listQueuedMessage )
        {
            outListMessage.push_back( std::move( message ) );
        }
        _listQueuedMessage.clear();
        return addedCount;
    }

    void EphemeralServerBus::onChannelMessage( const EphemeralMessage& message )
    {
        const string_view prefix{ EphemeralServerBusInternal::kChannelPrefix };
        const bool        bEnvelope = message._channel.size() > prefix.size() && StringUtil::startsWith( message._channel, prefix ) &&
                               message._bytes.size() >= EphemeralServerBusInternal::kEnvelopeSize &&
                               message._bytes[0] == EphemeralServerBusInternal::kEnvelopeVersion;
        if ( bEnvelope == false )
        {
            ++_droppedCount;
            return;
        }
        vector<uint8> bodyBytes( message._bytes.begin() + static_cast<ptrdiff_t>( EphemeralServerBusInternal::kEnvelopeSize ), message._bytes.end() );
        _listQueuedMessage.emplace_back( std::move( bodyBytes ), string( message._channel.substr( prefix.size() ) ), LittleEndianUtil::readUint64( message._bytes.data() + 1 ),
                                         LittleEndianUtil::readUint64( message._bytes.data() + 9 ) );
    }
} // namespace sw
