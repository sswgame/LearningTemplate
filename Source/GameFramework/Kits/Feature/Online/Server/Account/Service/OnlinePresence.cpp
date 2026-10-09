#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Server/Account/Service/OnlinePresence.h"

#include "Core/Network/BitStream.h"
#include "Core/String/StringUtil.h"

#include "GameFramework/Base/Online/Bus/ServerBus.h"
#include "GameFramework/Base/Online/Service/OnlineServiceHost.h"
#include "GameFramework/Base/Online/Store/ServiceKeyUtil.h"
#include "GameFramework/Kits/Feature/Online/Account/Protocol/AccountTypes.h"

namespace sw
{
    namespace
    {
        struct OnlinePresenceInternal
        {
            static constexpr const utf8* kAccountKeyPrefix = "presence:";
            static constexpr const utf8* kNameKeyPrefix    = "presence.name:";
            static constexpr const utf8* kPushTopicPrefix  = "push.";

            static vector<uint8> takeBytes( const BitWriter& writer )
            {
                return vector<uint8>( writer.getBytes().begin(), writer.getBytes().begin() + writer.getByteCount() );
            }

            [[nodiscard]] static bool readServerId( const vector<uint8>& valueBytes, uint64& outServerId )
            {
                BitReader reader( valueBytes.data(), static_cast<int32>( valueBytes.size() ) );
                outServerId = reader.readVarUint();
                return reader.hasOverflowed() == false && outServerId != 0;
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
        , _serverId{ 0 }
        , _nextLookupId{ 1 }
        , _nextRefreshMs{ 0 }
    {
    }

    OnlinePresence::~OnlinePresence() = default;

    void OnlinePresence::attach( OnlineServiceHost* pHost )
    {
        if ( pHost == nullptr || pHost->getEphemeralRouter() == nullptr || pHost->getServerBus() == nullptr )
            return; // 서버 한 대 — 이 프로세스의 계정 디렉터리로 충분하다
        _pHost    = pHost;
        _serverId = pHost->getServerBus()->getServerId();
        for ( const auto& [accountId, identity] : _mapAccountToIdentity )
        {
            writeAccountEntries( identity, false );
        }
    }

    void OnlinePresence::shutdown()
    {
        if ( _pHost != nullptr ) // 호스트가 살아 있다 — 라우터에 맡긴 요청이 이 객체를 부르지 않게 거둔다
        {
            EphemeralStoreRouter* pRouter = _pHost->getEphemeralRouter();
            for ( const auto& [requestId, pending] : _mapRequestToPending )
            {
                pRouter->cancel( requestId );
            }
        }
        detach();
        _mapAccountToIdentity.clear();
    }

    void OnlinePresence::detach()
    {
        _pHost = nullptr;
        _mapRequestToPending.clear();
        _listDeferred.clear();
    }

    void OnlinePresence::cancel( uint64 requestId )
    {
        for ( auto& [routerRequestId, pending] : _mapRequestToPending )
        {
            if ( pending._lookupId == requestId && ( pending._pendingKind == PendingKind::FindName || pending._pendingKind == PendingKind::FindAccount ) )
                pending._onFound = AccountPresenceDelegate{}; // 캐시 답은 그대로 받아 버린다
        }
        for ( size_t index = 0; index < _listDeferred.size(); )
        {
            if ( _listDeferred[index]._result._requestId == requestId )
                _listDeferred.erase( _listDeferred.begin() + static_cast<ptrdiff_t>( index ) );
            else
                ++index;
        }
    }

    void OnlinePresence::noteOnline( const AccountIdentity& identity )
    {
        const auto identityIt = _mapAccountToIdentity.find( identity._accountId );
        if ( identityIt != _mapAccountToIdentity.end() && identityIt->second._displayName != identity._displayName && _pHost != nullptr )
        {
            // 표시 이름이 바뀌었다(연동) — 옛 이름 표시를 지운다(내 것일 때만).
            const AccountIdentity& previous = identityIt->second;
            (void)_pHost->getEphemeralRouter()->submit( EphemeralRequest::makeCompareAndErase( makeNameKey( previous._displayName ), makeNameBytes( previous ) ),
                                                        EphemeralStoreRouter::ReplyDelegate{} );
        }
        _mapAccountToIdentity[identity._accountId] = identity;
        if ( _pHost != nullptr )
            writeAccountEntries( identity, false );
    }

    void OnlinePresence::noteOffline( AccountId accountId )
    {
        const auto identityIt = _mapAccountToIdentity.find( accountId );
        if ( identityIt == _mapAccountToIdentity.end() )
            return;
        const AccountIdentity identity = identityIt->second;
        _mapAccountToIdentity.erase( identityIt );
        if ( _pHost == nullptr )
            return;
        // 내 것일 때만 지운다 — 같은 계정이 다른 서버에 새로 붙었으면 그쪽 표시가 남는다.
        EphemeralStoreRouter* pRouter = _pHost->getEphemeralRouter();
        (void)pRouter->submit( EphemeralRequest::makeCompareAndErase( makeAccountKey( accountId ), makeServerBytes() ), EphemeralStoreRouter::ReplyDelegate{} );
        (void)pRouter->submit( EphemeralRequest::makeCompareAndErase( makeNameKey( identity._displayName ), makeNameBytes( identity ) ),
                               EphemeralStoreRouter::ReplyDelegate{} );
    }

    void OnlinePresence::tick( int64 nowMs )
    {
        deliverDeferred();
        if ( _pHost == nullptr || nowMs < _nextRefreshMs )
            return;
        _nextRefreshMs = nowMs + _settings._refreshIntervalMs;
        for ( const auto& [accountId, identity] : _mapAccountToIdentity )
        {
            writeAccountEntries( identity, true );
        }
    }

    bool OnlinePresence::handlePushMessage( const ServerBusMessage& message )
    {
        if ( _pHost == nullptr || message._topic != makePushTopic( _serverId ) )
            return false;
        BitReader       reader( message._bytes.data(), static_cast<int32>( message._bytes.size() ) );
        const AccountId accountId = reader.readVarUint();
        const uint16    kind      = static_cast<uint16>( reader.readVarUint() );
        vector<uint8>   bodyBytes;
        if ( reader.readBlob( bodyBytes, IServerBus::kMaxMessageSize ) == false || reader.hasOverflowed() )
            return true;
        BitWriter body;
        if ( bodyBytes.empty() == false )
            body.writeBytes( bodyBytes.data(), static_cast<int32>( bodyBytes.size() ) );
        (void)_pHost->sendPush( accountId, kind, body ); // 그새 떠났으면 버린다(최대 한 번)
        return true;
    }

    string OnlinePresence::makePushTopic( uint64 serverId ) { return string( OnlinePresenceInternal::kPushTopicPrefix ) + ServiceKeyUtil::makeHex64( serverId ); }

    string OnlinePresence::makeAccountKey( AccountId accountId ) { return string( OnlinePresenceInternal::kAccountKeyPrefix ) + ServiceKeyUtil::makeHex64( accountId ); }

    string OnlinePresence::makeNameKey( string_view displayName )
    {
        // 이름은 키 규칙(`[0-9a-z_.:/-]`) 밖의 글자를 가질 수 있다 — 대소문자 무시 해시로 적고, 값의 이름으로 다시 견준다.
        return string( OnlinePresenceInternal::kNameKeyPrefix ) + ServiceKeyUtil::makeHex64( StringUtil::computeHash64( displayName.data(), displayName.size() ) );
    }

    uint64 OnlinePresence::submitFindByDisplayName( string_view displayName, const AccountPresenceDelegate& onFound )
    {
        const uint64 lookupId = _nextLookupId++;
        if ( _pHost == nullptr )
        {
            _listDeferred.push_back( DeferredFound{
                AccountPresenceResult{ AccountIdentity{}, lookupId, 0 },
                onFound
            } );
            return lookupId;
        }
        PendingOperation pending;
        pending._nameKey     = StringUtil::toLower( string( displayName ).c_str() );
        pending._onFound     = onFound;
        pending._lookupId    = lookupId;
        pending._pendingKind = PendingKind::FindName;
        submitPending( EphemeralRequest::makeGet( makeNameKey( displayName ) ), std::move( pending ) );
        return lookupId;
    }

    uint64 OnlinePresence::submitFindByAccount( AccountId accountId, const AccountPresenceDelegate& onFound )
    {
        const uint64 lookupId = _nextLookupId++;
        if ( _pHost == nullptr || accountId == kInvalidAccountId )
        {
            _listDeferred.push_back( DeferredFound{
                AccountPresenceResult{ AccountIdentity{}, lookupId, 0 },
                onFound
            } );
            return lookupId;
        }
        PendingOperation pending;
        pending._onFound     = onFound;
        pending._accountId   = accountId;
        pending._lookupId    = lookupId;
        pending._pendingKind = PendingKind::FindAccount;
        submitPending( EphemeralRequest::makeGet( makeAccountKey( accountId ) ), std::move( pending ) );
        return lookupId;
    }

    bool OnlinePresence::sendRemotePush( AccountId accountId, uint16 kind, const BitWriter& body )
    {
        if ( _pHost == nullptr )
            return false;
        PendingOperation pending;
        pending._bodyBytes   = OnlinePresenceInternal::takeBytes( body );
        pending._accountId   = accountId;
        pending._kind        = kind;
        pending._pendingKind = PendingKind::Push;
        submitPending( EphemeralRequest::makeGet( makeAccountKey( accountId ) ), std::move( pending ) );
        return true;
    }

    void OnlinePresence::onCacheReply( const EphemeralReply& reply )
    {
        const auto pendingIt = _mapRequestToPending.find( reply._requestId );
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
        const uint64 requestId = _pHost->getEphemeralRouter()->submit( request, EphemeralStoreRouter::ReplyDelegate::create<&OnlinePresence::onCacheReply>( this ) );
        _mapRequestToPending.emplace( requestId, std::move( pending ) );
    }

    void OnlinePresence::writeAccountEntries( const AccountIdentity& identity, bool bRefresh )
    {
        const vector<uint8> serverBytes = makeServerBytes();
        const vector<uint8> nameBytes   = makeNameBytes( identity );
        const string        accountKey  = makeAccountKey( identity._accountId );
        const string        nameKey     = makeNameKey( identity._displayName );
        if ( bRefresh == false )
        {
            // 새로 붙었다 — 다른 서버의 옛 표시를 덮는다(새 로그인이 이긴다).
            EphemeralStoreRouter* pRouter = _pHost->getEphemeralRouter();
            (void)pRouter->submit( EphemeralRequest::makeSet( accountKey, serverBytes, _settings._ttlMs ), EphemeralStoreRouter::ReplyDelegate{} );
            (void)pRouter->submit( EphemeralRequest::makeSet( nameKey, nameBytes, _settings._ttlMs ), EphemeralStoreRouter::ReplyDelegate{} );
            return;
        }
        PendingOperation accountPending;
        accountPending._accountId   = identity._accountId;
        accountPending._pendingKind = PendingKind::RefreshAccount;
        submitPending( EphemeralRequest::makeCompareAndSet( accountKey, serverBytes, serverBytes, _settings._ttlMs ), std::move( accountPending ) );
        PendingOperation namePending;
        namePending._accountId   = identity._accountId;
        namePending._nameKey     = nameKey;
        namePending._pendingKind = PendingKind::RefreshName;
        submitPending( EphemeralRequest::makeCompareAndSet( nameKey, nameBytes, nameBytes, _settings._ttlMs ), std::move( namePending ) );
    }

    void OnlinePresence::finishFindName( const PendingOperation& pending, const EphemeralReply& reply )
    {
        AccountPresenceResult found{ AccountIdentity{}, pending._lookupId, 0 };
        if ( reply._result == EphemeralResult::Ok )
        {
            BitReader       reader( reply._value.data(), static_cast<int32>( reply._value.size() ) );
            AccountIdentity identity;
            identity._accountId = reader.readVarUint();
            const uint64 server = reader.readVarUint();
            identity._bGuest    = reader.readBool() ? SW_TRUE : SW_FALSE;
            const bool bNameOk  = ServiceKeyUtil::readString( reader, LoginConstant::kMaxDisplayNameSize, identity._displayName );
            // 해시가 같은 다른 이름이면 없는 것이다.
            if ( bNameOk && reader.hasOverflowed() == false && StringUtil::toLower( identity._displayName.c_str() ) == pending._nameKey )
                found = AccountPresenceResult{ std::move( identity ), pending._lookupId, server };
        }
        if ( pending._onFound.isBound() )
            pending._onFound( found );
    }

    void OnlinePresence::finishFindAccount( const PendingOperation& pending, const EphemeralReply& reply )
    {
        AccountPresenceResult found{ AccountIdentity{}, pending._lookupId, 0 };
        uint64                serverId = 0;
        if ( reply._result == EphemeralResult::Ok && OnlinePresenceInternal::readServerId( reply._value, serverId ) )
        {
            found._identity._accountId = pending._accountId;
            found._serverId            = serverId;
        }
        if ( pending._onFound.isBound() )
            pending._onFound( found );
    }

    void OnlinePresence::deliverDeferred()
    {
        if ( _listDeferred.empty() )
            return;
        // 델리게이트 안에서 다시 찾기를 맡길 수 있다 — 지금 목록을 떼어 낸 뒤 부른다.
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
        uint64 serverId = 0;
        if ( reply._result != EphemeralResult::Ok || OnlinePresenceInternal::readServerId( reply._value, serverId ) == false || _pHost == nullptr )
            return; // 접속해 있지 않다 — 알림은 버린다(다시 붙으면 저장소에서 다시 읽는다)
        BitWriter body;
        if ( pending._bodyBytes.empty() == false )
            body.writeBytes( pending._bodyBytes.data(), static_cast<int32>( pending._bodyBytes.size() ) );
        if ( serverId == _serverId )
        {
            (void)_pHost->sendPush( pending._accountId, pending._kind, body ); // 그새 이 서버로 옮겨 왔다
            return;
        }
        BitWriter message;
        message.writeVarUint( pending._accountId );
        message.writeVarUint( pending._kind );
        message.writeBlob( pending._bodyBytes.data(), static_cast<int32>( pending._bodyBytes.size() ) );
        _pHost->getServerBus()->publish( makePushTopic( serverId ), message.getBytes().data(), message.getByteCount() );
    }

    void OnlinePresence::finishRefresh( const PendingOperation& pending, const EphemeralReply& reply )
    {
        const bool bMissingOrMoved = reply._result == EphemeralResult::NotFound || reply._result == EphemeralResult::Conflict;
        if ( bMissingOrMoved == false || _pHost == nullptr )
            return; // Ok — 늘렸다
        const auto identityIt = _mapAccountToIdentity.find( pending._accountId );
        if ( identityIt == _mapAccountToIdentity.end() )
            return;
        // 비교가 어긋났다 — 키가 없거나(캐시가 비었다 · 시한이 지났다) 다른 서버의 값이다. "없을 때만" 다시 적어 다른 서버의 것은 덮지 않는다.
        const AccountIdentity& identity = identityIt->second;
        if ( pending._pendingKind == PendingKind::RefreshAccount )
            (void)_pHost->getEphemeralRouter()->submit(
                EphemeralRequest::makeSet( makeAccountKey( identity._accountId ), makeServerBytes(), _settings._ttlMs, EphemeralCondition::IfAbsent ),
                EphemeralStoreRouter::ReplyDelegate{} );
        else
            (void)_pHost->getEphemeralRouter()->submit(
                EphemeralRequest::makeSet( makeNameKey( identity._displayName ), makeNameBytes( identity ), _settings._ttlMs, EphemeralCondition::IfAbsent ),
                EphemeralStoreRouter::ReplyDelegate{} );
    }

    vector<uint8> OnlinePresence::makeServerBytes() const
    {
        BitWriter writer;
        writer.writeVarUint( _serverId );
        return OnlinePresenceInternal::takeBytes( writer );
    }

    vector<uint8> OnlinePresence::makeNameBytes( const AccountIdentity& identity ) const
    {
        BitWriter writer;
        writer.writeVarUint( identity._accountId );
        writer.writeVarUint( _serverId );
        writer.writeBool( identity._bGuest == SW_TRUE );
        ServiceKeyUtil::writeString( writer, identity._displayName );
        return OnlinePresenceInternal::takeBytes( writer );
    }
} // namespace sw
