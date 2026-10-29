#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Social/Runtime/FriendListModel.h"

#include "Core/Container/VectorUtil.h"

#include <algorithm>

namespace sw
{
    namespace
    {
        struct FriendListModelInternal
        {
            /** @brief 접속 중인 친구를 앞에, 같은 쪽끼리는 계정 번호 순으로 둡니다. */
            struct EntryOrder
            {
                bool operator()( const FriendEntry& left, const FriendEntry& right ) const
                {
                    const bool bLeftOnline  = left._status != SocialPresenceStatus::Offline;
                    const bool bRightOnline = right._status != SocialPresenceStatus::Offline;
                    if ( bLeftOnline != bRightOnline )
                        return bLeftOnline;
                    return left._accountID < right._accountID;
                }
            };
        };
    } // namespace

    FriendListModel::FriendListModel()
        : _listEntry{}
        , _listDrained{}
        , _pClient{ nullptr }
        , _revision{ 0 }
        , _pendingRequestCount{ 0 }
        , _errorCode{ OnlineError::kOk }
        , _result{ SocialResult::Ok }
        , _state{ FriendListState::Idle }
        , _bStale{ false }
    {
    }

    void FriendListModel::fail( uint16 errorCode, SocialResult result )
    {
        _listEntry.clear(); // 낡은 목록을 새것처럼 남기지 않는다
        _pendingRequestCount = 0;
        _errorCode           = errorCode;
        _result              = result;
        _state               = FriendListState::Failed;
        ++_revision;
    }

    bool FriendListModel::refresh()
    {
        if ( _state == FriendListState::Loading )
            return false;
        if ( _pClient == nullptr )
        {
            fail( OnlineError::kUnavailable, SocialResult::Unavailable );
            return false;
        }
        _state     = FriendListState::Loading;
        _errorCode = OnlineError::kOk;
        _result    = SocialResult::Ok;
        _bStale    = false;
        ++_revision; // 로딩 표시를 그리게 한다
        // 준비되지 않은 클라이언트는 이 호출 안에서 바로 onLinksReply(오류)로 끝난다.
        (void)_pClient->listLinks( SocialClient::ReplyDelegate::create<&FriendListModel::onLinksReply>( this ) );
        return _state != FriendListState::Failed;
    }

    void FriendListModel::onLinksReply( const SocialClient::Reply& reply )
    {
        if ( reply._errorCode != OnlineError::kOk || reply._reply._result != SocialResult::Ok )
        {
            fail( reply._errorCode, reply._reply._result );
            return;
        }
        applyLinks( reply._reply );
        // 접속 상태는 서버가 관계를 읽은 뒤에야 답할 수 있어 관계 목록 다음에 묻는다.
        (void)_pClient->queryFriendPresence( SocialClient::ReplyDelegate::create<&FriendListModel::onPresenceReply>( this ) );
    }

    void FriendListModel::applyLinks( const SocialReply& reply )
    {
        _listEntry.clear();
        _pendingRequestCount = 0;
        for ( const SocialLink& link : reply._listLink )
        {
            if ( link._state == SocialLinkState::Friend )
            {
                FriendEntry entry;
                entry._accountID = link._otherID;
                _listEntry.push_back( std::move( entry ) );
            }
            else if ( link._state == SocialLinkState::Incoming )
                ++_pendingRequestCount;
        }
    }

    void FriendListModel::onPresenceReply( const SocialClient::Reply& reply )
    {
        if ( reply._errorCode != OnlineError::kOk || reply._reply._result != SocialResult::Ok )
        {
            fail( reply._errorCode, reply._reply._result );
            return;
        }
        for ( const SocialPresence& presence : reply._reply._listPresence )
        {
            for ( FriendEntry& entry : _listEntry )
            {
                if ( entry._accountID != presence._accountID )
                    continue;
                entry._status   = presence._status;
                entry._activity = presence._activity;
                break;
            }
        }
        sortEntries();
        _state = FriendListState::Loaded;
        ++_revision;
    }

    void FriendListModel::applyNotification( const SocialNotification& notification )
    {
        switch ( notification._kind )
        {
            case SocialNotificationKind::PresenceChanged:
            {
                for ( FriendEntry& entry : _listEntry )
                {
                    if ( entry._accountID != notification._presence._accountID )
                        continue;
                    entry._status   = notification._presence._status;
                    entry._activity = notification._presence._activity;
                    sortEntries();
                    ++_revision;
                    break;
                }
                break;
            }
            case SocialNotificationKind::FriendRequested:
            case SocialNotificationKind::FriendAdded:
            case SocialNotificationKind::FriendRemoved:
            {
                _bStale = true;
                break;
            }
            case SocialNotificationKind::GuildInvited:
            case SocialNotificationKind::GuildChanged:
            case SocialNotificationKind::Count:
            {
                break; // 길드 알림은 친구 목록과 상관없다
            }
        }
    }

    bool FriendListModel::initialize( SocialClient* pClient )
    {
        _drainLease.release();
        _pClient = nullptr;
        if ( pClient != nullptr && pClient->claimNotifications( _drainLease ) == false )
            return false;
        _pClient = pClient;
        return true;
    }

    void FriendListModel::update()
    {
        if ( _pClient == nullptr )
            return;
        _listDrained.clear();
        _pClient->drainNotifications( _drainLease, _listDrained );
        for ( const SocialNotification& notification : _listDrained )
        {
            applyNotification( notification );
        }
        // 아직 한 번도 받지 않았거나 받는 중이면 다시 묻지 않는다 — 처음 받는 목록이 이미 최신이다.
        const bool bSettled = _state == FriendListState::Loaded || _state == FriendListState::Failed;
        if ( _bStale && bSettled )
            (void)refresh();
    }

    void FriendListModel::sortEntries()
    {
        VectorUtil::sort( _listEntry, FriendListModelInternal::EntryOrder{} );
    }
} // namespace sw
