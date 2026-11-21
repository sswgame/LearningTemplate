#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Chat/Runtime/ChatLog.h"

namespace sw
{
    ChatLog::ChatLog()
        : _listLine{}
        , _listPolled{}
        , _pClient{ nullptr }
        , _revision{ 0 }
        , _retryAfterMs{ 0 }
        , _lastResult{ ChatResult::Ok }
    {
    }

    bool ChatLog::initialize( ChatClient* pClient )
    {
        _consumerLease.release();
        _pClient = nullptr;
        if ( pClient != nullptr && pClient->claimMessages( _consumerLease ) == false )
            return false;
        _pClient = pClient;
        return true;
    }

    void ChatLog::update()
    {
        if ( _pClient == nullptr )
            return;
        _listPolled.clear();
        _pClient->pollMessages( _consumerLease, _listPolled );
        for ( const ChatMessage& message : _listPolled )
        {
            appendMessage( message );
        }
    }

    bool ChatLog::send( string_view channelID, string_view text )
    {
        _retryAfterMs = 0;
        if ( text.empty() )
        {
            _lastResult = ChatServiceResult::fail( OnlineError::kInvalidRequest );
            ++_revision;
            return false;
        }
        if ( _pClient == nullptr )
        {
            _lastResult = ChatServiceResult::fail( OnlineError::kUnavailable );
            ++_revision;
            return false;
        }
        // 준비되지 않은 클라이언트는 이 호출 안에서 바로 onSendReply(오류)로 끝난다.
        ChatSendRequest request;
        request._channelID = string( channelID );
        request._text      = string( text );
        (void)_pClient->call<ChatMethod::Send>( request, SW_DELEGATE_METHOD( OnlineReplyDelegate<ChatMethod::Send>, &ChatLog::onSendReply, this ) );
        return true;
    }

    void ChatLog::appendMessage( const ChatMessage& message )
    {
        _listLine.push_back( message );
        if ( _listLine.size() > kMaxLines )
            _listLine.pop_front();
        ++_revision;
    }

    void ChatLog::onSendReply( const OnlineResult<ChatSendReply>& reply )
    {
        _lastResult   = reply._errorCode != OnlineError::kOk ? ChatServiceResult::fail( reply._errorCode ) : ChatServiceResult{ reply._reply._result };
        _retryAfterMs = reply._reply._retryAfterMs;
        ++_revision;
    }
} // namespace sw
