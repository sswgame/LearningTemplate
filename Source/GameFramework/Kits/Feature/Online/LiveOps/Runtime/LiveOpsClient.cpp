#include "pch.h"

#include "GameFramework/Kits/Feature/Online/LiveOps/Runtime/LiveOpsClient.h"

#include "Core/Container/VectorUtil.h"

#include "GameFramework/Runtime/Online/Service/OnlineClientRegistry.h"

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
        return call<LiveOpsMethod::GetLiveState>( request, onReply );
    }

    bool LiveOpsClient::hasEventKind( string_view kind ) const
    {
        return VectorUtil::containsBy<&LiveEventState::_kind>( _listEvent, kind );
    }

    void LiveOpsClient::onPush( LiveOpsMethod::LiveState, const OnlineNoBody& body )
    {
        (void)body;
        if ( _bRequested == SW_FALSE )
            return;
        (void)requestLiveState( _region, _buildVersion, ReplyDelegate{} ); // 알림에는 내용이 없으므로 이 계정의 대상에 맞는 이벤트를 다시 받는다
    }

    void LiveOpsClient::onCallSucceeded( LiveOpsMethod::GetLiveState, const LiveOpsStateRequest& request, const LiveOpsReply& reply )
    {
        (void)request;
        _listEvent = reply._listEvent;
        ++_revision;
    }
} // namespace sw

namespace sw
{
    SW_REGISTER_ONLINE_CLIENT( LiveOpsClient );
} // namespace sw
