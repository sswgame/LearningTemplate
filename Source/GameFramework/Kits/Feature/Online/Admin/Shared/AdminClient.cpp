#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Admin/Shared/AdminClient.h"

#include "Core/Network/BitStream.h"

namespace sw
{
    AdminClient::AdminClient()
        : _callTable{}
        , _pClient{ nullptr }
        , _nextRequestID{ 1 }
    {
    }

    void AdminClient::initialize( OnlineServiceClient* pClient ) { _pClient = pClient; }

    uint64 AdminClient::sendCommand( uint16 method, const AdminRequest& request, const NetIdempotencyKey& key, ReplyDelegate onReply )
    {
        const NetIdempotencyKey usedKey   = AdminMethod::isMutating( method ) && key.isValid() == false ? NetIdempotencyKey::makeRandom() : key;
        const uint64            requestID = _nextRequestID++;
        const PendingCall       pendingCall{ onReply, usedKey, requestID, method };
        if ( _pClient == nullptr )
        {
            AdminClientReply reply;
            reply._requestID      = requestID;
            reply._method         = method;
            reply._idempotencyKey = usedKey;
            reply._errorCode      = OnlineError::kUnavailable;
            reply._reply._result  = AdminResult::Unavailable;
            if ( onReply.isBound() )
                onReply( reply );
            return requestID;
        }
        BitWriter body;
        AdminProtocol::writeRequest( body, request );
        NetRequestOptions options;
        options._idempotencyKey = usedKey;
        (void)_callTable.send( *_pClient, method, body, options, OnlineResponseDelegate::create<&AdminClient::onResponse>( this ), pendingCall ); // 응답은 pendingCall._requestID 로 알린다
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
        if ( _callTable.take( response._requestID, call ) == false )
            return;
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
