#include "pch.h"

#include "GameFramework/Kits/Feature/Online/LiveOps/Shared/LiveOpsClient.h"

#include "Core/Container/VectorUtil.h"
#include "Core/Network/BitStream.h"

namespace sw
{
    LiveOpsClient::LiveOpsClient()
        : _listEvent{}
        , _region{}
        , _revision{ 0 }
        , _buildVersion{ 0 }
        , _bRequested{ SW_FALSE }
    {
    }

    uint64 LiveOpsClient::requestLiveState( string_view region, uint32 buildVersion, const ReplyDelegate& onReply )
    {
        _region       = string( region );
        _buildVersion = buildVersion;
        _bRequested   = SW_TRUE;
        LiveOpsStateRequest request;
        request._region       = string( region );
        request._buildVersion = buildVersion;
        return sendRequest( LiveOpsMethod::kGetLiveState, request, NetIdempotencyKey{}, onReply );
    }

    uint64 LiveOpsClient::registerDevice( const PushDeviceRegistration& registration, const ReplyDelegate& onReply )
    {
        return sendRequest( LiveOpsMethod::kRegisterDevice, registration, NetIdempotencyKey{}, onReply );
    }

    uint64 LiveOpsClient::unregisterDevice( string_view providerID, string_view token, const ReplyDelegate& onReply )
    {
        LiveOpsDeviceKey key;
        key._providerID = string( providerID );
        key._token      = string( token );
        return sendRequest( LiveOpsMethod::kUnregisterDevice, key, NetIdempotencyKey{}, onReply );
    }

    bool LiveOpsClient::hasEventKind( string_view kind ) const
    {
        return VectorUtil::containsBy<&LiveEventState::_kind>( _listEvent, kind );
    }

    void LiveOpsClient::onServicePush( uint16 kind, BitReader& body )
    {
        (void)body;
        if ( kind != LiveOpsMethod::kPushLiveState || _bRequested == SW_FALSE )
            return;
        (void)requestLiveState( _region, _buildVersion, ReplyDelegate{} ); // 알림에는 내용이 없으므로 이 계정의 대상에 맞는 이벤트를 다시 받는다
    }

    void LiveOpsClient::onCallSucceeded( const Call& call, const LiveOpsReply& reply )
    {
        (void)call;
        _listEvent = reply._listEvent;
        ++_revision;
    }
} // namespace sw
