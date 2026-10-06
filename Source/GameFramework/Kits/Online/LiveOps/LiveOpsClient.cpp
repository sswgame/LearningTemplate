#include "pch.h"

#include "GameFramework/Kits/Online/LiveOps/LiveOpsClient.h"

#include "Core/Network/BitStream.h"

#include "GameFramework/Base/Online/Store/ServiceKeyUtil.h"

namespace sw
{
    LiveOpsClient::LiveOpsClient()
        : _callTable{}
        , _listEvent{}
        , _region{}
        , _pClient{ nullptr }
        , _revision{ 0 }
        , _buildVersion{ 0 }
        , _bRequested{ SW_FALSE }
    {
    }

    void LiveOpsClient::initialize( OnlineServiceClient* pClient ) { _pClient = pClient; }

    uint64 LiveOpsClient::requestLiveState( string_view region, uint32 buildVersion, const ReplyDelegate& onReply )
    {
        _region       = string( region );
        _buildVersion = buildVersion;
        _bRequested   = SW_TRUE;
        BitWriter body;
        ServiceKeyUtil::writeString( body, region );
        body.writeVarUint( buildVersion );
        return send( LiveOpsMethod::kGetLiveState, body, onReply );
    }

    uint64 LiveOpsClient::registerDevice( const PushDeviceRegistration& registration, const ReplyDelegate& onReply )
    {
        BitWriter body;
        LiveOpsProtocol::writeDevice( body, registration );
        return send( LiveOpsMethod::kRegisterDevice, body, onReply );
    }

    uint64 LiveOpsClient::unregisterDevice( string_view providerId, string_view token, const ReplyDelegate& onReply )
    {
        BitWriter body;
        ServiceKeyUtil::writeString( body, providerId );
        ServiceKeyUtil::writeString( body, token );
        return send( LiveOpsMethod::kUnregisterDevice, body, onReply );
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
        (void)requestLiveState( _region, _buildVersion, ReplyDelegate{} ); // 알림에는 내용이 없다 — 이 계정의 대상으로 다시 받는다
    }

    uint64 LiveOpsClient::send( uint16 method, const BitWriter& body, const ReplyDelegate& onReply )
    {
        if ( _pClient == nullptr )
        {
            LiveOpsClientReply reply;
            reply._errorCode     = OnlineError::kUnavailable;
            reply._reply._result = LiveOpsResult::Unavailable;
            if ( onReply.isBound() )
                onReply( reply );
            return 0;
        }
        return _callTable.send( *_pClient, method, body, NetRequestOptions{}, OnlineResponseDelegate::create<&LiveOpsClient::onResponse>( this ), onReply );
    }

    void LiveOpsClient::onResponse( const OnlineResponse& response )
    {
        ReplyDelegate onReply;
        if ( _callTable.take( response._requestId, onReply ) == false )
            return;
        LiveOpsClientReply reply;
        reply._requestId = response._requestId;
        reply._errorCode = response._errorCode;
        if ( response._errorCode != OnlineError::kOk || response.isOk() == false )
        {
            reply._reply._result = LiveOpsProtocol::fromErrorCode( response._errorCode == OnlineError::kOk ? OnlineError::kUnavailable : response._errorCode );
        }
        else
        {
            BitReader reader( response._pBody, response._bodySize );
            if ( LiveOpsProtocol::readReply( reader, reply._reply ) == false )
            {
                reply._reply         = LiveOpsReply{};
                reply._reply._result = LiveOpsResult::Invalid;
            }
            else if ( reply._reply._result == LiveOpsResult::Ok )
            {
                _listEvent = reply._reply._listEvent;
                ++_revision;
            }
        }
        if ( onReply.isBound() )
            onReply( reply );
    }
} // namespace sw
