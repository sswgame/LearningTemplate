#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Chat/Shared/ChatLogModel.h"

namespace sw
{
    ChatLogModel::ChatLogModel()
        : _listLine{}
        , _listDrained{}
        , _pClient{ nullptr }
        , _revision{ 0 }
        , _retryAfterMs{ 0 }
        , _lastResult{ ChatResult::Ok }
    {
    }

    bool ChatLogModel::initialize( ChatClient* pClient )
    {
        _drainLease.release();
        _pClient = nullptr;
        if ( pClient != nullptr && pClient->claimMessages( _drainLease ) == false )
            return false;
        _pClient = pClient;
        return true;
    }

    void ChatLogModel::update()
    {
        if ( _pClient == nullptr )
            return;
        _listDrained.clear();
        _pClient->drainMessages( _drainLease, _listDrained );
        for ( const ChatMessage& message : _listDrained )
        {
            appendMessage( message );
        }
    }

    bool ChatLogModel::send( string_view channelID, string_view text )
    {
        _retryAfterMs = 0;
        if ( text.empty() )
        {
            _lastResult = ChatResult::Invalid;
            ++_revision;
            return false;
        }
        if ( _pClient == nullptr )
        {
            _lastResult = ChatResult::Unavailable;
            ++_revision;
            return false;
        }
        // 준비되지 않은 클라이언트는 이 호출 안에서 바로 onSendReply(오류)로 끝난다.
        (void)_pClient->send( channelID, text, ChatClient::ReplyDelegate::create<&ChatLogModel::onSendReply>( this ) );
        return true;
    }

    void ChatLogModel::appendMessage( const ChatMessage& message )
    {
        _listLine.push_back( message );
        if ( _listLine.size() > kMaxLines )
            _listLine.pop_front();
        ++_revision;
    }

    void ChatLogModel::onSendReply( const ChatClient::Reply& reply )
    {
        // 공통 오류(`_errorCode`)가 있으면 서버가 아닌 클라이언트가 `_result` 를 그에 대응하는 값으로 채워 온다.
        _lastResult   = reply._reply._result;
        _retryAfterMs = reply._reply._retryAfterMs;
        ++_revision;
    }
} // namespace sw
