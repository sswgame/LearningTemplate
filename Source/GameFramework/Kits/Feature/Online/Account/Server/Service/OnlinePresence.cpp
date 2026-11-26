#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Account/Server/Service/OnlinePresence.h"

#include "Core/Hash/HashUtil.h"
#include "Core/Serialization/BitStream.h"
#include "Core/String/StringUtil.h"

#include "GameFramework/Kits/Feature/Online/Account/Runtime/AccountTypes.h"
#include "GameFramework/Runtime/Online/Bus/ServerBus.h"
#include "GameFramework/Runtime/Online/Cache/Server/EphemeralRecordTable.h"
#include "GameFramework/Runtime/Online/Service/Server/OnlineServiceHost.h"
#include "GameFramework/Runtime/Online/Store/ServiceKeyUtil.h"

namespace sw
{
    namespace
    {
        struct OnlinePresenceInternal
        {
            static constexpr const utf8* kAccountKeyPrefix = "presence:";
            static constexpr const utf8* kNameKeyPrefix    = "presence.name:";
        };
    } // namespace
} // namespace sw

namespace sw
{
    OnlinePresence::OnlinePresence()
        : _lookupTable{}
        , _mapAccountToIdentity{}
        , _listDeferredLookup{}
        , _settings{}
        , _pHost{ nullptr }
        , _serverID{ 0 }
        , _lookupIDAllocator{}
        , _nextRefreshMs{ 0 }
    {
    }

    OnlinePresence::~OnlinePresence() = default;

    void OnlinePresence::attach( OnlineServiceHost* pHost )
    {
        const bool bCannotAttachPresence = pHost == nullptr || pHost->getEphemeralRouter() == nullptr || pHost->getServerBus() == nullptr;
        if ( bCannotAttachPresence )
            return; // 서버가 한 대이므로 이 프로세스의 계정 디렉터리로 충분하다
        _pHost      = pHost;
        _replyOwner = pHost->getEphemeralRouter()->openReplyOwner();
        _serverID   = pHost->getServerBus()->getServerID();
        for ( const auto& [accountID, identity] : _mapAccountToIdentity )
        {
            writeAccountEntries( identity, false );
        }
    }

    void OnlinePresence::shutdown()
    {
        detach();
        _mapAccountToIdentity.clear();
    }

    void OnlinePresence::detach()
    {
        _replyOwner.reset(); // 라우터에 맡긴 요청의 답이 이 객체를 부르지 않게 한다
        _pHost = nullptr;
        _lookupTable.discardAll();
        _listDeferredLookup.clear();
    }

    void OnlinePresence::releaseSubscription( uint64 ownerID )
    {
        _lookupTable.discardOwnedBy( ownerID ); // 캐시 응답은 그대로 받아서 버린다
    }

    void OnlinePresence::recordOnline( const AccountIdentity& identity )
    {
        const auto identityIt          = _mapAccountToIdentity.find( identity._accountID );
        const bool bDisplayNameChanged = identityIt != _mapAccountToIdentity.end() && identityIt->second._displayName != identity._displayName && _pHost != nullptr;
        if ( bDisplayNameChanged )
        {
            // 표시 이름이 바뀌었으면(연동) 이전 이름의 기록을 지운다. 내 것일 때만 지운다.
            const AccountIdentity& previous = identityIt->second;
            _pHost->getEphemeralRouter()->submit( EphemeralRequest::makeCompareAndErase( makeNameKey( previous._displayName ), makeNameValue( previous ) ) );
        }
        _mapAccountToIdentity[identity._accountID] = identity;
        if ( _pHost != nullptr )
            writeAccountEntries( identity, false );
    }

    void OnlinePresence::recordOffline( AccountID accountID )
    {
        const auto identityIt = _mapAccountToIdentity.find( accountID );
        if ( identityIt == _mapAccountToIdentity.end() )
            return;
        const AccountIdentity identity = identityIt->second;
        _mapAccountToIdentity.erase( identityIt );
        if ( _pHost == nullptr )
            return;
        // 내 것일 때만 지운다. 같은 계정이 다른 서버에 새로 접속했으면 그쪽 기록이 남는다.
        EphemeralStoreRouter* pRouter = _pHost->getEphemeralRouter();
        pRouter->submit( EphemeralRequest::makeCompareAndErase( makeAccountKey( accountID ), makeServerValue() ) );
        pRouter->submit( EphemeralRequest::makeCompareAndErase( makeNameKey( identity._displayName ), makeNameValue( identity ) ) );
    }

    void OnlinePresence::update( UnixMilliseconds nowMs )
    {
        deliverDeferred();
        if ( _pHost == nullptr || nowMs < _nextRefreshMs )
            return;
        _nextRefreshMs = nowMs + _settings._refreshIntervalMs;
        for ( const auto& [accountID, identity] : _mapAccountToIdentity )
        {
            writeAccountEntries( identity, true );
        }
    }

    bool OnlinePresence::handlePushMessage( const ServerBusMessage& message )
    {
        if ( _pHost == nullptr || message.getTopic() != makePushTopic( _serverID ) )
            return false;
        OnlinePushRelay relay;
        if ( OnlinePresenceBus::kPushTopic.read( message, relay ) == ServerBusReadResult::Read )
            deliverRelay( relay ); // 못 읽으면 읽기가 로그와 카운터를 남겼다
        return true;
    }

    string OnlinePresence::makePushTopic( uint64 serverID )
    {
        return OnlinePresenceBus::kPushTopic.makeTopic( ServiceKeyUtil::makeHex64( serverID ) );
    }

    string OnlinePresence::makeAccountKey( AccountID accountID )
    {
        StringBuilder<constant::buffer::kMax64> key;
        key.appendFormat( "%#%#", OnlinePresenceInternal::kAccountKeyPrefix, ServiceKeyUtil::makeHex64( accountID ) );
        return string( key.view() );
    }

    string OnlinePresence::makeNameKey( string_view displayName )
    {
        // 이름은 키 규칙(`[0-9a-z_.:/-]`) 밖의 글자를 가질 수 있다. 그래서 대소문자를 무시한 해시로 기록하고, 값에 포함된 이름으로 다시 비교한다.
        StringBuilder<constant::buffer::kMax64> key;
        key.appendFormat( "%#%#", OnlinePresenceInternal::kNameKeyPrefix, ServiceKeyUtil::makeHex64( HashUtil::computeStringHash64( displayName.data(), displayName.size() ) ) );
        return string( key.view() );
    }

    uint64 OnlinePresence::openLookup( const AccountPresenceDelegate& onFound, uint64 ownerID )
    {
        const uint64 lookupID = _lookupIDAllocator.allocate();
        (void)_lookupTable.track( lookupID, ownerID, [onFound]( const AccountPresenceResult& result )
        {
            if ( onFound.isBound() )
                onFound( result );
        } ); // 번호는 방금 냈다
        return lookupID;
    }

    template <typename TComplete>
    void OnlinePresence::submitCache( const EphemeralRequest& request, TComplete&& complete )
    {
        _pHost->getEphemeralRouter()->submit( request, _replyOwner, std::forward<TComplete>( complete ) );
    }

    uint64 OnlinePresence::submitFindByDisplayName( string_view displayName, const AccountPresenceDelegate& onFound, const Subscription& owner )
    {
        const uint64 ownerID = _subscriptions.findIssued( owner );
        if ( ownerID == 0 )
        {
            SW_LOG_ERROR_ID( "OnlinePresence.OnlinePresenceFindWithoutLive", "OnlinePresence: find without a live find owner of this presence - nothing is submitted" );
            return 0;
        }
        const uint64 lookupID = openLookup( onFound, ownerID );
        if ( _pHost == nullptr )
        {
            _listDeferredLookup.push_back( lookupID );
            return lookupID;
        }
        submitCache( EphemeralRequest::makeGet( makeNameKey( displayName ) ), [this, lookupID, nameKey = StringUtil::toLower( displayName )]( const EphemeralReply& reply )
        { finishFindName( lookupID, nameKey, reply ); } );
        return lookupID;
    }

    uint64 OnlinePresence::submitFindByAccount( AccountID accountID, const AccountPresenceDelegate& onFound, const Subscription& owner )
    {
        const uint64 ownerID = _subscriptions.findIssued( owner );
        if ( ownerID == 0 )
        {
            SW_LOG_ERROR_ID( "OnlinePresence.OnlinePresenceFindWithoutLive", "OnlinePresence: find without a live find owner of this presence - nothing is submitted" );
            return 0;
        }
        const uint64 lookupID = openLookup( onFound, ownerID );
        if ( _pHost == nullptr || accountID == kInvalidAccountID )
        {
            _listDeferredLookup.push_back( lookupID );
            return lookupID;
        }
        submitCache( EphemeralRequest::makeGet( makeAccountKey( accountID ) ), [this, lookupID, accountID]( const EphemeralReply& reply )
        { finishFindAccount( lookupID, accountID, reply ); } );
        return lookupID;
    }

    bool OnlinePresence::relayRemotePush( const OnlinePushRelay& relay )
    {
        if ( _pHost == nullptr )
            return false;
        submitCache( EphemeralRequest::makeGet( makeAccountKey( relay._accountID ) ), [this, relay]( const EphemeralReply& reply )
        { finishPush( relay, reply ); } );
        return true;
    }

    void OnlinePresence::deliverRelay( const OnlinePushRelay& relay )
    {
        uint16    kind = 0;
        BitWriter body;
        if ( OnlinePushRelayRegistry::convert( relay, kind, body ) )
            (void)_pHost->sendPush( relay._accountID, kind, body ); // 그사이 떠났으면 버린다. 배달은 최대 한 번이다
    }

    void OnlinePresence::writeAccountEntries( const AccountIdentity& identity, bool bRefresh )
    {
        const EphemeralValue serverBytes = makeServerValue();
        const EphemeralValue nameBytes   = makeNameValue( identity );
        const string         accountKey  = makeAccountKey( identity._accountID );
        const string         nameKey     = makeNameKey( identity._displayName );
        if ( bRefresh == false )
        {
            // 새로 접속했으므로 다른 서버의 이전 기록을 덮는다. 새 로그인이 이긴다.
            EphemeralStoreRouter* pRouter = _pHost->getEphemeralRouter();
            pRouter->submit( EphemeralRequest::makeSet( accountKey, serverBytes, _settings._ttlMs ) );
            pRouter->submit( EphemeralRequest::makeSet( nameKey, nameBytes, _settings._ttlMs ) );
            return;
        }
        // 비교가 어긋나면(키가 없거나 다른 서버의 값이다) "없을 때만" 다시 기록해서 다른 서버의 값은 덮지 않는다.
        submitCache( EphemeralRequest::makeCompareAndSet( accountKey, serverBytes, serverBytes, _settings._ttlMs ), [this, accountID = identity._accountID]( const EphemeralReply& reply )
        {
            if ( const AccountIdentity* pIdentity = findLostEntry( accountID, reply ); pIdentity != nullptr )
                _pHost->getEphemeralRouter()->submit( EphemeralRequest::makeSet( makeAccountKey( pIdentity->_accountID ), makeServerValue(), _settings._ttlMs, EphemeralCondition::IfAbsent ) );
        } );
        submitCache( EphemeralRequest::makeCompareAndSet( nameKey, nameBytes, nameBytes, _settings._ttlMs ), [this, accountID = identity._accountID]( const EphemeralReply& reply )
        {
            if ( const AccountIdentity* pIdentity = findLostEntry( accountID, reply ); pIdentity != nullptr )
                _pHost->getEphemeralRouter()->submit( EphemeralRequest::makeSet( makeNameKey( pIdentity->_displayName ), makeNameValue( *pIdentity ), _settings._ttlMs, EphemeralCondition::IfAbsent ) );
        } );
    }

    void OnlinePresence::finishFindName( uint64 lookupID, const string& nameKey, const EphemeralReply& reply )
    {
        AccountPresenceResult found{ AccountIdentity{}, lookupID, 0 };
        if ( reply._result == EphemeralResult::Ok )
        {
            OnlinePresenceNameRecord record;
            // 해시는 같지만 다른 이름이면 없는 것으로 본다.
            const bool bNameMatchesLookup = EphemeralRecordTable<OnlinePresenceNameRecord>{}.decode( reply, record ) && StringUtil::toLower( record._displayName.c_str() ) == nameKey;
            if ( bNameMatchesLookup )
            {
                AccountIdentity identity;
                identity._accountID   = record._accountID;
                identity._bGuest      = record._bGuest;
                identity._displayName = std::move( record._displayName );
                found                 = AccountPresenceResult{ std::move( identity ), lookupID, record._serverID };
            }
        }
        (void)_lookupTable.answer( lookupID, found ); // 모르는 번호는 주인이 몫을 놓은 찾기다
    }

    void OnlinePresence::finishFindAccount( uint64 lookupID, AccountID accountID, const EphemeralReply& reply )
    {
        AccountPresenceResult found{ AccountIdentity{}, lookupID, 0 };
        uint64                serverID = 0;
        if ( EphemeralValueUtil::readID( reply, serverID ) )
        {
            found._identity._accountID = accountID;
            found._serverID            = serverID;
        }
        (void)_lookupTable.answer( lookupID, found ); // 모르는 번호는 주인이 몫을 놓은 찾기다
    }

    void OnlinePresence::deliverDeferred()
    {
        // 델리게이트 안에서 다시 찾기를 맡길 수 있다. 그래서 지금 목록을 분리한 뒤 답한다.
        const vector<uint64> listLookup = std::move( _listDeferredLookup );
        _listDeferredLookup.clear();
        for ( const uint64 lookupID : listLookup )
        {
            (void)_lookupTable.answer( lookupID, AccountPresenceResult{ AccountIdentity{}, lookupID, 0 } ); // 모르는 번호는 주인이 몫을 놓은 찾기다
        }
    }

    void OnlinePresence::finishPush( const OnlinePushRelay& relay, const EphemeralReply& reply )
    {
        uint64     serverID          = 0;
        const bool bDropNotification = EphemeralValueUtil::readID( reply, serverID ) == false || _pHost == nullptr;
        if ( bDropNotification )
            return; // 접속해 있지 않으므로 알림은 버린다. 다시 접속하면 저장소에서 다시 읽는다
        if ( serverID == _serverID )
        {
            deliverRelay( relay ); // 그사이 이 서버로 옮겨 왔다
            return;
        }
        (void)OnlinePresenceBus::kPushTopic.publish( *_pHost->getServerBus(), ServiceKeyUtil::makeHex64( serverID ), relay ); // 실패는 발행이 로그로 남겼다
    }

    const AccountIdentity* OnlinePresence::findLostEntry( AccountID accountID, const EphemeralReply& reply ) const
    {
        const bool bMissingOrMoved = reply._result == EphemeralResult::NotFound || reply._result == EphemeralResult::Conflict;
        if ( bMissingOrMoved == false || _pHost == nullptr )
            return nullptr; // Ok 이므로 시한을 늘린 것이다
        const auto identityIt = _mapAccountToIdentity.find( accountID );
        return identityIt != _mapAccountToIdentity.end() ? &identityIt->second : nullptr;
    }

    EphemeralValue OnlinePresence::makeServerValue() const
    {
        return EphemeralValueUtil::makeID( _serverID );
    }

    EphemeralValue OnlinePresence::makeNameValue( const AccountIdentity& identity ) const
    {
        OnlinePresenceNameRecord record;
        record._accountID   = identity._accountID;
        record._serverID    = _serverID;
        record._bGuest      = identity._bGuest;
        record._displayName = identity._displayName;
        EphemeralValue value;
        (void)EphemeralRecordTable<OnlinePresenceNameRecord>{}.makeValue( record, value ); // 이름 규칙 밖이면 표가 오류 로그를 남기고 빈 값 — 찾기가 없는 것으로 읽는다
        return value;
    }
} // namespace sw
