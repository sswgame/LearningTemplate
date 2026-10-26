#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Account/Server/Service/OnlinePresence.h"

#include "Core/Common/HashUtil.h"
#include "Core/Container/StringUtil.h"
#include "Core/Network/BitStream.h"

#include "GameFramework/Base/Online/Bus/ServerBus.h"
#include "GameFramework/Base/Online/Service/OnlineServiceHost.h"
#include "GameFramework/Base/Online/Store/ServiceKeyUtil.h"
#include "GameFramework/Kits/Feature/Online/Account/Shared/Protocol/AccountTypes.h"

namespace sw
{
    namespace
    {
        struct OnlinePresenceInternal
        {
            static constexpr const utf8* kAccountKeyPrefix = "presence:";
            static constexpr const utf8* kNameKeyPrefix    = "presence.name:";

            static vector<uint8> takeBytes( const BitWriter& writer )
            {
                return vector<uint8>( writer.getBytes().begin(), writer.getBytes().begin() + writer.getByteCount() );
            }

            [[nodiscard]] static bool readServerID( const vector<uint8>& valueBytes, uint64& outServerID )
            {
                BitReader reader( valueBytes.data(), static_cast<int32>( valueBytes.size() ) );
                outServerID = reader.readVarUint();
                return reader.hasOverflowed() == false && outServerID != 0;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    OnlinePresence::OnlinePresence()
        : _mapRequestToPending{}
        , _mapAccountToIdentity{}
        , _listDeferred{}
        , _settings{}
        , _pHost{ nullptr }
        , _serverID{ 0 }
        , _nextLookupID{ 1 }
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
        _mapRequestToPending.clear();
        _listDeferred.clear();
    }

    void OnlinePresence::releaseSubscription( uint64 ownerID )
    {
        for ( auto& [routerRequestID, pending] : _mapRequestToPending )
        {
            if ( pending._ownerID == ownerID )
                pending._onFound = AccountPresenceDelegate{}; // 캐시 응답은 그대로 받아서 버린다
        }
        for ( size_t index = 0; index < _listDeferred.size(); )
        {
            if ( _listDeferred[index]._ownerID == ownerID )
                _listDeferred.erase( _listDeferred.begin() + static_cast<ptrdiff_t>( index ) );
            else
                ++index;
        }
    }

    void OnlinePresence::noteOnline( const AccountIdentity& identity )
    {
        const auto identityIt          = _mapAccountToIdentity.find( identity._accountID );
        const bool bDisplayNameChanged = identityIt != _mapAccountToIdentity.end() && identityIt->second._displayName != identity._displayName && _pHost != nullptr;
        if ( bDisplayNameChanged )
        {
            // 표시 이름이 바뀌었으면(연동) 이전 이름의 기록을 지운다. 내 것일 때만 지운다.
            const AccountIdentity& previous = identityIt->second;
            _pHost->getEphemeralRouter()->submit( EphemeralRequest::makeCompareAndErase( makeNameKey( previous._displayName ), makeNameBytes( previous ) ) );
        }
        _mapAccountToIdentity[identity._accountID] = identity;
        if ( _pHost != nullptr )
            writeAccountEntries( identity, false );
    }

    void OnlinePresence::noteOffline( AccountID accountID )
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
        pRouter->submit( EphemeralRequest::makeCompareAndErase( makeAccountKey( accountID ), makeServerBytes() ) );
        pRouter->submit( EphemeralRequest::makeCompareAndErase( makeNameKey( identity._displayName ), makeNameBytes( identity ) ) );
    }

    void OnlinePresence::update( int64 nowMs )
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
        return string( OnlinePresenceInternal::kAccountKeyPrefix ) + ServiceKeyUtil::makeHex64( accountID );
    }

    string OnlinePresence::makeNameKey( string_view displayName )
    {
        // 이름은 키 규칙(`[0-9a-z_.:/-]`) 밖의 글자를 가질 수 있다. 그래서 대소문자를 무시한 해시로 기록하고, 값에 포함된 이름으로 다시 비교한다.
        return string( OnlinePresenceInternal::kNameKeyPrefix ) + ServiceKeyUtil::makeHex64( HashUtil::computeStringHash64( displayName.data(), displayName.size() ) );
    }

    uint64 OnlinePresence::deferNotFound( const AccountPresenceDelegate& onFound, uint64 ownerID )
    {
        const uint64 lookupID = _nextLookupID++;
        _listDeferred.push_back( DeferredFound{
            AccountPresenceResult{ AccountIdentity{}, lookupID, 0 },
            onFound, ownerID
        } );
        return lookupID;
    }

    uint64 OnlinePresence::submitFindByDisplayName( string_view displayName, const AccountPresenceDelegate& onFound, const Subscription& owner )
    {
        const uint64 ownerID = findIssuedID( owner );
        if ( ownerID == 0 )
        {
            SW_LOG_ERROR( "OnlinePresence: find without a live find owner of this presence - nothing is submitted" );
            return 0;
        }
        if ( _pHost == nullptr )
            return deferNotFound( onFound, ownerID );
        const uint64     lookupID = _nextLookupID++;
        PendingOperation pending;
        pending._nameKey     = StringUtil::toLower( string( displayName ).c_str() );
        pending._onFound     = onFound;
        pending._lookupID    = lookupID;
        pending._ownerID     = ownerID;
        pending._pendingKind = PendingKind::FindName;
        submitPending( EphemeralRequest::makeGet( makeNameKey( displayName ) ), std::move( pending ) );
        return lookupID;
    }

    uint64 OnlinePresence::submitFindByAccount( AccountID accountID, const AccountPresenceDelegate& onFound, const Subscription& owner )
    {
        const uint64 ownerID = findIssuedID( owner );
        if ( ownerID == 0 )
        {
            SW_LOG_ERROR( "OnlinePresence: find without a live find owner of this presence - nothing is submitted" );
            return 0;
        }
        if ( _pHost == nullptr || accountID == kInvalidAccountID )
            return deferNotFound( onFound, ownerID );
        const uint64     lookupID = _nextLookupID++;
        PendingOperation pending;
        pending._onFound     = onFound;
        pending._accountID   = accountID;
        pending._lookupID    = lookupID;
        pending._ownerID     = ownerID;
        pending._pendingKind = PendingKind::FindAccount;
        submitPending( EphemeralRequest::makeGet( makeAccountKey( accountID ) ), std::move( pending ) );
        return lookupID;
    }

    bool OnlinePresence::relayRemotePush( const OnlinePushRelay& relay )
    {
        if ( _pHost == nullptr )
            return false;
        PendingOperation pending;
        pending._relay       = relay;
        pending._accountID   = relay._accountID;
        pending._pendingKind = PendingKind::Push;
        submitPending( EphemeralRequest::makeGet( makeAccountKey( relay._accountID ) ), std::move( pending ) );
        return true;
    }

    void OnlinePresence::deliverRelay( const OnlinePushRelay& relay )
    {
        uint16    kind = 0;
        BitWriter body;
        if ( OnlinePushRelayRegistry::convert( relay, kind, body ) )
            (void)_pHost->sendPush( relay._accountID, kind, body ); // 그사이 떠났으면 버린다. 배달은 최대 한 번이다
    }

    void OnlinePresence::onCacheReply( const EphemeralReply& reply )
    {
        const auto pendingIt = _mapRequestToPending.find( reply._requestID );
        if ( pendingIt == _mapRequestToPending.end() )
            return;
        const PendingOperation pending = std::move( pendingIt->second );
        _mapRequestToPending.erase( pendingIt );
        switch ( pending._pendingKind )
        {
            case PendingKind::FindName:
            {
                finishFindName( pending, reply );
                break;
            }
            case PendingKind::FindAccount:
            {
                finishFindAccount( pending, reply );
                break;
            }
            case PendingKind::Push:
            {
                finishPush( pending, reply );
                break;
            }
            case PendingKind::RefreshAccount:
            case PendingKind::RefreshName:
            {
                finishRefresh( pending, reply );
                break;
            }
        }
    }

    void OnlinePresence::submitPending( const EphemeralRequest& request, PendingOperation&& pending )
    {
        const uint64 requestID = _pHost->getEphemeralRouter()->submit( request, EphemeralStoreRouter::ReplyDelegate::create<&OnlinePresence::onCacheReply>( this ), _replyOwner );
        _mapRequestToPending.emplace( requestID, std::move( pending ) );
    }

    void OnlinePresence::writeAccountEntries( const AccountIdentity& identity, bool bRefresh )
    {
        const vector<uint8> serverBytes = makeServerBytes();
        const vector<uint8> nameBytes   = makeNameBytes( identity );
        const string        accountKey  = makeAccountKey( identity._accountID );
        const string        nameKey     = makeNameKey( identity._displayName );
        if ( bRefresh == false )
        {
            // 새로 접속했으므로 다른 서버의 이전 기록을 덮는다. 새 로그인이 이긴다.
            EphemeralStoreRouter* pRouter = _pHost->getEphemeralRouter();
            pRouter->submit( EphemeralRequest::makeSet( accountKey, serverBytes, _settings._ttlMs ) );
            pRouter->submit( EphemeralRequest::makeSet( nameKey, nameBytes, _settings._ttlMs ) );
            return;
        }
        PendingOperation accountPending;
        accountPending._accountID   = identity._accountID;
        accountPending._pendingKind = PendingKind::RefreshAccount;
        submitPending( EphemeralRequest::makeCompareAndSet( accountKey, serverBytes, serverBytes, _settings._ttlMs ), std::move( accountPending ) );
        PendingOperation namePending;
        namePending._accountID   = identity._accountID;
        namePending._nameKey     = nameKey;
        namePending._pendingKind = PendingKind::RefreshName;
        submitPending( EphemeralRequest::makeCompareAndSet( nameKey, nameBytes, nameBytes, _settings._ttlMs ), std::move( namePending ) );
    }

    void OnlinePresence::finishFindName( const PendingOperation& pending, const EphemeralReply& reply )
    {
        AccountPresenceResult found{ AccountIdentity{}, pending._lookupID, 0 };
        if ( reply._result == EphemeralResult::Ok )
        {
            BitReader       reader( reply._value.data(), static_cast<int32>( reply._value.size() ) );
            AccountIdentity identity;
            identity._accountID = reader.readVarUint();
            const uint64 server = reader.readVarUint();
            identity._bGuest    = reader.readBool() ? SW_TRUE : SW_FALSE;
            const bool bNameOk  = ServiceKeyUtil::readString( reader, LoginConstant::kMaxDisplayNameSize, identity._displayName );
            // 해시는 같지만 다른 이름이면 없는 것으로 본다.
            const bool bNameMatchesLookup = bNameOk && reader.hasOverflowed() == false && StringUtil::toLower( identity._displayName.c_str() ) == pending._nameKey;
            if ( bNameMatchesLookup )
                found = AccountPresenceResult{ std::move( identity ), pending._lookupID, server };
        }
        if ( pending._onFound.isBound() )
            pending._onFound( found );
    }

    void OnlinePresence::finishFindAccount( const PendingOperation& pending, const EphemeralReply& reply )
    {
        AccountPresenceResult found{ AccountIdentity{}, pending._lookupID, 0 };
        uint64                serverID = 0;
        if ( reply._result == EphemeralResult::Ok && OnlinePresenceInternal::readServerID( reply._value, serverID ) )
        {
            found._identity._accountID = pending._accountID;
            found._serverID            = serverID;
        }
        if ( pending._onFound.isBound() )
            pending._onFound( found );
    }

    void OnlinePresence::deliverDeferred()
    {
        if ( _listDeferred.empty() )
            return;
        // 델리게이트 안에서 다시 찾기를 맡길 수 있다. 그래서 지금 목록을 분리한 뒤 델리게이트를 부른다.
        vector<DeferredFound> listDeferred = std::move( _listDeferred );
        _listDeferred.clear();
        for ( const DeferredFound& deferred : listDeferred )
        {
            if ( deferred._onFound.isBound() )
                deferred._onFound( deferred._result );
        }
    }

    void OnlinePresence::finishPush( const PendingOperation& pending, const EphemeralReply& reply )
    {
        uint64     serverID          = 0;
        const bool bDropNotification = reply._result != EphemeralResult::Ok || OnlinePresenceInternal::readServerID( reply._value, serverID ) == false || _pHost == nullptr;
        if ( bDropNotification )
            return; // 접속해 있지 않으므로 알림은 버린다. 다시 접속하면 저장소에서 다시 읽는다
        if ( serverID == _serverID )
        {
            deliverRelay( pending._relay ); // 그사이 이 서버로 옮겨 왔다
            return;
        }
        (void)OnlinePresenceBus::kPushTopic.publish( *_pHost->getServerBus(), ServiceKeyUtil::makeHex64( serverID ), pending._relay ); // 실패는 발행이 로그로 남겼다
    }

    void OnlinePresence::finishRefresh( const PendingOperation& pending, const EphemeralReply& reply )
    {
        const bool bMissingOrMoved = reply._result == EphemeralResult::NotFound || reply._result == EphemeralResult::Conflict;
        if ( bMissingOrMoved == false || _pHost == nullptr )
            return; // Ok 이므로 시한을 늘린 것이다
        const auto identityIt = _mapAccountToIdentity.find( pending._accountID );
        if ( identityIt == _mapAccountToIdentity.end() )
            return;
        // 비교가 어긋났다. 키가 없거나(캐시가 비었거나 시한이 지났다) 다른 서버의 값이다. "없을 때만" 다시 기록해서 다른 서버의 값은 덮지 않는다.
        const AccountIdentity& identity = identityIt->second;
        if ( pending._pendingKind == PendingKind::RefreshAccount )
            _pHost->getEphemeralRouter()->submit(
                EphemeralRequest::makeSet( makeAccountKey( identity._accountID ), makeServerBytes(), _settings._ttlMs, EphemeralCondition::IfAbsent ) );
        else
            _pHost->getEphemeralRouter()->submit(
                EphemeralRequest::makeSet( makeNameKey( identity._displayName ), makeNameBytes( identity ), _settings._ttlMs, EphemeralCondition::IfAbsent ) );
    }

    vector<uint8> OnlinePresence::makeServerBytes() const
    {
        BitWriter writer;
        writer.writeVarUint( _serverID );
        return OnlinePresenceInternal::takeBytes( writer );
    }

    vector<uint8> OnlinePresence::makeNameBytes( const AccountIdentity& identity ) const
    {
        BitWriter writer;
        writer.writeVarUint( identity._accountID );
        writer.writeVarUint( _serverID );
        writer.writeBool( identity._bGuest == SW_TRUE );
        ServiceKeyUtil::writeString( writer, identity._displayName );
        return OnlinePresenceInternal::takeBytes( writer );
    }
} // namespace sw
