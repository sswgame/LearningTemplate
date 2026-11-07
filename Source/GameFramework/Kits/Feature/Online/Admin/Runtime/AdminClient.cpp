#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Admin/Runtime/AdminClient.h"

namespace sw
{
    namespace
    {
        /** @brief 실행 때 고른 번호를 서술자로 옮겨 보냅니다. */
        struct AdminCommandVisitor
        {
            AdminClient&                      _client;
            const AdminRequest&               _request;
            const NetIdempotencyKey&          _key;
            const AdminClient::ReplyDelegate& _onReply;
            uint64                            _requestID;

            template <typename TMethod>
            void visit()
            {
                _requestID = _client.template call<TMethod>( _request, _onReply, _key );
            }
        };
    } // namespace

    uint64 AdminClient::sendCommand( uint16 method, const AdminRequest& request, const NetIdempotencyKey& key, const ReplyDelegate& onReply )
    {
        AdminCommandVisitor visitor{ *this, request, key, onReply, 0 };
        if ( AdminProtocol::Table::visitMethod( method, visitor ) )
            return visitor._requestID;
        Reply reply;
        reply._method    = method;
        reply._errorCode = OnlineError::kInvalidRequest;
        if ( onReply.isBound() )
            onReply( reply );
        return 0;
    }
} // namespace sw
