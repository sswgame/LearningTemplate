#include "pch.h"

#include "GameFramework/Kits/Feature/Online/ServerDirectory/Runtime/ServerList.h"

namespace sw
{
    ServerList::ServerList()
        : _listEntry{}
        , _pClient{ nullptr }
        , _revision{ 0 }
        , _errorCode{ OnlineError::kOk }
        , _state{ ServerListState::Idle }
    {
    }

    bool ServerList::refresh( string_view kind )
    {
        if ( _state == ServerListState::Loading )
            return false;
        if ( _pClient == nullptr )
        {
            _listEntry.clear();
            _errorCode = OnlineError::kUnavailable;
            _state     = ServerListState::Failed;
            ++_revision;
            return false;
        }
        _state     = ServerListState::Loading;
        _errorCode = OnlineError::kOk;
        ++_revision; // 로딩 표시를 그리게 한다
        // 범위 밖 종류는 보내지 않고 이 호출 안에서 바로 onServerList(kInvalidRequest)로 끝난다.
        ServerListRequest request;
        request._kind = string( kind );
        (void)_pClient->call<ServerDirectoryMethod::ListServers>( request, SW_DELEGATE_METHOD( OnlineReplyDelegate<ServerDirectoryMethod::ListServers>, &ServerList::onServerList, this ) );
        return _state != ServerListState::Failed;
    }

    void ServerList::onServerList( const OnlineResult<ServerListReply>& result )
    {
        _errorCode = result._errorCode;
        if ( result._errorCode == OnlineError::kOk )
        {
            _listEntry = result._reply._listEntry;
            _state     = ServerListState::Loaded;
        }
        else
        {
            _listEntry.clear(); // 낡은 목록을 새것처럼 남기지 않는다
            _state = ServerListState::Failed;
        }
        ++_revision;
    }
} // namespace sw
