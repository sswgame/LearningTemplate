#include "pch.h"

#include "GameFramework/Kits/Feature/Online/LiveOps/Shared/LiveOpsClient.h"

#include "Core/Network/BitStream.h"

#include "GameFramework/Base/Online/Store/ServiceKeyUtil.h"

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
        BitWriter body;
        ServiceKeyUtil::writeString( body, region );
        body.writeVarUint( buildVersion );
        return sendCall( LiveOpsMethod::kGetLiveState, body, NetIdempotencyKey{}, onReply );
    }

    uint64 LiveOpsClient::registerDevice( const PushDeviceRegistration& registration, const ReplyDelegate& onReply )
    {
        BitWriter body;
        LiveOpsProtocol::writeDevice( body, registration );
        return sendCall( LiveOpsMethod::kRegisterDevice, body, NetIdempotencyKey{}, onReply );
    }

    uint64 LiveOpsClient::unregisterDevice( string_view providerID, string_view token, const ReplyDelegate& onReply )
    {
        BitWriter body;
        ServiceKeyUtil::writeString( body, providerID );
        ServiceKeyUtil::writeString( body, token );
        return sendCall( LiveOpsMethod::kUnregisterDevice, body, NetIdempotencyKey{}, onReply );
    }

    bool LiveOpsClient::hasEventKind( string_view kind ) const
    {
        for ( const LiveEventState& state : _listEvent )
        {
            if ( state._kind == kind )
                return true;
        }
        return false;
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
