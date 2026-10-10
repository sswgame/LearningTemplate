#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Admin/Shared/AdminClient.h"

#include "Core/Network/BitStream.h"

namespace sw
{
    AdminClient::AdminClient()
        : _mapClientIDToCall{}
        , _sendingCall{}
        , _pClient{ nullptr }
        , _nextRequestID{ 1 }
        , _bSending{ SW_FALSE }
    {
    }

    void AdminClient::initialize( OnlineServiceClient* pClient ) { _pClient = pClient; }

    uint64 AdminClient::sendCommand( uint16 method, const AdminRequest& request, const NetIdempotencyKey& key, ReplyDelegate onReply )
    {
        const NetIdempotencyKey usedKey   = AdminMethod::isMutating( method ) && key.isValid() == false ? NetIdempotencyKey::makeRandom() : key;
        const uint64            requestID = _nextRequestID++;
        _sendingCall                      = PendingCall{ onReply, usedKey, requestID, method };
        if ( _pClient == nullptr )
        {
            AdminClientReply reply;
            reply._requestID      = requestID;
            reply._method         = method;
            reply._idempotencyKey = usedKey;
            reply._errorCode      = OnlineError::kUnavailable;
            reply._reply._result  = AdminResult::Unavailable;
            _sendingCall          = PendingCall{};
            if ( onReply.isBound() )
                onReply( reply );
            return requestID;
        }
        BitWriter body;
        AdminProtocol::writeRequest( body, request );
        NetRequestOptions options;
        options._idempotencyKey = usedKey;
        _bSending               = SW_TRUE;
        const uint64 clientID   = _pClient->sendRequest( method, body, options, OnlineResponseDelegate::create<&AdminClient::onResponse>( this ) );
        _bSending               = SW_FALSE;
        if ( _sendingCall._requestID != 0 )
            _mapClientIDToCall[clientID] = _sendingCall;
        _sendingCall = PendingCall{};
        return requestID;
    }

    void AdminClient::onServicePush( uint16 kind, BitReader& body )
    {
        (void)kind; // GM 서비스는 알림을 보내지 않는다
        (void)body;
    }

    void AdminClient::onResponse( const OnlineResponse& response )
    {
        PendingCall call;
        const auto  callIt = _mapClientIDToCall.find( response._requestID );
        if ( callIt != _mapClientIDToCall.end() )
        {
            call = callIt->second;
            _mapClientIDToCall.erase( callIt );
        }
        else if ( _bSending == SW_TRUE && _sendingCall._requestID != 0 )
        {
            call                    = _sendingCall;
            _sendingCall._requestID = 0;
        }
        else
        {
            return;
        }
        AdminClientReply reply;
        reply._requestID      = call._requestID;
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
