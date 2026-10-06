#include "pch.h"

#include "GameFramework/Kits/Online/Admin/AdminClient.h"

#include "Core/Network/BitStream.h"

namespace sw
{
    AdminClient::AdminClient()
        : _mapClientIdToCall{}
        , _sendingCall{}
        , _pClient{ nullptr }
        , _nextRequestId{ 1 }
        , _bSending{ SW_FALSE }
    {
    }

    void AdminClient::initialize( OnlineServiceClient* pClient ) { _pClient = pClient; }

    uint64 AdminClient::sendCommand( uint16 method, const AdminRequest& request, const NetIdempotencyKey& key, ReplyDelegate onReply )
    {
        const NetIdempotencyKey usedKey   = AdminMethod::isMutating( method ) && key.isValid() == false ? NetIdempotencyKey::makeRandom() : key;
        const uint64            requestId = _nextRequestId++;
        _sendingCall                      = PendingCall{ onReply, usedKey, requestId, method };
        if ( _pClient == nullptr )
        {
            AdminClientReply reply;
            reply._requestId      = requestId;
            reply._method         = method;
            reply._idempotencyKey = usedKey;
            reply._errorCode      = OnlineError::kUnavailable;
            reply._reply._result  = AdminResult::Unavailable;
            _sendingCall          = PendingCall{};
            if ( onReply.isBound() )
                onReply( reply );
            return requestId;
        }
        BitWriter body;
        AdminProtocol::writeRequest( body, request );
        NetRequestOptions options;
        options._idempotencyKey = usedKey;
        _bSending               = SW_TRUE;
        const uint64 clientId   = _pClient->sendRequest( method, body, options, OnlineResponseDelegate::create<&AdminClient::onResponse>( this ) );
        _bSending               = SW_FALSE;
        if ( _sendingCall._requestId != 0 )
            _mapClientIdToCall[clientId] = _sendingCall;
        _sendingCall = PendingCall{};
        return requestId;
    }

    void AdminClient::onServicePush( uint16 kind, BitReader& body )
    {
        (void)kind; // GM 서비스는 알림을 보내지 않는다
        (void)body;
    }

    void AdminClient::onResponse( const OnlineResponse& response )
    {
        PendingCall call;
        const auto  callIt = _mapClientIdToCall.find( response._requestId );
        if ( callIt != _mapClientIdToCall.end() )
        {
            call = callIt->second;
            _mapClientIdToCall.erase( callIt );
        }
        else if ( _bSending == SW_TRUE && _sendingCall._requestId != 0 )
        {
            call                    = _sendingCall;
            _sendingCall._requestId = 0;
        }
        else
        {
            return;
        }
        AdminClientReply reply;
        reply._requestId      = call._requestId;
        reply._method         = call._method;
        reply._idempotencyKey = call._key;
        reply._errorCode      = response._errorCode;
        if ( response.isOk() == false )
        {
            reply._reply._result = AdminProtocol::fromErrorCode( response._errorCode );
        }
        else
        {
            BitReader body( response._pBody, response._bodySize );
            if ( AdminProtocol::readReply( body, reply._reply ) == false )
            {
                reply._reply         = AdminReply{};
                reply._reply._result = AdminResult::Unavailable;
            }
        }
        if ( call._onReply.isBound() )
            call._onReply( reply );
    }
} // namespace sw
